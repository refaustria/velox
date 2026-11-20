#!/usr/bin/env python3
"""ADR 0008 G1/G2 (task 22, B6): per-map soak bands of the five M7 blockouts by the ADR 0006 E3 / D51 (1) method.

Lower band k (sum over a set, or the per-run / per-round minimum over a set): the largest k with P(false FAIL | unchanged bots and
layout) <= 2.5 %, under Poisson (rate = observed mean) and under a bootstrap over the observed runs / rounds; the smaller k (D51 1).
A per-run / per-round minimum is judged on every run of the set, so its P(false FAIL) is the family-wise 1 - (1 - p)^n.
Mega per run / per round is a capped count (one take per 35 s cycle), not Poisson: Binomial(cycles, p) with p at its Wilson 95 % lower
limit over the pooled takes. Timed Mega returns (BotDuel): Binomial(N, p) with N the smallest observed number of timed returns per set.
Upper band K (G2, hazard_death_self only): the smallest K with P(false FAIL) <= 2.5 % under Poisson with the rate at its one-sided 95 %
upper confidence limit (Garwood; the observed counts are mostly 0, a plug-in rate would give a degenerate band) and under a bootstrap;
the larger K. Detection power is printed against the broken-edge sets (Orbita B5 / H12 before the edge fixes).

Basis A = final layout and current bots (H15 1fe0997 and later); basis B = A plus older-bot or items-only-layout sets (D40 caveat, as
E4 basis B). The band is the more lenient of A and B. A band needs a basis A (G1 b); a map or script without one gets candidate values
only (printed, not set). Per-run / per-round figures are typed in from the raw .log.json / nettest output under
/mnt/oldroot/velox-scratch/ (paths per set below; the committed summaries in ../0008-*-blockout/ carry the set lines).
Run: python3 -I b6_bands.py > b6_bands.out"""
import math
import random

random.seed(6)
B = 200000
ALPHA = 0.025


def lpois(i, lam):
    return -lam + i * math.log(lam) - math.lgamma(i + 1) if lam > 0 else (0.0 if i == 0 else -math.inf)


def pois_lt(k, lam):
    return sum(math.exp(lpois(i, lam)) for i in range(0, k))


def pois_gt(k, lam):
    return max(0.0, 1.0 - pois_lt(k + 1, lam))


def binom_lt(k, n, p):
    return sum(math.comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(0, k))


def wilson_lo(x, n, z=1.6449):
    if n == 0:
        return 0.0
    ph = x / n
    d = 1 + z * z / n
    c = ph + z * z / (2 * n)
    return (c - z * math.sqrt(ph * (1 - ph) / n + z * z / (4 * n * n))) / d


def garwood_hi(x, conf=0.95):
    """One-sided upper limit of a Poisson mean for x observed events: P(X <= x | lam) = 1 - conf."""
    lo, hi = 0.0, 10.0 * (x + 3)
    for _ in range(200):
        mid = (lo + hi) / 2
        if pois_lt(x + 1, mid) > 1 - conf:
            lo = mid
        else:
            hi = mid
    return hi


def family(p, n):
    return 1 - (1 - p) ** n


def boot_sums(vals, n):
    return [sum(random.choice(vals) for _ in range(n)) for _ in range(B)]


def lower_sum(vals, n):
    lam = n * sum(vals) / len(vals)
    kp = max(k for k in range(0, int(lam) + 2) if pois_lt(k, lam) <= ALPHA)
    s = sorted(boot_sums(vals, n))
    kb = max(k for k in range(0, int(lam) + 2) if sum(1 for x in s[:int(ALPHA * B) + 2] if x < k) / B <= ALPHA)
    k = min(kp, kb)
    return k, pois_lt(k, lam), lam


def lower_min(vals, n):
    """Per-run (per-round) minimum judged on each of n runs (rounds)."""
    lam = sum(vals) / len(vals)
    kp = max(k for k in range(0, int(lam) + 2) if family(pois_lt(k, lam), n) <= ALPHA)
    kb = max(k for k in range(0, int(lam) + 2) if family(sum(1 for v in vals if v < k) / len(vals), n) <= ALPHA)
    k = min(kp, kb)
    return k, family(pois_lt(k, lam), n), lam


