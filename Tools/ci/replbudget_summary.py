#!/usr/bin/env python3
"""Replication budget summary (M6 task 10, ADR 0007 section 5.6). Stdlib only; run with python3 -I.

  replbudget_summary.py --one <run dir>      one run (n<N>/ written by infra/ci/replbudget.sh): the per-run row
  replbudget_summary.py <dir> [--json out]   <dir>/n<N>/ for every N present: per-N table, fit, 16-connection extrapolation, verdict

Per run the windows are the server's `VeloxNetStats` lines (10 s each) with conns == N, starting WARM_S after the first such
window (bots spawned, Countdown over) and ending before the first clients leave. A window is `contended` when the host sidecar
agrees (ADR 0005 D46: main-thread run-queue wait > 2 %, steal > 2 %, majflt > 20 or PSI > 5 %), else `clean`. The fits use the clean
windows of a run when it has at least MIN_CLEAN of them, else all windows (flagged).

Frame time = game-thread CPU time per frame (cpu_ms_*), i.e. the work without the t.MaxFPS sleep: wall frame time is pinned at
16.7 ms by the cap. p50/p99 of a run are the median / maximum of the per-window values (windows are not poolable).
"""
import calendar, json, math, re, statistics, sys, os, glob

WARM_S = float(os.environ.get("REPLBUDGET_WARM_S", "30"))
MIN_CLEAN = 4
FPS = 60.0
THRESH = {"mean_ms": 8.0, "p99_ms": 16.0, "down_kbps": 100.0}   # ADR 0007 section 5.6
TARGET_CONNS = 16
TARGET_BOTS = 8

TS = re.compile(r"^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d+)\]")


def ts(line):
    m = TS.match(line)
    if not m:
        return None
    y, mo, d, h, mi, s, ms = map(int, m.groups())
    return calendar.timegm((y, mo, d, h, mi, s)) + ms / 1000.0


def kv(line):
    out = {}
    for m in re.finditer(r"(\w+)=(\[[^\]]*\]|\S+)", line):
        out[m.group(1)] = m.group(2)
    return out


