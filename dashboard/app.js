const $ = id => document.getElementById(id);
const PWM_KEYS = ['pwm-fl', 'pwm-fr', 'pwm-rl', 'pwm-rr'];
const PWM_META = {
  'pwm-fl': { short: 'FL', name: 'Front left', encoder: false },
  'pwm-fr': { short: 'FR', name: 'Front right', encoder: false },
  'pwm-rl': { short: 'RL', name: 'Rear left', encoder: true },
  'pwm-rr': { short: 'RR', name: 'Rear right', encoder: true },
};
const SETTING_IDS = [...PWM_KEYS, 'heading-kp', 'heading-max',
  'heading-deadband-deg', 'heading-sign', 'heading-enabled'];
const DIRECTION_LABELS = {
  forward: 'Forward', reverse: 'Reverse', left: 'Strafe left',
  right: 'Strafe right', ccw: 'Rotate CCW', cw: 'Rotate CW',
};
const TUNING_DIRECTIONS = ['forward', 'reverse', 'left', 'right'];

let status = {};
let session = { active: false, tests: [], calibration: {} };
let workflow = null;
let baselinePrevious = null;
let selectedDirection = 'forward';
let testMagnitude = 0.45;
let testDuration = 5000;
let runToken = 0;
const dismissedRecommendations = new Set();

try {
  baselinePrevious = JSON.parse(localStorage.getItem('mechbot-baseline-previous') || 'null');
} catch (_) {
  baselinePrevious = null;
}

async function request(path, options = {}) {
  const response = await fetch(path, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });
  const raw = await response.text();
  let data = {};
  try { data = raw ? JSON.parse(raw) : {}; } catch (_) { data = { error: raw || 'Invalid response' }; }
  if (!response.ok) throw new Error(data.error || `Request failed (${response.status})`);
  return data;
}

const post = (path, payload = {}) => request(path, {
  method: 'POST', body: JSON.stringify({ board_id: status.board?.id, ...payload }),
});
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
const lastTest = () => (session.tests || []).at(-1);
const signed = value => `${value > 0 ? '+' : ''}${value}`;
const pwmValue = (settings, key) => Math.round(Number(
  settings?.[key] ?? session.proven_baseline?.[key] ?? 0));
const encoderWheels = () => status.board?.encoder_wheels || [];

function renderBoard() {
  const board = status.board || {};
  const known = status.serial_connected && board.id && board.id !== 'unknown';
  for (const meta of Object.values(PWM_META)) meta.encoder = encoderWheels().includes(meta.short);
  $('encoderLabel').textContent = board.id === 'maker' ? 'FOUR ENCODERS' : 'ENCODERS';
  $('encoderDetail').textContent = encoderWheels().join(' / ') || 'Waiting for board identity';
  $('baselineTitle').textContent = board.baseline_label || 'Identify the controller';
  $('baselineNote').textContent = board.baseline_note || 'Waiting for firmware identity.';
  $('applyBaseline').disabled = !known || Boolean(session.calibration?.running);
  $('feedbackTitle').textContent = board.id === 'maker' ? 'Four-wheel feedback on Maker' : 'Rear-encoder feedback on the legacy chassis';
  $('feedbackCopy').textContent = board.id === 'maker'
    ? 'All four encoders were measured over ten wheel turns. This console compares each wheel with its own earlier response. The supervised automatic bench tool uses those measured scales to assess starting PWM and steady speed in both directions. A single PWM trim may not correct the observed rear-left direction difference.'
    : 'Compare each rear encoder with its own earlier run. Legacy raw counts are not established as equal distance units. Heading and path observations support small reversible chassis trims.';
  const canUpdate = known && !status.gamepad_connected && !status.deadman && !session.active &&
    !status.calibration_active && !status.maintenance;
  $('updateFirmware').disabled = !canUpdate;
  $('updateFirmware').textContent = known ? `Compile & flash ${board.name}` : 'Identify controller before updating';
  $('updateFirmware').title = canUpdate ? 'Use the connected board target' : 'Identify the board, disconnect the gamepad and finish tuning first';
  $('wheelDiagnostics').textContent = board.diagnostics
    ? encoderWheels().map(wheel => {
      const diagnostic = status.wheel_diagnostics?.[wheel];
      if (!diagnostic || Date.now() / 1000 - diagnostic.updated > 3) return `${wheel}: unavailable / stale`;
      return `${wheel}: PWM ${diagnostic.pwm} · A ${diagnostic.a_edges} · B ${diagnostic.b_edges} · invalid ${diagnostic.invalid_transitions}`;
    }).join('\n') : 'Per-wheel diagnostics are available on Maker firmware.';
  renderControlModel();
  document.querySelectorAll('[data-workflow]').forEach(button => { button.disabled = !known; });
  for (const id of ['apply', 'save']) $(id).disabled = !known || Boolean(session.active) || Boolean(status.maintenance);
}

function card(id, state) {
  $(id).closest('.status-card').className = `status-card ${state}`;
}

function health(line = '') {
  const parts = line.split(' ');
  return parts[0] === 'H' ? {
    available: parts[3] === '1', last: Number(parts[4]), resets: Number(parts[5]),
  } : null;
}

function cfg(lines = []) {
  const output = {};
  for (const line of lines) {
    const match = line.match(/^CFG ([\w-]+) (.+)$/);
    if (match) output[match[1]] = match[2];
  }
  return output;
}