def mega_min(vals, cycles, n):
    p = wilson_lo(sum(vals), cycles * len(vals))
    k = max(k for k in range(0, cycles + 1) if family(binom_lt(k, cycles, p), n) <= ALPHA)
    return k, family(binom_lt(k, cycles, p), n), p


def returns_min(n_ok, n_all, n_set):
    p = wilson_lo(n_ok, n_all)
    k = max(k for k in range(0, n_set + 1) if binom_lt(k, n_set, p) <= ALPHA)
    return k, binom_lt(k, n_set, p), p


def upper_sum(vals, n):
    x = sum(vals)
    lam_hi = garwood_hi(x) / len(vals) * n
    kp = min(k for k in range(0, 1000) if pois_gt(k, lam_hi) <= ALPHA)
    s = boot_sums(vals, n)
    kb = min(k for k in range(0, 1000) if sum(1 for v in s if v > k) / B <= ALPHA)
    k = max(kp, kb)
    return k, pois_gt(k, lam_hi), lam_hi, kp, kb


def upper_round(vals, n):
    """Per-round maximum judged on each of n rounds."""
    lam_hi = garwood_hi(sum(vals)) / len(vals)
    kp = min(k for k in range(0, 1000) if family(pois_gt(k, lam_hi), n) <= ALPHA)
    kb = min(k for k in range(0, 1000) if family(sum(1 for v in vals if v > k) / len(vals), n) <= ALPHA)
    k = max(kp, kb)
    return k, family(pois_gt(k, lam_hi), n), lam_hi


def line(name, basis, res, extra=''):
    k, p, ref = res[0], res[1], res[2]
    print(f'  {name:<26} {basis:<4} band {k:>4}   P(false FAIL) {100 * p:5.2f} %   ref {ref:8.3f} {extra}')


def ffa_block(title, A, B_, nruns=10, cycles=4):
    """A / B_: dicts kills, pickups, mega (lists over runs)."""
    print(title)
    for tag, d in (('A', A), ('B', B_)):
        if d is None:
            continue
        print(f'   basis {tag}: {len(d["kills"])} runs, kills mean {sum(d["kills"]) / len(d["kills"]):.2f}, '
              f'pickups {min(d["pickups"])}-{max(d["pickups"])}, mega {min(d["mega"])}-{max(d["mega"])}')
        line('FFA_KILLS_MIN (sum/10)', tag, lower_sum(d['kills'], nruns))
        line('FFA_RUN_KILLS_MIN', tag, lower_min(d['kills'], nruns))
        line('FFA_RUN_PICKUPS_MIN', tag, lower_min(d['pickups'], nruns))
        line('FFA_RUN_MEGA_MIN', tag, mega_min(d['mega'], cycles, nruns))


def cat(*ds):
    return {k: sum((d[k] for d in ds), []) for k in ds[0]}


def S(kills, pickups, mega, self_=None):
    return {'kills': kills, 'pickups': pickups, 'mega': mega, 'self': self_ or [0] * len(kills)}


# ================================================================ data (raw logs under /mnt/oldroot/velox-scratch/)
# Sacrarium FFA x10 at 6 bots
SAC6_FINAL = S([24, 21, 26, 27, 23, 20, 27, 22, 25, 21], [115, 117, 122, 105, 112, 115, 120, 113, 109, 122], [4] * 10)  # 31-sacrarium-route/ffa10c (b35c294)
SAC6_PRE31 = S([25, 21, 23, 23, 25, 24, 26, 19, 27, 27], [117, 116, 118, 114, 119, 118, 116, 112, 116, 116], [4] * 10)  # 31/ffa10a (pre-31 layout, H15)
SAC6_H15V2 = S([22, 24, 22, 21, 30, 24, 21, 25, 22, 24], [118, 116, 111, 105, 108, 116, 120, 116, 108, 114], [4] * 10)  # m7-h15/v2/sac
SAC6_H10 = S([19, 25, 25, 26, 23, 23, 24, 22, 24, 19], [107, 106, 112, 110, 112, 109, 119, 118, 113, 107], [4] * 10)    # m7-h10/ffa6 (H10 bots)
SAC3_H10 = S([5, 8, 3, 7, 9, 3, 7, 6, 2, 5], [59, 64, 62, 63, 75, 60, 65, 62, 76, 67], [4] * 10)                         # m7-h10/ffa3 (H10 bots)

