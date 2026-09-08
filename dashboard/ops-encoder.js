/* Passive encoder observation. No motion or configuration endpoints. */
(() => {
  const UI = window.UI, math = window.EncoderWindow;
  if (!UI || !math) return;
  const wheels = ['FL', 'FR', 'RL', 'RR'];
  const state = {active: false, start: null, last: null, result: null, error: null};
  UI.encoderCheck = state;
  function available(data) {
    if (UI.replay) throw new Error('Exit replay before observing live encoders.');
    return math.snapshot(data?.observed, data?.mode, data?.bridge?.serial_connected);
  }
  function render(ready, reason) {
    if (!UI.el('encoderStart')) return;
    UI.el('encoderStart').disabled = state.active || !ready;
    UI.el('encoderFinish').disabled = !state.active;
    UI.el('encoderDownload').disabled = !state.start || state.active;
    let message = reason || 'Ready · IMU not required. Start a new observation.';
    if (state.error) message = 'Observation invalidated: '+state.error+' Start a new observation when ready.';
    else if (state.result) {
      const result = state.result;
      message = (state.active ? 'Observing' : 'Finished')+' · '+result.mode.toUpperCase()+' · '+UI.fmt(result.elapsed_s, 1)+' s · '+
        (result.invalid_observed ? 'New invalid transitions observed.' : result.movement_observed ?
          'Movement observed; no new invalid transitions in this window.' : 'No movement observed; motion validation remains pending.');
    }
    UI.text('encoderCheckState', message);
    const rows = !state.error && state.result ? wheels.map(wheel => {
      const values = state.result.wheels[wheel], row = UI.node('tr');
      [wheel, values.count_delta, UI.fmt(values.revolutions, 3), values.a_edges, values.b_edges, values.invalid_transitions]
        .forEach(value => row.appendChild(UI.node('td', String(value))));
      return row;
    }) : [];
    UI.el('encoderCheckRows').replaceChildren(...rows);
  }
  UI.updateEncoderCheck = function(data) {
    let current = null, reason = null;
    try {
      current = available(data);
      if (state.active) {
        math.difference(state.last, current); // Latch a reset even if counters later exceed the starting value.
        if (current.host_time-state.last.host_time > 3) throw new Error('Telemetry observation gap exceeded 3 seconds.');
        state.result = math.difference(state.start, current);
        state.last = current;
      }
    } catch (error) {
      reason = error.message;
      if (state.active) { state.error = reason; state.active = false; }
    }
    render(!!current, reason);
  };
  UI.startEncoderCheck = async function() {
    const data = await UI.api('/api/operations');
    const current = available(data);
    Object.assign(state, {active: true, start: current, last: current, result: math.difference(current, current), error: null});
    render(true);
  };
  UI.finishEncoderCheck = async function() {
    if (!state.active) return;
    try { UI.updateEncoderCheck(await UI.api('/api/operations')); }
    catch (error) { state.error = error.message; }
    state.active = false;
    render(!state.error);
  };
  UI.encoderCheckReport = () => ({schema: 'mechbot-encoder-observation-v1',
    valid_window: !!state.start && !state.active && !state.error, error: state.error,
    mode: state.start?.mode || null, start: state.start, end: state.last, result: state.result,
    physical_validation: 'Not established by this observation',
    scope: 'Browser observation of wheel totals and asynchronous diagnostic samples; no motor commands sent.'});
  document.addEventListener('DOMContentLoaded', () => {
    UI.el('encoderStart').addEventListener('click', UI.run(UI.startEncoderCheck));
    UI.el('encoderFinish').addEventListener('click', UI.run(UI.finishEncoderCheck));
    UI.el('encoderDownload').addEventListener('click', UI.run(() => {
      if (!state.start || state.active) throw new Error('Finish the observation before downloading.');
      const blob = new Blob([JSON.stringify(UI.encoderCheckReport(), null, 2)], {type: 'application/json'});
      const url = URL.createObjectURL(blob), link = document.createElement('a');
      link.href = url; link.download = 'encoder-observation-'+state.start.mode+'.json';
      link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
    }));
  });
})();
