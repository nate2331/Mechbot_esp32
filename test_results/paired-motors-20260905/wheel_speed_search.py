"""Bounded static PWM matching with trusted encoder feedback; no persistence."""
import statistics
from dataclasses import asdict


def search(rig, report, save):
    def pulse(pwm, direction, label):
        entry = dict(name=label, pwm=list(pwm), direction=direction, seconds=2.4, status='running')
        report['pulses'].append(entry)
        save()
        obs = rig.pulse(list(pwm), direction, 2.4)
        entry.update(observation=asdict(obs), status='measured')
        save()
        print(f'{label}: PWM {pwm}, RPM {[round(v,2) for v in obs.rpm]}', flush=True)
        return obs

    report['matching'] = {}
    ceilings = [177,177,160,150]
    for direction in (1, -1):
        # Start from the already measured front-wheel range, avoiding further
        # high-speed rear baselines. Every selected duty is verified below.
        rpm = [27.95,21.09,58.58,71.54]
        target = 21.09
        if target <= 0:
            raise RuntimeError('A wheel did not sustain baseline motion')
        anchor = rpm.index(target)
        low, high = [0]*4, list(ceilings)
        best = list(ceilings)
        errors = [abs(v-target) for v in rpm]
        history = []
        result = dict(target_rpm=target, anchor=anchor, baseline_rpm=rpm, history=history)
        report['matching'][str(direction)] = result
        for attempt in range(8):
            pwm = [ceilings[i] if i == anchor else (low[i]+high[i])//2 for i in range(4)]
            obs = pulse(pwm, direction, f'search {direction} step {attempt+1}')
            history.append(asdict(obs))
            for i, value in enumerate(obs.rpm):
                error = abs(value-target)
                if obs.sustained[i] and error < errors[i]:
                    best[i], errors[i] = pwm[i], error
                if value < target:
                    low[i] = min(high[i], pwm[i]+1)
                else:
                    high[i] = max(low[i], pwm[i])
            save()
        validation = [pulse(best, direction, f'verify {direction} repeat {r+1}') for r in range(3)]
        result.update(best_pwm=best, validation=[asdict(o) for o in validation])
        save()
    candidate = [round(sum(report['matching'][str(d)]['best_pwm'][i] for d in (1,-1))/2) for i in range(4)]
    report['shared_candidate'] = candidate
    report['shared_validation'] = []
    for direction in (1,-1):
        for repeat in range(3):
            obs = pulse(candidate, direction, f'shared verify {direction} repeat {repeat+1}')
            report['shared_validation'].append(asdict(obs))
            save()