# Nemus (final layout df83346; H13 bots, no H14/H15 set exists)
NEM3_B = S([11, 10, 7, 6, 7, 7, 7, 8, 5, 6], [53, 68, 64, 59, 56, 67, 58, 50, 55, 68], [2, 3, 4, 4, 3, 3, 3, 3, 3, 4])   # m7-nemus-b2b/ffa3b
NEM3_C = S([10, 6, 6, 8, 8, 4, 7, 5, 5, 7], [63, 49, 61, 61, 59, 46, 56, 63, 59, 68], [3, 3, 3, 4, 2, 2, 3, 3, 3, 3])    # m7-nemus-b2b/ffa3c
NEM6_B = S([22, 22, 24, 25, 23, 21, 23, 18, 29, 26], [106, 112, 117, 114, 110, 110, 102, 113, 106, 110], [4, 4, 3, 3, 4, 4, 4, 3, 4, 3])  # m7-nemus-b2b/ffa6b
NEM_BOTRUN = {'kills': [6, 8, 10], 'pickups': [54, 52, 50], 'mega': [3, 3, 1], 'links': [14, 20, 21]}   # ../0008-nemus-blockout/botrun-set.txt

# Culmen (final layout 111166b = run3; run2 = kiosk layout, pre-19b items: items-only difference) — H15 bots both
CUL3_R3 = S([7, 8, 5, 6, 4, 4, 7, 6, 4, 6], [70, 68, 71, 66, 65, 66, 62, 69, 66, 64], [4, 3, 4, 4, 4, 4, 4, 4, 4, 4])     # m7-culmen/run3/s1/ffa3
CUL3_R2 = S([8, 6, 6, 6, 4, 5, 6, 8, 5, 4], [66, 61, 66, 58, 76, 63, 65, 58, 70, 64], [4, 4, 4, 3, 3, 3, 4, 3, 4, 4])     # m7-culmen/run2/s1/ffa3
CUL6_R3 = S([19, 21, 16, 25, 24, 22, 24, 19, 21, 22], [98, 110, 109, 107, 105, 113, 116, 111, 117, 111], [4, 4, 4, 4, 4, 4, 3, 4, 4, 4])  # run3/s1/ffa6
CUL6_R2 = S([21, 22, 27, 15, 19, 20, 21, 18, 22, 22], [105, 113, 108, 112, 120, 106, 118, 110, 109, 115], [4] * 10)       # run2/s1/ffa6
CUL8_R3 = S([41, 36, 32, 41, 35, 30, 30, 34, 43, 33], [141, 138, 135, 145, 132, 145, 119, 138, 141, 136], [4, 4, 4, 3, 4, 4, 4, 4, 4, 4])  # run3/s1/ffa8
CUL8_R2 = S([34, 39, 30, 36, 33, 36, 34, 38, 32, 38], [141, 141, 138, 132, 137, 139, 145, 132, 132, 144], [4] * 10)       # run2/s1/ffa8
CUL_BOTRUN_R3 = {'kills': [6, 7, 5], 'pickups': [66, 54, 61], 'mega': [3, 4, 3], 'links': [18, 25, 26]}   # run3/s1/botrun.out
CUL_BOTRUN_R2 = {'kills': [8, 7, 8], 'pickups': [49, 49, 67], 'mega': [3, 3, 3], 'links': [26, 18, 22]}   # run2/s1/botrun.out