function renderControlModel() {
  const enabled = $('heading-enabled').checked;
  const limit = Number($('heading-max').value);
  const state = $('headingLayerState');
  const detail = $('headingLayerDetail');
  state.textContent = enabled ? 'Heading hold: ON (dynamic overlay)' : 'Heading hold: OFF (baseline only)';
  detail.textContent = enabled && Number.isFinite(limit)
    ? `It may adjust left and right wheel commands by up to ${(limit * 100).toFixed(0)}% of the drive command. Your saved wheel PWM remains the baseline.`
    : 'Tune and save wheel balance first. Turn this on only for a controlled heading comparison.';
  state.className = enabled ? 'enabled' : 'disabled';
}

async function loadSettings() {
  try {
    const values = cfg((await request('/api/settings')).lines);
    for (const id of SETTING_IDS) {
      if (id === 'heading-enabled') $(id).checked = values[id] === '1';
      else if (values[id] != null) $(id).value = values[id];
    }
    renderControlModel();
    $('message').textContent = 'Loaded from ESP32';
  } catch (error) {
    $('message').textContent = error.message;
  }
}

async function refresh() {
  try {
    const wasRunning = Boolean(session.calibration?.running);
    const [nextStatus, nextSession] = await Promise.all([
      request('/api/status'), request('/api/tuning'),
    ]);
    status = nextStatus;
    session = nextSession;
    const age = status.updated ? Date.now() / 1000 - status.updated : 999;
    const imuHealth = health(status.health);
    const imuFresh = Boolean(imuHealth?.available && status.imu_valid && status.imu_updated &&
      Date.now() / 1000 - status.imu_updated < 1);
    const encoderAge = status.encoder_updated
      ? Date.now() / 1000 - status.encoder_updated : 999;
    const encoderFresh = Boolean(status.serial_connected && encoderAge < 1.5);
    const encoders = Array.isArray(status.encoders) ? status.encoders : [0, 0, 0, 0];

    $('serialState').textContent = status.serial_connected ? 'Connected' : 'Disconnected';
    $('firmware').textContent = status.firmware || (status.board?.identity_source === 'maker-help'
      ? 'Maker identified · firmware version not reported' : status.serial_port) || 'No serial device';
    const pwmConfig = status.pwm_config;
    $('firmware').textContent += status.serial_connected && pwmConfig && Date.now()/1000-pwmConfig.updated < 3
      ? ` · PWM ${pwmConfig.hz} Hz (${pwmConfig.bits}-bit ${pwmConfig.clock})`
      : ' · PWM frequency unconfirmed';
    card('serialState', status.serial_connected ? 'good' : 'bad');
    $('encoderState').textContent = encoderFresh ? 'Telemetry fresh' : 'Unavailable / stale';
    card('encoderState', encoderFresh ? 'good' : 'warn');
    $('imuState').textContent = imuFresh ? 'Fresh' : 'Unavailable / stale';
    $('imuDetail').textContent = imuHealth
      ? `${imuHealth.resets} reset(s) · ${status.imu ? 'telemetry seen' : 'waiting for telemetry'}`
      : 'No health frame';
    card('imuState', imuFresh ? 'good' : 'warn');
    $('sessionState').textContent = session.active ? 'In progress' : 'None';
    $('sessionDetail').textContent = session.active
      ? `${session.workflow} · ${(session.tests || []).length} test(s) stored`
      : (session.error || (status.rearm_required ? 'Release the deadman before driving' : session.phase) || 'Safe settings retained');
    card('sessionState', session.active ? 'warn' : 'good');
    $('freshness').textContent = age < 999
      ? `Updated ${Math.max(0, Math.round(age))}s ago` : 'Waiting for data';
    $('encoders').innerHTML = ['FL', 'FR', 'RL', 'RR']
      .map((name, index) => `<div class="encoder"><span>${name}</span><strong>${encoderWheels().includes(name) ? encoders[index] : 'N/A'}</strong></div>`)
      .join('');
    $('events').textContent = (status.recent_lines || []).slice(-18).join('\n') || 'Waiting…';
    renderBoard();
    renderNext();

    if (!$('sessionModal').hidden) {
      if (session.calibration?.running) updateRunningTelemetry(session.calibration);
      else if (wasRunning) renderSession();
    }
  } catch (error) {
    $('freshness').textContent = error.message;
  }
}

function renderNext() {
  const title = $('nextTitle');
  const copy = $('nextCopy');
  const button = $('nextAction');
  if (session.active) {
    button.disabled = false;
    title.textContent = 'Resume the active PWM session';
    copy.textContent = `${(session.tests || []).length} test(s) and all live PWM changes are retained. Resume, save the winner, or restore the original settings.`;
    button.textContent = 'Resume session';
    button.onclick = () => openSession(session.workflow);
  } else {
    const encoderFresh = Boolean(status.serial_connected && status.encoder_updated &&
      Date.now() / 1000 - status.encoder_updated < 1.5);
    button.disabled = !status.serial_connected || !encoderFresh || status.board?.id === 'unknown';
    title.textContent = encoderFresh
      ? 'Continue supervised motor validation' : 'Wait for fresh encoder telemetry';
    copy.textContent = encoderFresh
      ? 'Check wheel response with the wheels raised first. For a floor test, use a clear area and record heading and path drift. The bench tool measures starting thresholds and steady speed separately.'
      : 'A known controller and current encoder telemetry are required for tuning.';
    button.textContent = 'Open bench check';
    button.onclick = () => openSession('bench');
  }
}

function workflowCopy() {
  if (workflow === 'bench') return {
    title: 'Encoder response check',
    copy: 'Compare calibrated wheel RPM using steady powered samples. Run for five seconds, review the suggested PWM reductions, and apply them yourself.',
    warning: 'All four wheels must be securely off the floor. Pulse totals include startup and are not steady-speed measurements.',
  };
  if (workflow === 'imu') return {
    title: 'Heading hold comparison',
    copy: 'Run a baseline with correction off, then enable heading hold and repeat. Adjust gain, correction limit, and deadband here.',
    warning: 'Use a clear floor area. Keep the stop control within reach. Fresh RVC V2 heading is required for these runs.',
  };
  return {
    title: 'Guided floor PWM tuning',
    copy: 'Run repeatable moves and review encoder-based PWM suggestions. Record floor behavior to assess traction and heading separately.',
    warning: 'Use a clear, consistent floor and stay within immediate reach of power. Begin with low test output.',
  };
}

