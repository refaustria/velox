"""D36/D51 (7) set-level bands over <outdir>/ffa-*.log.json (written by ffa_summary.py). Usage: ffa_set.py <outdir>

ADR 0006 section 7: the sum-of-kills band is per map (BAND_FFA_KILLS_MIN, infra/ci/bands/<Map>.sh via lib.sh ci_load_bands; unset =
report-only, `band=none`). The map-independent invariants are judged on every map and printed as a table after the SET lines:
left_arena 0, world_kill 0, no_nav_point 0, giveup 0, sum of stuck strikes <= 5, bursts 0, max dwell <= 5 s, nav build <= 2 s,
link traversal success >= 90 % per link type, and item coverage (every pickup present at Live start taken at least once over the set).
Link success (ADR 0006 E0.3 b): ok / (ok + failed) per type pooled over the set; failed = reason missed, timeout, noimpulse, tooslow (and
unknown reasons, printed); replaced, below_start, ineligible, air_start, relaunched (task m5-06b) are re-plans, not attempts, printed as aborted=<n>; judged from 30 attempts
(INV_LINK_MIN_ATTEMPTS); FLAG (report-only) when aborted >= ok. Item coverage (E0.3 c): per instance, keyed (definition, at=) from the
VeloxPickupReset / VeloxPickup lines, when every run's log has at=; else per definition (old logs). BAND_FFA_COVERAGE_EXEMPT (definitions,
space separated) is honoured only on L_ItemTest, L_CombatTest and L_MovementTest (E0.3 a); on any other map it is ignored with a warning.
Dwell (ADR 0006 E2): the per-run max_dwell excludes camp=1 and timed-wait samples (ffa_summary.py); the SET line adds wait_max_s (report-only).
Exit status 1 on any FAIL."""
import collections
import glob
import json
import os
import re
import sys

SUM_STUCK_MAX = 30    # D36 (kept; the invariant below is stricter)
RUN_STUCK_MAX = 7     # D36 (per run; enforced by ffa_summary.py, re-checked here)
DWELL6_RUNS_MAX = 2   # D36: runs with max_dwell_s >= 6 (kept; the invariant below is stricter)
INV_STUCK_SUM_MAX = 5   # ADR 0006 section 7
INV_DWELL_MAX = 5
INV_NAV_BUILD_MAX_S = 2.0
INV_LINK_SUCCESS_MIN_PCT = 90.0
INV_LINK_MIN_ATTEMPTS = 30   # E0.3 (b): a link type with fewer attempts over the set is printed, not judged
EXEMPT_MAPS = ("L_ItemTest", "L_CombatTest", "L_MovementTest")   # E0.3 (a): the coverage exemption exists for test maps only
D51_KILLS_RUNS = 10          # the kills band is defined for the ten-run set


def band(key):
    v = os.environ.get('BAND_' + key, '')
    return int(v) if v.strip() else None


band_name = os.environ.get('VELOX_BAND_NAME', 'none')
has_band = band_name != 'none' and any(k.startswith('BAND_') and not k.startswith('BAND_HAZARD_') and v.strip() for k, v in os.environ.items())   # ADR 0008 3.5: a band file with no BAND_* value is report-only
band_label = os.environ.get('VELOX_BAND_LABEL', band_name)   # '<map> (file <F>)' when the map's band file sources another map's
SUM_KILLS_MIN = band('FFA_KILLS_MIN')    # D51 (1)/(7): was 70 (D49); largest k with P(sum < k) <= 2.5 % at 10 runs