# Fornax FFA x10 at 3 (final layout d19266d = 38 ffa-b; ffa-a = one shard elsewhere; v2/fix = pre-38 items) — H15 bots
FOR3_FINAL = S([11, 18, 15, 15, 14, 16, 14, 14, 13, 11], [89, 90, 90, 75, 90, 83, 87, 81, 77, 89], [4] * 10)             # 38-fornax-items/ffa-b
FOR3_38A = S([18, 16, 15, 14, 18, 13, 11, 14, 16, 15], [83, 83, 82, 84, 88, 76, 81, 78, 90, 80], [4] * 10)               # 38-fornax-items/ffa-a
FOR3_V2 = S([18, 16, 13, 13, 16, 18, 16, 19, 12, 16], [68, 80, 75, 79, 80, 82, 81, 76, 78, 72], [4] * 10)                # m7-h15/v2/for
FOR3_FIX = S([15, 14, 19, 10, 15, 20, 14, 14, 13, 20], [72, 83, 77, 79, 80, 82, 83, 76, 77, 71], [4] * 10)               # m7-h15/fix/for
FOR_SELF_OLD = [0] * 10 + [0, 0, 0, 0, 0, 0, 1, 0, 0, 0] + [0, 0, 0, 0, 0, 1, 1, 0, 0, 0] + [0, 0, 0, 1, 0, 0, 0, 0, 0, 0]  # h14 base, h14 fix, fornax s2, s3
# BotDuel x3: m7-38 (H15, final) ; B4 + b4b + h9 fornaxduel2 (pre-H15)
FOR_DUEL_A = {'frags': [12, 11, 16], 'camp': [6, 8, 6], 'ret3': 6, 'ret10': 7, 'ret_n': 7, 'self': [0, 0, 0]}
FOR_DUEL_OLD = {'frags': [10, 11, 12, 12, 9, 11, 9], 'camp': [7, 7, 12, 4], 'ret3': 5 + 3, 'ret10': 6 + 3, 'ret_n': 6 + 3, 'self': [0, 0, 1, 0, 0, 0, 0]}

