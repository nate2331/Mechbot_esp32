"""Bounded user-supervised encoder trial. Reuses tested pulse/stop/restoration logic."""
import json
from pathlib import Path
import sys
import time

from maker_auto_tune import MakerSerialRig, CalibrationError, WHEELS, asdict


class LoggedWheelSerial:
    """Retain all raw evidence; omit IMU records only from the IMU-dependent tuner parser."""
    def __init__(self, connection, log):
        self.connection, self.log = connection, log

    def record(self, direction, raw):
        self.log.write(json.dumps({'host_time':time.monotonic(), 'direction':direction,
                                   'raw':raw.decode('ascii', errors='replace')})+'\n')
        self.log.flush()

    def write(self, raw):
        result = self.connection.write(raw)
        self.record('tx', raw[:result])
        return result

    def readline(self, limit):
        raw = self.connection.readline(limit)
        if raw:
            self.record('rx', raw)
        # This is exclusively a wheel check with heading/field disabled. No IMU
        # validity or reset-counter conclusion can be made from it.
        if raw.startswith((b'I ', b'H ')):
            return b''
        return raw

    def reset_input_buffer(self):
        self.connection.reset_input_buffer()


class WheelOnlyRig(MakerSerialRig):
    def healthy(self):
        now = self.clock()
        if not self.frame or now-self.frame.received > .6:
            raise CalibrationError('Encoder telemetry is stale')
        if now-self.nav_at > .6 or self.nav != (0, 0):
            raise CalibrationError('Control-state telemetry stale or heading/field enabled')
        if now-self.began > 120:
            raise CalibrationError('Wheel check exceeded two minutes')

    def rest(self):
        # STOP is acknowledged first. Offline-IMU retries can pause telemetry
        # while stopped. Wait for fresh stationary evidence afterward, never
        # relax the freshness deadline while motors are commanded to move.
        self.command('X', 'OK STOP')
        began=self.clock(); previous=None; still_since=None
        while self.clock()-began < 6:
            self.read()
            now=self.clock()
            if not self.frame or now-self.frame.received > .6 or now-self.nav_at > .6:
                previous=None; still_since=None
                continue
            self.healthy()
            if self.frame is previous:
                continue
            if previous and self.frame.counts==previous.counts:
                if still_since is None: still_since=previous.received
            else:
                still_since=None
            previous=self.frame
            if still_since is not None and now-began>=1 and self.frame.received-still_since>=.6:
                if any(abs(v[0])>.1 for v in self.get_diagnostics().values()):
                    raise CalibrationError('Nonzero output while stopped')
                self.healthy()
                return
        raise CalibrationError('No fresh stationary evidence within six seconds')


