import json
from pathlib import Path
import sys
sys.path.insert(0, 'overnight-work')
from maker_auto_tune import Frame, analyse, asdict

rows=[]
for directory in sorted(Path('.').glob('maker-paired-motors-*')):
    report=json.loads((directory/'results.json').read_text())
    entries=[json.loads(line) for line in (directory/'serial.jsonl').read_text().splitlines()]
    windows=[]; window=None; previous=None
    for item in entries:
        raw=item['raw'].strip(); stamp=item['host_time']
        if item['direction']=='tx' and raw.startswith('V '):
            if window is None:
                window={'start':stamp,'frames':[previous] if previous else []}
        elif item['direction']=='tx' and raw=='X' and window is not None:
            windows.append(window); window=None
        elif item['direction']=='rx' and raw.startswith('T '):
            parts=list(map(int,raw.split()[1:]))
            previous=Frame(stamp,parts[0],tuple(parts[1:]))
            if window is not None: window['frames'].append(previous)
    for pulse,window in zip(report['pulses'],windows):
        row={'name':pulse['name'],'run':directory.name,'status':pulse['status'],
             'error':report.get('error') if pulse['status']!='passed' else None}
        try:
            obs=analyse(window['frames'],window['start'],pulse['pwm'],pulse['direction'])
            row['reconstructed_observation']=asdict(obs)
            if 'observation' in pulse:
                assert all(abs(a-b)<.01 for a,b in zip(obs.rpm,pulse['observation']['rpm']))
        except Exception as exc: row['analysis_error']=str(exc)
        rows.append(row)
        print(row['name'],row['status'],[round(v,2) for v in row.get('reconstructed_observation',{}).get('rpm',[])], row.get('analysis_error',''))
Path('paired-motor-summary.json').write_text(json.dumps(rows,indent=2))