runs = [json.load(open(p)) for p in sorted(glob.glob(sys.argv[1] + '/ffa-*.log.json'), key=lambda p: int(p.split('ffa-')[-1].split('.')[0]))]
st = sum(r['stuck'] for r in runs)
pk = sum(r['pocket'] for r in runs)
ki = sum(r['kills'] for r in runs)
d6 = sum(1 for r in runs if r['max_dwell'] >= 6)
rp = sum(r['repaths'] for r in runs)
bursts = sum(r['bursts'] for r in runs)
max_dwell = max((r['max_dwell'] for r in runs), default=0)
max_wait = max((r.get('wait_max_s', 0.0) for r in runs), default=0.0)   # ADR 0006 E2: report-only
kills_judged = SUM_KILLS_MIN is not None and len(runs) == D51_KILLS_RUNS
# Sample-size invariants (link success per type, item coverage) are defined for the ten-run set: with fewer runs they are printed
# as "n/j" (not judged), because three runs take 3/10 of the items and a handful of link attempts.
full_set = len(runs) >= D51_KILLS_RUNS
fails = []
if st > SUM_STUCK_MAX: fails.append(f'sum_stuck>{SUM_STUCK_MAX}')
if pk > 0: fails.append('pocket>0')
if kills_judged and ki < SUM_KILLS_MIN: fails.append(f'sum_kills<{SUM_KILLS_MIN}')
if d6 > DWELL6_RUNS_MAX: fails.append(f'dwell>=6_runs>{DWELL6_RUNS_MAX}')
if any(r['stuck'] > RUN_STUCK_MAX for r in runs): fails.append(f'run_stuck>{RUN_STUCK_MAX}')
bad = [r['label'] for r in runs if r['fails']]
if bad: fails.append('per_run:' + ','.join(bad))

# ---- map-independent invariants (ADR 0006 section 7)
inv = []   # (name, value text, limit text, ok)
la = [r['left_arena'] for r in runs if r.get('left_arena')]
inv.append(('left_arena', str(len(la)), '0', not la))
wk = sum(r.get('world_kills', 0) for r in runs)
inv.append(('world_kill', str(wk), '0', wk == 0))
nn = sum(r.get('no_nav_point', 0) for r in runs)
inv.append(('no_nav_point', str(nn), '0', nn == 0))
gu = sum(r.get('giveup', 0) for r in runs)
inv.append(('giveup', str(gu), '0', gu == 0))
inv.append(('sum_stuck', str(st), f'<= {INV_STUCK_SUM_MAX}', st <= INV_STUCK_SUM_MAX))
inv.append(('bursts', str(bursts), '0', bursts == 0))
inv.append(('max_dwell_s', str(max_dwell), f'<= {INV_DWELL_MAX}', max_dwell <= INV_DWELL_MAX))
navs = [r['nav_seconds'] for r in runs if r.get('nav_seconds') is not None]
inv.append(('nav_build_s', f'{max(navs):.3f}' if navs else 'n/a', f'<= {INV_NAV_BUILD_MAX_S}', not navs or max(navs) <= INV_NAV_BUILD_MAX_S))
lt = {}
lu = collections.Counter()
for r in runs:
    for t, v in (r.get('link_types') or {}).items():
        a = lt.setdefault(t, [0, 0, 0])
        a[0] += v[0]
        a[1] += v[1]
        a[2] += v[2] if len(v) > 2 else 0
    lu.update(r.get('link_unknown') or {})
link_flags = []
for t, (ok_, bad_, ab_) in sorted(lt.items()):
    n = ok_ + bad_
    pct = 100.0 * ok_ / n if n else 100.0
    judged = n >= INV_LINK_MIN_ATTEMPTS
    inv.append((f'link_success[{t}]', f'{pct:.1f} % ({ok_}/{n}) aborted={ab_}' + ('' if judged else ' n<%d, not judged' % INV_LINK_MIN_ATTEMPTS) + ('' if full_set else ' n/j'),
                f'>= {INV_LINK_SUCCESS_MIN_PCT:.0f} %', (not judged) or (not full_set) or pct >= INV_LINK_SUCCESS_MIN_PCT))
    if ab_ > 0 and ab_ >= ok_:
        link_flags.append(f'FLAG link {t}: aborted {ab_} >= ok {ok_} (re-planned more than traversed; report-only)')
if not lt:
    inv.append(('link_success', 'n/a (no VeloxBotLink lines)', f'>= {INV_LINK_SUCCESS_MIN_PCT:.0f} %', True))

