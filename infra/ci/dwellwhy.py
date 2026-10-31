"""Dwell triage tool (ADR 0006 E2 (f), task 04c; from the dwell report's dwellwhy.py).
Usage: dwellwhy.py <min_samples> <server.log>...

For every Live dwell run of >= <min_samples> consecutive 1 Hz VeloxBot samples within 150 cm (2-D) of the run's first sample (the D34 run
rule of nettest.sh / ffa_summary.py, but WITHOUT their exclusions, so the tool shows what the runners exclude) it prints the bot, the length,
the position, goals / items, the arrived / stuck / vis / speed strings, the route line in force and the respawn / pickup lines inside the
run, plus  class=camp | timed-wait | other :
  camp        camp=1 was in force for at least one sample of the run (the D34 exclusion),
  timed-wait  the run opens with timed-wait samples (E2 (b), same constants as the runners) and has at most a shorter tail after them
              (the bot still standing at the item after the take: the runner starts a new run there); the line shows the wait sample count,
  other       anything else (a stall, a fight stand, a refused pickup ...): the run the 5 s cap is for.
Standard library only (-I safe)."""
import math
import re
import sys

DWELL_RADIUS = 150.0
WAIT_RADIUS = 300.0      # E2: same constants as nettest.sh / ffa_summary.py
WAIT_MAX_S = 20.0
WAIT_SLACK_S = 1.0
POSR = re.compile(r"VeloxBot name=(\S+) (.*?) pos=V\(([^)]*)\)")
TS = re.compile(r"^\[(\d+)\.(\d+)\.(\d+)-(\d+)\.(\d+)\.(\d+):(\d+)\]")
ROUTE_GOAL = re.compile(r"VeloxBotRoute bot=(\S+) goal=(\S+)")
SPAWN = re.compile(r"VeloxPickupSpawn item=(\S+) at=V\(([^)]*)\)")


def ts(line):
    m = TS.match(line)
    return int(m.group(4)) * 3600 + int(m.group(5)) * 60 + int(m.group(6)) + int(m.group(7)) / 1000.0 if m else None


def vec(txt):
    """(X, Y, Z); ToCompactString drops zero components (V(X=500.00) is (500, 0, 0))."""
    d = dict(re.findall(r"([XYZ])=([-\d.]+)", txt))
    return float(d.get("X", 0)), float(d.get("Y", 0)), float(d.get("Z", 0))