def main():
    import fcntl
    import hashlib
    import signal
    import tempfile
    import urllib.request
    import serial
    home = Path('/home/nate')
    directory = Path(tempfile.mkdtemp(prefix='maker-pwm-readback-trial-', dir=home))
    report = {'scope':'single raised-wheel forward pulse to compare peripheral PWM readback with requested duty',
              'status':'preflight', 'directory':str(directory), 'pulses':[],
              'started':time.time(), 'settings_saved_to_nvs':False,
              'imu_validation':'excluded; heading and field control must remain disabled'}
    def save():
        (directory/'results.json').write_text(json.dumps(report,indent=2))
        (home/'maker-pwm-readback-trial-latest.json').write_text(json.dumps(report,indent=2))
    def api(path, data=None):
        request = urllib.request.Request('http://127.0.0.1:8765'+path,
            data=None if data is None else json.dumps(data).encode(), headers={'Content-Type':'application/json'})
        with urllib.request.urlopen(request,timeout=5) as response:
            return json.load(response)
    def interrupted(*args):
        raise CalibrationError('Trial interrupted')
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig,interrupted)
    connection=rig=log=None
    maintenance=False
    lock=(home/'.mechbot-firmware-update.lock').open('a')
    fcntl.flock(lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
    try:
        state=api('/api/status'); report['preflight']=state
        if (not state['serial_connected'] or state['board']['id']!='maker' or state['simulated'] or
            state['deadman'] or state['maintenance'] or state['calibration_active']):
            raise CalibrationError('Requires idle connected Maker and released deadman; maintenance isolates gamepad')
        if api('/api/firmware')['state'] in ('starting','running'):
            raise CalibrationError('Firmware job active')
        diagnostics=state['wheel_diagnostics']
        if set(diagnostics)!=set(WHEELS) or any(v['pwm']!=0 or time.time()-v['updated']>3 for v in diagnostics.values()):
            raise CalibrationError('Requires fresh zero outputs')
        if state['serial_port']!='/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0':
            raise CalibrationError('USB adapter changed')
        report['settings_before']=api('/api/settings')
        upload=json.loads((home/'maker-upload-latest.json').read_text())
        if not upload.get('flash_verified') or not upload.get('fresh_ready'):
            raise CalibrationError('No verified firmware upload record')
        report['firmware_upload_manifest']=upload['manifest']
        report['runner_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        save()
        api('/api/stop',{})
        maintenance=True
        api('/api/maintenance',{'enabled':True})
        held=api('/api/status')
        if not held['maintenance'] or held['serial_connected']:
            raise CalibrationError('Bridge did not release serial')
        connection=serial.Serial(port=None,baudrate=115200,timeout=.01,write_timeout=.3,exclusive=True)
        connection.dtr=False; connection.rts=False; connection.port=state['serial_port']; connection.open()
        log=(directory/'serial.jsonl').open('w')
        rig=WheelOnlyRig(LoggedWheelSerial(connection,log))
        rig.prepare()
        report['original_config']=rig.original
        for name, values, direction in [('All forward PWM readback',[177]*4,1)]:
            entry={'name':name,'pwm':values,'direction':direction,'seconds':1.2,'status':'running'}
            report['pulses'].append(entry); report['status']='running'; save()
            print('PULSE '+name,flush=True)
            entry['before']=rig.get_diagnostics()
            observation=rig.pulse(values,direction,1.2)
            entry['observation']=asdict(observation)
            entry['after']=rig.get_diagnostics()
            if not all(observation.sustained[i] for i,v in enumerate(values) if v):
                raise CalibrationError('An enabled wheel did not sustain encoder movement; no further pulses')
            if any(entry['after'][w][1]!=entry['before'][w][1] for w in WHEELS):
                raise CalibrationError('Invalid transitions changed after pulse')
            entry['status']='passed'; save()
        report['status']='completed'
    except Exception as exc:
        report.update(status='aborted',error=str(exc))
    finally:
        if rig:
            try:
                rig.restore(); report['restored']=rig.restored
                report['restored_config']=rig.get_config()
                if report['restored_config']!=rig.original:
                    raise CalibrationError('Original settings mismatch')
                report['final_diagnostics']=rig.get_diagnostics()
                if any(v[0]!=0 for v in report['final_diagnostics'].values()):
                    raise CalibrationError('Final output nonzero')
            except Exception as exc:
                report.update(status='aborted',restore_error=str(exc))
            finally:
                try: rig.send('X')
                except Exception: pass
        if connection: connection.close()
        if log: log.close()
        if maintenance:
            try:
                api('/api/maintenance',{'enabled':False})
                end=time.monotonic()+20
                while time.monotonic()<end:
                    state=api('/api/status')
                    if state['serial_connected'] and state['board']['id']=='maker': break
                    time.sleep(.3)
                api('/api/stop',{})
                report['postflight']=api('/api/status')
                report['settings_after']=api('/api/settings')
            except Exception as exc:
                report.update(status='aborted',resume_error=str(exc))
        report['finished']=time.time(); save(); lock.close()
        print(json.dumps({'status':report['status'],'directory':str(directory),
                          'error':report.get('error'),'restore_error':report.get('restore_error'),
                          'resume_error':report.get('resume_error')}),flush=True)
    return 0 if report['status']=='completed' else 1

if __name__=='__main__':
    sys.exit(main())
