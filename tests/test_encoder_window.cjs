const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const root = path.join(__dirname, '../dashboard');
const wheels = ['FL','FR','RL','RR'];
const copy = value => JSON.parse(JSON.stringify(value));
function observation(t=10, ticks=0, invalid=0) {
  return {profile:{id:'maker', counts_per_revolution:Object.fromEntries(wheels.map(w => [w, 100]))}, epoch:2,
    rates:{host_time:t, fresh:true, valid:true},
    samples:[{host_time:t, device_ms:Math.round(t*1000), counts:[ticks,-ticks,ticks,-ticks]}],
    diagnostics:Object.fromEntries(wheels.map(w => [w, {host_time:t, fresh:true, pwm:0,
      a_edges:Math.abs(ticks)*2, b_edges:Math.abs(ticks)*2, invalid_transitions:invalid}]))};
}
function setup() {
  const elements = new Map();
  class Element {
    constructor(text='') { this.textContent=text; this.children=[]; }
    appendChild(item) { this.children.push(item); }
    replaceChildren(...items) { this.children=items; }
  }
  const UI = {replay:null, el(id) { if (!elements.has(id)) elements.set(id, new Element()); return elements.get(id); },
    text(id,text) { this.el(id).textContent=text; }, node(tag,text='') { return new Element(text); },
    fmt:(value,n) => Number.isFinite(value) ? value.toFixed(n) : '—'};
  const window = {UI};
  const context = vm.createContext({window, document:{addEventListener() {}}, setTimeout});
  for (const file of ['ops-encoder-math.js', 'ops-encoder.js']) vm.runInContext(fs.readFileSync(path.join(root,file),'utf8'), context);
  return {UI, math:window.EncoderWindow};
}
const data = observed => ({mode:'live', observed, bridge:{serial_connected:true}});
test('wheel-only snapshot works with no IMU; signed turns and errors are measured', () => {
  const {math} = setup(), start = math.snapshot(observation(), 'live', true);
  const end = math.snapshot(observation(11,100,2), 'live', true);
  const result = math.difference(start,end);
  assert.equal(result.wheels.FL.revolutions,1); assert.equal(result.wheels.FR.revolutions,-1);
  assert.equal(result.wheels.FR.a_edges,200); assert.equal(result.wheels.RL.invalid_transitions,2);
  assert.equal(result.invalid_observed,true); assert.equal(result.movement_observed,true);
});
test('stationary observation does not establish motion validation', () => {
  const {math} = setup(); const s=math.snapshot(observation(),'live',true);
  const result=math.difference(s,s);
  assert.equal(result.movement_observed,false); assert.equal(result.invalid_observed,false);
  assert.equal(result.passed,undefined);
});
test('snapshots are detached from their input', () => {
  const {math}=setup(), input=observation(), snapshot=math.snapshot(input,'live',true);
  input.samples[0].counts[0]=99; input.diagnostics.FL.a_edges=99;
  assert.equal(snapshot.counts[0],0); assert.equal(snapshot.diagnostics.FL.a_edges,0);
});
test('unknown board, stale samples, unsafe counters and disconnected transport are rejected', () => {
  const {math}=setup();
  for (const mutate of [o=>o.profile.id='s3', o=>o.rates.fresh=false,
    o=>o.samples[0].host_time=9, o=>o.samples[0].counts[0]=Number.MAX_SAFE_INTEGER+1,
    o=>o.diagnostics.FR.fresh=false, o=>o.diagnostics.RL.a_edges=-1,
    o=>o.diagnostics.RR.invalid_transitions=NaN, o=>o.epoch=-1]) {
    const o=observation(); mutate(o); assert.throws(()=>math.snapshot(o,'live',true));
  }
  assert.throws(()=>math.snapshot(observation(),'replay',true));
  assert.throws(()=>math.snapshot(observation(),'live',false));
});
test('uncalibrated wheels retain counts but do not invent revolutions', () => {
  const {math}=setup(), o=observation(); o.profile.counts_per_revolution.FR=null;
  const s=math.snapshot(o,'live',true); assert.equal(math.difference(s,s).wheels.FR.revolutions,null);
});
test('identity, mode, calibration, counter and host-clock resets invalidate comparisons', () => {
  const {math}=setup(), s=math.snapshot(observation(10,100),'live',true);
  for (const mutate of [e=>e.epoch++, e=>e.mode='simulated', e=>e.profile='s3',
    e=>e.cpr.FL=101, e=>e.host_time=9, e=>e.host_time=3611,
    e=>e.device_ms=100, e=>e.diagnostics.FL.a_edges=0,
    e=>e.diagnostics.RR.host_time=9]) {
    const e=math.snapshot(observation(11,110),'live',true); mutate(e);
    assert.throws(()=>math.difference(s,e));
  }
});
test('device clock rollover is allowed while diagnostic counter rollover is rejected', () => {
  const {math}=setup(), s=math.snapshot(observation(),'live',true), e=copy(s);
  s.device_ms=0xffffff00; e.device_ms=100; e.host_time+=.356;
  assert.doesNotThrow(()=>math.difference(s,e));
  s.diagnostics.FL.a_edges=0xffffffff;
  assert.throws(()=>math.difference(s,e));
});
test('observation uses only GET operations and exports a finished stationary result', async () => {
  const {UI}=setup(), calls=[];
  UI.api=async (...args)=>{calls.push(args);return data(observation());};
  await UI.startEncoderCheck(); assert.equal(UI.encoderCheck.active,true);
  await UI.finishEncoderCheck();
  assert.equal(UI.encoderCheckReport().valid_window,true);
  assert.match(UI.el('encoderCheckState').textContent,/No movement observed/);
  assert.ok(calls.every(args=>args.length===1 && args[0]==='/api/operations'));
});
test('an intermediate counter reset stays invalid even after larger counts return', async () => {
  const {UI}=setup(); UI.api=async()=>data(observation(10,100));
  await UI.startEncoderCheck(); UI.updateEncoderCheck(data(observation(11,110)));
  UI.updateEncoderCheck(data(observation(12,0)));
  UI.updateEncoderCheck(data(observation(13,200)));
  assert.equal(UI.encoderCheck.active,false); assert.equal(UI.encoderCheckReport().valid_window,false);
  assert.ok(UI.encoderCheck.error); assert.equal(UI.el('encoderCheckRows').children.length,0);
});
test('disconnect, replay, stale diagnostics and observation gaps latch an invalid window', async () => {
  for (const fault of ['disconnect','replay','stale','gap']) {
    const {UI}=setup(); UI.api=async()=>data(observation()); await UI.startEncoderCheck();
    const d=data(observation(11));
    if(fault==='disconnect') d.bridge.serial_connected=false;
    if(fault==='replay') UI.replay={};
    if(fault==='stale') d.observed.diagnostics.FL.fresh=false;
    if(fault==='gap') d.observed=observation(14);
    UI.updateEncoderCheck(d); assert.equal(UI.encoderCheckReport().valid_window,false,fault);
    assert.ok(UI.encoderCheck.error,fault);
  }
});
test('new observation clears a previous failure; completed reports survive disconnection', async () => {
  const {UI}=setup(); UI.api=async()=>data(observation());
  await UI.startEncoderCheck(); UI.updateEncoderCheck(null);
  await UI.startEncoderCheck(); await UI.finishEncoderCheck(); UI.updateEncoderCheck(null);
  assert.equal(UI.encoderCheckReport().valid_window,true); assert.equal(UI.encoderCheck.error,null);
});
