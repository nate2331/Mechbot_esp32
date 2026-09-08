'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../dashboard/ops-live.js'), 'utf8');
const colors = {FL:'#59d4cf', FR:'#f4b85f', RL:'#b6a3f5', RR:'#f08da6'};

class CanvasContext {
  constructor() { this.strokes = []; this.labels = []; this.points = []; this.path = []; this.clears = 0; }
  numbers(values) { values.forEach(value => assert.ok(Number.isFinite(value), 'nonfinite canvas coordinate: '+value)); }
  clearRect(...values) { this.numbers(values); this.strokes = []; this.labels = []; this.points = []; this.clears++; }
  beginPath() { this.path = []; }
  moveTo(...values) { this.numbers(values); this.path.push(['move', ...values]); }
  lineTo(...values) { this.numbers(values); this.path.push(['line', ...values]); }
  stroke() { this.strokes.push({color:this.strokeStyle, width:this.lineWidth, path:this.path.slice()}); }
  fillText(text, ...values) { this.numbers(values); this.labels.push(String(text)); }
  fillRect(...values) { this.numbers(values); this.points.push({color:this.fillStyle, values}); }
}
function canvas(width=1000, height=300) {
  const ctx = new CanvasContext();
  return {width, height, ctx, getContext:() => ctx};
}
class Element {
  constructor(tag='div', text='', className='') { this.tagName=tag; this.children=[]; this._text=text; this.className=className; this.style={}; this.value=''; this.disabled=false; }
  get textContent() { return this._text + this.children.map(child => child.textContent).join(''); }
  set textContent(value) { this._text=String(value); this.children=[]; }
  set innerHTML(value) { throw new Error('rendering must use text nodes, not HTML: '+value); }
  appendChild(child) { this.children.push(child); return child; }
  replaceChildren(...children) { this.children=children; this._text=''; }
}
function application() {
  const ids = ['modeBadge','connection','boardName','rateState','imuState','rvcDetail','poseState','rateUnits','wheelCards',
    'diagnosticRows','eventList','poseReadout','geometryState','geometryForm','geometrySubmit','resetPose',
    'wheel_diameter_m','wheelbase_m','track_width_m','nextAction'];
  const nodes = Object.fromEntries(ids.map(id => [id, new Element()]));
  nodes.speedChart=canvas(); nodes.poseChart=canvas(800,360);
  nodes.geometryForm.querySelector=() => nodes.geometrySubmit;
  const UI = {el:id => nodes[id] || null, text(id,value) { if(nodes[id]) nodes[id].textContent=value; },
    fmt:(value, digits=1) => Number.isFinite(value) ? value.toFixed(digits) : '—',
    node:(...args) => new Element(...args)};
  const document = {activeElement:null};
  vm.runInNewContext(source, {window:{UI}, document}, {filename:'ops-live.js'});
  return {UI,nodes,document};
}
function rates(t, values={}, extra={}) { return {host_time:t, rates:{valid:true, rpm:values, ...extra}}; }
function pose(t,x,y,yaw=0,valid=true) { return {host_time:t, pose:{valid,x_m:x,y_m:y,yaw_rad:yaw}}; }
function trace(chart, wheel='FL') { return chart.ctx.strokes.find(s => s.color===colors[wheel] && s.width===2)?.path || []; }
function observation() {
  return {profile:{id:'maker',name:'Maker ESP32 Pro',encoder_wheels:Object.keys(colors),counts_per_revolution:{FL:2468,FR:2468,RL:2468,RR:2468}},
    rates:{valid:true,fresh:true,host_time:10,age_s:0.2,rpm:{FL:0,FR:2,RL:3,RR:4},ticks_per_s:{FL:0,FR:80,RL:120,RR:160}},
    imu:{valid:true,fresh:true,host_time:10,age_s:0.2,yaw_rad:Math.PI/2},
    diagnostics:{FL:{pwm:0,a_edges:0,b_edges:3,invalid_transitions:0,host_time:10,age_s:0.2,fresh:true}},
    geometry:{wheel_diameter_m:0.1,wheelbase_m:0.3,track_width_m:0.25}, pose:{valid:true,x_m:1,y_m:2,yaw_rad:0},
    samples:[], events:[{host_time:9,message:'<img src=x onerror=bad()>',level:'warn'}]};
}

