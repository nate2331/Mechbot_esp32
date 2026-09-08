'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const test = require('node:test');

class Element {
  constructor(tag, id = '') {
    this.tagName = tag.toUpperCase(); this.id = id; this.children = []; this.listeners = {};
    this._text = ''; this._value = ''; this.hidden = false; this.disabled = false; this.className = '';
  }
  set textContent(value) { this._text = String(value); this.children = []; }
  get textContent() { return this._text + this.children.map(child => child.textContent).join(' '); }
  set innerHTML(_) { throw new Error('Unsafe HTML write'); }
  get value() { return this._value || (this.tagName === 'SELECT' ? this.children[0]?.value || '' : ''); }
  set value(value) { this._value = String(value); }
  append(...children) { this.children.push(...children); }
  appendChild(child) { this.append(child); return child; }
  replaceChildren(...children) { this._text = ''; this.children = []; this._value = ''; this.append(...children); }
  addEventListener(type, listener) { (this.listeners[type] ||= []).push(listener); }
  querySelector(tag) { return this.walk().find(element => element.tagName === tag.toUpperCase()) || null; }
  walk() { return this.children.flatMap(child => [child, ...child.walk()]); }
  emit(type) {
    const event = {prevented: false, preventDefault() { this.prevented = true; }};
    this.lastEvent = event;
    return Promise.all((this.listeners[type] || []).map(listener => listener(event)));
  }
}

const clean = value => value === undefined ? undefined : JSON.parse(JSON.stringify(value));
const flush = () => new Promise(resolve => setImmediate(resolve));
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return {promise, resolve, reject};
}
function harness() {
  const elements = new Map(), calls = [], notices = [], replayRequests = [], timers = new Map();
  let now = 0, nextTimer = 1, activeReplay = 0, maxReplay = 0;
  const el = id => {
    if (!elements.has(id)) elements.set(id, new Element(id.includes('Form') ? 'form' :
      id.startsWith('compare') ? 'select' : 'div', id));
    return elements.get(id);
  };
  for (const id of ['geometryForm', 'compareForm']) el(id).append(new Element('button'));
  const UI = {
    replay: null, playing: false, reports: [], live: {capture: {state: 'idle'}},
    el, node(tag, text = '', className = '') { const node = new Element(tag); node.textContent = text; node.className = className; return node; },
    text(id, text) { el(id).textContent = text; }, notify(message, error = false) { notices.push({message, error}); },
    run(action) { return async event => { event?.preventDefault(); try { await action(event); } catch (error) { UI.notify(error.message, true); } }; },
    refreshCalls: 0, async refresh() { UI.refreshCalls++; }, showView(name) { UI.view = name; },
    charts: [], drawRates(canvas, samples) { UI.charts.push({canvas: canvas.id, samples: clean(samples)}); },
    async api(route, data) {
      calls.push({route, data: clean(data)});
      if (route === '/api/replay') {
        const pending = deferred(); activeReplay++; maxReplay = Math.max(maxReplay, activeReplay);
        replayRequests.push({data: clean(data), resolve(value) { activeReplay--; pending.resolve(value); },
          reject(error) { activeReplay--; pending.reject(error); }});
        return pending.promise;
      }
      return UI.responses[route] instanceof Function ? UI.responses[route](data)
        : UI.responses[route] || {};
    }, responses: {'/api/recordings': {recordings: []}, '/api/evidence': {reports: [], errors: []}}
  };
  const original = {run: UI.run, refresh: UI.refresh, node: UI.node};
  const context = vm.createContext({window: {UI}, document: {getElementById: el,
    createElement: tag => new Element(tag)}, performance: {now: () => now},
    setTimeout(callback, delay) { const id = nextTimer++; timers.set(id, {at: now + delay, callback}); return id; },
    clearTimeout(id) { timers.delete(id); }});
  for (const name of ['ops-sessions.js', 'ops-evidence.js']) vm.runInContext(
    fs.readFileSync(path.join(__dirname, '..', 'dashboard', name), 'utf8'), context, {filename: name});
  assert.equal(context.window.UI, UI);
  for (const key of Object.keys(original)) assert.equal(UI[key], original[key], `${key} must not be replaced`);
  UI.bindSessions(); UI.bindEvidence();
  return {UI, el, calls, notices, replayRequests, maxReplay: () => maxReplay,
    async advance(milliseconds) {
      const target = now + milliseconds;
      while (true) {
        const next = [...timers].filter(([, timer]) => timer.at <= target).sort((a, b) => a[1].at - b[1].at)[0];
        if (!next) break;
        timers.delete(next[0]); now = next[1].at; next[1].callback(); await flush();
      }
      now = target; await flush();
    }};
}
function replay(id, position = 0, duration = 10) {
  return {mode: 'replay', id, position_s: position, duration_s: duration,
    metadata: {label: 'Recorded <script>text</script>', simulated: null}, state: 'incomplete', dropped: 0,
    observed: {samples: [{host_time: position, rates: {rpm: {FL: 1}}}]},
    tx: [{direction: 'tx', line: 'V 1 0 0'}]};
}
async function opened(h, id = 'a', duration = 10) {
  const promise = h.UI.openReplay(id);
  h.replayRequests.at(-1).resolve(replay(id, 0, duration)); await promise; await flush();
}

