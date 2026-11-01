#!/usr/bin/env python3
"""ADR 0006 E4 (task 07c): DM1 set bands re-derived by the D51 (1) method over every round on the final layout.

Basis A = the rounds with the current bots on a quiet host: task 06b on L_DM1_Blockout (`../0006-dm1-blockout-06b/`, head 8e95bfd)
plus task 15 on L_DM1 (`../0006-dm1-art/`, same layout by `--diff-collision`): 6 BotRun, 6 BotDuel, 20 FFA.
Basis B = A plus task 06 set 6 (final layout, pre-06b bots, contended host: behaviour counts valid under D40, no cost; E3's second basis):
9 BotRun, 9 BotDuel, 30 FFA. Task 06 set 5 (layout minus the Quad/shard commit) is printed as a check, never in a basis.
Per-round figures are read from the committed `.stats.json` / `.log.json` where they exist (06b rounds, both FFA sets) and parsed from the
committed console output `run.out.gz` where they do not (the task-15 BotRun/BotDuel sets carry only the last round's stats.json); task 06
set 5/6 figures are typed in from `../0006-dm1-blockout/<set>/run.out.gz` (cross-checked against E3's `e3_bands.py`).

Lower band k on a set sum: largest k with P(sum < k | unchanged bots and layout) <= 2.5 % under Poisson(n x per-round mean) and under a
bootstrap of n rounds drawn with replacement from the observed rounds; the smaller of the two (D51 1). Run from this directory:
python3 -I e4_bands.py [evidence root, default ..]"""
import gzip
import json
import math
import random
import re
import statistics as st
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else '..'
random.seed(6)
B = 200000


# ---------------------------------------------------------------- statistics
def pois_cdf_lt(k, lam):
    return sum(math.exp(-lam) * lam ** i / math.factorial(i) for i in range(0, k))


def binom_lt(k, n, p):
    return sum(math.comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(0, k))


def binom_le(k, n, p):
    return binom_lt(k + 1, n, p)


def binom_ge(k, n, p):
    return sum(math.comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(k, n + 1))


def fisher_two_sided(a, b, c, d):
    """2x2 table [[a, b], [c, d]] (ok/failed per set): two-sided Fisher exact p (sum of tables at most as probable)."""
    n = a + b + c + d
    r1, c1 = a + b, a + c
    def p_of(x):
        return math.comb(r1, x) * math.comb(n - r1, c1 - x) / math.comb(n, c1)
    p_obs = p_of(a)
    return sum(p_of(x) for x in range(max(0, c1 - (n - r1)), min(r1, c1) + 1) if p_of(x) <= p_obs + 1e-12)


def wilson(ok, n, z=1.96):
    p = ok / n
    den = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / den
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den
    return 100 * (centre - half), 100 * (centre + half)


def band(name, rounds, n, old, check_rounds=None, floor=1):
    mean = st.mean(rounds)
    lam = n * mean
    k_p = max(k for k in range(0, int(lam) + 1) if pois_cdf_lt(k, lam) <= 0.025) if lam > 0 else 0
    sums = sorted(sum(random.choice(rounds) for _ in range(n)) for _ in range(B))
    k_b = max((k for k in range(0, int(max(sums)) + 1) if sum(1 for s in sums if s < k) / B <= 0.025), default=0)
    k = max(floor, min(k_p, k_b))
    p_fail_pois, p_fail_boot = pois_cdf_lt(k, lam), sum(1 for s in sums if s < k) / B
    p_old_pois, p_old_boot = pois_cdf_lt(old, lam), sum(1 for s in sums if s < old) / B
    p_30, p_half = pois_cdf_lt(k, lam * 0.7), pois_cdf_lt(k, lam / 2)
    obs_sets = [sum(rounds[i:i + n]) for i in range(0, len(rounds) - n + 1, n)]
    chk = ''
    if check_rounds:
        chk = '; check rounds %s (sets %s)' % (check_rounds, [sum(check_rounds[i:i + n]) for i in range(0, len(check_rounds) - n + 1, n)])
    print('%-30s rounds=%s mean/round=%.2f set mean=%.1f | Poisson k=%d | bootstrap k=%d | band k=%d: P(fail|unchanged) Poisson %.2f %% '
          'bootstrap %.2f %%; E3 band %d: P(fail) Poisson %.2f %% bootstrap %.2f %%; P(fail|-30 %%) %.0f %%; P(fail|halved) %.0f %%; '
          'observed sets %s%s' % (name, rounds, mean, lam, k_p, k_b, k, 100 * p_fail_pois, 100 * p_fail_boot, old, 100 * p_old_pois,
                                   100 * p_old_boot, 100 * p_30, 100 * p_half, obs_sets, chk))
    return k