def scan(path, min_samples):
    lines = open(path, errors="replace").read().splitlines()
    live = False
    camp = {}
    dwell = {}
    runs = []
    lastroute = {}
    spawn = {}
    episodes = {}   # bot -> {X, t0, R}
    for i, l in enumerate(lines):
        if "VeloxMatch phase=Live" in l:
            live = True
            dwell.clear()
            continue
        m = SPAWN.search(l)
        if m:
            spawn[m.group(1)] = vec(m.group(2))
            continue
        m = re.search(r"VeloxPickupRespawn item=(\S+)", l)
        if m:
            for n_ in [n for n, e in episodes.items() if e["X"] == m.group(1)]:
                del episodes[n_]
            continue
        m = re.search(r"VeloxBotRoute bot=(\S+) (goal=.*)", l)
        if m:
            n = m.group(1)
            lastroute[n] = (i, m.group(2))
            cm = re.search(r" camp=(\d)", l)
            if cm:
                camp[n] = int(cm.group(1))
            if ROUTE_GOAL.search(l):
                episodes.pop(n, None)
                if " goal=Item " in l and " available=0 " in l:
                    wi, wr = re.search(r" item=(\S+)", l), re.search(r" respawn_in=(-?[\d.]+)", l)
                    if wi and wr and ts(l) is not None:
                        episodes[n] = {"X": "DA_Item_" + wi.group(1).replace(".", "_"), "t0": ts(l), "R": float(wr.group(1))}
            continue
        m = POSR.search(l)
        if not (m and live):
            continue
        n = m.group(1)
        x, y, _ = vec(m.group(3))
        t = ts(l)
        # E2 (b): is this sample a timed-wait sample?
        wait = False
        e = episodes.get(n)
        if e and t is not None:
            if t - e["t0"] > min(e["R"], WAIT_MAX_S) + WAIT_SLACK_S:
                del episodes[n]
            else:
                im = re.search(r" goal=item .*?item=(\S+)", l)
                at = spawn.get(e["X"])
                wait = bool(im and im.group(1) == e["X"] and (at is None or math.hypot(x - at[0], y - at[1]) <= WAIT_RADIUS))
        cflag = bool(camp.get(n, 0))
        d = dwell.get(n)
        if d and math.hypot(x - d[0], y - d[1]) <= DWELL_RADIUS:
            d[2] += 1
            d[4] = i
            d[5].append(l)
            d[7].append(wait)
            d[8] = d[8] or cflag
        else:
            if d and d[2] >= min_samples:
                runs.append((n, d))
            dwell[n] = [x, y, 1, i, i, [l], lastroute.get(n), [wait], cflag]
    for n, d in dwell.items():
        if d[2] >= min_samples:
            runs.append((n, d))
    for n, d in runs:
        samples = d[5]
        items = sorted({re.search(r" item=(\S+)", s).group(1) for s in samples})
        goals = sorted({re.search(r" goal=(\S+)", s).group(1) for s in samples})
        arr = "".join(re.search(r" arrived=(\d)", s).group(1) for s in samples)
        spd = ",".join(re.search(r" speed=(\d+)", s).group(1) for s in samples)
        stk = "".join(re.search(r" stuck=(\d)", s).group(1) for s in samples)
        vis = "".join(re.search(r" vis=(\d)", s).group(1) for s in samples)
        t0, t1 = ts(samples[0]), ts(samples[-1])
        k = 0
        while k < len(d[7]) and d[7][k]:
            k += 1
        # timed-wait: the run opens with timed-wait samples and ends, at most, with a shorter tail after the closing event (the bot keeps
        # standing where it took the item; E2 (d) judges that tail by the plain cap, the runner starts a new run there)
        cls = "camp" if d[8] else ("timed-wait" if (k > 0 and len(d[7]) - k < k) else "other")
        # route line in force at the first sample and any route / respawn / pickup lines inside the run
        rl = None
        for j in range(d[3], -1, -1):
            if "VeloxBotRoute bot=%s goal=" % n in lines[j]:
                rl = lines[j]
                break
        inside = [lines[j] for j in range(d[3], min(len(lines), d[4] + 120))
                  if ("VeloxBotRoute bot=%s goal=" % n in lines[j]) or ("VeloxPickup" in lines[j] and ("by=%s " % n in lines[j] or "Respawn" in lines[j]))]
        print("=== %s | %s dwell=%d at (%.0f,%.0f) span=%.1fs goals=%s items=%s class=%s (wait samples %d/%d)" % (path[-60:], n, d[2], d[0], d[1], t1 - t0, goals, items, cls, sum(d[7]), d[2]))
        print("    arrived=%s stuck=%s vis=%s speed=[%s]" % (arr, stk, vis, spd))
        if rl:
            r = re.sub(r" (candidates|eval_s|from|start|filter|paths)=\S+", "", rl)
            print("    route@%+.1fs: %s" % (ts(rl) - t0, r[r.find("goal="):][:200]))
        for l in inside:
            tt = ts(l)
            if tt is None or tt > t1 + 1.5:
                continue
            r = re.sub(r" (candidates|eval_s|from|start|filter|paths)=\S+", "", l)
            k = r.find("Velox", 30)
            print("    %+.1fs %s" % (tt - t0, r[k:][:200]))


if len(sys.argv) < 3:
    sys.exit(__doc__)
for f in sys.argv[2:]:
    scan(f, int(sys.argv[1]))
