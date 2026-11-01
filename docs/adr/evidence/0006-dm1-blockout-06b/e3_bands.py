#!/usr/bin/env python3
"""ADR 0006 E3: DM1 set bands by the D51 (1) method. Per-round figures are typed in from the committed evidence
(06b: `dm1-botrun`, `dm1-botduel`, `dm1-ffa` `.stats.json` / `.log.json`, quiet host; task 06 sets 6 and 5 from
`../0006-dm1-blockout/<set>/run.out.gz`, contended host: behaviour figures valid under D40, no cost). For a lower band k on a
set sum: largest k with P(sum < k | unchanged bots and layout) <= 2.5 % under Poisson(n x per-round mean) and under a bootstrap
of n rounds drawn with replacement from the observed rounds; the smaller of the two is proposed (D51 1). Run: python3 -I e3_bands.py"""
import math, random, statistics as st

random.seed(6)
B = 200000


def pois_cdf_lt(k, lam):
    """P(X < k) for X ~ Poisson(lam)."""
    return sum(math.exp(-lam) * lam ** i / math.factorial(i) for i in range(0, k))


def binom_lt(k, n, p):
    return sum(math.comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(0, k))


def binom_ge(k, n, p):
    return sum(math.comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(k, n + 1))


def band(name, rounds, n, extra_rounds=None, floor=1):
    """rounds: per-round counts of the basis; n: rounds per set; extra: supplementary rounds (printed, not in the basis)."""
    mean = st.mean(rounds)
    lam = n * mean
    k_p = max(k for k in range(0, int(lam) + 1) if pois_cdf_lt(k, lam) <= 0.025) if lam > 0 else 0
    sums = sorted(sum(random.choice(rounds) for _ in range(n)) for _ in range(B))
    k_b = max((k for k in range(0, int(max(sums)) + 1) if sum(1 for s in sums if s < k) / B <= 0.025), default=0)
    k = max(floor, min(k_p, k_b))
    p_fail_pois = pois_cdf_lt(k, lam)
    p_fail_boot = sum(1 for s in sums if s < k) / B
    p_half = pois_cdf_lt(k, lam / 2)
    p_30 = pois_cdf_lt(k, lam * 0.7)
    obs_sets = [sum(rounds[i:i + n]) for i in range(0, len(rounds) - n + 1, n)]
    extra = ""
    if extra_rounds:
        extra = "; supplementary rounds %s (sets %s)" % (extra_rounds, [sum(extra_rounds[i:i + n]) for i in range(0, len(extra_rounds) - n + 1, n)])
    print("%-28s rounds=%s mean/round=%.2f set mean=%.1f | Poisson k=%d | bootstrap k=%d | band k=%d: P(fail|unchanged) Poisson %.2f %% bootstrap %.2f %%; "
          "P(fail|-30 %%) %.0f %%; P(fail|halved) %.0f %%; observed sets %s%s" % (
              name, rounds, mean, lam, k_p, k_b, k, 100 * p_fail_pois, 100 * p_fail_boot, 100 * p_30, 100 * p_half, obs_sets, extra))
    return k


def per_round(name, rounds, k):
    mean = st.mean(rounds)
    p0 = pois_cdf_lt(k, mean)
    print("%-28s rounds=%s mean=%.2f per-round band >= %d: P(round < %d | unchanged) = %.2f %% per round (D36: outlier cap ~0.5 %%, a band tighter than the 10 %% tail is not a band)" % (
        name, rounds, mean, k, k, 100 * p0))


print("== BotRun x3 (FFA, 3 Medium, 90 s), L_DM1_Blockout ==")
botrun_kills_06b = [6, 8, 5]            # 06b dm1-botrun (quiet host)
botrun_kills_06 = [4, 4, 11]            # task 06 set 6 (final layout, contended host, pre-06b bots)
botrun_kills_05 = [6, 7, 7]             # task 06 set 5 (layout minus the Quad/shard commit)
band("BotRun sum kills", botrun_kills_06b + botrun_kills_06, 3, extra_rounds=botrun_kills_05)
per_round("BotRun kills per round", botrun_kills_06b + botrun_kills_06, 1)
botrun_pick = [64, 69, 56, 58, 56, 72]  # 06b + set 6; set 5: 64 59 65
print("BotRun pickups per round: %s mean %.1f sd %.1f min %d (set 5: 64 59 65) -> band >= 30 is %.1f sd below the mean (pickups are not Poisson: 35 items, 90 s; "
      "the band guards a dead router, not the rate)" % (botrun_pick, st.mean(botrun_pick), st.pstdev(botrun_pick), min(botrun_pick), (st.mean(botrun_pick) - 30) / st.pstdev(botrun_pick)))
