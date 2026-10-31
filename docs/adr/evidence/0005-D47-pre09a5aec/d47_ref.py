#!/usr/bin/env python3
"""Reference implementation of ADR 0005 D47 (a)-(c), the process-slow window class of the bot cost classifier, with the
D48 (a) transition rule (2026-10-31: a window with over_share > 5 % and R_other < 2.0 is process-slow, not cost, iff
rr <= 2.5 and a neighbouring window has R_other >= 2.0; the fixture numbers below are unchanged by it).

usage: d47_ref.py <skip> <server.log> [<server.log> ...]
  <skip> = number of leading windows excluded from the reference set (D47: 1, i.e. windows 2..n).

Reads the VeloxBotBudget / VeloxBotCost line pairs of a dedicated-server log and prints, per log, the reference, the
class string (. clean, P process-slow incl. transition, X cost) and the round figures nettest.sh must reproduce. No host
sidecar is read: the fixture has none, so the `contended` branch of D47 (b) never fires here (nettest.sh adds it before
the P/X tests).
"""
import re
import sys

R_OTHER_SLOW = 2.0    # D47 (b): R_other at or above this = the thread slowed
RR_WINDOW_MAX = 2.5   # D47 (b): rr above this in a slow window = cost (bots slowed clearly more than the thread)
RR_ROUND_MAX = 2.0    # D47 (c): aggregate rr over the P windows
REF_BAND = 1.6        # D47 (a): reference set = windows with frame_other <= REF_BAND * min3
MIN_CLEAN_WINDOWS = 6 # D47 (c)
BUDGET_MAX = 300.0    # section 13 band, also applied to p_norm_mean_us


def kv(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def windows(path):
    rows, budget = [], None
    for line in open(path, errors="replace"):
        if "VeloxBotBudget bots=" in line:
            budget = kv(line.split("VeloxBotBudget ")[1])
        elif "VeloxBotCost ticks=" in line and budget:
            rows.append((budget, kv(line.split("VeloxBotCost ")[1])))
            budget = None
    return rows


def frame_other(cost):
    if "frame_other_us" in cost:
        return float(cost["frame_other_us"])
    return max(0.0, float(cost["frame_us"]) - float(cost["frame_bots_us"]))


def classify(rows, skip):
    n = len(rows)
    fo = [frame_other(c) for _, c in rows]
    mu = [float(b["mean_us"]) for b, _ in rows]
    over = [100.0 * float(b["over"]) / float(b["ticks"]) if float(b["ticks"]) > 0 else 0.0 for b, _ in rows]
    cand = list(range(max(1, skip), n))
    min3 = min(sum(fo[i:i + 3]) / 3.0 for i in cand if i + 3 <= n)
    ref = [i for i in cand if fo[i] <= REF_BAND * min3]
    fref = sum(fo[i] for i in ref) / len(ref)
    mref = sum(mu[i] for i in ref) / len(ref)
    r_other = [f / fref for f in fo]
    rr = [(mu[i] / mref) / r_other[i] for i in range(n)]
    cls = []
    for i in range(n):
        if over[i] <= 5.0:
            cls.append("." if r_other[i] < R_OTHER_SLOW else "P")
        elif r_other[i] < R_OTHER_SLOW:
            # D46 1c: the thread did not slow, the bots did -> cost. D48 (a): unless the slow state begins or ends
            # inside this window (a neighbour is slow) and the bots track the thread (rr <= RR_WINDOW_MAX): a
            # transition window, process-slow. Printed as P; the runner adds transition=1 to its detail line.
            neighbours = [j for j in (i - 1, i + 1) if 0 <= j < n]
            transition = rr[i] <= RR_WINDOW_MAX and any(r_other[j] >= R_OTHER_SLOW for j in neighbours)
            cls.append("P" if transition else "X")
        else:
            cls.append("X" if rr[i] > RR_WINDOW_MAX else "P")
    return fo, mu, cls, fref, mref, min3, len(ref)


def main():
    skip = int(sys.argv[1])
    for path in sys.argv[2:]:
        rows = windows(path)
        if len(rows) < 5:
            print("%s: %d windows, no reference (D46 classes apply)" % (path, len(rows)))
            continue
        fo, mu, cls, fref, mref, min3, ref_n = classify(rows, skip)
        n = len(rows)
        clean = [i for i in range(n) if cls[i] == "."]
        slow = [i for i in range(n) if cls[i] == "P"]
        cost = [i for i in range(n) if cls[i] == "X"]
        judged = len(clean) >= MIN_CLEAN_WINDOWS and 2 * (len(clean) + len(slow)) >= n
        clean_mean = sum(mu[i] for i in clean) / len(clean) if clean else 0.0
        line = "%s: n=%d ref=%d frame_other_ref=%.0f mean_ref=%.1f min3=%.0f classes [%s] clean=%d P=%d X=%d clean_mean_us=%.1f" % (
            path, n, ref_n, fref, mref, min3, "".join(cls), len(clean), len(slow), len(cost), clean_mean)
        fails = []
        if cost:
            fails.append("cost window(s) %s" % ",".join(str(i + 1) for i in cost))
        if judged and clean_mean > BUDGET_MAX:
            fails.append("clean mean_us %.1f > %.0f" % (clean_mean, BUDGET_MAX))
        if len(slow) >= 3:
            rr_round = (sum(mu[i] for i in slow) / len(slow) / mref) / (sum(fo[i] for i in slow) / len(slow) / fref)
            p_norm = sum(mu[i] / (fo[i] / fref) for i in slow) / len(slow)
            line += " p_norm_mean_us=%.1f rr_round=%.2f" % (p_norm, rr_round)
            if judged and p_norm > BUDGET_MAX:
                fails.append("p_norm_mean_us %.1f > %.0f" % (p_norm, BUDGET_MAX))
            if judged and rr_round > RR_ROUND_MAX:
                fails.append("rr_round %.2f > %.1f" % (rr_round, RR_ROUND_MAX))
        line += " cost=%s" % ("n/j" if not judged else ("FAIL " + "; ".join(fails) if fails else "PASS"))
        print(line)


if __name__ == "__main__":
    main()