def num(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def lst(v):
    v = (v or "").strip("[]")
    return [float(x) for x in v.split(",") if x != ""]


def read_kv_file(path):
    rows = []
    try:
        for line in open(path, encoding="utf-8", errors="replace"):
            d = kv(line)
            if "t" in d:
                rows.append({k: num(v) for k, v in d.items()})
    except OSError:
        pass
    return rows


def nearest(rows, t):
    return min(rows, key=lambda r: abs(r["t"] - t)) if rows else None


def host_window(rows, t0, t1):
    """D46 host figures for [t0, t1] from two sidecar samples; None without a sidecar."""
    a, b = nearest(rows, t0), nearest(rows, t1)
    if not a or not b or b["t"] - a["t"] < 4.0:
        return None
    dt = b["t"] - a["t"]
    out = {}
    out["rq_wait_pct"] = max(0.0, (b["main_wait_ns"] - a["main_wait_ns"]) / (dt * 1e9) * 100) if a["main_wait_ns"] >= 0 else -1
    tot = b["cpu_total"] - a["cpu_total"]
    out["steal_pct"] = (b["cpu_steal"] - a["cpu_steal"]) / tot * 100 if tot > 0 else 0.0
    out["majflt"] = (b["srv_majflt"] - a["srv_majflt"]) if a["srv_majflt"] >= 0 else 0
    psi = [(b[k] - a[k]) / (dt * 1e6) * 100 for k in ("psi_cpu_us", "psi_mem_us", "psi_io_us") if a[k] >= 0]
    out["psi_pct"] = max(psi) if psi else 0.0
    out["load1"] = b["load1"]
    out["contended"] = out["rq_wait_pct"] > 2 or out["steal_pct"] > 2 or out["majflt"] > 20 or out["psi_pct"] > 5
    return out


def mean(x):
    return sum(x) / len(x) if x else float("nan")


def fit(xs, ys):
    """Least squares y = a + b x; returns (a, b, se_b) (se from the residuals, nan with < 3 points)."""
    n = len(xs)
    mx, my = mean(xs), mean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    if n < 2 or sxx == 0:
        return my, 0.0, float("nan")
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    a = my - b * mx
    se = float("nan")
    if n > 2:
        res = sum((y - (a + b * x)) ** 2 for x, y in zip(xs, ys))
        se = math.sqrt(res / (n - 2) / sxx)
    return a, b, se


def analyse(d):
    run = dict(kv(open(os.path.join(d, "run.txt")).read()))
    n = int(run["clients"])
    fps = num(run.get("fps"), FPS)
    lines = open(os.path.join(d, "server.stats.log"), encoding="utf-8", errors="replace").read().splitlines()
    stats = []
    for l in lines:
        if "VeloxNetStats conns=" in l:
            t = ts(l)
            k = kv(l)
            if t is not None:
                stats.append((t, k))
    full = [(t, k) for t, k in stats if int(num(k["conns"])) == n]
    r = {"n": n, "dir": d, "windows_total": len(stats), "bots": int(num(run.get("bots"))), "fps": fps}
    if not full:
        r["error"] = "no window with conns == N"
        return r
    t_first = full[0][0]
    sel = [(t, k) for t, k in full if t - t_first >= WARM_S]
    host = read_kv_file(os.path.join(d, "host.log"))
    W = []
    for t, k in sel:
        w = num(k["window"])
        h = host_window(host, t - w, t)
        W.append({
            "t": t, "window": w, "frames": num(k["frames"]), "pawns": num(k["pawns"]),
            "cpu_mean": num(k["cpu_ms_mean"]), "cpu_p50": num(k["cpu_ms_p50"]), "cpu_p99": num(k["cpu_ms_p99"]), "cpu_max": num(k["cpu_ms_max"]),
            "wall_mean": num(k["wall_ms_mean"]), "wall_p99": num(k["wall_ms_p99"]),
            "out_conn": lst(k["out_bps_per_conn"]), "in_total": num(k["in_bps_total"]), "out_inst": lst(k["out_inst_per_conn"]),
            "host": h, "contended": bool(h and h["contended"]),
        })
    r["selected"] = len(W)
    clean = [w for w in W if not w["contended"]]
    r["clean"] = len(clean)
    basis = clean if len(clean) >= MIN_CLEAN else W
    r["basis"] = "clean" if basis is clean else "all (fewer than %d clean windows)" % MIN_CLEAN
    if not basis:
        r["error"] = "no windows after warm-up"
        return r
    fr = sum(w["frames"] for w in basis)
    r["frame_deficit"] = 1.0 - sum(min(w["frames"], fps * w["window"]) for w in basis) / (fps * sum(w["window"] for w in basis))
    r["cpu_mean"] = sum(w["cpu_mean"] * w["frames"] for w in basis) / fr
    r["cpu_p50"] = statistics.median(w["cpu_p50"] for w in basis)
    r["cpu_p99"] = statistics.median(w["cpu_p99"] for w in basis)
    r["cpu_p99_max"] = max(w["cpu_p99"] for w in basis)
    r["cpu_max"] = max(w["cpu_max"] for w in basis)
    r["wall_mean"] = mean([w["wall_mean"] for w in basis])
    r["wall_p99"] = statistics.median(w["wall_p99"] for w in basis)
    per = [v for w in basis for v in w["out_conn"]]
    r["out_mean_bps"] = mean(per)
    r["out_max_bps"] = max(per) if per else float("nan")
    r["out_inst_max_bps"] = max((v for w in basis for v in w["out_inst"]), default=float("nan"))
    r["in_total_bps"] = mean([w["in_total"] for w in basis])
    r["pawns"] = mean([w["pawns"] for w in basis])
    r["cpu_windows"] = [w["cpu_mean"] for w in basis]
    # bots: VeloxBotBudget / VeloxBotCost over the same period
    t_lo, t_hi = sel[0][0] - num(sel[0][1]["window"]), sel[-1][0]
    bm, bf = [], []
    for l in lines:
        if "VeloxBotBudget bots=" in l or "VeloxBotCost ticks=" in l:
            t = ts(l)
            if t is None or t < t_lo or t > t_hi:
                continue
            k = kv(l)
            if "mean_us" in k and "frames" in k:
                bm.append((num(k["mean_us"]), num(k["frames"]), num(k["window"]), num(k["bots"])))
            if "frame_us" in k:
                bf.append((num(k["frame_us"]), num(k.get("frame_bots_us"))))
    if bm:
        r["bot_tick_mean_us"] = mean([x[0] for x in bm])
        r["budget_frame_deficit"] = 1.0 - sum(x[1] for x in bm) / (fps * sum(x[2] for x in bm))
    if bf:
        r["world_tick_us"] = mean([x[0] for x in bf])
        r["bots_ms_per_frame"] = mean([x[1] for x in bf]) / 1000.0
    # host
    hs = [w["host"] for w in basis if w["host"]]
    r["host_classes"] = "".join("C" if w["contended"] else "." for w in W)
    if hs:
        r["rq_wait_pct_max"] = max(h["rq_wait_pct"] for h in hs)
        r["steal_pct_max"] = max(h["steal_pct"] for h in hs)
        r["psi_pct_max"] = max(h["psi_pct"] for h in hs)
        r["load1_max"] = max(h["load1"] for h in hs)
    rss = [x["srv_rss_kb"] for x in host if x.get("srv_rss_kb", -1) > 0]
    r["rss_max_mb"] = max(rss) / 1024.0 if rss else float("nan")
    # production background load during the selected span
    prod = read_kv_file(os.path.join(d, "prod.log"))
    a, b = nearest(prod, t_lo), nearest(prod, t_hi)
    if a and b and b["t"] > a["t"]:
        r["prod_cpu_pct"] = (b["prod_cpu"] - a["prod_cpu"]) / 100.0 / (b["t"] - a["t"]) * 100.0
    # clients
    fpss = []
    for f in glob.glob(os.path.join(d, "client*.stats.log")):
        for l in open(f, encoding="utf-8", errors="replace"):
            if "role=client" in l:
                fpss.append(num(kv(l)["fps"]))
    r["client_fps_max"] = max(fpss) if fpss else float("nan")
    r["client_fps_mean"] = mean(fpss)
    try:
        r["load"] = open(os.path.join(d, "gate.txt")).read().split()
    except OSError:
        pass
    return r


def row(r):
    if "error" in r:
        return "N=%d: %s" % (r["n"], r["error"])
    return ("N=%d windows=%d (clean %d, basis %s) classes=%s pawns=%.1f frame_deficit=%.1f%% | cpu_ms mean=%.2f p50=%.2f p99=%.2f (max-window p99 %.2f, max %.1f) "
            "wall_ms mean=%.2f p99=%.2f | out/conn mean=%.1f KB/s max=%.1f KB/s (engine 1s max %.1f) in_total=%.1f KB/s | bot_tick=%.0f us bots_ms/frame=%.2f world_tick=%.0f us | "
            "rss_max=%.0f MB | host rq_wait<=%.1f%% steal<=%.1f%% psi<=%.1f%% load1<=%.2f | prod_cpu=%.0f%% of a core | client fps mean=%.1f max=%.1f") % (
        r["n"], r["selected"], r["clean"], r["basis"], r["host_classes"], r["pawns"], r["frame_deficit"] * 100,
        r["cpu_mean"], r["cpu_p50"], r["cpu_p99"], r["cpu_p99_max"], r["cpu_max"], r["wall_mean"], r["wall_p99"],
        r["out_mean_bps"] / 1000, r["out_max_bps"] / 1000, r["out_inst_max_bps"] / 1000, r["in_total_bps"] / 1000,
        r.get("bot_tick_mean_us", float("nan")), r.get("bots_ms_per_frame", float("nan")), r.get("world_tick_us", float("nan")),
        r["rss_max_mb"], r.get("rq_wait_pct_max", float("nan")), r.get("steal_pct_max", float("nan")), r.get("psi_pct_max", float("nan")),
        r.get("load1_max", float("nan")), r.get("prod_cpu_pct", float("nan")), r["client_fps_mean"], r["client_fps_max"])


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__); return 2
    if args[0] == "--one":
        print(row(analyse(args[1].rstrip("/")))); return 0
    root = args[0].rstrip("/")
    out_json = args[args.index("--json") + 1] if "--json" in args else None
    runs = []
    for d in sorted(glob.glob(os.path.join(root, "n[0-9]*")), key=lambda p: int(re.search(r"n(\d+)$", p).group(1))):
        if os.path.exists(os.path.join(d, "run.txt")):
            runs.append(analyse(d))
    for r in runs:
        print(row(r))
    ok = [r for r in runs if "error" not in r]
    if len(ok) < 2:
        print("need at least two N for a fit"); return 1
    ns = [r["n"] for r in ok]
    pawns_mean = mean([r["pawns"] for r in ok])
    print("\n== table (frame = game-thread CPU ms per frame; down = server out bytes/s per connection)")
    print("%3s %6s %8s %8s %8s %9s %9s %10s %9s %8s" % ("N", "pawns", "mean ms", "p50 ms", "p99 ms", "down mean", "down max", "in total", "deficit", "rss MB"))
    for r in ok:
        print("%3d %6.1f %8.2f %8.2f %8.2f %7.1f KB %7.1f KB %8.1f KB %8.1f%% %8.0f" % (
            r["n"], r["pawns"], r["cpu_mean"], r["cpu_p50"], r["cpu_p99"], r["out_mean_bps"] / 1000, r["out_max_bps"] / 1000,
            r["in_total_bps"] / 1000, r["frame_deficit"] * 100, r["rss_max_mb"]))
    res = {"runs": ok}
    # frame fits on the run means; slope error from the pooled windows (every window is a point (N, cpu_mean))
    pts_x = [r["n"] for r in ok for _ in r["cpu_windows"]]
    pts_y = [v for r in ok for v in r["cpu_windows"]]
    pa, pb, pse = fit(pts_x, pts_y)
    a, b, _ = fit(ns, [r["cpu_mean"] for r in ok])
    a99, b99, _ = fit(ns, [r["cpu_p99"] for r in ok])
    measurable = (not math.isnan(pse)) and pb > 2 * pse
    print("\n== fit  frame_ms_mean(N) = a + b*N   (run means, N=%s)" % ",".join(map(str, ns)))
    print("a (fixed: bots, physics, game) = %.2f ms   b (per connection) = %.3f ms   [pooled-window fit b = %.3f +- %.3f ms (1 se) -> per-connection part %s]" % (
        a, b, pb, pse, "measurable" if measurable else "NOT measurable (slope < 2 se: the fixed part dominates)"))
    print("p99:  a = %.2f ms   b = %.3f ms per connection" % (a99, b99))
    # downstream per connection vs pawn count: D(P) = alpha + beta*P  (pawns = N + bots in the runs)
    al, be, _ = fit([r["pawns"] for r in ok], [r["out_mean_bps"] / 1000 for r in ok])
    alm, bem, _ = fit([r["pawns"] for r in ok], [r["out_max_bps"] / 1000 for r in ok])
    print("downstream per connection (KB/s) D(P) = alpha + beta*P, P = pawns in the world (N + bots): mean alpha=%.2f beta=%.3f | max alpha=%.2f beta=%.3f" % (al, be, alm, bem))
    # extrapolation
    bots_pf = mean([r["bots_ms_per_frame"] / r["bots"] for r in ok if "bots_ms_per_frame" in r and r["bots"]]) if any("bots_ms_per_frame" in r for r in ok) else 0.0
    rows = []
    for conns, bots in ((TARGET_CONNS, TARGET_BOTS), (TARGET_CONNS, ok[0]["bots"])):
        P = conns + bots
        scale = P / pawns_mean
        fixed = a - (ok[0]["bots"] - bots) * bots_pf
        mean16 = fixed + conns * b * scale
        mean16_unscaled = fixed + conns * b
        # p99 is not additive; with a flat frame-mean slope the observed p99 level is the estimate, otherwise the same linear model
        p99_16 = (mean([r["cpu_p99"] for r in ok]) if not measurable else a99 + conns * b99 * scale) - (ok[0]["bots"] - bots) * bots_pf
        down = al + be * P
        down_max = alm + bem * P
        rows.append({"conns": conns, "bots": bots, "pawns": P, "mean_ms": mean16, "mean_ms_unscaled": mean16_unscaled, "p99_ms": p99_16,
                     "repl_part_ms": conns * b * scale, "down_kbps": down, "down_max_kbps": down_max, "fixed_ms": fixed})
    print("\n== extrapolation to %d connections (linear in connections; the per-connection part also scales with pawns: b * P/%.1f; bots removed at %.3f ms/frame each)" % (TARGET_CONNS, pawns_mean, bots_pf))
    print("formula: mean(C, B) = (a - (%d - B) * bot_ms) + C * b * (C + B) / %.1f ; down(C, B) = alpha + beta * (C + B)" % (ok[0]["bots"], pawns_mean))
    for x in rows:
        print("C=%d B=%d pawns=%d: frame mean %.2f ms (unscaled %.2f; replication part %.2f ms of it, fixed %.2f ms)  p99 %.2f ms  down mean %.1f KB/s (max-based %.1f KB/s)" % (
            x["conns"], x["bots"], x["pawns"], x["mean_ms"], x["mean_ms_unscaled"], x["repl_part_ms"], x["fixed_ms"], x["p99_ms"], x["down_kbps"], x["down_max_kbps"]))
    res["fit"] = {"a": a, "b": b, "a99": a99, "b99": b99, "pooled_b": pb, "pooled_se": pse, "alpha": al, "beta": be}
    res["extrapolation"] = rows
    # verdict on the reference configuration (16 players + 8 bots)
    x = rows[0]
    over = []
    if x["mean_ms"] > THRESH["mean_ms"]:
        over.append("frame mean %.2f ms > %.0f ms" % (x["mean_ms"], THRESH["mean_ms"]))
    if x["p99_ms"] > THRESH["p99_ms"]:
        over.append("frame p99 %.2f ms > %.0f ms" % (x["p99_ms"], THRESH["p99_ms"]))
    if x["down_kbps"] > THRESH["down_kbps"]:
        over.append("downstream %.1f KB/s > %.0f KB/s" % (x["down_kbps"], THRESH["down_kbps"]))
    print("\n== verdict vs ADR 0007 section 5.6 (16 players + 8 bots)")
    if not over:
        v = "VERDICT keep classic: frame mean %.2f ms (<= 8), p99 %.2f ms (<= 16), downstream %.1f KB/s per connection (<= 100)" % (x["mean_ms"], x["p99_ms"], x["down_kbps"])
    else:
        repl_share = x["repl_part_ms"]
        v = "VERDICT thresholds crossed: " + "; ".join(over) + ". Replication part of the frame at 16 connections: %.2f ms; fixed part %.2f ms (%s)." % (
            repl_share, x["fixed_ms"], "the fixed part alone exceeds the threshold - bots/physics, not replication, so Replication Graph/Iris would not recover it" if x["fixed_ms"] > THRESH["mean_ms"] else "replication is the cause: revisit")
    if not measurable:
        v += " Per-connection frame cost not measurable against run-to-run noise (fixed part dominates)."
    print(v)
    res["verdict"] = v
    if out_json:
        json.dump(res, open(out_json, "w"), indent=1, default=str)
    return 0


if __name__ == "__main__":
    sys.exit(main())