botrun_links = [23, 22, 17, 28, 30, 13]  # VeloxBotLink result=ok per round, 06b + set 6; set 5: 24 13 16
per_round("BotRun links ok per round", botrun_links, 5)
print("BotRun Mega takes per round: 06b [4, 3, 4] (35 s respawn, 90 s: <= 4 incl. the Live reset) -> BAND_BOTRUN_MEGA_REQUIRED=1 (presence)")
print()
print("== BotDuel x3 (Duel, 2 Hard, 120 s) ==")
duel_frags_06b = [7, 3, 4]
duel_frags_06 = [4, 6, 4]
duel_frags_05 = [2, 6, 4]
band("BotDuel sum frags", duel_frags_06b + duel_frags_06, 3, extra_rounds=duel_frags_05)
per_round("BotDuel frags per round", duel_frags_06b + duel_frags_06, 1)
camp_06b = [11, 12, 7]
camp_06 = [10, 8, 14]
camp_05 = [11, 6, 7]
band("BotDuel sum camp episodes", camp_06b + camp_06, 3, extra_rounds=camp_05)
mega_06b = [[3.1, 1.5], [2.4, 4.5], [3.5, 0.6]]
mega_06 = [[6.4, 7.1], [2.8, 1.4], [18.9, 3.3]]
mega_05 = [[0.8, 0.0, 0.5], [4.2, 0.1], [1.3, 5.4]]
for label, rounds in (("06b only", mega_06b), ("06b + set 6", mega_06b + mega_06), ("06b + sets 6 + 5", mega_06b + mega_06 + mega_05)):
    flat = sum(rounds, [])
    p3 = sum(1 for v in flat if v <= 3.0) / len(flat)
    p10 = sum(1 for v in flat if v <= 10.0) / len(flat)
    n_ret = round(len(flat) / len(rounds) * 3)
    f3 = f10 = 0
    for _ in range(B):
        s = sum((random.choice(rounds) for _ in range(3)), [])
        f3 += sum(1 for v in s if v <= 3.0) < 1
        f10 += sum(1 for v in s if v <= 10.0) < 2
    print("Mega timed returns (%s): %d returns in %d rounds, p(<=3 s)=%.2f p(<=10 s)=%.2f; rule '>= 1 <= 3 s and >= 2 <= 10 s' (D51 6): "
          "P(fail) binomial(n=%d) %.1f %% / %.2f %%, bootstrap over rounds %.1f %% / %.2f %%" % (
              label, len(flat), len(rounds), p3, p10, n_ret, 100 * binom_lt(1, n_ret, p3), 100 * binom_lt(2, n_ret, p10), 100 * f3 / B, 100 * f10 / B))
print("RA returns (report-only): 06b BotRun [0.0,6.3] [3.6,1.3] [12.6,1.3]; 06b BotDuel [4.0,8.6,0.0] [1.6,6.7,5.0] [0.2,2.4,4.1]; set 6 BotDuel [0.8,0.2,1.0] [0.0,2.3,1.8] [0.0,9.3,0.5,0.9]")
print()
print("== FFA x10 (3 Medium, 120 s, seeds 1-10) ==")
ffa_06b = [4, 7, 3, 5, 4, 3, 6, 5, 7, 3]
ffa_06 = [3, 4, 5, 5, 1, 3, 6, 7, 6, 6]
band("FFA sum kills (10 runs)", ffa_06b + ffa_06, 10)
per_round("FFA kills per run", ffa_06b + ffa_06, 1)
per_round("FFA kills per run (>= 2?)", ffa_06b + ffa_06, 2)
ffa_pick_06b = [71, 71, 76, 78, 77, 82, 65, 68, 74, 73]
ffa_pick_06 = [79, 69, 80, 62, 55, 65, 65, 86, 81, 74]
fp = ffa_pick_06b + ffa_pick_06
print("FFA pickups per run: 06b %s, set 6 %s: mean %.1f sd %.1f min %d -> band >= 40 is %.1f sd below the mean" % (
    ffa_pick_06b, ffa_pick_06, st.mean(fp), st.pstdev(fp), min(fp), (st.mean(fp) - 40) / st.pstdev(fp)))
print("FFA Mega takes per run: 4 in all 20 runs (06b, set 6; 35 s respawn, 120 s: 4 = every cycle) -> band >= 2 (half the cycles; a dead ledge route gives 0)")
print()
print("== Link success: false-FAIL probability of the 90 % invariant at the observed rates (E0.3 b) ==")
for label, ok, n in (("FFA x10 Drop (06b)", 131, 139), ("BotRun x3 Drop (06b)", 43, 43), ("BotDuel x3 Drop (06b)", 30, 31),
                     ("pooled DM1 Drop (06b, 3 sets)", 204, 213), ("FFA x10 JumpPad (06b)", 92, 92)):
    fails_needed = min(k for k in range(0, n + 1) if (n - k) / n < 0.90)
    for p_assumed in (0.942, 0.958):
        print("%-30s %d/%d = %.1f %%; set FAILs at >= %d failed of %d; at a true rate of %.1f %% P(FAIL) = %.2f %%" % (
            label, ok, n, 100 * ok / n, fails_needed, n, 100 * p_assumed, 100 * binom_ge(fails_needed, n, 1 - p_assumed)))


def min_rate(n):
    fails_needed = min(k for k in range(0, n + 1) if (n - k) / n < 0.90)
    return 100 * min(p for p in [i / 1000 for i in range(900, 1000)] if binom_ge(fails_needed, n, 1 - p) <= 0.10)


print("Lowest true rate with <= 10 %% false-FAIL of the 90 %% invariant: n=30 -> %.1f %%, n=43 -> %.1f %%, n=100 -> %.1f %%, n=139 -> %.1f %%" % tuple(min_rate(n) for n in (30, 43, 100, 139)))