function configureFooter({ primaryText, primaryAction, primaryDisabled = false,
  secondaryText = '', secondaryAction = null }) {
  const primary = $('sessionPrimary');
  const secondary = $('sessionSecondary');
  primary.textContent = primaryText;
  primary.disabled = primaryDisabled;
  primary.onclick = primaryAction || null;
  secondary.hidden = !secondaryText;
  secondary.textContent = secondaryText;
  secondary.onclick = secondaryAction;
}

function openSession(nextWorkflow) {
  if (nextWorkflow === 'imu') {
    testDuration = 5000;
    testMagnitude = 1.0;
  }
  workflow = nextWorkflow || session.workflow || 'floor';
  $('sessionModal').hidden = false;
  renderSession();
}

function renderSession() {
  workflow = session.active ? session.workflow : (workflow || 'floor');
  $('modalTitle').textContent = workflowCopy().title;
  $('sessionMessage').textContent = session.active
    ? `${(session.tests || []).length} test(s) stored · changes are live, not saved`
    : 'Not started';
  if (session.calibration?.error) $('sessionMessage').textContent =
    `Stopped after ${(session.calibration.elapsed_ms / 1000).toFixed(2)} s: ${session.calibration.error}`;

  if (!session.active) return renderPreparation();
  if (session.calibration?.running) return renderRunning(session.calibration);

  const last = lastTest();
  if (last && !last.score) return renderObservation(last);
  if (last?.recommendation && !last.recommendation_applied &&
      !dismissedRecommendations.has(last.id)) return renderRecommendation(last);
  return renderWorkbench();
}

function renderPreparation() {
  const details = workflowCopy();
  $('sessionProgress').style.width = '8%';
  $('sessionBody').innerHTML = `
    <div class="cal-step-label">STEP 1 · PREPARE</div>
    <h3>${details.title}</h3>
    <p>${details.copy}</p>
    <div class="cal-warning">${details.warning}</div>
    <div class="method-grid">
      <div><strong>1. Set</strong><span>Edit all four PWM ceilings inside the session.</span></div>
      <div><strong>2. Test</strong><span>Run a bounded move at a chosen output and duration.</span></div>
      <div><strong>3. Observe</strong><span>Record nose rotation, path drift, and quality.</span></div>
      <div><strong>4. Compare</strong><span>Apply a trim, repeat exactly, and keep the better result.</span></div>
    </div>
    <label class="check-row"><input id="safeCheck" type="checkbox">
      ${workflow === 'bench'
        ? 'I confirm all wheels are securely off the floor and I can stop the robot.'
        : 'I confirm the floor area is clear and I can stop the robot immediately.'}
    </label>`;
  configureFooter({
    primaryText: 'Begin tuning session',
    primaryDisabled: true,
    primaryAction: startSession,
  });
  $('safeCheck').onchange = event => { $('sessionPrimary').disabled = !event.target.checked; };
}

async function startSession() {
  try {
    $('sessionPrimary').disabled = true;
    session = await post('/api/tuning/start', {
      workflow,
      confirmation: workflow === 'bench' ? 'WHEELS_UP' : 'AREA_CLEAR',
    });
    renderSession();
  } catch (error) {
    $('sessionMessage').textContent = error.message;
    $('sessionPrimary').disabled = false;
  }
}

function latestRearRate(name) {
  const test = [...(session.tests || [])].reverse().find(item => item.encoder_rates?.[name] != null);
  return test ? Math.abs(Number(test.encoder_rates[name])).toFixed(1) : '—';
}

