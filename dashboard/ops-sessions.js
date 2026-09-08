/* Recording inspection is offline; recorded TX is never dispatched. */
(() => {
  const UI = window.UI;
  let generation = 0, recordingGeneration = 0, pending = null, inFlight = false;
  let seekTimer = null, playTimer = null, anchorTime = 0, anchorPosition = 0, bound = false;
  const show = value => value == null ? '—' : String(value);
  const cancelPending = () => { if (pending) pending.resolve(null); pending = null; };
  function pause(invalidate = false) {
    UI.playing = false; clearTimeout(playTimer); playTimer = null;
    UI.text('replayPlay', 'Play');
    if (invalidate) { generation++; clearTimeout(seekTimer); cancelPending(); }
  }
  function controls(replay) {
    UI.el('captureStart').disabled = replay || ['active', 'stopping'].includes(UI.live?.capture?.state);
    UI.el('geometryForm').querySelector('button').disabled = replay;
    UI.el('resetPose').disabled = replay;
    UI.el('stop').disabled = false;
  }
  async function drain() {
    if (inFlight || !pending) return;
    const job = pending; pending = null; inFlight = true;
    try {
      const data = await UI.api('/api/replay', {id: job.id, until_s: job.position});
      if (job.generation === generation && UI.replay?.id === job.id) {
        if (data.mode !== 'replay' || data.id !== job.id) throw new Error('Unexpected replay response.');
        UI.replay = data; UI.renderReplay(data); job.resolve(data);
      } else job.resolve(null);
    } catch (error) {
      if (job.generation === generation && UI.replay?.id === job.id) {
        pause(true); UI.notify('Replay unavailable: ' + (error.message || error), true);
      }
      job.resolve(null);
    } finally { inFlight = false; drain(); }
  }
  function request(position) {
    if (!UI.replay) return Promise.resolve(null);
    cancelPending();
    return new Promise(resolve => {
      pending = {id: UI.replay.id, position, generation, resolve}; drain();
    });
  }
  function tick() {
    if (!UI.playing || !UI.replay) return;
    const duration = Number(UI.replay.duration_s) || 0;
    const position = Math.min(duration, anchorPosition + Math.max(0, performance.now() - anchorTime) / 1000);
    request(position);
    if (position >= duration) pause();
    else playTimer = setTimeout(tick, 250);
  }
  UI.loadRecordings = async function() {
    const version = ++recordingGeneration, list = UI.el('recordingList');
    try {
      const data = await UI.api('/api/recordings');
      if (version !== recordingGeneration) return;
      const recordings = Array.isArray(data.recordings) ? data.recordings : [];
      list.replaceChildren();
      if (!recordings.length) list.append(UI.node('p', 'No recordings yet.', 'empty'));
      for (const recording of recordings) {
        const id = recording.id, card = UI.node('article', '', 'recording-card');
        const label = recording.metadata?.label || id || 'Unrecorded label';
        card.append(UI.node('h3', label), UI.node('p',
          `${show(recording.state)} · ${show(recording.written)} saved · ${show(recording.dropped)} dropped`));
        if (recording.error) card.append(UI.node('p', String(recording.error), 'error'));
        const inspect = UI.node('button', 'Inspect replay'); inspect.type = 'button';
        inspect.addEventListener('click', UI.run(() => UI.openReplay(id)));
        const download = UI.node('a', 'Download JSON');
        download.href = '/api/recordings/' + encodeURIComponent(id); download.download = id + '.json';
        card.append(inspect, download); list.append(card);
      }
    } catch (error) {
      if (version !== recordingGeneration) return;
      list.replaceChildren(UI.node('p', 'Recordings unavailable: ' + (error.message || error), 'error'));
      UI.notify(error.message || String(error), true);
    }
  };
  UI.openReplay = function(id) {
    if (typeof id !== 'string' || !id) throw new Error('Choose a recording to inspect.');
    pause(true);
    UI.replay = {mode: 'replay', id, duration_s: 0, position_s: 0,
      metadata: {}, state: 'loading', observed: {samples: []}};
    UI.showView('recordings'); UI.renderReplay(UI.replay);
    return request(0);
  };
  UI.renderReplay = function(data) {
    UI.el('replayPanel').hidden = false; UI.text('modeBadge', 'OFFLINE REPLAY'); controls(true);
    const duration = Math.max(0, Number(data.duration_s) || 0);
    const position = Math.min(duration, Math.max(0, Number(data.position_s) || 0));
    UI.el('replayScrub').max = String(duration); UI.el('replayScrub').value = String(position);
    UI.text('replayTime', `${position.toFixed(2)} / ${duration.toFixed(2)} seconds`);
    const metadata = data.metadata || {};
    const source = metadata.simulated === true ? ' · SIMULATED'
      : metadata.simulated === false ? '' : ' · UNKNOWN SOURCE';
    UI.text('replaySummary', `${metadata.label || data.id} · ${show(data.state)} · ${show(data.dropped)} dropped${source}`);
    UI.drawRates(UI.el('replayChart'), data.observed?.samples || []);
  };
  UI.bindSessions = function() {
    if (bound) return; bound = true;
    UI.el('captureForm').addEventListener('submit', UI.run(async () => {
      if (UI.replay) throw new Error('Return to live before starting a capture.');
      const battery = UI.el('captureBattery').value.trim();
      const voltage = battery === '' ? null : Number(battery);
      if (voltage !== null && (!Number.isFinite(voltage) || voltage < 0 || voltage > 60))
        throw new Error('Battery voltage must be between 0 and 60, or blank.');
      await UI.api('/api/recordings/start', {label: UI.el('captureLabel').value,
        surface: UI.el('captureSurface').value, load: UI.el('captureLoad').value, battery_voltage: voltage});
      UI.notify('Capture started.'); await UI.refresh();
    }));
    for (const id of ['captureStop', 'replayPlay', 'replayExit']) UI.el(id).type = 'button';
    UI.el('captureStop').addEventListener('click', UI.run(async () => {
      await UI.api('/api/recordings/stop', {}); await UI.refresh(); await UI.loadRecordings();
    }));
    UI.el('replayScrub').addEventListener('input', () => {
      if (!UI.replay) return;
      pause(true);
      const position = Math.min(Number(UI.replay.duration_s) || 0, Math.max(0, Number(UI.el('replayScrub').value)));
      if (!Number.isFinite(position)) return;
      seekTimer = setTimeout(() => request(position), 150);
    });
    UI.el('replayPlay').addEventListener('click', UI.run(() => {
      if (!UI.replay || !(UI.replay.duration_s > 0)) return;
      if (UI.playing) { pause(true); return; }
      pause(true); UI.playing = true; UI.text('replayPlay', 'Pause');
      anchorPosition = UI.replay.position_s >= UI.replay.duration_s ? 0 : UI.replay.position_s;
      anchorTime = performance.now();
      if (anchorPosition === 0 && UI.replay.position_s !== 0) request(0);
      playTimer = setTimeout(tick, 250);
    }));
    UI.el('replayExit').addEventListener('click', UI.run(async () => {
      pause(true); UI.replay = null; UI.el('replayPanel').hidden = true; controls(false);
      UI.showView('overview'); await UI.refresh();
    }));
  };
})();