def per_round(name, rounds, k):
    mean = st.mean(rounds)
    print('%-30s rounds=%s mean=%.2f per-round band >= %d: P(round < %d | unchanged) = %.2f %% per round (D36: a per-round band tighter '
          'than the 10 %% tail is not a band; outlier cap ~0.5 %%)' % (name, rounds, mean, k, k, 100 * pois_cdf_lt(k, mean)))


def sd_band(name, values, k):
    m, sd = st.mean(values), st.pstdev(values)
    print('%-30s values=%s mean %.1f sd %.1f min %d -> band >= %d is %.1f sd below the mean' % (name, values, m, sd, min(values), k, (m - k) / sd))


# ---------------------------------------------------------------- evidence readers
def stats_rounds(path_fmt, script, n=3):
    out = []
    for k in range(1, n + 1):
        d = json.load(open(path_fmt % (k, script)))
        links_ok = sum(v[0] for v in d['link_types'].values())
        out.append(dict(kills=d['kills'], pickups=d['pickups'], mega=d['mega_takes'], links_ok=links_ok, camp=d['camp_episodes'],
                        timed=[round(t, 1) for t in d['timed']], first_after_live=round(d['first_after_live'], 1)))
    return out


def ffa_runs(path_fmt, n=10):
    out = []
    for k in range(1, n + 1):
        d = json.load(open(path_fmt % k))
        out.append(dict(kills=d['kills'], pickups=d['pickups'], mega=d['mega'],
                        drop=tuple(d['link_types'].get('Drop', [0, 0, 0])[:2]), pad=tuple(d['link_types'].get('JumpPad', [0, 0, 0])[:2])))
    return out


def console_rounds(path, script):
    """Per-round figures from a nettest run.out.gz (the task-15 sets): the round line, pickups list, links line, camp line, Mega line."""
    out, cur = [], None
    with gzip.open(path, 'rt', errors='replace') as fh:
        for line in fh:
            m = re.match(r'^%s\s+(PASS|FAIL)\s.*kills=(\d+).*pickups=(\d+)' % script, line)
            if m:
                cur = dict(kills=int(m.group(2)), pickups=int(m.group(3)), mega=0, links_ok=0, camp=0, timed=[], first_after_live=None)
                out.append(cur)
                continue
            if cur is None:
                continue
            m = re.search(r'Health_Mega=(\d+)', line)
            if m and 'pickups_by_bots=' in line:
                cur['mega'] = int(m.group(1))
            m = re.search(r'links ok=(\d+)/(\d+)', line)
            if m:
                cur['links_ok'] = int(m.group(1))
            m = re.search(r'camp_episodes=(\d+)', line)
            if m and 'camp_lines=' in line:
                cur['camp'] = int(m.group(1))
            m = re.search(r'timed_returns_s=\[([^\]]*)\].*first_after_live_s=([\d.]+)|first_after_live_s=([\d.]+).*timed_returns_s=\[([^\]]*)\]', line)
            if m:
                lst = m.group(1) if m.group(1) is not None else m.group(4)
                fal = m.group(2) if m.group(2) is not None else m.group(3)
                cur['timed'] = [float(x) for x in lst.split(',') if x.strip()]
                cur['first_after_live'] = float(fal)
    return out


b06_run = stats_rounds(ROOT + '/0006-dm1-blockout-06b/dm1-botrun/logs/round-%d/%s.stats.json', 'BotRun')
b06_duel = stats_rounds(ROOT + '/0006-dm1-blockout-06b/dm1-botduel/logs/round-%d/%s.stats.json', 'BotDuel')
b06_ffa = ffa_runs(ROOT + '/0006-dm1-blockout-06b/dm1-ffa/logs/ffa-%d.log.json')
art_run = console_rounds(ROOT + '/0006-dm1-art/botrun/run.out.gz', 'BotRun')
art_duel = console_rounds(ROOT + '/0006-dm1-art/botduel/run.out.gz', 'BotDuel')
art_ffa = ffa_runs(ROOT + '/0006-dm1-art/ffa/ffa-%d.log.json')
assert len(art_run) == 3 and len(art_duel) == 3, (art_run, art_duel)

