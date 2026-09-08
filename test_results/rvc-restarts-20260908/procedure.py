import fcntl,json,time,tempfile,urllib.request
from pathlib import Path
import serial
from mechbot_telemetry import parse_event

home=Path('/home/nate')
stage=Path(tempfile.mkdtemp(prefix='rvc-restarts-',dir=home))
report={'stage':str(stage),'scope':'Three ESP32 resets; IMU remains powered; motor power off per operator','rounds':[]}
def api(path,data=None):
    req=urllib.request.Request('http://127.0.0.1:8765'+path,data=None if data is None else json.dumps(data).encode(),headers={'Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=8) as r:return json.load(r)
def stopped(s):
    d=s.get('wheel_diagnostics',{})
    return set(d)=={'FL','FR','RL','RR'} and all(x['pwm']==0 and 0<=time.time()-x['updated']<3 for x in d.values())
def save(): (stage/'results.json').write_text(json.dumps(report,indent=2))
lock=(home/'.mechbot-firmware-update.lock').open('a')
fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
maintenance=False
try:
    before=api('/api/status');report['before']=before
    assert before['board']['firmware']=='ESP32_MAKER_MECANUM_RVC_V2'
    assert before['serial_connected'] and not before['gamepad_connected'] and not before['deadman']
    assert not before['maintenance'] and not before['calibration_active'] and stopped(before)
    assert api('/api/firmware')['state'] not in ('starting','running')
    api('/api/maintenance',{'enabled':True});maintenance=True
    assert not api('/api/status')['serial_connected']
    c=serial.Serial(port=None,baudrate=115200,timeout=.1,write_timeout=.3,exclusive=True)
    c.dtr=False;c.rts=False;c.port=before['serial_port']
    try:
        c.open()
        for n in range(3):
            c.reset_input_buffer();c.rts=True;time.sleep(.12);c.rts=False
            start=time.monotonic();rows=[];queried=False
            while time.monotonic()-start<6:
                elapsed=time.monotonic()-start
                if elapsed>1.5 and not queried:
                    c.write(b'X\nDIAG\nCFG GET\n');c.flush();queried=True
                raw=c.read_until(b'\n',2048)
                if raw: rows.append({'elapsed_s':time.monotonic()-start,'line':raw.decode('ascii','replace').strip()})
            ready=[r for r in rows if r['line']=='READY ESP32_MAKER_MECANUM_RVC_V2']
            imu=[(r,parse_event(r['line'],r['elapsed_s'])) for r in rows if r['line'].startswith('IR2 ')]
            valid=[(r,e) for r,e in imu if e and e['valid']]
            assert ready and valid,'missing startup identity/qualified sensor'
            assert all(e is not None and not e['heading_accepted'] and e['bad_checksum']==e['discontinuities']==e['uart_errors']==0 for _,e in imu)
            d=[parse_event(r['line'],r['elapsed_s']) for r in rows if r['line'].startswith('D ')]
            assert {e['wheel'] for e in d if e}=={'FL','FR','RL','RR'} and all(e and e['pwm']==0 for e in d)
            assert any(r['line']=='CFG heading-enabled 0' for r in rows)
            nav=[r['line'].split() for r in rows if r['line'].startswith('N ')]
            assert nav and all(t[-3:]==['0','0','0'] for t in nav)
            result=dict(round=n+1,ready_s=ready[0]['elapsed_s'],first_qualified_report_s=valid[0][0]['elapsed_s'],
                        first_heading_rad=valid[0][1]['yaw_rad'],valid_reports=len(valid),zero_outputs=True,heading_off=True,lines=rows)
            report['rounds'].append(result);save()
            print('PASS restart',n+1,'READY',round(result['ready_s'],3),'RVC',round(result['first_qualified_report_s'],3),flush=True)
    finally:c.close()
except Exception as e:
    report['error']=str(e)
finally:
    if maintenance:
        try:
            api('/api/maintenance',{'enabled':False})
            deadline=time.monotonic()+25
            while time.monotonic()<deadline:
                state=api('/api/status')
                if state['serial_connected'] and stopped(state):break
                time.sleep(.5)
            report['after']=state
            assert state['board']['firmware']=='ESP32_MAKER_MECANUM_RVC_V2' and stopped(state) and not state['deadman'] and state['rearm_required']
        except Exception as e:report['resume_error']=str(e)
    save();lock.close()
    print(json.dumps({k:report.get(k) for k in ('stage','error','resume_error')}),flush=True)
if report.get('error') or report.get('resume_error'):raise SystemExit(1)