# item coverage: the pickups present at Live start are the VeloxPickupReset lines; takes are counted per item definition
# (VeloxPickup lines carry no actor identity, so two pickups of one definition cannot be told apart: a definition with several
# instances is covered by one take; locations come from the VeloxPickupSpawn lines).
present = collections.Counter()
locs = collections.defaultdict(list)
for r in runs:
    for d in r.get('resets', []):
        present[d] = max(present[d], r['resets'].count(d))
    for d, at in r.get('spawns', []):
        if at not in locs[d]:
            locs[d].append(at)
takes = collections.Counter()
for r in runs:
    for d, n in (r.get('taken') or {}).items():
        takes[d] += n
# A band file may list definitions it knowingly leaves untaken (BAND_FFA_COVERAGE_EXEMPT, space separated, printed as exempt): a
# visible, per-map exception for the test grid L_ItemTest, whose bots never fetch need-less ammo; no other map has one.
exempt = set(os.environ.get('BAND_FFA_COVERAGE_EXEMPT', '').split())
warnings = []
if exempt and band_name not in EXEMPT_MAPS:
    warnings.append(f'exemption ignored on {band_name}: test maps only (ADR 0006 E0.3 a)')
    exempt = set()
# Per instance (E0.3 c) when every run's log carries at= on its VeloxPickupReset lines (and a reset exists); else per definition.
inst_mode = bool(present) and all(r.get('resets_at') is not None and all(at for _, at in r['resets_at']) for r in runs)
if inst_mode:
    inst_present = set()
    for r in runs:
        inst_present.update((d, at) for d, at in r['resets_at'])
    inst_takes = collections.Counter()
    for r in runs:
        for d, at in r.get('takes_at', []):
            inst_takes[(d, at)] += 1
    inst_untaken_all = sorted(i for i in inst_present if inst_takes[i] == 0)
    inst_untaken = [i for i in inst_untaken_all if i[0] not in exempt]
    untaken_all = [i[0] for i in inst_untaken_all]
    untaken = [i[0] for i in inst_untaken]
else:
    untaken_all = sorted(d for d in present if takes[d] == 0)
    untaken = [d for d in untaken_all if d not in exempt]


def fmt_at(at):
    return '(' + ','.join(re.findall(r'=(-?[\d.]+)', at)) + ')'


if inst_mode:
    inv.append(('item_coverage', f'{len(inst_present) - len(inst_untaken_all)}/{len(inst_present)} pickups ({len(present)} definitions)'
                + (f', {len(inst_untaken_all) - len(inst_untaken)} exempt' if len(inst_untaken_all) != len(inst_untaken) else '') + ('' if full_set else ' n/j'),
                'all', (not inst_untaken) or (not full_set)))
elif present:
    inv.append(('item_coverage', f'{len(present) - len(untaken_all)}/{len(present)} item definitions taken ({sum(present.values())} pickups)'
                + (f', {len(untaken_all) - len(untaken)} exempt' if len(untaken_all) != len(untaken) else '') + ('' if full_set else ' n/j'),
                'all', (not untaken) or (not full_set)))
else:
    inv.append(('item_coverage', 'n/a (no VeloxPickupReset lines)', 'all', True))
inv_fail = [n for n, _, _, ok in inv if not ok]
for n in inv_fail:
    fails.append('inv:' + n)

hz_t = sum(r.get('hazard_touch', 0) for r in runs)
hz_c = sum(r.get('hazard_death_credited', 0) for r in runs)
hz_s = sum(r.get('hazard_death_self', 0) for r in runs)
hz_b = {}
for r in runs:
    for b, v in (r.get('hazard_bots') or {}).items():
        a = hz_b.setdefault(b, [0, 0, 0])
        for i in range(3):
            a[i] += v[i]