# task 06 sets 6 and 5 (../0006-dm1-blockout/<set>/run.out.gz; typed in, cross-checked against e3_bands.py and the set-6 console)
s6_run = dict(kills=[4, 4, 11], pickups=[58, 56, 72], links_ok=[28, 30, 13], mega=[4, 3, 4])
s5_run = dict(kills=[6, 7, 7], pickups=[64, 59, 65], links_ok=[24, 13, 16])
s6_duel = dict(frags=[4, 6, 4], camp=[10, 8, 14], timed=[[6.4, 7.1], [2.8, 1.4], [18.9, 3.3]])
s5_duel = dict(frags=[2, 6, 4], camp=[11, 6, 7], timed=[[0.8, 0.0, 0.5], [4.2, 0.1], [1.3, 5.4]])
s6_ffa = dict(kills=[3, 4, 5, 5, 1, 3, 6, 7, 6, 6], pickups=[79, 69, 80, 62, 55, 65, 65, 86, 81, 74])

print('== Evidence read ==')
print('06b BotRun  :', b06_run)
print('art BotRun  :', art_run)
print('06b BotDuel :', b06_duel)
print('art BotDuel :', art_duel)
print('06b FFA     :', b06_ffa)
print('art FFA     :', art_ffa)
print('Rounds on the final layout with the current (post-06b) bots on a quiet host: BotRun %d, BotDuel %d, FFA %d (basis A); with task 06 '
      'set 6 (pre-06b bots, D40 host): %d / %d / %d (basis B). D51 asks for >= 12 fresh rounds per bot script: NOT met for BotRun/BotDuel.'
      % (len(b06_run) + len(art_run), len(b06_duel) + len(art_duel), len(b06_ffa) + len(art_ffa),
         len(b06_run) + len(art_run) + 3, len(b06_duel) + len(art_duel) + 3, len(b06_ffa) + len(art_ffa) + 10))
print()

# ---------------------------------------------------------------- BotRun
print('== BotRun x3 (FFA, 3 Medium bots, 90 s) ==')
kills_a = [r['kills'] for r in b06_run] + [r['kills'] for r in art_run]
kills_b = kills_a + s6_run['kills']
band('BotRun sum kills (A)', kills_a, 3, 11, check_rounds=s5_run['kills'])
K_BOTRUN = band('BotRun sum kills (B)', kills_b, 3, 11, check_rounds=s5_run['kills'])
per_round('BotRun kills per round (B)', kills_b, 1)
sd_band('BotRun pickups per round (B)', [r['pickups'] for r in b06_run] + [r['pickups'] for r in art_run] + s6_run['pickups'], 30)
per_round('BotRun links ok per round (B)', [r['links_ok'] for r in b06_run] + [r['links_ok'] for r in art_run] + s6_run['links_ok'], 5)
print('BotRun Mega takes per round: 06b %s, art %s, set 6 %s (35 s respawn, 90 s: <= 4 incl. the Live reset) -> BAND_BOTRUN_MEGA_REQUIRED=1 '
      'stays a presence band' % ([r['mega'] for r in b06_run], [r['mega'] for r in art_run], s6_run['mega']))
print()

# ---------------------------------------------------------------- BotDuel
print('== BotDuel x3 (Duel, 2 Hard, 120 s) ==')
frags_a = [r['kills'] for r in b06_duel] + [r['kills'] for r in art_duel]
frags_b = frags_a + s6_duel['frags']
band('BotDuel sum frags (A)', frags_a, 3, 7, check_rounds=s5_duel['frags'])
K_FRAGS = band('BotDuel sum frags (B)', frags_b, 3, 7, check_rounds=s5_duel['frags'])
per_round('BotDuel frags per round (B)', frags_b, 1)
camp_a = [r['camp'] for r in b06_duel] + [r['camp'] for r in art_duel]
camp_b = camp_a + s6_duel['camp']
band('BotDuel sum camp episodes (A)', camp_a, 3, 21, check_rounds=s5_duel['camp'])
K_CAMP = band('BotDuel sum camp episodes (B)', camp_b, 3, 21, check_rounds=s5_duel['camp'])
mega_a = [r['timed'] for r in b06_duel] + [r['timed'] for r in art_duel]
mega_b = mega_a + s6_duel['timed']
for label, rounds in (('A: 06b + art', mega_a), ('B: A + set 6', mega_b), ('B + set 5 (check)', mega_b + s5_duel['timed'])):
    flat = sum(rounds, [])
    p3 = sum(1 for v in flat if v <= 3.0) / len(flat)
    p10 = sum(1 for v in flat if v <= 10.0) / len(flat)
    n_ret = round(len(flat) / len(rounds) * 3)
    f3 = f10 = 0
    for _ in range(B):
        s = sum((random.choice(rounds) for _ in range(3)), [])
        f3 += sum(1 for v in s if v <= 3.0) < 1
        f10 += sum(1 for v in s if v <= 10.0) < 2
    print('Mega timed returns (%s): %d returns in %d rounds %s, p(<=3 s)=%.2f p(<=10 s)=%.2f; rule ">= 1 <= 3 s and >= 2 <= 10 s" (D51 6): '
          'P(fail) binomial(n=%d) %.1f %% / %.2f %%, bootstrap over rounds %.1f %% / %.2f %%' % (
              label, len(flat), len(rounds), rounds, p3, p10, n_ret, 100 * binom_lt(1, n_ret, p3), 100 * binom_lt(2, n_ret, p10),
              100 * f3 / B, 100 * f10 / B))
