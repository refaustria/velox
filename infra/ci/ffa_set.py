"""D36/D51 (7) set-level bands over <outdir>/ffa-*.log.json (written by ffa_summary.py). Usage: ffa_set.py <outdir>"""
import glob
import json
import sys

SUM_KILLS_MIN = 74    # D51 (1)/(7): was 70 (D49); largest k with P(sum < k) <= 2.5 % at 10 runs
SUM_STUCK_MAX = 30    # D36
RUN_STUCK_MAX = 7     # D36 (per run; enforced by ffa_summary.py, re-checked here)
DWELL6_RUNS_MAX = 2   # D36: runs with max_dwell_s >= 6

runs = [json.load(open(p)) for p in sorted(glob.glob(sys.argv[1] + '/ffa-*.log.json'), key=lambda p: int(p.split('ffa-')[-1].split('.')[0]))]
st = sum(r['stuck'] for r in runs)
pk = sum(r['pocket'] for r in runs)
ki = sum(r['kills'] for r in runs)
d6 = sum(1 for r in runs if r['max_dwell'] >= 6)
rp = sum(r['repaths'] for r in runs)
fails = []
if st > SUM_STUCK_MAX: fails.append(f'sum_stuck>{SUM_STUCK_MAX}')
if pk > 0: fails.append('pocket>0')
if ki < SUM_KILLS_MIN: fails.append(f'sum_kills<{SUM_KILLS_MIN}')
if d6 > DWELL6_RUNS_MAX: fails.append(f'dwell>=6_runs>{DWELL6_RUNS_MAX}')
if any(r['stuck'] > RUN_STUCK_MAX for r in runs): fails.append(f'run_stuck>{RUN_STUCK_MAX}')
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
