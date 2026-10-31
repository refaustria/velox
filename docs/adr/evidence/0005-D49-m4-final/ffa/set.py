"""04g: D36 set-level bands over <outdir>/ffa-*.log.json (written by summary.py). Usage: set.py <outdir>"""
import glob
import json
import sys

runs = [json.load(open(p)) for p in sorted(glob.glob(sys.argv[1] + '/ffa-*.log.json'), key=lambda p: int(p.split('ffa-')[-1].split('.')[0]))]
st = sum(r['stuck'] for r in runs)
pk = sum(r['pocket'] for r in runs)
ki = sum(r['kills'] for r in runs)
d6 = sum(1 for r in runs if r['max_dwell'] >= 6)
rp = sum(r['repaths'] for r in runs)
fails = []
if st > 30: fails.append('sum_stuck>30')
if pk > 0: fails.append('pocket>0')
if ki < 70: fails.append('sum_kills<70')
if d6 > 2: fails.append('dwell>=6_runs>2')
bad = [r['label'] for r in runs if r['fails']]
if bad: fails.append('per_run:' + ','.join(bad))
rules = {}
for r in runs:
    for k, v in r['rules'].items():
        rules[k] = rules.get(k, 0) + v
print(f"SET runs={len(runs)} {'PASS' if not fails else 'FAIL(' + ';'.join(fails) + ')'} sum_stuck={st} max_run_stuck={max((r['stuck'] for r in runs), default=0)} "
      f"bursts={sum(r['bursts'] for r in runs)} pocket={pk} dwell_ge6_runs={d6} max_dwell={max((r['max_dwell'] for r in runs), default=0)} "
      f"max_link_dt={max((r['link_max_dt'] for r in runs), default=0):.2f} sum_kills={ki} min_kills={min((r['kills'] for r in runs), default=0)} "
      f"sum_repaths={rp} max_run_repaths={max((r['repaths'] for r in runs), default=0)} rules={rules}")
sp = [s for r in runs for s in r['speed_strikes']]
print(f"SET speed strikes: {' '.join(sp) if sp else '-'}")
