/* Passive observations and bounded plots; this module sends no commands. */
(() => {
  const UI = window.UI;
  if (!UI) return;
  const wheels = ['FL', 'FR', 'RL', 'RR'], colors = {FL:'#59d4cf', FR:'#f4b85f', RL:'#b6a3f5', RR:'#f08da6'};
  const finite = Number.isFinite, history = samples => Array.isArray(samples) ? samples.slice(-300) : [];
  const timestamp = sample => finite(sample?.host_time) && sample.host_time >= 0;
  const goodPose = pose => pose?.valid === true && finite(pose.x_m) && finite(pose.y_m);
  const degrees = yaw => finite(yaw) ? (yaw % (2 * Math.PI)) * 180 / Math.PI : null;
  const filledGeometry = new Set();
  function frame(canvas) {
    const ctx = canvas?.getContext?.('2d'), w = canvas?.width, h = canvas?.height;
    if (!ctx || !finite(w) || !finite(h) || w < 140 || h < 100) return null;
    ctx.clearRect(0, 0, w, h); ctx.font = '14px system-ui'; ctx.textAlign = 'left'; ctx.fillStyle = '#96a8bb';
    return {ctx, w, h, left:65, right:w-25, top:28, bottom:h-38};
  }
  function grid(f) {
    const {ctx, left, right, top, bottom} = f;
    ctx.strokeStyle = '#293b4c'; ctx.lineWidth = 1; ctx.beginPath();
    for (let i = 0; i <= 4; i++) {
      const x = left + (right-left)*i/4, y = top + (bottom-top)*i/4;
      ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.moveTo(x, top); ctx.lineTo(x, bottom);
    }
    ctx.stroke();
  }
  function waiting(f, message) { f.ctx.textAlign = 'center'; f.ctx.fillText(message, f.w/2, f.h/2); }
  UI.drawRates = function(canvas, samples) {
    const rows = history(samples), ready = s => timestamp(s) && s.rates?.valid === true;
    const key = rows.some(s => ready(s) && wheels.some(w => finite(s.rates?.rpm?.[w]))) ? 'rpm' : 'ticks_per_s';
    const unit = key === 'rpm' ? 'RPM' : 'ticks/s', f = frame(canvas);
    if (!f) return unit;
    const valid = rows.filter(s => ready(s) && wheels.some(w => finite(s.rates?.[key]?.[w])));
    if (!valid.length) { waiting(f, 'Waiting for timestamped wheel samples'); return unit; }
    const times = rows.filter(timestamp).map(s => s.host_time), first = Math.min(...times), elapsed = Math.max(...times)-first;
    const peak = Math.max(1, ...valid.flatMap(s => wheels.map(w => finite(s.rates?.[key]?.[w]) ? Math.abs(s.rates[key][w]) : 0)));
    const bound = peak < Number.MAX_VALUE/1.1 ? peak*1.1 : peak, {ctx, left, right, top, bottom} = f;
    const x = t => left + ((t-first)/(elapsed || 1))*(right-left), y = value => (top+bottom)/2 - (value/bound)*(bottom-top)/2;
    grid(f); ctx.fillText(unit, left, 18); ctx.fillText(UI.fmt(bound, 1), 2, top+5);
    ctx.fillText('0', 2, (top+bottom)/2+5); ctx.fillText(UI.fmt(-bound, 1), 2, bottom+5);
    ctx.fillText('0 s', left, f.h-10); ctx.textAlign = 'right'; ctx.fillText(UI.fmt(elapsed, 1)+' s elapsed', right, f.h-10);
    wheels.forEach(wheel => {
      ctx.strokeStyle = colors[wheel]; ctx.fillStyle = colors[wheel]; ctx.lineWidth = 2; ctx.beginPath(); let previous = null;
      rows.forEach(s => {
        const value = s?.rates?.[key]?.[wheel];
        if (!ready(s) || !finite(value)) { previous = null; return; }
        const px = x(s.host_time), py = y(value);
        if (previous === null || s.host_time <= previous) ctx.moveTo(px, py); else ctx.lineTo(px, py);
        ctx.fillRect(px-2, py-2, 4, 4); previous = s.host_time;
      });
      ctx.stroke();
    });
    return unit;
  };
  UI.drawPose = function(canvas, samples, pose) {
    const f = frame(canvas); if (!f) return;
    const rows = history(samples), ready = s => timestamp(s) && goodPose(s.pose), valid = rows.filter(ready);
    if (!valid.length) { waiting(f, 'Waiting for valid pose samples'); return; }
    const points = valid.map(s => s.pose); if (goodPose(pose)) points.push(pose);
    // Normalize before range arithmetic so opposite extreme finite coordinates cannot overflow.
    const magnitude = Math.max(1, ...points.flatMap(p => [Math.abs(p.x_m), Math.abs(p.y_m)]));
    const xs = [0, ...points.map(p => p.x_m/magnitude)], ys = [0, ...points.map(p => p.y_m/magnitude)];
    const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
    const {ctx, left, right, top, bottom} = f, cx = (left+right)/2, cy = (top+bottom)/2;
    const scale = Math.min((right-left)/Math.max(maxX-minX, 0.1/magnitude), (bottom-top)/Math.max(maxY-minY, 0.1/magnitude))*0.85;
    const x = value => cx + (value/magnitude-(minX+maxX)/2)*scale;
    const y = value => cy - (value/magnitude-(minY+maxY)/2)*scale;
    grid(f); ctx.fillText('Wheel odometry · meters · +x forward / +y left', left, 18);
    ctx.fillText('x: '+UI.fmt(minX*magnitude, 2)+' to '+UI.fmt(maxX*magnitude, 2)+' m', left, f.h-10);
    ctx.textAlign = 'right'; ctx.fillText('y: '+UI.fmt(minY*magnitude, 2)+' to '+UI.fmt(maxY*magnitude, 2)+' m', right, f.h-10);
    ctx.strokeStyle = '#96a8bb'; ctx.beginPath(); ctx.moveTo(x(0)-5, y(0)); ctx.lineTo(x(0)+5, y(0));
    ctx.moveTo(x(0), y(0)-5); ctx.lineTo(x(0), y(0)+5); ctx.stroke();
    ctx.strokeStyle = colors.FL; ctx.fillStyle = colors.FL; ctx.lineWidth = 2; ctx.beginPath(); let previous = null;
    rows.forEach(s => {
      if (!ready(s)) { previous = null; return; }
      const px = x(s.pose.x_m), py = y(s.pose.y_m);
      if (previous === null || s.host_time <= previous) ctx.moveTo(px, py); else ctx.lineTo(px, py);
      ctx.fillRect(px-2, py-2, 4, 4); previous = s.host_time;
    });
    ctx.stroke();
    if (goodPose(pose) && finite(pose.yaw_rad)) {
      const px = x(pose.x_m), py = y(pose.y_m), angle = pose.yaw_rad % (2*Math.PI);
      const tipX = px+18*Math.cos(angle), tipY = py-18*Math.sin(angle);
      ctx.strokeStyle = colors.FR; ctx.beginPath(); ctx.moveTo(px, py); ctx.lineTo(tipX, tipY);
      [-0.55, 0.55].forEach(offset => { ctx.moveTo(tipX, tipY); ctx.lineTo(tipX-7*Math.cos(angle+offset), tipY+7*Math.sin(angle+offset)); }); ctx.stroke();
    }
  };
  const age = record => finite(record?.age_s) && record.age_s >= 0 ? UI.fmt(record.age_s, 1)+' s old' : 'age unknown';
  const reason = (record, fallback) => typeof record?.reason === 'string' ? record.reason.replaceAll('_', ' ') : fallback;
  function replace(id, children) { UI.el(id)?.replaceChildren(...children); }
  UI.renderLive = function(observed, mode, bridge = {}) {
    const offline = !observed, data = observed || {}, profile = data.profile || {}, rates = data.rates, imu = data.imu;
    const label = offline ? 'OFFLINE' : mode === 'simulated' ? 'SIMULATED' : mode === 'replay' ? 'REPLAY' : 'LIVE';
    const supported = Array.isArray(profile.encoder_wheels) ? profile.encoder_wheels : [];
    const calibrated = wheels.every(w => supported.includes(w) && finite(profile.counts_per_revolution?.[w]) && profile.counts_per_revolution[w] > 0);
    const geometryKeys = ['wheel_diameter_m', 'wheelbase_m', 'track_width_m'];
    const measured = geometryKeys.every(key => finite(data.geometry?.[key]) && data.geometry[key] > 0 && data.geometry[key] <= 3);
    const freshRates = rates?.fresh === true && rates.valid === true, freshImu = imu?.fresh === true && imu.valid === true && finite(imu.yaw_rad);
    const tracking = measured && goodPose(data.pose) && freshRates;
    UI.text('modeBadge', label);
    UI.text('connection', offline ? 'Disconnected · stale data cleared' : bridge?.serial_connected === true ? 'Connected · '+(bridge.serial_port || 'controller') : 'Disconnected');
    UI.text('boardName', offline ? 'Unknown controller' : profile.name || 'Unidentified controller');
    const navigationAvailable = !offline && mode === 'live' && !UI.replay && profile.imu_transport === 'uart-rvc' && bridge?.serial_connected === true;
    for (const id of ['navRobot','navField','navZero','navRevoke','navAccept']) {
      if (UI.el(id)) UI.el(id).disabled = !navigationAvailable;
    }
    UI.text('navStatus', navigationAvailable ? 'Commands stop first. A sensor fault requires renewed heading acceptance and an explicit frame selection.' : 'Integrated RVC firmware and a live connection are required.');
    UI.text('rateState', offline ? 'Unknown · offline' : freshRates ? 'Fresh feedback' : reason(rates, 'Awaiting samples')+(rates ? ' · '+age(rates) : ''));
    UI.text('imuState', offline ? 'Unknown · offline' : freshImu ? UI.fmt(degrees(imu.yaw_rad), 1)+'° · '+age(imu) : imu?.reason === 'offline' ? 'Sensor offline · '+age(imu) : 'Waiting/stale'+(imu ? ' · '+reason(imu, 'invalid')+' · '+age(imu) : ''));
    const rvc = data.rvc;
    UI.text('rvcDetail', '');
    if (imu?.transport === 'uart-rvc' && !offline) {
      UI.text('imuState', freshImu ? 'RVC yaw '+UI.fmt(degrees(imu.yaw_rad), 2)+'° · '+age(imu) : 'RVC · '+reason(imu, 'stale'));
      UI.text('rvcDetail', (imu.heading_convention === 'ccw-positive' ? 'Counterclockwise-positive heading. ' : 'Raw yaw; heading convention unverified. ')+
        (imu.control_ready === true && freshImu ? 'Heading accepted for this session. ' : 'Heading acceptance required. ')+
        'Calibration status and gyro unavailable; acceleration is raw mg. Checksum '+UI.fmt(imu.bad_checksum,0)+
        ' · index '+UI.fmt(imu.discontinuities,0)+' · UART '+UI.fmt(imu.uart_errors,0)+'.');
    }
    if (rvc && !offline) {
      const current = rvc.valid === true && rvc.run?.fresh === true && rvc.value?.fresh === true;
      UI.text('imuState', current ? 'RVC yaw '+UI.fmt(rvc.value.ypr_deg?.[0], 2)+'° · '+age(rvc.value) : 'RVC · '+reason(rvc, 'awaiting report'));
      const rate = rvc.run?.fresh === true && rvc.run.window_ms > 0 ? rvc.run.new*1000/rvc.run.window_ms : null;
      const counts = rvc.counts?.fresh === true ? rvc.counts : null;
      const uart = rvc.uart?.fresh === true ? rvc.uart.totals : null;
      UI.text('rvcDetail', UI.fmt(rate, 0)+' frames/s · checksum '+UI.fmt(counts?.bad_checksum, 0)+
        ' · index '+UI.fmt(counts?.discontinuities, 0)+' · UART '+UI.fmt(Array.isArray(uart) ? uart.reduce((a,b)=>a+b,0) : null, 0)+
        '. Sensor diagnostic; heading accuracy unverified. Gyro and calibration status unavailable.');
    }
    UI.text('poseState', offline ? 'Unknown · offline' : !measured ? 'Geometry required' : tracking ? 'Tracking' : 'Paused');
    UI.text('rateUnits', UI.drawRates(UI.el('speedChart'), data.samples)+' · signed wheel feedback');
    UI.drawPose(UI.el('poseChart'), data.samples, tracking ? data.pose : null);
    replace('wheelCards', wheels.map(wheel => {
      const available = supported.includes(wheel), rpm = rates?.rpm?.[wheel], ticks = rates?.ticks_per_s?.[wheel];
      const value = available && freshRates ? finite(rpm) ? UI.fmt(rpm, 1)+' RPM' : finite(ticks) ? UI.fmt(ticks, 1)+' ticks/s' : '—' : '—';
      const status = !available ? profile.id === 'unknown' || offline ? 'Unknown capability' : 'Not supported' : !freshRates ? (rates?.reason === 'stale' ? 'Stale feedback · '+age(rates) : 'Awaiting feedback') : 'Supported · '+age(rates);
      const card = UI.node('div', '', 'wheel-card'); card.style.borderColor = colors[wheel];
      card.appendChild(UI.node('strong', wheel)); card.appendChild(UI.node('div', value, 'value')); card.appendChild(UI.node('small', status, 'label')); return card;
    }));
    replace('diagnosticRows', wheels.map(wheel => {
      const d = data.diagnostics?.[wheel], row = UI.node('tr', '', d && d.fresh !== true ? 'stale' : '');
      [wheel, UI.fmt(d?.pwm, 0), UI.fmt(d?.a_edges, 0), UI.fmt(d?.b_edges, 0), UI.fmt(d?.invalid_transitions, 0), d ? age(d)+(d.fresh === true ? '' : ' · stale') : '—']
        .forEach(value => row.appendChild(UI.node('td', value))); return row;
    }));
    const events = Array.isArray(data.events) ? data.events : [], samples = history(data.samples);
    const times = [rates, imu, ...Object.values(data.diagnostics || {})].map(r => r?.host_time+r?.age_s)
      .concat(samples.map(s => s?.host_time), events.map(e => e?.host_time)).filter(t => finite(t) && t >= 0);
    const now = times.length ? Math.max(...times) : null;
    replace('eventList', events.slice(-20).reverse().map(event => {
      const relative = finite(now) && finite(event.host_time) ? UI.fmt(Math.max(0, now-event.host_time), 1)+' s ago' : 'age unknown';
      return UI.node('li', relative+' · '+(event.wheel ? event.wheel+' · ' : '')+(event.message || event.code || event.type || 'Event'));
    }));
    UI.text('poseReadout', offline ? 'No current pose' : measured && finite(data.pose?.x_m) && finite(data.pose?.y_m) ?
      (tracking ? 'Pose' : 'Last pose · paused')+': x '+UI.fmt(data.pose.x_m, 3)+' m · y '+UI.fmt(data.pose.y_m, 3)+' m · yaw '+UI.fmt(degrees(data.pose.yaw_rad), 1)+'°' : 'Measure geometry before interpreting travel in meters.');
    UI.text('geometryState', offline ? 'Unknown · offline' : measured ? 'Measured geometry · wheel-only estimate'+(calibrated ? '' : ' · controller calibration unavailable') : 'Unmeasured · all three loaded dimensions are required');
    const locked = offline || label === 'REPLAY' || !calibrated, save = UI.el('saveGeometry') || UI.el('geometryForm')?.querySelector('button');
    if (save) save.disabled = locked; if (UI.el('resetPose')) UI.el('resetPose').disabled = locked || !measured;
    geometryKeys.forEach(key => {
      const input = UI.el(key);
      if (measured && input && !filledGeometry.has(key) && input.value === '' && document.activeElement !== input) { input.value = String(data.geometry[key]); filledGeometry.add(key); }
    });
    const invalid = Object.values(data.diagnostics || {}).some(d => finite(d?.invalid_transitions) && d.invalid_transitions > 0);
    const next = offline ? 'Reconnect and identify the controller before reviewing current signals.' : label === 'REPLAY' ? 'Offline replay; exit replay before changing live geometry.' : supported.length < 4 ? 'Identify four wheel encoder capabilities before enabling odometry.' : invalid ? 'Inspect encoder wiring and invalid-transition counts before the next supervised test.' : !freshRates ? 'Check wheel telemetry and freshness before a supervised bench test.' : !freshImu ? 'Check IMU telemetry; wheel-only odometry does not confirm heading.' : !measured ? 'Measure loaded wheel geometry before interpreting travel.' : 'Review a supervised bench run; floor performance remains unverified.';
    UI.text('nextAction', rvc && !offline && label !== 'REPLAY' ? 'Record startup and known-angle checks for the RVC sensor. Robot firmware integration and motor-noise testing remain pending.' : next);
  };
})();