print('first takes after Live (s): 06b %s, art %s -> the Live reset stays uncounted (E3 b)' % (
    [r['first_after_live'] for r in b06_duel], [r['first_after_live'] for r in art_duel]))
print()

# ---------------------------------------------------------------- FFA
print('== FFA x10 (3 Medium, 120 s, seeds 1-10) ==')
fk_a = [r['kills'] for r in b06_ffa] + [r['kills'] for r in art_ffa]
fk_b = fk_a + s6_ffa['kills']
band('FFA sum kills, 10 runs (A)', fk_a, 10, 34)
K_FFA = band('FFA sum kills, 10 runs (B)', fk_b, 10, 34)
per_round('FFA kills per run (B)', fk_b, 1)
per_round('FFA kills per run >= 2? (B)', fk_b, 2)
sd_band('FFA pickups per run (B)', [r['pickups'] for r in b06_ffa] + [r['pickups'] for r in art_ffa] + s6_ffa['pickups'], 40)
print('FFA Mega takes per run: 06b %s, art %s, set 6 all 4 -> band >= 2 stays (half the cycles; a dead ledge route gives 0)' % (
    [r['mega'] for r in b06_ffa], [r['mega'] for r in art_ffa]))
print()

# ---------------------------------------------------------------- links (E3 c demand at the observed attempt counts)
print('== Link success: the E3 (c) demand (false FAIL of the 90 % invariant at the set\'s own n <= 10 %) at the observed rates ==')


def fails_needed(n):
    return min(k for k in range(0, n + 1) if (n - k) / n < 0.90)


def min_rate(n):
    f = fails_needed(n)
    return 100 * min(p for p in [i / 1000 for i in range(900, 1000)] if binom_ge(f, n, 1 - p) <= 0.10)


drop_06b_ffa = (sum(r['drop'][0] for r in b06_ffa), sum(sum(r['drop']) for r in b06_ffa))
drop_art_ffa = (sum(r['drop'][0] for r in art_ffa), sum(sum(r['drop']) for r in art_ffa))
pad_06b_ffa = (sum(r['pad'][0] for r in b06_ffa), sum(sum(r['pad']) for r in b06_ffa))
pad_art_ffa = (sum(r['pad'][0] for r in art_ffa), sum(sum(r['pad']) for r in art_ffa))
# bot sets: 06b from the stats.json link_types [ok, failed, aborted]; art from the task-15 links-by-record TOTAL lines
drop_06b_run = (sum(sum(v[0] for k, v in json.load(open(ROOT + '/0006-dm1-blockout-06b/dm1-botrun/logs/round-%d/BotRun.stats.json' % i))['link_types'].items() if k == 'Drop') for i in (1, 2, 3)),
                sum(sum(v[0] + v[1] for k, v in json.load(open(ROOT + '/0006-dm1-blockout-06b/dm1-botrun/logs/round-%d/BotRun.stats.json' % i))['link_types'].items() if k == 'Drop') for i in (1, 2, 3)))
drop_06b_duel = (sum(sum(v[0] for k, v in json.load(open(ROOT + '/0006-dm1-blockout-06b/dm1-botduel/logs/round-%d/BotDuel.stats.json' % i))['link_types'].items() if k == 'Drop') for i in (1, 2, 3)),
                 sum(sum(v[0] + v[1] for k, v in json.load(open(ROOT + '/0006-dm1-blockout-06b/dm1-botduel/logs/round-%d/BotDuel.stats.json' % i))['link_types'].items() if k == 'Drop') for i in (1, 2, 3)))