function renderWorkbench() {
  const live = session.live_settings || session.proven_baseline || {};
  const tests = session.tests || [];
  $('sessionProgress').style.width = tests.length ? '55%' : '25%';
  $('sessionBody').innerHTML = `
    <div class="cal-step-label">SET PWM · RUN · COMPARE</div>
    <div class="workbench-head">
      <div><h3>Current live PWM</h3><p>Adjustments stop the motors first and remain reversible until you finish.</p></div>
      <label class="compact-select">Button step
        <select id="pwmStep"><option value="1">1</option><option value="2">2</option><option value="3" selected>3</option><option value="5">5</option><option value="10">10</option></select>
      </label>
    </div>
    <div class="pwm-workbench">
      ${PWM_KEYS.map(key => {
        const meta = PWM_META[key];
        return `<div class="pwm-card ${meta.encoder ? 'measured' : ''}">
          <div class="pwm-card-head"><span>${meta.short}</span><small>${meta.encoder ? 'encoder' : 'no encoder'}</small></div>
          <strong>${meta.name}</strong>
          <div class="stepper">
            <button class="secondary" data-pwm-adjust="-1" data-pwm-key="${key}" aria-label="Decrease ${meta.name}">−</button>
            <input id="tune-${key}" aria-label="${meta.name} PWM" type="number" min="0" max="255" value="${pwmValue(live, key)}">
            <button class="secondary" data-pwm-adjust="1" data-pwm-key="${key}" aria-label="Increase ${meta.name}">+</button>
          </div>
          <span class="motor-feedback">${meta.encoder ? `Last rate ${latestRearRate(meta.short)} ticks/s` : 'Visual floor feedback'}</span>
        </div>`;
      }).join('')}
    </div>
    <div class="inline-actions">
      <button id="applySessionPwm">Apply PWM live</button>
      <button id="sessionBaseline" class="secondary">Load ${PWM_KEYS.map(key => pwmValue(session.proven_baseline, key)).join(' / ')}</button>
      <button id="undoPwm" class="secondary" ${(session.adjustments || []).length ? '' : 'disabled'}>Undo last PWM change</button>
      <span id="pwmApplyState" class="muted">Live on ESP32 · not saved</span>
    </div>

    <section class="test-builder">
      <div class="workbench-head"><div><h3>Next controlled test</h3><p>Change one thing at a time, then repeat the same direction, output, and duration.</p></div></div>
      <div class="direction-grid compact">
        ${TUNING_DIRECTIONS.map(direction =>
          [direction, DIRECTION_LABELS[direction]]).map(([direction, label]) =>
          `<button class="secondary ${direction === selectedDirection ? 'selected' : ''}" data-direction="${direction}">${label}</button>`).join('')}
      </div>
      <div class="test-options">
        <label>Test output
          <select id="testMagnitude">
            ${[15, 20, 25, 35, 45, 60, 75, 100].map(value => `<option value="${value / 100}" ${Math.round(testMagnitude * 100) === value ? 'selected' : ''}>${value}%</option>`).join('')}
          </select>
        </label>
        <label>Duration
          <select id="testDuration">
            ${[[750, '0.75 s'], [1000, '1.0 s'], [1500, '1.5 s'], [2000, '2.0 s'], [3000, '3.0 s'], [5000, '5.0 s']]
              .map(([value, label]) => `<option value="${value}" ${testDuration === value ? 'selected' : ''}>${label}</option>`).join('')}
          </select>
        </label>
        <div class="duty-preview"><span>Approx. duty this test</span><strong id="dutyPreview">—</strong></div>
      </div>
      ${workflow === 'imu' ? `<section class="heading-controls"><h3>Heading hold</h3>
        <label class="check-row"><input id="trialHeadingEnabled" type="checkbox" ${live['heading-enabled'] ? 'checked' : ''}>Enable correction for the next run</label>
        <div class="test-options">
          <label>Gain<input id="trialHeadingKp" type="number" min="0" max="5" step="0.05" value="${Number(live['heading-kp'] ?? .7)}"></label>
          <label>Correction limit<input id="trialHeadingLimit" type="number" min="0" max="1" step="0.05" value="${Number(live['heading-max'] ?? .3)}"></label>
          <label>Deadband (degrees)<input id="trialHeadingDeadband" type="number" min="0" max="30" step="0.5" value="${Number(live['heading-deadband-deg'] ?? 1.5)}"></label>
        </div>
        <label class="check-row"><input id="trialHeadingConfirmed" type="checkbox">I have checked the mounted sensor's heading direction and accept it for this trial.</label>
        <button id="applyTrialHeading" class="secondary">Apply heading settings</button>
        <p>Applied: correction ${live['heading-enabled'] ? 'ON' : 'OFF'}. Changes stop motors first. Correction-on stops if heading is lost; correction-off records IMU data without depending on it. Encoder errors are recorded for review.</p>
      </section>` : '<div class="cal-warning subtle">Heading correction starts disabled. The controller is disconnected while the session owns the motors.</div>'}
      <button id="runTest" class="run-test">3-second countdown · run ${DIRECTION_LABELS[selectedDirection]}</button>
    </section>

    ${renderHistory(tests)}
    <div class="finish-save">
      <div><strong>Confident in the current winner?</strong><span>Saving writes the live PWM values to ESP32 nonvolatile storage and ends the session.</span></div>
      <button id="finishSave" class="secondary" ${tests.some(test => test.score) ? '' : 'disabled'}>Save PWM &amp; finish</button>
    </div>`;

  configureFooter({
    primaryText: 'Finish · keep live only',
    primaryAction: () => endSession(true, false),
    secondaryText: 'Restore originals & finish',
    secondaryAction: () => endSession(false, false),
  });
  bindWorkbench();
  updateDutyPreview();
}

function renderHistory(tests) {
  const scored = tests.filter(test => test.score).slice(-6).reverse();
  if (!scored.length) return `
    <section class="history"><h3>Test history</h3><p class="empty-state">No scored tests yet. Your first result becomes the comparison point.</p></section>`;
  return `<section class="history"><div class="workbench-head"><div><h3>Recent scored tests</h3><p>Best is tracked separately for each direction.</p></div></div>
    <div class="history-list">${scored.map(test => {
      const best = session.best_test_ids?.[test.command] === test.id;
      const rates = encoderWheels().map(wheel => `${wheel} ${Math.abs(Number(test.encoder_rates?.[wheel] || 0)).toFixed(0)}`).join(' · ');
      return `<div class="history-row">
        <span>#${test.id}${best ? ' · BEST' : ''}</span>
        <strong>${DIRECTION_LABELS[test.command]}</strong>
        <span>${Math.round((test.magnitude || 1) * 100)}% · ${test.duration_ms / 1000}s · hold ${test.heading_enabled ? 'ON' : 'OFF'} · ${test.score}/5</span>
        <span>${rates} ticks/s</span>
        <span>${PWM_KEYS.map(key => pwmValue(test.settings, key)).join(' / ')}</span>
        <button class="secondary" data-restore-test="${test.id}">Use #${test.id} PWM</button>
      </div>`;
    }).join('')}</div></section>`;
}

function readPwmDraft() {
  const output = {};
  for (const key of PWM_KEYS) {
    const input = $(`tune-${key}`);
    const value = Number(input.value);
    if (!Number.isFinite(value) || value < 0 || value > 255) {
      throw new Error(`${PWM_META[key].name} PWM must be from 0 to 255`);
    }
    output[key] = Math.round(value);
    input.value = output[key];
  }
  return output;
}

