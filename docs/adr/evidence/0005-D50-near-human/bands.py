"""ADR 0005 D50 (6)(iv) / D49 (1) re-derivation of the bot count bands from the task's acceptance rounds.

Usage: python3 -I bands.py [<evidence dir>]   (default: the directory of this script)

Reads every round's stats.json under <dir>/botrun/**/BotRun.stats.json and <dir>/botduel/**/BotDuel.stats.json (the four
--repeat 3 sets per script) and the ten FFA run summaries <dir>/ffa/ffa-*.log.json. Only valid rounds are used (not
INVALID, left_arena none, no world kill). For each lower band it prints the per-round values, the per-round mean and the
largest k with P(sum < k | bots unchanged) <= 2.5 % under
  (a) the empirical bootstrap: the sum of n rounds drawn with replacement from the observed rounds (n = 3 for the
      --repeat 3 sets, 10 for the FFA), 200 000 draws, fixed seed;
  (b) the Poisson model: sum ~ Poisson(n x per-round mean).
The proposed k is the smaller of the two (both must hold). The current D36/D48/D49 value and P(fail | unchanged) under
both models are printed beside it. The upper bands are re-checked (not moved), and the D48 (b) Mega band is evaluated
with p re-estimated on the timed returns of the BotDuel rounds.
"""
import glob
import json
import math
import os
import random
import sys

DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
ALPHA = 0.025
DRAWS = 200000
rng = random.Random(50)


def load(pattern):
    out = []
    for p in sorted(glob.glob(os.path.join(DIR, pattern), recursive=True)):
        with open(p) as f:
            d = json.load(f)
        d["_path"] = os.path.relpath(p, DIR)
        out.append(d)
    return out


def valid(r):
    return not r.get("invalid") and not r.get("left_arena") and not r.get("world_kills")


def poisson_cdf_below(k, lam):
    """P(X < k) for X ~ Poisson(lam)."""
    if k <= 0:
        return 0.0
    term = math.exp(-lam); acc = term
    for i in range(1, k):
        term *= lam / i; acc += term
    return min(acc, 1.0)


def boot_sums(xs, n):
    return [sum(rng.choice(xs) for _ in range(n)) for _ in range(DRAWS)]


def largest_k(p_below, hi):
    k = 0
    for c in range(0, hi + 1):
        if p_below(c) <= ALPHA:
            k = c
        else:
            break
    return k


def lower_band(name, xs, n, current):
    rng.seed(50)   # each band draws from its own seeded sequence (output independent of the band order)
    if not xs:
        print("%-28s no rounds" % name); return
    mean = sum(xs) / len(xs)
    sums = sorted(boot_sums(xs, n))
    import bisect
    pb = lambda k: bisect.bisect_left(sums, k) / len(sums)
    pp = lambda k: poisson_cdf_below(k, n * mean)
    hi = int(n * max(xs)) + 1
    kb, kp = largest_k(pb, hi), largest_k(pp, hi)
    print("%-28s rounds=%d values=%s mean=%.2f | k_bootstrap=%d k_poisson=%d -> proposed k=%d | current >= %d: P(fail) bootstrap %.2f %% poisson %.2f %%" % (
        name, len(xs), xs, mean, kb, kp, min(kb, kp), current, 100 * pb(current), 100 * pp(current)))