test('RVC diagnostic stays separate from integrated IMU and robot readiness', () => {
  const {UI,nodes}=application(), data=observation();
  data.profile={id:'unknown',name:'Maker UART-RVC diagnostic',encoder_wheels:[]};
  data.imu=null; data.rates=null; data.geometry=null;
  data.rvc={valid:true,reason:'ready',run:{fresh:true,new:100,window_ms:1000},
    value:{fresh:true,ypr_deg:[-.01,0,0],age_s:.1},
    counts:{fresh:true,bad_checksum:2,discontinuities:3},uart:{fresh:true,totals:[0,1,0,0,0]}};
  UI.renderLive(data,'live',{serial_connected:true});
  assert.match(nodes.imuState.textContent,/RVC yaw -0.01/);
  assert.match(nodes.rvcDetail.textContent,/100 frames\/s.*checksum 2.*index 3.*UART 1/);
  assert.match(nodes.rvcDetail.textContent,/accuracy unverified/);
  assert.ok(nodes.geometrySubmit.disabled && nodes.resetPose.disabled);
  data.rvc.valid=false; data.rvc.reason='stale'; data.rvc.counts.fresh=false;
  UI.renderLive(data,'replay',{});
  assert.equal(nodes.imuState.textContent,'RVC · stale');
  assert.match(nodes.rvcDetail.textContent,/checksum —/);
  assert.match(nodes.nextAction.textContent,/Offline replay/);
  UI.renderLive(null,'offline',{});
  assert.equal(nodes.rvcDetail.textContent,'');
});

test('integrated RVC presents unknown calibration and explicit acceptance', () => {
  const {UI,nodes}=application(), data=observation();
  Object.assign(data.imu,{transport:'uart-rvc',control_ready:false,bad_checksum:0,discontinuities:1,uart_errors:0});
  UI.renderLive(data,'live',{});
  assert.match(nodes.imuState.textContent,/RVC yaw 90.00/);
  assert.match(nodes.rvcDetail.textContent,/Heading acceptance required/);
  assert.match(nodes.rvcDetail.textContent,/Calibration status and gyro unavailable/);
  data.imu.control_ready=true; UI.renderLive(data,'live',{});
  assert.match(nodes.rvcDetail.textContent,/accepted for this session/);
  data.imu.fresh=false; data.imu.valid=false; data.imu.reason='stale'; UI.renderLive(data,'live',{});
  assert.equal(nodes.imuState.textContent,'RVC · stale');
  assert.match(nodes.rvcDetail.textContent,/acceptance required/);
});