drop_art_run, drop_art_duel = (35, 36), (27, 30)          # 0006-dm1-art/<set>/links-by-record.txt TOTAL Drop
pooled_06b = (drop_06b_ffa[0] + drop_06b_run[0] + drop_06b_duel[0], drop_06b_ffa[1] + drop_06b_run[1] + drop_06b_duel[1])
pooled_art = (drop_art_ffa[0] + drop_art_run[0] + drop_art_duel[0], drop_art_ffa[1] + drop_art_run[1] + drop_art_duel[1])
pooled_all = (pooled_06b[0] + pooled_art[0], pooled_06b[1] + pooled_art[1])
p_pooled = pooled_all[0] / pooled_all[1]
for label, (ok, n) in (('FFA x10 Drop (06b)', drop_06b_ffa), ('FFA x10 Drop (art)', drop_art_ffa), ('BotRun x3 Drop (06b)', drop_06b_run),
                       ('BotRun x3 Drop (art)', drop_art_run), ('BotDuel x3 Drop (06b)', drop_06b_duel), ('BotDuel x3 Drop (art)', drop_art_duel),
                       ('pooled Drop (06b, 3 sets)', pooled_06b), ('pooled Drop (art, 3 sets)', pooled_art), ('pooled Drop (final layout, 6 sets)', pooled_all),
                       ('FFA x10 JumpPad (06b)', pad_06b_ffa), ('FFA x10 JumpPad (art)', pad_art_ffa)):
    f = fails_needed(n)
    judged = 'judged' if n >= 30 else 'n < 30: not judged'
    print('%-38s %d/%d = %.1f %%; %s; set FAILs at >= %d failed of %d; P(FAIL | true %.1f %%) = %.2f %%; lowest true rate with <= 10 %% false FAIL at n=%d: %.1f %%'
          % (label, ok, n, 100 * ok / n, judged, f, n, 100 * p_pooled, 100 * binom_ge(f, n, 1 - p_pooled), n, min_rate(n)))
print('Demand met: pooled Drop %.1f %% over %d attempts on the final layout >= the lowest acceptable rate at every set size used (n = 30: 94.1 %%).'
      % (100 * p_pooled, pooled_all[1]))
print()

# ---------------------------------------------------------------- dock N
print('== Dock N (Drop_DockW_N (-450,133,330)->(-450,550,10), Drop_DockE_N (450,133,330)->(450,550,10)) ==')
# ok / failed per set; aborted (replaced, below_start, relaunched, air_start, ineligible) is not an attempt (E0.3 b, 06b)
dock = {
    '06b FFA x10': (36, 4),      # dm1-ffa/links-by-record.txt: W 22/3, E 14/1
    '06b BotRun x3': (12 + 9 + 0, 0),   # pool_links.py over the three server logs (W 7+4+1, E 5+5+3 = 25 ok? see note) -- replaced below
    '06b BotDuel x3': (0, 0),
    'art FFA x10': (41, 5),      # ffa/links-by-record.txt: W 17/3, E 24/2
    'art BotRun x3': (7, 1),     # botrun/links-by-record.txt: W 3/0, E 4/1
    'art BotDuel x3': (10, 3),   # botduel/links-by-record.txt: W 2/0, E 8/3
}
# the 06b bot sets have no links-by-record.txt; tally the committed server logs directly
pat = re.compile(r'VeloxBotLink bot=\S+ type=Drop from=V\(X=(-?450)\.00, Y=133\.00, Z=330\.00\) to=V\(X=-?450\.00, Y=550\.00, Z=10\.00\) result=(\S+) reason=(\S+)')
FAILED = {'missed', 'timeout', 'noimpulse', 'tooslow'}
for key, fmt, script in (('06b BotRun x3', ROOT + '/0006-dm1-blockout-06b/dm1-botrun/logs/round-%d/%s-server.log.gz', 'BotRun'),
                         ('06b BotDuel x3', ROOT + '/0006-dm1-blockout-06b/dm1-botduel/logs/round-%d/%s-server.log.gz', 'BotDuel')):
    ok = failed = 0
    for i in (1, 2, 3):
        with gzip.open(fmt % (i, script), 'rt', errors='replace') as fh:
            for line in fh:
                m = pat.search(line)
                if not m:
                    continue
                if m.group(2) == 'ok':
                    ok += 1
                elif m.group(3) in FAILED:
                    failed += 1
    dock[key] = (ok, failed)