def main():
    br = [r for r in load("botrun/**/BotRun.stats.json") if not r.get("calibration")]
    bd = [r for r in load("botduel/**/BotDuel.stats.json") if not r.get("calibration")]
    ffa = load("ffa/ffa-*.log.json")
    brv, bdv = [r for r in br if valid(r)], [r for r in bd if valid(r)]
    print("=== D50 (6)(iv) re-derivation over %s ===" % DIR)
    print("BotRun rounds %d (valid %d), BotDuel rounds %d (valid %d), FFA runs %d" % (len(br), len(brv), len(bd), len(bdv), len(ffa)))
    for r in br + bd:
        if not valid(r):
            print("  left out: %s (invalid=%s left_arena=%s world_kills=%s)" % (r["_path"], r.get("invalid"), r.get("left_arena"), r.get("world_kills")))
    print("--- lower bands (largest k with P(sum < k | unchanged) <= 2.5 %; both models) ---")
    lower_band("BotRun sum kills (3)", [r["kills"] for r in brv], 3, 12)
    lower_band("BotDuel sum frags (3)", [r["frags"] for r in bdv], 3, 9)
    lower_band("BotDuel sum camp episodes (3)", [r.get("camp_episodes", 0) for r in bdv], 3, 2)
    lower_band("FFA sum kills (10)", [r["kills"] for r in ffa], 10, 70)

    print("--- D48 (b) Mega band (set of three BotDuel rounds: sum timed returns <= 6 s >= 3 and >= 1 within 3 s) ---")
    timed = [v for r in bdv for v in r.get("timed", [])]
    rng.seed(50)
    if timed and bdv:
        p6 = sum(1 for v in timed if v <= 6.0) / len(timed)
        p3 = sum(1 for v in timed if v <= 3.0) / len(timed)
        per_round = len(timed) / len(bdv)
        nset = int(round(3 * per_round))
        binom_below = lambda k, n, p: sum(math.comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(0, min(k, n + 1)))
        p_fail_model = 1 - (1 - binom_below(3, nset, p6)) * (1 - (1 - p3) ** nset)
        fails = 0
        rows = [r.get("timed", []) for r in bdv]
        for _ in range(DRAWS // 4):
            pick = [rng.choice(rows) for _ in range(3)]
            vs = [v for row in pick for v in row]
            if sum(1 for v in vs if v <= 6.0) < 3 or not any(v <= 3.0 for v in vs):
                fails += 1
        print("timed returns n=%d (%.2f per round) p(<=6 s)=%.2f p(<=3 s)=%.2f values=%s" % (len(timed), per_round, p6, p3, ["%.1f" % v for v in timed]))
        print("P(Mega band fails | unchanged): binomial at n=%d per set %.2f %%, bootstrap over rounds %.2f %% -> %s" % (
            nset, 100 * p_fail_model, 100.0 * fails / (DRAWS // 4), "stands" if max(p_fail_model, fails / (DRAWS // 4)) <= ALPHA else "ABOVE 2.5 %: to the architect"))
    else:
        print("no timed returns")

    print("--- upper bands (re-checked, not moved) ---")
    for name, rs, n in (("BotRun", brv, 3), ("BotDuel", bdv, 3)):
        sets = {}
        for r in rs:
            key = r["_path"].split(os.sep)[1] if os.sep in r["_path"] else "?"
            sets.setdefault(key, []).append(r)
        print("%s sets: %s" % (name, " ".join("%s:stuck=%d" % (k, sum(x["stuck"] for x in v)) for k, v in sorted(sets.items()))))
        print("%s per round: stuck max=%d (cap 6) bursts=%d pocket=%d max_dwell=%d (<= 8) link_max_dt=%s (<= 8)" % (
            name, max([r["stuck"] for r in rs] or [0]), sum(r.get("bursts", 0) for r in rs), sum(r.get("pocket", 0) for r in rs),
            max([r["max_dwell"] for r in rs] or [0]), "%.2f" % max(r["link_max"] for r in rs) if all("link_max" in r for r in rs) and rs else "n/a"))
    if ffa:
        print("FFA: sum stuck=%d (<= 30) max run stuck=%d (cap 7) bursts=%d pocket=%d max_dwell=%d dwell>=6 runs=%d link_max_dt=%.2f" % (
            sum(r["stuck"] for r in ffa), max(r["stuck"] for r in ffa), sum(r["bursts"] for r in ffa), sum(r["pocket"] for r in ffa),
            max(r["max_dwell"] for r in ffa), sum(1 for r in ffa if r["max_dwell"] >= 6), max(r["link_max_dt"] for r in ffa)))

    print("--- report-only ranges ---")
    for name, rs in (("BotRun", brv), ("BotDuel", bdv)):
        if not rs:
            continue
        mf = [r.get("max_fire_us", 0) for r in rs]
        cm = [sum(r["clean_means"]) / len(r["clean_means"]) for r in rs if r.get("clean_means")]
        sh = [100.0 * r.get("camp_episodes", 0) / r["goal_changes"] for r in rs if r.get("goal_changes")]
        cf = [v for r in rs for v in r.get("clean_max_fire", [])]
        print("%s: clean mean_us range %.1f-%.1f (rounds > 300: %d; not cost-judged rounds: %d), max_fire_us (all windows) range %.0f-%.0f, "
              "clean-window max_fire_us > 2000: %d/%d (%.0f %%), camp_share_pct range %.1f-%.1f" % (
            name, min(cm or [0]), max(cm or [0]), sum(1 for v in cm if v > 300), sum(1 for r in rs if not r.get("cost_judged")), min(mf), max(mf),
            sum(1 for v in cf if v > 2000), len(cf), 100.0 * sum(1 for v in cf if v > 2000) / len(cf) if cf else 0.0, min(sh or [0]), max(sh or [0])))


main()