# Orbita FFA x10 at 4 (final layout d52e0a3 + H12 nav tile). v2 = final H15 bots; fix = H15 iteration; H13 sets = older bots
ORB4_V2 = S([15, 16, 12, 16, 11, 15, 12, 11, 18, 14], [80, 85, 80, 77, 91, 85, 80, 89, 79, 85], [4] * 10, [0, 0, 3, 0, 0, 2, 0, 1, 0, 0])
ORB4_FIX = S([13, 19, 17, 16, 15, 12, 15, 12, 14, 19], [78, 86, 86, 86, 85, 77, 89, 81, 81, 82], [4] * 10, [0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
ORB4_H13 = S([17, 16, 17, 12, 11, 13, 20, 13, 18, 15] + [15, 14, 16, 12, 12, 12, 14, 16, 16, 14] + [10, 13, 14, 15, 18, 13, 10, 11, 11, 14]
             + [11, 11, 19, 15, 19, 17, 14, 12, 17, 13],
             [88, 86, 84, 90, 83, 87, 88, 78, 83, 85] + [87, 79, 80, 77, 85, 82, 81, 79, 74, 79] + [84, 91, 76, 82, 76, 79, 85, 86, 92, 90]
             + [83, 73, 83, 81, 75, 87, 73, 82, 83, 92],
             [4] * 10 + [4] * 10 + [4, 4, 4, 3, 4, 4, 4, 4, 4, 4] + [4] * 10,
             [0, 0, 1, 1, 0, 2, 0, 1, 0, 0] + [0, 0, 0, 1, 1, 0, 0, 0, 0, 1] + [1, 1, 0, 1, 1, 1, 2, 0, 1, 0] + [0, 0, 0, 0, 0, 1, 0, 0, 0, 1])  # m7-h13/{ffa4,set1,set2,set3}
ORB_SELF_BROKEN = {'B5 FFA (m7-orbita/s1)': [17, 16, 20, 21, 21, 14, 18, 16, 20, 19], 'H12 FFA (m7-h12/ffa4)': [14, 9, 17, 11, 9, 13, 14, 17, 11, 13]}
ORB_DUEL_H13 = {'frags': [4, 3, 3], 'camp': [14, 10, 11], 'ret3': 8, 'ret10': 8, 'ret_n': 8, 'self': [1, 0, 0]}   # botduel-set-b5b.txt
ORB_DUEL_BROKEN_SELF = [10, 19, 22]                                                                                  # botduel-set.txt (B5)


def botrun_block(title, A, B_):
    print(title)
    for tag, d in (('A', A), ('B', B_)):
        if d is None:
            continue
        print(f'   basis {tag}: {len(d["kills"])} rounds, kills {d["kills"]}, pickups {d["pickups"]}, mega {d["mega"]}, links {d["links"]}')
        line('ROUND_KILLS_MIN', tag, lower_min(d['kills'], 3))
        line('BOTRUN_PICKUPS_MIN', tag, lower_min(d['pickups'], 3))
        line('BOTRUN_LINK_OK_MIN', tag, lower_min(d['links'], 3))
        line('BOTRUN_MEGA_REQUIRED (>=1)', tag, mega_min(d['mega'], 4, 3), '(binomial, up to 4 takes per round observed)')
        line('BOTRUN_KILLS_MIN (sum/3)', tag, lower_sum(d['kills'], 3))


def duel_block(title, A, B_):
    print(title)
    for tag, d in (('A', A), ('B', B_)):
        if d is None:
            continue
        print(f'   basis {tag}: frags {d["frags"]}, camp {d["camp"]}, returns <=3 s {d["ret3"]}/{d["ret_n"]}, <=10 s {d["ret10"]}/{d["ret_n"]}')
        line('ROUND_KILLS_MIN (frags)', tag, lower_min(d['frags'], 3))
        line('BOTDUEL_FRAGS_MIN (sum/3)', tag, lower_sum(d['frags'], 3))
        line('BOTDUEL_CAMP_MIN (sum/3)', tag, lower_sum(d['camp'], 3))
        nset = 6
        line('MEGA_WITHIN3_MIN', tag, returns_min(d['ret3'], d['ret_n'], nset), f'(N = {nset} timed returns per set)')
        line('MEGA_WITHIN10_MIN', tag, returns_min(d['ret10'], d['ret_n'], nset), f'(N = {nset})')


def hazard_set(title, vals_by_basis, broken=None):
    print(title)
    for tag, v in vals_by_basis:
        k, p, lam, kp, kb = upper_sum(v, 10)
        print(f'  HAZARD_SELF_SET_MAX        {tag:<4} band {k:>4}   P(false FAIL) {100 * p:5.2f} %   rate {sum(v)}/{len(v)} runs, '
              f'upper-95 set mean {lam:.2f} (Poisson K {kp}, bootstrap K {kb})')
        for name, bv in (broken or {}).items():
            print(f'      power vs {name}: set sum {sum(bv)} > {k}: caught')
        for r in (1.0, 2.0):
            print(f'      power vs a mild breakage of {r:.0f} self death(s) per run: P(caught) {100 * pois_gt(k, 10 * r):.1f} %')


def hazard_round(title, vals_by_basis, broken=None):
    print(title)
    for tag, v in vals_by_basis:
        k, p, lam = upper_round(v, 3)
        print(f'  HAZARD_SELF_ROUND_MAX      {tag:<4} band {k:>4}   P(false FAIL) {100 * p:5.2f} % over 3 rounds   rate {sum(v)}/{len(v)} rounds, upper-95 {lam:.2f}')
        if broken:
            print(f'      power vs B5 BotDuel rounds {broken}: {sum(1 for x in broken if x > k)}/{len(broken)} rounds over the band')


print('ADR 0008 G1/G2 band derivation (task 22, B6). P(false FAIL) is under the Poisson / binomial model at the chosen band.\n')

ffa_block('== Sacrarium FFA x10 at 6 bots (A = final layout b35c294, H15; B = A + pre-31 layout H15 x2)', SAC6_FINAL, cat(SAC6_FINAL, SAC6_PRE31, SAC6_H15V2))
ffa_block('   (no basis A at 3 bots; candidate from H10 bots only)', None, SAC3_H10)
print('   Sacrarium BotRun: no basis A (b1b/b1c/h9 are H9-H10 bots on older layouts) -> report-only\n')

ffa_block('== Nemus FFA x10 at 3 (no basis A: H13 bots only; candidates)', None, cat(NEM3_B, NEM3_C))
ffa_block('== Nemus FFA x10 at 6 (candidates)', None, NEM6_B)
botrun_block('== Nemus BotRun x3 (candidates, H13)', None, NEM_BOTRUN)
print()

ffa_block('== Culmen FFA x10 at 3 (A = run3 final 111166b; B = A + run2 items-only)', CUL3_R3, cat(CUL3_R3, CUL3_R2))
ffa_block('== Culmen FFA x10 at 6', CUL6_R3, cat(CUL6_R3, CUL6_R2))
ffa_block('== Culmen FFA x10 at 8 (ADR 0008 5.3: report-only; printed for the record)', CUL8_R3, cat(CUL8_R3, CUL8_R2))
botrun_block('== Culmen BotRun x3', CUL_BOTRUN_R3, cat(CUL_BOTRUN_R3, CUL_BOTRUN_R2))
print()

ffa_block('== Fornax FFA x10 at 3 (A = 38 ffa-b final d19266d; B = A + 38 ffa-a + H15 v2 + fix, pre-38 items)', FOR3_FINAL,
          cat(FOR3_FINAL, FOR3_38A, FOR3_V2, FOR3_FIX))
duel_block('== Fornax BotDuel x3 (A = m7-38 H15 final; B = A + B4, b4b, h9 pre-H15)', FOR_DUEL_A,
           {'frags': FOR_DUEL_A['frags'] + FOR_DUEL_OLD['frags'], 'camp': FOR_DUEL_A['camp'] + FOR_DUEL_OLD['camp'],
            'ret3': FOR_DUEL_A['ret3'] + FOR_DUEL_OLD['ret3'], 'ret10': FOR_DUEL_A['ret10'] + FOR_DUEL_OLD['ret10'],
            'ret_n': FOR_DUEL_A['ret_n'] + FOR_DUEL_OLD['ret_n']})
print('   Fornax BotRun: no basis A (B4 set, pre-H13, FAILs on the BotWatch park world-kill artefact) -> report-only')
hazard_set('== Fornax hazard_death_self, FFA x10 at 3',
           [('A', FOR3_FINAL['self'] + FOR3_38A['self'] + FOR3_V2['self'] + FOR3_FIX['self']),
            ('B', FOR3_FINAL['self'] + FOR3_38A['self'] + FOR3_V2['self'] + FOR3_FIX['self'] + FOR_SELF_OLD)],
           ORB_SELF_BROKEN)
hazard_round('== Fornax hazard_death_self per BotDuel round', [('A', FOR_DUEL_A['self']), ('B', FOR_DUEL_A['self'] + FOR_DUEL_OLD['self'])],
             ORB_DUEL_BROKEN_SELF)
print()

ffa_block('== Orbita FFA x10 at 4 (A = H15 v2 final; B = A + H15 fix + H13 x4)', ORB4_V2, cat(ORB4_V2, ORB4_FIX, ORB4_H13))
duel_block('== Orbita BotDuel x3 (no basis A: H13 b5b only; candidates)', None, ORB_DUEL_H13)
hazard_set('== Orbita hazard_death_self, FFA x10 at 4',
           [('A', ORB4_V2['self'] + ORB4_FIX['self']), ('B', ORB4_V2['self'] + ORB4_FIX['self'] + ORB4_H13['self'])], ORB_SELF_BROKEN)
hazard_round('== Orbita hazard_death_self per BotDuel round (H13 only)', [('B', ORB_DUEL_H13['self'])], ORB_DUEL_BROKEN_SELF)