function pwmDraftChanged() {
  try {
    const draft = readPwmDraft();
    return PWM_KEYS.some(key => draft[key] !== pwmValue(session.live_settings, key));
  } catch (_) {
    return true;
  }
}

function updatePwmApplyState() {
  const state = $('pwmApplyState');
  if (state) state.textContent = pwmDraftChanged() ? 'Edits not applied yet' : 'Live on ESP32 · not saved';
  updateDutyPreview();
}

function updateDutyPreview() {
  const preview = $('dutyPreview');
  if (!preview) return;
  try {
    const draft = readPwmDraft();
    preview.textContent = PWM_KEYS.map(key => `${PWM_META[key].short} ${Math.round(draft[key] * testMagnitude)}`).join(' · ');
  } catch (_) {
    preview.textContent = 'Fix PWM values';
  }
  const run = $('runTest');
  if (run) run.textContent = `3-second countdown · run ${DIRECTION_LABELS[selectedDirection]}`;
}

function bindWorkbench() {
  document.querySelectorAll('[data-pwm-adjust]').forEach(button => {
    button.onclick = () => {
      const key = button.dataset.pwmKey;
      const amount = Number($('pwmStep').value) * Number(button.dataset.pwmAdjust);
      const input = $(`tune-${key}`);
      input.value = clamp(Math.round(Number(input.value) || 0) + amount, 0, 255);
      updatePwmApplyState();
    };
  });
  PWM_KEYS.forEach(key => { $(`tune-${key}`).oninput = updatePwmApplyState; });
  document.querySelectorAll('[data-direction]').forEach(button => {
    button.onclick = () => {
      selectedDirection = button.dataset.direction;
      document.querySelectorAll('[data-direction]').forEach(item =>
        item.classList.toggle('selected', item.dataset.direction === selectedDirection));
      updateDutyPreview();
    };
  });
  $('testMagnitude').onchange = event => {
    testMagnitude = Number(event.target.value);
    updateDutyPreview();
  };
  $('testDuration').onchange = event => { testDuration = Number(event.target.value); };
  $('applySessionPwm').onclick = applySessionPwm;
  $('sessionBaseline').onclick = async () => {
    try {
      session = await post('/api/tuning/baseline');
      $('sessionMessage').textContent = 'Proven baseline applied live · not saved';
      renderWorkbench();
    } catch (error) { $('sessionMessage').textContent = error.message; }
  };
  $('undoPwm').onclick = async () => {
    const adjustment = (session.adjustments || []).at(-1);
    if (!adjustment) return;
    try {
      session = await post('/api/tuning/settings', {
        settings: adjustment.before, source: 'undo-last-change',
      });
      renderWorkbench();
      $('sessionMessage').textContent = 'Previous PWM restored live · ready for a repeat';
    } catch (error) { $('sessionMessage').textContent = error.message; }
  };
  document.querySelectorAll('[data-restore-test]').forEach(button => {
    button.onclick = () => restoreTestPwm(Number(button.dataset.restoreTest));
  });
  $('runTest').onclick = runTest;
  if ($('applyTrialHeading')) $('applyTrialHeading').onclick = async () => {
    try {
      session = await post('/api/tuning/heading', {
        enabled: $('trialHeadingEnabled').checked,
        kp: Number($('trialHeadingKp').value),
        limit: Number($('trialHeadingLimit').value),
        deadband: Number($('trialHeadingDeadband').value),
        confirmation: $('trialHeadingConfirmed').checked ? 'HEADING_MEASURED' : null,
      });
      renderWorkbench();
      $('sessionMessage').textContent = 'Heading settings sent. Run checks verify live sensor and mode before motion.';
    } catch (error) { $('sessionMessage').textContent = error.message; }
  };
  $('finishSave').onclick = () => endSession(true, true);
}

async function applySessionPwm() {
  try {
    const settings = readPwmDraft();
    $('applySessionPwm').disabled = true;
    session = await post('/api/tuning/settings', { settings, source: 'manual' });
    renderWorkbench();
    $('sessionMessage').textContent = 'PWM applied live · run a test before saving';
  } catch (error) {
    $('sessionMessage').textContent = error.message;
    if ($('applySessionPwm')) $('applySessionPwm').disabled = false;
  }
}

async function runTest() {
  const token = ++runToken;
  try {
    const settings = readPwmDraft();
    if (PWM_KEYS.some(key => settings[key] !== pwmValue(session.live_settings, key))) {
      session = await post('/api/tuning/settings', { settings, source: 'pre-test' });
    }
    for (let count = 3; count > 0; count -= 1) {
      if (token !== runToken) return;
      renderCountdown(count);
      await delay(1000);
    }
    if (token !== runToken) return;
    const calibration = await post('/api/calibration/pulse', {
      direction: selectedDirection,
      duration_ms: testDuration,
      magnitude: testMagnitude,
    });
    renderRunning(calibration);
    let current = calibration;
    while (current.running && token === runToken) {
      await delay(150);
      current = await request('/api/calibration');
      updateRunningTelemetry(current);
    }
    if (token !== runToken) return;
    session = await request('/api/tuning');
    renderSession();
  } catch (error) {
    $('sessionMessage').textContent = error.message;
    session = await request('/api/tuning').catch(() => session);
    renderSession();
  }
}

function renderCountdown(count) {
  $('sessionProgress').style.width = '35%';
  $('sessionBody').innerHTML = `
    <div class="cal-step-label">CLEAR THE ROBOT</div>
    <div class="countdown"><strong>${count}</strong><span>${DIRECTION_LABELS[selectedDirection]} starts automatically</span></div>
    <div class="cal-warning">Use STOP NOW at any point. The test will also stop automatically after ${(testDuration / 1000).toFixed(2).replace(/0+$/, '').replace(/\.$/, '')} seconds.</div>`;
  configureFooter({ primaryText: 'Test pending…', primaryDisabled: true });
  $('sessionMessage').textContent = `PWM live · ${Math.round(testMagnitude * 100)}% output`;
}