test('capture submits once, converts voltage, waits for success, and blocks replay capture', async () => {
  const h = harness(), pending = deferred();
  h.UI.responses['/api/recordings/start'] = () => pending.promise;
  h.el('captureLabel').value = 'bench'; h.el('captureSurface').value = 'raised';
  h.el('captureLoad').value = ''; h.el('captureBattery').value = '0';
  h.UI.bindSessions();
  const submitted = h.el('captureForm').emit('submit'); await flush();
  assert.equal(h.el('captureForm').lastEvent.prevented, true);
  assert.deepEqual(h.calls[0], {route: '/api/recordings/start', data: {label: 'bench', surface: 'raised', load: '', battery_voltage: 0}});
  assert.equal(h.UI.refreshCalls, 0);
  pending.resolve({state: 'active'}); await submitted;
  assert.equal(h.UI.refreshCalls, 1);
  assert.equal(h.el('captureForm').listeners.submit.length, 1);
  h.el('captureBattery').value = 'nan'; await h.el('captureForm').emit('submit');
  assert.equal(h.calls.length, 1);
  await opened(h); await h.el('captureForm').emit('submit');
  assert.equal(h.calls.filter(call => call.route === '/api/recordings/start').length, 1);
  assert.equal(h.el('stop').disabled, false);
  assert.equal(h.el('geometryForm').querySelector('button').disabled, true);
});

test('recording controls are explicit buttons and download IDs are encoded', async () => {
  const h = harness();
  h.UI.responses['/api/recordings'] = {recordings: [{id: 'a/b <x>', state: 'completed', metadata: {label: '<img>'}, written: 0, dropped: 0}]};
  await h.UI.loadRecordings();
  const card = h.el('recordingList').children[0], button = card.querySelector('button'), link = card.querySelector('a');
  assert.equal(button.type, 'button');
  assert.equal(link.href, '/api/recordings/a%2Fb%20%3Cx%3E');
  assert.match(card.textContent, /0 saved.*0 dropped/);
  assert.equal(card.walk().filter(node => node.tagName === 'IMG').length, 0);
  await h.el('captureStop').emit('click');
  assert.deepEqual(h.calls.find(call => call.route.endsWith('/stop')), {route: '/api/recordings/stop', data: {}});
});

test('seek requests serialize and stale positions cannot overwrite a newer seek', async () => {
  const h = harness(); await opened(h);
  h.el('replayScrub').value = '2'; await h.el('replayScrub').emit('input'); await h.advance(150);
  assert.equal(h.replayRequests[1].data.until_s, 2);
  h.el('replayScrub').value = '7'; await h.el('replayScrub').emit('input'); await h.advance(150);
  assert.equal(h.replayRequests.length, 2);
  h.replayRequests[1].resolve(replay('a', 2)); await flush();
  assert.equal(h.UI.replay.position_s, 0);
  assert.equal(h.replayRequests[2].data.until_s, 7);
  h.replayRequests[2].resolve(replay('a', 7)); await flush();
  assert.equal(h.UI.replay.position_s, 7);
  assert.equal(h.el('replayScrub').value, '7');
  assert.equal(h.maxReplay(), 1);
});

test('changing recordings or returning live invalidates in-flight results', async () => {
  const h = harness();
  const first = h.UI.openReplay('a'), second = h.UI.openReplay('b');
  assert.equal(h.replayRequests.length, 1);
  h.replayRequests[0].resolve(replay('a', 4)); await first; await flush();
  assert.equal(h.UI.replay.id, 'b'); assert.equal(h.UI.replay.position_s, 0);
  h.replayRequests[1].resolve(replay('b', 0)); await second;
  const third = h.UI.openReplay('c'); await h.el('replayExit').emit('click');
  h.replayRequests[2].resolve(replay('c', 9)); await third; await flush();
  assert.equal(h.UI.replay, null); assert.equal(h.el('replayPanel').hidden, true);
  assert.equal(h.UI.view, 'overview'); assert.equal(h.maxReplay(), 1);
});

