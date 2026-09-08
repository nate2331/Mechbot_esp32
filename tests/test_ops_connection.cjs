const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');

function setup() {
  const elements = new Map();
  const document = {
    addEventListener() {},
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, {textContent: '', className: '', disabled: false});
      return elements.get(id);
    }
  };
  const window = {};
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../dashboard/ops.js'), 'utf8'), {window, document});
  const UI = window.UI;
  UI.renderLive = () => {};
  return UI;
}
const live = () => ({mode: 'live', observed: {}, bridge: {}, capture: {state: 'idle', written: 0, dropped: 0}});
async function disconnect(UI) {
  UI.api = async () => { throw new Error('Failed to fetch'); };
  await UI.refresh();
  assert.match(UI.el('notice').textContent, /^Connection unavailable/);
}

test('successful polling clears its own stale connection failure', async () => {
  const UI = setup();
  await disconnect(UI);
  UI.api = async () => live();
  await UI.refresh();
  assert.equal(UI.el('notice').textContent, 'Connection restored.');
  assert.equal(UI.el('notice').className, '');
  assert.equal(UI.connectionErrorMessage, null);
});
test('recovery preserves another notice shown after the connection failure', async () => {
  const UI = setup();
  await disconnect(UI);
  UI.notify('A recording could not be opened.', true);
  UI.api = async () => live();
  await UI.refresh();
  assert.equal(UI.el('notice').textContent, 'A recording could not be opened.');
  assert.equal(UI.el('notice').className, 'error');
});
test('a current observation fault remains visible after network recovery', async () => {
  const UI = setup();
  await disconnect(UI);
  UI.api = async () => ({...live(), bridge: {operations_error: {method: 'feed', error: 'bad sample'}}});
  await UI.refresh();
  assert.equal(UI.el('notice').textContent, 'Observation fault (feed): bad sample');
  assert.equal(UI.el('notice').className, 'error');
});
test('ordinary successful polling does not erase operator notifications', async () => {
  const UI = setup();
  UI.notify('Capture started.');
  UI.api = async () => live();
  await UI.refresh();
  assert.equal(UI.el('notice').textContent, 'Capture started.');
});