function renderRunning(calibration) {
  $('sessionBody').innerHTML = `
    <div class="cal-step-label">MOTORS RUNNING · AUTOMATIC STOP ARMED</div>
    <div class="running-test"><strong>${DIRECTION_LABELS[calibration.direction || selectedDirection]}</strong><span id="runTime">0.0 / ${(Number(calibration.duration_ms || testDuration) / 1000).toFixed(1)} s</span></div>
    <div class="run-meter"><i id="runMeter"></i></div>
    <div class="live-rate-grid">
      ${encoderWheels().map(wheel => `<div><span>${wheel} live delta</span><strong id="live${wheel}">0</strong></div>`).join('')}
    </div>
    <p class="sensor-caveat">Pulse response includes startup; compare identical tests on the same wheel.</p>`;
  configureFooter({ primaryText: 'Running…', primaryDisabled: true });
  updateRunningTelemetry(calibration);
}

function updateRunningTelemetry(calibration) {
  if (!$('runMeter')) return;
  const elapsed = Number(calibration.elapsed_ms || 0);
  const duration = Number(calibration.duration_ms || testDuration);
  $('runMeter').style.width = `${clamp(elapsed / duration * 100, 0, 100)}%`;
  $('runTime').textContent = `${(elapsed / 1000).toFixed(1)} / ${(duration / 1000).toFixed(1)} s`;
  const deltas = calibration.live_deltas || [0, 0, 0, 0];
  ['FL', 'FR', 'RL', 'RR'].forEach((wheel, index) => {
    if ($(`live${wheel}`)) $(`live${wheel}`).textContent = deltas[index] ?? 0;
  });
  $('sessionProgress').style.width = `${35 + clamp(elapsed / duration, 0, 1) * 25}%`;
  $('sessionMessage').textContent = `Running at ${Math.round(Number(calibration.magnitude || testMagnitude) * 100)}% output`;
}

function rearComparison(test, name) {
  if (test.comparison && test.comparison.comparable === false) {
    return `Not compared with #${test.comparison.test_id}: ${test.comparison.reason}`;
  }
  const info = test.comparison?.wheels?.[name] || test.comparison?.rear?.[name];
  if (!info) return 'First run in this direction';
  if (info.rate_change_percent == null) return `Prior test #${test.comparison.test_id} had no usable rate`;
  return `${signed(info.rate_change_percent)}% vs test #${test.comparison.test_id} · PWM ${signed(info.pwm_change)}`;
}

function pathOptions(command) {
  if (command === 'forward' || command === 'reverse') return [
    ['none', 'Held the line'], ['drift-left', 'Path drifted left'],
    ['drift-right', 'Path drifted right'], ['unsure', 'Not sure'],
  ];
  if (command === 'left' || command === 'right') return [
    ['none', 'Stayed sideways'], ['drift-forward', 'Path drifted forward'],
    ['drift-back', 'Path drifted backward'], ['unsure', 'Not sure'],
  ];
  return [['none', 'Rotation stayed centered'], ['unsure', 'Not sure']];
}

function radioCards(name, options, checkedValue) {
  return `<div class="observation-grid">${options.map(([value, label]) =>
    `<label><input type="radio" name="${name}" value="${value}" ${value === checkedValue ? 'checked' : ''}><span>${label}</span></label>`).join('')}</div>`;
}

function renderObservation(test) {
  $('sessionProgress').style.width = '68%';
  const magnitude = Math.round(Number(test.magnitude || 1) * 100);
  const chassisQuestions = workflow === 'bench' ? `
    <div class="cal-warning subtle">Wheels-up tests cannot reveal chassis heading or path error. Score wheel response quality, then review the encoder-based recommendation and repeat.</div>` : `
    <fieldset><legend>Did the robot’s nose rotate? <small>Left = counterclockwise viewed from above</small></legend>
      ${radioCards('heading', [['straight', 'No rotation'], ['yaw-left', 'Nose turned left'], ['yaw-right', 'Nose turned right'], ['unsure', 'Not sure']], 'straight')}
    </fieldset>
    <fieldset><legend>Did the path move off-axis?</legend>
      ${radioCards('path', pathOptions(test.command), 'none')}
    </fieldset>`;
  $('sessionBody').innerHTML = `
    <div class="cal-step-label">TEST #${test.id} COMPLETE · DESCRIBE WHAT HAPPENED</div>
    <div class="result-head"><div><h3>${DIRECTION_LABELS[test.command]}</h3><p>${magnitude}% output · ${(test.duration_ms / 1000).toFixed(2).replace(/0+$/, '').replace(/\.$/, '')} s</p></div><span>Auto-stopped</span></div>
    <div class="rear-feedback">
      ${encoderWheels().map(name => `<div class="rate-card">
        <span>${name} encoder</span>
        <strong>${test.encoder_measurement?.valid ? Number(test.encoder_measurement.rpm[name]).toFixed(1) : Math.abs(Number(test.encoder_rates?.[name] || 0)).toFixed(1)} <small>${test.encoder_measurement?.valid ? "steady RPM" : "pulse ticks/s"}</small></strong>
        <em>${rearComparison(test, name)}</em>
      </div>`).join('')}
    </div>
    <p class="sensor-caveat">${test.encoder_measurement ? test.encoder_measurement.reason : "Legacy pulse rates include startup; repeat to collect steady RPM."}</p>
    <p class="sensor-caveat">${test.heading_measurement?.summary || "No recorded heading measurement for this run."}</p>
    <div class="actual-duty">${PWM_KEYS.map(key => `<div><span>${PWM_META[key].short} duty</span><strong>${test.actual_duty?.[key] ?? '—'}</strong><small>cap ${pwmValue(test.settings, key)}</small></div>`).join('')}</div>

    ${chassisQuestions}
    <fieldset><legend>Motion quality</legend>
      ${radioCards('quality', [['normal', 'Smooth / normal'], ['jerky', 'Jerky or intermittent'], ['no-motion', 'Stalled / no motion']], 'normal')}
    </fieldset>
    <div class="score-row">
      <label>Overall quality <strong id="scoreValue">3 / 5</strong><input id="score" type="range" min="1" max="5" value="3"></label>
      <label>Suggested trim size<select id="adjustmentStep"><option value="1">1 · fine</option><option value="2">2</option><option value="3" selected>3 · normal</option><option value="5">5 · coarse</option><option value="10">10 · diagnostic</option></select></label>
    </div>
    <label class="notes-label">Notes<textarea id="notes" rows="3" placeholder="Surface, battery, distance, sound, traction…"></textarea></label>`;
  $('score').oninput = event => { $('scoreValue').textContent = `${event.target.value} / 5`; };
  configureFooter({
    primaryText: 'Analyze & recommend PWM',
    primaryAction: () => submitObservation(test),
  });
}