# ADR 0008 5.3 / H9 (F9): provisional hard gate, judged when the band file sets BAND_HAZARD_SELF_SET_MAX (Fornax 6, Orbita 10 over FFA x 10).
# It does not switch the other bands from report-only to judged (has_band ignores BAND_HAZARD_*). Judged for the ten-run set only.
HAZ_SET_MAX = band('HAZARD_SELF_SET_MAX')
hz_gate = 'report-only, ADR 0008 3.5'
if HAZ_SET_MAX is not None:
    if len(runs) == D51_KILLS_RUNS:
        hz_gate = f'GATE set sum <= {HAZ_SET_MAX} (ADR 0008 5.3)'
        if hz_s > HAZ_SET_MAX: fails.append(f'hazard_death_self>{HAZ_SET_MAX}')
    else:
        hz_gate = f'gate {HAZ_SET_MAX} judged for {D51_KILLS_RUNS} runs only; {len(runs)} run(s): reported'
rules = {}
for r in runs:
    for k, v in r['rules'].items():
        rules[k] = rules.get(k, 0) + v
print(f"SET runs={len(runs)} {'PASS' if not fails else 'FAIL(' + ';'.join(fails) + ')'} sum_stuck={st} max_run_stuck={max((r['stuck'] for r in runs), default=0)} "
      f"bursts={bursts} pocket={pk} dwell_ge6_runs={d6} max_dwell={max_dwell} wait_max_s={max_wait:.1f} "
      f"max_link_dt={max((r['link_max_dt'] for r in runs), default=0):.2f} sum_kills={ki} min_kills={min((r['kills'] for r in runs), default=0)} "
      f"sum_repaths={rp} max_run_repaths={max((r['repaths'] for r in runs), default=0)} not_at_goal={sum(r.get('not_at_goal', 0) for r in runs)} rules={rules}")
sp = [s for r in runs for s in r['speed_strikes']]
print(f"SET speed strikes: {' '.join(sp) if sp else '-'}")
if not has_band:
    print(f"SET band={'none' if band_name == 'none' else band_label + ' (no BAND_* value)'} (report-only): sum_kills={ki} not judged; the per-run kills / pickups / Mega bands are not judged either")
else:
    print(f"SET bands={band_label}: sum_kills >= {SUM_KILLS_MIN}" + ('' if kills_judged else f' (judged for {D51_KILLS_RUNS} runs only; {len(runs)} run(s): reported)'))
print(f"SET hazard ({hz_gate}): hazard_touch={hz_t} hazard_death_credited={hz_c} hazard_death_self={hz_s}"
      + (' per bot touch/credited/self: ' + ' '.join(f'{b}:{v[0]}/{v[1]}/{v[2]}' for b, v in sorted(hz_b.items())) if hz_b else '')
      + f" per run self=[{','.join(str(r.get('hazard_death_self', 0)) for r in runs)}]")
print('SET invariants (ADR 0006 section 7, every map):')
for n, v, lim, ok in inv:
    print(f'  {n:<28} {v:<44} limit {lim:<8} {"PASS" if ok else "FAIL"}')
if inst_mode:
    if inst_untaken:
        print(('  untaken items (layout or link defect): ' if full_set else '  untaken items (reported; judged for %d runs): ' % D51_KILLS_RUNS) + '; '.join(f'{d}@{fmt_at(at)}' for d, at in inst_untaken))
    if len(inst_untaken_all) != len(inst_untaken):
        print(f'  untaken but exempt by the {band_name} band file: ' + '; '.join(f'{d}@{fmt_at(at)}' for d, at in inst_untaken_all if d in exempt))
else:
    if untaken:
        print(('  untaken items (layout or link defect): ' if full_set else '  untaken items (reported; judged for %d runs): ' % D51_KILLS_RUNS) + '; '.join(f"{d} at {' / '.join(locs[d]) or '?'}" for d in untaken))
    if untaken_all != untaken:
        print(f'  untaken but exempt by the {band_name} band file: ' + '; '.join(f"{d} at {' / '.join(locs[d]) or '?'}" for d in untaken_all if d in exempt))
for w in warnings:
    print('  WARNING: ' + w)
for f_ in link_flags:
    print('  ' + f_)
if lu:
    print('  link unknown reasons (counted failed): ' + ' '.join(f'{k}={v}' for k, v in sorted(lu.items())))
sys.exit(1 if fails else 0)
