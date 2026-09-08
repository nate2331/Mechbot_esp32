"""Supervised wheels-up trial; run on Pi via stdin. Does not save settings."""
import sys
import time
import serial

pwms = list(map(int, sys.argv[1:5]))
assert len(pwms) == 4 and all(150 <= p <= 177 for p in pwms)
s = serial.Serial('/dev/ttyUSB0', 115200, timeout=.01,
                  write_timeout=.3, exclusive=True)

def send(command):
    s.write((command + '\n').encode())
    s.flush()

def read(seconds):
    out = []
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        line = s.readline().decode(errors='replace').strip()
        if line:
            out.append((time.monotonic(), line))
    return out

try:
    time.sleep(1)
    s.reset_input_buffer()
    send('X')
    send('?')
    pre = read(1)
    assert any('Maker mapping: FL=M2 FR=M3 RL=M1 RR=M0' in l for _, l in pre)
    send('F 0')
    send('CFG SET heading-enabled 0')
    for wheel, pwm in zip(['fl', 'fr', 'rl', 'rr'], pwms):
        send(f'CFG SET pwm-{wheel} {pwm}')
    send('CFG GET')
    send('DIAG')
    pre = read(1)
    for wheel, pwm in zip(['fl', 'fr', 'rl', 'rr'], pwms):
        assert any(l == f'CFG pwm-{wheel} {pwm}' for _, l in pre)
    imu = [l for _, l in pre if l.startswith('I ')]
    assert len(imu) >= 3 and all(len(l.split()) == 13 for l in imu[-3:])
    health = [l.split()[-2:] for _, l in pre if l.startswith('H ')][-1]
    baseline = {l.split()[1]: int(l.split()[-1]) for _, l in pre if l.startswith('D ')}
    for direction in [1, -1]:
        print('START', direction, 'PWM', pwms, flush=True)
        start = time.monotonic()
        next_send = start
        next_diag = start + 1
        last_imu = start
        data = []
        while time.monotonic() - start < 3:
            now = time.monotonic()
            assert now - last_imu < .6, 'IMU lost'
            if now >= next_send:
                send(f'V {direction} 0 0')
                next_send = now + .05
            if now >= next_diag:
                send('DIAG')
                next_diag = now + 1
            fresh = read(.01)
            data += fresh
            for stamp, line in fresh:
                if line.startswith('I '):
                    assert len(line.split()) == 13, line
                    last_imu = stamp
                if line.startswith('H '):
                    assert line.split()[-2:] == health, 'IMU reset'
                if line.startswith('D '):
                    assert int(line.split()[-1]) - baseline[line.split()[1]] <= 5
                assert not line.startswith('FAULT'), line
        send('X')
        after = read(4)
        send('DIAG')
        after += read(.4)
        rows = [list(map(int, l.split()[1:])) for t, l in data
                if l.startswith('T ') and t - start >= 1.5]
        a, b = rows[0], rows[-1]
        dt = (b[0] - a[0]) / 1000
        cps = [(y-x)/dt for x, y in zip(a[1:], b[1:])]
        print('RPM FL FR RL RR', [round(abs(v)*60/2470, 2) for v in cps], flush=True)
        assert all(v * direction > 0 for v in cps), 'No motion/wrong direction'
        for wheel, pwm in zip(['FL', 'FR', 'RL', 'RR'], pwms):
            assert any(l.startswith('D '+wheel+' PWM ') and abs(float(l.split()[3])) == pwm
                       for _, l in data), 'PWM not reached'
        coast = [l.split()[2:] for _, l in after if l.startswith('T ')]
        assert coast[-1] == coast[-2] == coast[-3], 'Still moving'
        new = {l.split()[1]: int(l.split()[-1]) for _, l in after if l.startswith('D ')}
        print('NEW INVALID', {w: new[w]-baseline[w] for w in baseline}, flush=True)
        assert all(0 <= new[w]-baseline[w] <= 5 for w in baseline)
        assert all(l.split()[-2:] == health for _, l in after if l.startswith('H '))
        baseline = new
finally:
    send('X')
    s.close()