async function submitObservation(test) {
  try {
    $('sessionPrimary').disabled = true;
    const value = name => document.querySelector(`input[name="${name}"]:checked`)?.value;
    session = await post('/api/tuning/observe', {
      test_id: test.id,
      observation: value('heading'),
      heading: value('heading'),
      path: value('path'),
      quality: value('quality'),
      score: Number($('score').value),
      adjustment_step: Number($('adjustmentStep').value),
      notes: $('notes').value,
    });
    renderSession();
  } catch (error) {
    $('sessionMessage').textContent = error.message;
    $('sessionPrimary').disabled = false;
  }
}

function renderRecommendation(test) {
  const recommendation = test.recommendation;
  const hasTrim = recommendation.kind === 'pwm-vector';
  const priorBest = (session.tests || []).find(item => item.id === test.prior_best_id);
  const scoreComparison = priorBest ? `
    <div class="score-comparison ${test.score < priorBest.score ? 'worse' : ''}">
      <div><strong>${test.score > priorBest.score ? 'Better than' : test.score < priorBest.score ? 'Worse than' : 'Tied with'} the prior best for ${DIRECTION_LABELS[test.command]}</strong>
      <span>Current #${test.id}: ${test.score}/5 · prior #${priorBest.id}: ${priorBest.score}/5. ${test.score < priorBest.score ? 'Restore the winner before trying a smaller trim.' : 'Repeat before deciding this is the new winner.'}</span></div>
      <button id="restorePriorBest" class="secondary">Restore #${priorBest.id} PWM</button>
    </div>` : '';
  $('sessionProgress').style.width = '82%';
  $('sessionBody').innerHTML = `
    <div class="cal-step-label">TEST #${test.id} · FEEDBACK</div>
    <div class="recommendation ${hasTrim ? '' : 'hold'}">
      <span>${hasTrim ? 'SUGGESTED NEXT TRIM' : 'REPEAT / HOLD'}</span>
      <strong>${recommendation.summary}</strong>
      <p>${recommendation.basis}</p>
    </div>
    <div class="trim-grid">
      ${PWM_KEYS.map(key => {
        const before = pwmValue(test.settings, key);
        const after = pwmValue(recommendation.suggested_settings, key);
        const delta = Number(recommendation.deltas?.[key] || 0);
        return `<div class="trim-card ${delta ? 'changed' : ''}"><span>${PWM_META[key].short}</span><strong>${before} <i>→</i> ${after}</strong><small>${delta ? `${signed(delta)} PWM` : 'no change'}</small></div>`;
      }).join('')}
    </div>
    ${scoreComparison}
    <div class="repeat-card"><strong>Required comparison</strong><p>Repeat <b>${DIRECTION_LABELS[test.command]}</b> at <b>${Math.round(Number(test.magnitude || 1) * 100)}%</b> for <b>${(test.duration_ms / 1000).toFixed(2).replace(/0+$/, '').replace(/\.$/, '')} s</b>. Changing the setup makes the A/B result ambiguous.</p></div>
    <div class="cal-warning subtle">${recommendation.source === "encoders" ? "Encoder RPM determines this trim. Apply it, then repeat at the same output and direction. No automatic motor run or continuous PID." : "This trim is based on chassis observations."}</div>`;

  const skip = () => {
    dismissedRecommendations.add(test.id);
    selectedDirection = test.command;
    testMagnitude = Number(test.magnitude || testMagnitude);
    testDuration = Number(test.duration_ms || testDuration);
    renderWorkbench();
    $('sessionMessage').textContent = 'Suggestion skipped · edit manually or repeat current values';
  };
  configureFooter({
    primaryText: hasTrim ? 'Apply trim · prepare repeat' : 'Prepare exact repeat',
    primaryAction: hasTrim ? () => applyRecommendation(test) : skip,
    secondaryText: hasTrim ? 'Skip · edit manually' : '',
    secondaryAction: hasTrim ? skip : null,
  });
  if ($('restorePriorBest')) {
    $('restorePriorBest').onclick = () => restoreTestPwm(priorBest.id);
  }
}

