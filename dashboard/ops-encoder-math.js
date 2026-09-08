(function(){
  const wheelNames=['FL','FR','RL','RR'];
  function isSafeInteger(n){return Number.isSafeInteger(n);} 
  function assert(cond,msg){if(!cond)throw new Error(msg);} 
  function snapshot(observed,mode,connected){
    assert(connected===true,'Controller disconnected.');
    assert(mode==='live'||mode==='simulated','Live or simulated observation required.');
    assert(observed?.profile?.id==='maker','Identify the Maker controller first.');
    assert(isSafeInteger(observed.epoch)&&observed.epoch>=0,'observed.epoch must be nonnegative safe integer');
    const rates=observed.rates; assert(rates?.fresh===true,'Waiting for fresh wheel feedback.');
    assert(rates.valid===true,'Waiting for valid wheel feedback.');
    const samples=observed.samples; assert(Array.isArray(samples)&&samples.length,'Waiting for wheel counts.');
    const last=samples[samples.length-1]; assert(last,'Waiting for wheel counts.');
    assert(Number.isFinite(last.host_time)&&last.host_time>=0,'counts sample host_time must be finite nonnegative');
    assert(isSafeInteger(last.device_ms)&&last.device_ms>=0&&last.device_ms<=0xffffffff,'device_ms must be uint32');
    const counts=last.counts; assert(Array.isArray(counts)&&counts.length===4,'Four wheel counts required.');
    for(const c of counts){assert(isSafeInteger(c),'Wheel count exceeds exact numeric precision.');}
    assert(last.host_time===rates.host_time,'counts host_time must equal rates.host_time');
    const diagnostics={};
    for(const w of wheelNames){
      const diag=observed.diagnostics?.[w];
      assert(diag?.fresh===true,'Waiting for fresh '+w+' diagnostics.');
      assert(Number.isFinite(diag.host_time)&&diag.host_time>=0,'diagnostics['+w+'].host_time must be finite nonnegative');
      assert(Number.isFinite(diag.pwm),'diagnostics['+w+'].pwm must be finite');
      assert(isSafeInteger(diag.a_edges)&&diag.a_edges>=0,'diagnostics['+w+'].a_edges must be nonnegative safe integer');
      assert(isSafeInteger(diag.b_edges)&&diag.b_edges>=0,'diagnostics['+w+'].b_edges must be nonnegative safe integer');
      assert(isSafeInteger(diag.invalid_transitions)&&diag.invalid_transitions>=0,'diagnostics['+w+'].invalid_transitions must be nonnegative safe integer');
      diagnostics[w]={host_time:diag.host_time,pwm:diag.pwm,a_edges:diag.a_edges,b_edges:diag.b_edges,invalid_transitions:diag.invalid_transitions};
    }
    const cpr={};
    for(const w of wheelNames){
      const val=observed.profile.counts_per_revolution?.[w];
      cpr[w]=Number.isFinite(val)&&val>0?val:null;
    }
    return {mode,epoch:observed.epoch,profile:'maker',host_time:rates.host_time,device_ms:last.device_ms,counts:counts.slice(),diagnostics, cpr};
  }
  function difference(start,current){
    assert(start.mode===current.mode,'mode differs');
    assert(start.profile===current.profile,'profile differs');
    assert(start.epoch===current.epoch,'epoch differs');
    const elapsed_s=current.host_time-start.host_time;
    assert(elapsed_s>=0,'host_time decreased');
    assert(elapsed_s<=3600,'elapsed host seconds exceeds 3600');
    const device_delta=current.device_ms-start.device_ms;
    if(device_delta<0){
      const max=0xffffffff; const unsigned=(current.device_ms+max+1)-start.device_ms;
      assert(unsigned<0x80000000,'device_ms rollover invalid');
    }
    const wheels={};
    let movement_observed=false; let invalid_observed=false;
    for(const w of wheelNames){
      assert(start.cpr[w]===current.cpr[w],'Wheel calibration changed.');
      const s=start.diagnostics[w]; const c=current.diagnostics[w];
      assert(c.host_time>=s.host_time,'diagnostics host_time decreased');
      const a_delta=c.a_edges-s.a_edges; const b_delta=c.b_edges-s.b_edges; const inv_delta=c.invalid_transitions-s.invalid_transitions;
      assert(a_delta>=0,'a_edges decreased');
      assert(b_delta>=0,'b_edges decreased');
      assert(inv_delta>=0,'invalid_transitions decreased');
      assert([a_delta,b_delta,inv_delta].every(isSafeInteger),'Diagnostic counter difference exceeds exact numeric precision.');
      const count_delta=current.counts[wheelNames.indexOf(w)]-start.counts[wheelNames.indexOf(w)];
      assert(isSafeInteger(count_delta),'count_delta unsafe');
      const cpr_val=start.cpr[w];
      const revolutions=cpr_val?count_delta/cpr_val:null;
      assert(revolutions===null||Number.isFinite(revolutions),'Revolution calculation exceeds numeric range.');
      wheels[w]={count_delta, revolutions, a_edges:a_delta, b_edges:b_delta, invalid_transitions:inv_delta};
      if(count_delta!==0||a_delta!==0||b_delta!==0)movement_observed=true;
      if(inv_delta>0)invalid_observed=true;
    }
    return {elapsed_s,mode:start.mode,wheels,movement_observed,invalid_observed};
  }
  window.EncoderWindow={snapshot,difference};
})();