test('constant/single signed wheel samples never divide by zero', () => {
  const {UI}=application();
  for(const rows of [[rates(4,{FL:0})],[rates(4,{FL:20}),rates(5,{FL:20})]]) {
    const chart=canvas(); UI.drawRates(chart,rows);
    assert.ok(trace(chart).length); assert.ok(chart.ctx.labels.includes('RPM'));
  }
  const chart=canvas(); UI.drawRates(chart,[rates(0,{FL:-10}),rates(1,{FL:0}),rates(2,{FL:10})]);
  const points=trace(chart); assert.ok(points[0][2]>points[1][2]); assert.ok(points[1][2]>points[2][2]);
  assert.equal(points[1][2],(28+300-38)/2);
});
test('FR data is plotted even if FL is missing; ticks/s is used when RPM is absent', () => {
  const {UI}=application(), chart=canvas();
  assert.equal(UI.drawRates(chart,[rates(1,{FR:3}),rates(2,{FR:4})]),'RPM');
  assert.equal(trace(chart,'FR').filter(p=>p[0]==='line').length,1);
  assert.equal(trace(chart,'FL').length,0);
  assert.equal(UI.drawRates(chart,[rates(1,{}, {ticks_per_s:{RL:-12}}),rates(2,{}, {ticks_per_s:{RL:12}})]),'ticks/s');
  assert.ok(chart.ctx.labels.includes('ticks/s')); assert.equal(trace(chart,'RL').length,2);
});
test('invalid samples, missing wheel values, and reversed timestamps break speed traces', () => {
  const {UI}=application(), chart=canvas();
  UI.drawRates(chart,[rates(0,{FL:1}),rates(1,{FL:2}),rates(2,{FL:99},{valid:false}),rates(3,{FL:3}),
    rates(4,{FL:NaN}),rates(5,{FL:4}),rates(4,{FL:5}),rates(Infinity,{FL:8}),rates(6,{FL:6})]);
  assert.deepEqual(trace(chart).map(p=>p[0]),['move','line','move','move','move','move']);
});
test('all chart coordinates stay finite for extreme finite data and invalid input', () => {
  const {UI}=application();
  UI.drawRates(canvas(),[null,rates(0,{FL:Number.MAX_VALUE}),rates(Number.MAX_VALUE,{FL:-Number.MAX_VALUE}),rates(3,{FR:Infinity})]);
  UI.drawPose(canvas(800,360),[pose(0,-Number.MAX_VALUE,0),pose(1,Number.MAX_VALUE,Number.MAX_VALUE),pose(2,NaN,1)],
    {valid:true,x_m:Number.MAX_VALUE,y_m:0,yaw_rad:Number.MAX_VALUE});
  assert.doesNotThrow(()=>UI.drawRates(null,[])); assert.doesNotThrow(()=>UI.drawPose(null,[],null));
});
test('history is bounded to the latest 300 samples before scaling or rendering', () => {
  const {UI}=application(), chart=canvas();
  const rows=Array.from({length:305},(_,i)=>rates(i,{FL:i<5 ? 1e100 : 1}));
  UI.drawRates(chart,rows); assert.equal(trace(chart).length,300);
  assert.ok(!chart.ctx.labels.some(label=>label.includes('e+100')));
  const positions=Array.from({length:305},(_,i)=>pose(i,i<5 ? 1e100 : i,0));
  UI.drawPose(chart,positions,null); assert.equal(trace(chart).length,300);
});
test('pose traces preserve invalid gaps and use the same scale for x and y', () => {
  const {UI}=application(), chart=canvas(800,360);
  UI.drawPose(chart,[pose(0,0,0),pose(1,1,0),pose(2,1,1),pose(3,2,2,0,false),pose(4,3,3),pose(5,NaN,2),pose(6,4,4)],null);
  const points=trace(chart); assert.deepEqual(points.map(p=>p[0]),['move','line','line','move','move']);
  const dx=points[1][1]-points[0][1], dy=points[1][2]-points[2][2];
  assert.ok(Math.abs(dx-dy)<1e-9); assert.ok(dx>0 && dy>0);
});
test('pose heading uses +x right and +y up, and invalid yaw is never drawn', () => {
  const {UI}=application(), chart=canvas(800,360), rows=[pose(0,0,0)];
  UI.drawPose(chart,rows,{valid:true,x_m:0,y_m:0,yaw_rad:0});
  let arrow=chart.ctx.strokes.find(s=>s.color===colors.FR).path;
  assert.ok(arrow[1][1]>arrow[0][1]); assert.equal(arrow[1][2],arrow[0][2]);
  UI.drawPose(chart,rows,{valid:true,x_m:0,y_m:0,yaw_rad:Math.PI/2});
  arrow=chart.ctx.strokes.find(s=>s.color===colors.FR).path; assert.ok(arrow[1][2]<arrow[0][2]);
  UI.drawPose(chart,rows,{valid:true,x_m:0,y_m:0,yaw_rad:Infinity});
  assert.ok(!chart.ctx.strokes.some(s=>s.color===colors.FR));
});
test('empty and entirely invalid traces show waiting text rather than fabricated data', () => {
  const {UI}=application(), chart=canvas(); UI.drawRates(chart,[rates(1,{FL:5},{valid:false})]);
  assert.ok(chart.ctx.labels.includes('Waiting for timestamped wheel samples')); assert.equal(chart.ctx.points.length,0);
  UI.drawPose(chart,[pose(0,0,0,0,false)],null); assert.ok(chart.ctx.labels.includes('Waiting for valid pose samples'));
});
test('live render uses bridge keys, keeps zero diagnostics, and produces exactly six columns', () => {
  const {UI,nodes}=application(), data=observation(); UI.renderLive(data,'live',{serial_connected:true,serial_port:'/dev/ttyACM0'});
  assert.equal(nodes.modeBadge.textContent,'LIVE'); assert.match(nodes.connection.textContent,/Connected.*ttyACM0/);
  assert.equal(nodes.rateState.textContent,'Fresh feedback'); assert.match(nodes.imuState.textContent,/90.0°/);
  assert.equal(nodes.poseState.textContent,'Tracking'); assert.equal(nodes.diagnosticRows.children.length,4);
  nodes.diagnosticRows.children.forEach(row=>assert.equal(row.children.length,6));
  assert.deepEqual(nodes.diagnosticRows.children[0].children.slice(0,5).map(td=>td.textContent),['FL','0','0','3','0']);
  assert.match(nodes.eventList.textContent,/1.2 s ago.*<img src=x onerror=bad\(\)>/);
  assert.match(nodes.wheelCards.children[0].textContent,/0.0 RPM/);
});
test('stale feedback is never labeled fresh and stale counters retain values and age', () => {
  const {UI,nodes}=application(), data=observation();
  data.rates={...data.rates,valid:true,fresh:false,reason:'stale',age_s:4}; data.imu.fresh=false;
  data.diagnostics.FL={...data.diagnostics.FL,fresh:false,age_s:8,invalid_transitions:7};
  UI.renderLive(data,'live',{}); assert.match(nodes.rateState.textContent,/stale.*4.0 s old/);
  assert.match(nodes.imuState.textContent,/Waiting\/stale/); assert.equal(nodes.poseState.textContent,'Paused');
  assert.match(nodes.wheelCards.children[0].textContent,/—.*Stale feedback/);
  assert.equal(nodes.diagnosticRows.children[0].children[4].textContent,'7');
  assert.match(nodes.diagnosticRows.children[0].children[5].textContent,/8.0 s old.*stale/);
  assert.match(nodes.nextAction.textContent,/invalid-transition/);
});
test('null observations clear prior data and disable geometry controls', () => {
  const {UI,nodes}=application(); UI.renderLive(observation(),'simulated',{serial_connected:true});
  UI.renderLive(null,'offline',{}); assert.equal(nodes.modeBadge.textContent,'OFFLINE');
  assert.match(nodes.rateState.textContent,/Unknown.*offline/); assert.equal(nodes.eventList.children.length,0);
  assert.ok(nodes.wheelCards.children.every(card=>card.textContent.includes('—')));
  assert.ok(nodes.geometrySubmit.disabled && nodes.resetPose.disabled);
  assert.ok(nodes.speedChart.ctx.labels.includes('Waiting for timestamped wheel samples'));
});
test('geometry gates require four positive finite CPRs and disallow replay changes', () => {
  const {UI,nodes}=application(), data=observation(); UI.renderLive(data,'live',{});
  assert.equal(nodes.geometrySubmit.disabled,false); assert.equal(nodes.resetPose.disabled,false);
  UI.renderLive(data,'replay',{}); assert.ok(nodes.geometrySubmit.disabled && nodes.resetPose.disabled);
  data.profile.counts_per_revolution.FL=0; UI.renderLive(data,'live',{}); assert.ok(nodes.geometrySubmit.disabled);
  data.profile.counts_per_revolution.FL=Infinity; UI.renderLive(data,'live',{}); assert.ok(nodes.geometrySubmit.disabled);
  data.profile.counts_per_revolution.FL=2468; data.geometry=null; UI.renderLive(data,'live',{});
  assert.equal(nodes.geometrySubmit.disabled,false); assert.equal(nodes.resetPose.disabled,true);
  assert.equal(nodes.poseState.textContent,'Geometry required');
});
test('measured geometry fills empty unfocused fields once without overwriting edits', () => {
  const {UI,nodes,document}=application(), data=observation(); nodes.wheelbase_m.value='0.42';
  document.activeElement=nodes.track_width_m; UI.renderLive(data,'live',{});
  assert.equal(nodes.wheel_diameter_m.value,'0.1'); assert.equal(nodes.wheelbase_m.value,'0.42'); assert.equal(nodes.track_width_m.value,'');
  nodes.wheel_diameter_m.value=''; document.activeElement=null; UI.renderLive(data,'live',{});
  assert.equal(nodes.wheel_diameter_m.value,''); assert.equal(nodes.track_width_m.value,'0.25');
});
test('ticks-only capabilities stay explicit and event list retains latest twenty in reverse order', () => {
  const {UI,nodes}=application(), data=observation(); data.profile.encoder_wheels=['RL','RR']; data.profile.counts_per_revolution={}; data.rates.rpm={};
  data.events=Array.from({length:25},(_,i)=>({host_time:i,message:'event-'+i})); UI.renderLive(data,'live',{});
  assert.match(nodes.wheelCards.children[0].textContent,/Not supported/); assert.match(nodes.wheelCards.children[2].textContent,/120.0 ticks\/s/);
  assert.equal(nodes.eventList.children.length,20); assert.match(nodes.eventList.children[0].textContent,/event-24$/);
  assert.match(nodes.eventList.children[19].textContent,/event-5$/); assert.ok(nodes.geometrySubmit.disabled);
});
