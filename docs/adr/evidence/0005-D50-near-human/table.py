"""Per-round table of the D50 acceptance rounds and the near-human quantities pooled over all of them.
Usage: python3 -I table.py [<evidence dir>] [botrun|botduel|calibration/<tag>/<set>...]"""
import glob, json, os, sys

DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
GROUPS = sys.argv[2:] or ["botrun", "botduel"]


def vals(q):
    shots = q["hs_shots"] + q["pj_shots"]
    f = lambda a, b, m=100.0: ("%.1f" % (m * a / b)) if b else "n/a"
    return [f(q["hs_hits"], q["hs_shots"]), f(q["pj_hits"], q["pj_shots"]), f(shots, q["kills"], 1.0) if shots else "n/a",
            f(q["fire"], q["vis"]), f(q["held"], q["fire"]), f(q["camp_samples"], q["samples"])]


HEAD = "%-34s %5s %5s %5s %5s %6s %-22s %7s %6s | %6s %6s %6s %6s %6s %6s" % (
    "round", "kills", "frags", "stuck", "camp", "megaT", "timed_s", "mean_us", "cost", "acc_hs", "acc_pj", "spk", "trig", "held", "campT")
for g in GROUPS:
    rows = sorted(glob.glob(os.path.join(DIR, g, "**", "*.stats.json"), recursive=True))
    if not rows:
        continue
    print(HEAD)
    pool = {}
    for p in rows:
        r = json.load(open(p))
        cm = r.get("clean_means") or []
        q = r.get("nh")
        if q:
            for k, v in q.items():
                pool[k] = pool.get(k, 0) + v
        print("%-34s %5d %5d %5d %5d %6d %-22s %7s %6s | %s" % (
            os.path.relpath(p, DIR).replace("/nettest/", ":").replace(".stats.json", "")[-34:], r["kills"], r["frags"], r["stuck"], r.get("camp_episodes", 0),
            r.get("mega_takes", 0), ",".join("%.1f" % v for v in r.get("timed", [])) or "-", "%.1f" % (sum(cm) / len(cm)) if cm else "-",
            "judged" if r.get("cost_judged") else "n/j", " ".join("%6s" % v for v in (vals(q) if q else ["n/a"] * 6))))
    if pool:
        print("%-34s %s" % ("POOLED " + g, " ".join("%s=%s" % kv for kv in zip(("acc_hitscan_pct", "acc_proj_pct", "shots_per_kill", "trigger_share_pct", "held_share_pct", "camp_time_pct"), vals(pool)))))
    print()