test('playback uses elapsed time, coalesces requests, ends paused, and never sends recorded TX', async () => {
  const h = harness(); await opened(h, 'a', 1);
  await h.el('replayPlay').emit('click'); await h.advance(1000);
  assert.equal(h.replayRequests.length, 2); assert.equal(h.replayRequests[1].data.until_s, 0.25);
  h.replayRequests[1].resolve(replay('a', 0.25, 1)); await flush();
  assert.equal(h.replayRequests[2].data.until_s, 1);
  h.replayRequests[2].resolve(replay('a', 1, 1)); await flush();
  assert.equal(h.UI.playing, false); assert.equal(h.el('replayPlay').textContent, 'Play');
  assert.equal(h.UI.replay.position_s, 1); assert.equal(h.maxReplay(), 1);
  assert.ok(h.calls.every(call => call.route === '/api/replay'));
  assert.ok(h.calls.every(call => !JSON.stringify(call.data).includes('V 1 0 0')));
  assert.match(h.el('replaySummary').textContent, /UNKNOWN SOURCE/);
  assert.match(h.el('replaySummary').textContent, /0 dropped/);
});

test('replay failures pause playback and expose the error', async () => {
  const h = harness(); await opened(h);
  await h.el('replayPlay').emit('click'); await h.advance(250);
  h.replayRequests[1].reject(new Error('broken recording')); await flush();
  assert.equal(h.UI.playing, false);
  assert.match(h.notices.at(-1).message, /broken recording/);
  assert.equal(h.el('stop').disabled, false);
});

function report(id, simulated = null) {
  const startup = {};
  for (const direction of ['forward', 'reverse']) startup[direction] = Object.fromEntries(
    ['FL', 'FR', 'RL', 'RR'].map(wheel => [wheel, {status: direction === 'forward' ? 'measured' : 'unrecorded',
      minimum_start_pwm: direction === 'forward' ? 119 : null, confirmation_starts: direction === 'forward' ? 3 : null}]));
  return {id, simulated, kind: 'automatic', status: 'aborted', error: 'Encoder <fault>', restored: null,
    maximum_pwm: null, maximum_trial_pwm: 177, trial_count: 4, completed_trial_count: 3,
    startup, directional_matching: {}, recommended_shared_pwm: null,
    conditions: {battery_voltage: null, surface: null, load: null}};
}
test('evidence keeps aborted, unknown and partial labels, displays errors, and preserves selection', async () => {
  const h = harness(), a = report('a <img>'), b = report('b', true);
  h.UI.responses['/api/evidence'] = {reports: [a, b], errors: [{id: 'bad', error: 'Invalid <json>'}]};
  await h.UI.loadEvidence();
  const text = h.el('evidenceList').textContent;
  for (const expected of ['aborted', 'SOURCE UNRECORDED', 'SIMULATED', '3 / 4', 'Configured PWM ceiling', 'Requested trial peak PWM', 'battery voltage', 'bad: Invalid <json>'])
    assert.ok(text.includes(expected), expected);
  assert.equal(h.el('evidenceList').walk().some(node => node.tagName === 'IMG'), false);
  assert.ok(h.el('evidenceList').walk().some(node => node.title === '3 confirmed starts'));
  assert.equal(h.el('compareLeft').value, a.id); assert.equal(h.el('compareRight').value, b.id);
  h.el('compareLeft').value = b.id; h.el('compareRight').value = a.id;
  await h.UI.loadEvidence();
  assert.equal(h.el('compareLeft').value, b.id); assert.equal(h.el('compareRight').value, a.id);
  h.UI.responses['/api/evidence'] = {reports: [a], errors: []}; await h.UI.loadEvidence();
  assert.equal(h.el('compareForm').querySelector('button').disabled, true);
});

test('comparison displays actual field names, missing conditions, zero deltas, and object values', async () => {
  const h = harness(), left = report('a', false), right = report('b');
  h.UI.responses['/api/evidence'] = {reports: [left, right], errors: []}; await h.UI.loadEvidence();
  h.UI.responses['/api/evidence/compare'] = {left, right, conditions: {
    battery_voltage: {left: null, right: null, status: 'unrecorded'},
    original_settings: {left: {heading: false, pwm: 0}, right: {heading: false, pwm: 0}, status: 'same'}},
    threshold_deltas: {forward: {FL: 0, FR: 2, RL: null, RR: null}, reverse: {}}};
  await h.el('compareForm').emit('submit');
  const text = h.el('comparison').textContent;
  assert.match(text, /battery voltage/); assert.match(text, /unrecorded/);
  assert.ok(text.includes('{"heading":false,"pwm":0}'));
  assert.ok(h.el('comparison').walk().some(node => node.tagName === 'TD' && node.textContent === '0'));
  assert.match(text, /SOURCE UNRECORDED/); assert.match(text, /aborted/);
  assert.equal(h.el('comparison').walk().some(node => node.className.includes('good')), false);
  assert.deepEqual(h.calls.at(-1), {route: '/api/evidence/compare', data: {left: 'a', right: 'b'}});
});