for k, (ok, f) in dock.items():
    print('  %-16s dock N ok/failed %d/%d = %.1f %%' % (k, ok, f, 100 * ok / (ok + f) if ok + f else float('nan')))
o06 = sum(dock[k][0] for k in dock if k.startswith('06b')); f06 = sum(dock[k][1] for k in dock if k.startswith('06b'))
oart = sum(dock[k][0] for k in dock if k.startswith('art')); fart = sum(dock[k][1] for k in dock if k.startswith('art'))
lo, hi = wilson(o06 + oart, o06 + f06 + oart + fart)
print('06b all sets %d/%d = %.1f %%; art all sets %d/%d = %.1f %%; Fisher exact two-sided p = %.2f (the two rounds do not differ beyond noise)' % (
    o06, o06 + f06, 100 * o06 / (o06 + f06), oart, oart + fart, 100 * oart / (oart + fart), fisher_two_sided(o06, f06, oart, fart)))
pooled_ok, pooled_n = o06 + oart, o06 + f06 + oart + fart
p_dock = pooled_ok / pooled_n
print('pooled dock N on the final layout: %d/%d = %.1f %% (Wilson 95 %%: %.1f-%.1f %%); dock N carries %d of the %d Drop misses on %d of %d Drop attempts' % (
    pooled_ok, pooled_n, 100 * p_dock, lo, hi, f06 + fart, pooled_all[1] - pooled_all[0], pooled_n, pooled_all[1]))
others_ok = pooled_all[0] - pooled_ok
others_n = pooled_all[1] - pooled_n
p_others = others_ok / others_n
print('the other nine DROP records: %d/%d = %.1f %%' % (others_ok, others_n, 100 * p_others))
share = (drop_06b_ffa[1] and (44 / drop_06b_ffa[1] + 46 / drop_art_ffa[1]) / 2)
print('dock N share of FFA Drop attempts: 06b 44/%d, art 46/%d (mean %.0f %%)' % (drop_06b_ffa[1], drop_art_ffa[1], 100 * share))
print('FFA x10 Drop rate implied by a dock N true rate q with the other records at %.1f %%: ' % (100 * p_others) +
      ', '.join('q=%d %% -> %.1f %%' % (100 * q, 100 * (share * q + (1 - share) * p_others)) for q in (0.90, 0.87, 0.85, 0.82, 0.80, 0.75)) +
      '  (E3 c layout trigger: FFA Drop < 93.1 %)')
print('Watch thresholds, false-trigger probability with dock N unchanged at %.1f %%:' % (100 * p_dock))
for n in (46, 67, 132):
    for t in (0.85, 0.87, 0.80):
        print('  n=%3d, trigger "< %d %%" (<= %d ok): P = %.1f %%' % (n, 100 * t, math.ceil(t * n) - 1, 100 * binom_le(math.ceil(t * n) - 1, n, p_dock)), end='')
    print()
print('Detection with a dock N true rate of 80 %% (a real regression): n=67 "< 85 %%": %.0f %%; n=132 "< 85 %%": %.0f %%' % (
    100 * binom_le(math.ceil(0.85 * 67) - 1, 67, 0.80), 100 * binom_le(math.ceil(0.85 * 132) - 1, 132, 0.80)))
print('BotDuel x3 Drop at n = 30 (art: 27/30, three misses, all DockE_N): the invariant allows 3 failed of 30; P(>= 4 failed | pooled Drop %.1f %%) = %.1f %%, '
      'P(>= 4 | 94.1 %%) = %.1f %% -- a small bot set sits at the invariant\'s n floor by construction' % (
          100 * p_pooled, 100 * binom_ge(4, 30, 1 - p_pooled), 100 * binom_ge(4, 30, 0.059)))
print()
print('== Proposed BAND values (basis B) ==')
print('BAND_BOTRUN_KILLS_MIN=%d (E3 11); BAND_BOTDUEL_FRAGS_MIN=%d (E3 7); BAND_BOTDUEL_CAMP_MIN=%d (E3 21); BAND_FFA_KILLS_MIN=%d (E3 34)' % (K_BOTRUN, K_FRAGS, K_CAMP, K_FFA))