async function restoreTestPwm(testId) {
  const test = (session.tests || []).find(item => item.id === testId);
  if (!test) return;
  try {
    session = await post('/api/tuning/settings', {
      settings: Object.fromEntries(PWM_KEYS.map(key => [key, pwmValue(test.settings, key)])),
      source: `restore-test-${testId}`,
    });
    const latest = lastTest();
    if (latest) dismissedRecommendations.add(latest.id);
    selectedDirection = test.command;
    testMagnitude = Number(test.magnitude || testMagnitude);
    testDuration = Number(test.duration_ms || testDuration);
    renderWorkbench();
    $('sessionMessage').textContent = `Test #${testId} PWM restored live · exact setup selected`;
  } catch (error) { $('sessionMessage').textContent = error.message; }
}

async function applyRecommendation(test) {
  try {
    $('sessionPrimary').disabled = true;
    session = await post('/api/tuning/recommendation', { test_id: test.id });
    selectedDirection = test.command;
    testMagnitude = Number(test.magnitude || testMagnitude);
    testDuration = Number(test.duration_ms || testDuration);
    dismissedRecommendations.add(test.id);
    renderWorkbench();
    $('sessionMessage').textContent = `Trim applied live · repeat test #${test.id} setup`;
  } catch (error) {
    $('sessionMessage').textContent = error.message;
    $('sessionPrimary').disabled = false;
  }
}

async function endSession(keepLive, save) {
  try {
    runToken += 1;
    session = await post('/api/tuning/end', { keep_live: keepLive, save });
    $('sessionModal').hidden = true;
    await loadSettings();
    await refresh();
    $('message').textContent = save
      ? 'Winning PWM saved to ESP32'
      : (keepLive ? 'Tuned PWM kept live; not saved' : 'Original settings restored');
  } catch (error) {
    $('sessionMessage').textContent = error.message;
  }
}

$('sessionStop').onclick = async () => {
  runToken += 1;
  try {
    await post('/api/stop');
    for (let attempt = 0; attempt < 20; attempt += 1) {
      const calibration = await request('/api/calibration');
      if (!calibration.running) break;
      await delay(100);
    }
    session = await request('/api/tuning');
    renderSession();
    $('sessionMessage').textContent = 'Motors stopped · session and live PWM retained';
  } catch (error) { $('sessionMessage').textContent = error.message; }
};
$('closeSession').onclick = () => { $('sessionModal').hidden = true; };
document.querySelectorAll('[data-workflow]').forEach(button => {
  button.onclick = () => openSession(button.dataset.workflow);
});

$('applyBaseline').onclick = async () => {
  try {
    baselinePrevious = { board_id: status.board?.id, values: PWM_KEYS.map(key => Number($(key).value)) };
    localStorage.setItem('mechbot-baseline-previous', JSON.stringify(baselinePrevious));
    if (session.active) {
      session = await post('/api/tuning/baseline');
    } else {
      if (!status.board?.id || status.board.id === 'unknown') throw new Error('Identify the connected controller first');
      for (const [key, value] of Object.entries(status.board.baseline)) await post('/api/settings', { key, value });
    }
    await loadSettings();
    $('message').textContent = 'Baseline applied live; not saved';
  } catch (error) { $('message').textContent = error.message; }
};

$('revert').onclick = async () => {
  try {
    if (session.active) return endSession(false, false);
    if (!baselinePrevious) {
      $('message').textContent = 'No baseline change to revert';
      return;
    }
    if (baselinePrevious.board_id !== status.board?.id) throw new Error('Previous settings belong to another or unidentified board');
    for (let index = 0; index < PWM_KEYS.length; index += 1) {
      await post('/api/settings', { key: PWM_KEYS[index], value: baselinePrevious.values[index] });
    }
    baselinePrevious = null;
    localStorage.removeItem('mechbot-baseline-previous');
    await loadSettings();
    $('message').textContent = 'Previous motor values restored live';
  } catch (error) { $('message').textContent = error.message; }
};

$('stop').onclick = async () => {
  try { await post('/api/stop'); $('freshness').textContent = 'Motors stopped'; }
  catch (error) { $('freshness').textContent = error.message; }
};
$('reload').onclick = loadSettings;
$('apply').onclick = async () => {
  if (session.active) {
    $('message').textContent = 'Use the PWM controls inside the active tuning session';
    return;
  }
  try {
    for (const id of SETTING_IDS) {
      const element = $(id);
      const value = id === 'heading-enabled' ? (element.checked ? 1 : 0) : Number(element.value);
      await post('/api/settings', { key: id, value });
    }
    $('message').textContent = 'Applied live; not saved';
  } catch (error) { $('message').textContent = error.message; }
};
$('save').onclick = async () => {
  if (session.active) {
    $('message').textContent = 'Finish the session before saving so heading state is restored safely';
    return;
  }
  try {
    await post('/api/settings/save');
    $('message').textContent = 'Saved to ESP32';
  } catch (error) { $('message').textContent = error.message; }
};

async function firmwareStatus() {
  try {
    const job = await request('/api/firmware');
    $('firmwareJobState').textContent = job.state.toUpperCase();
    $('firmwareJobState').className = `job-state ${job.state}`;
    $('firmwareLog').textContent = (job.lines || []).join('\n') || 'No update running.';
  } catch (error) { $('firmwareLog').textContent = error.message; }
}

$('updateFirmware').onclick = async () => {
  if (confirm(`Compile and flash ${status.board?.name}? Raise the wheels, switch motor power off, and disconnect the gamepad first.`)) {
    try { await post('/api/firmware/start'); await firmwareStatus(); }
    catch (error) { $('firmwareLog').textContent = error.message; }
  }
};

['heading-enabled', 'heading-max'].forEach(id => {
  $(id).addEventListener('input', renderControlModel);
  $(id).addEventListener('change', renderControlModel);
});

loadSettings();
refresh();
firmwareStatus();
setInterval(refresh, 1000);
setInterval(firmwareStatus, 2500);
