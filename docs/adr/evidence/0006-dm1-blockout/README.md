# Task m5-06 evidence: DM1 blockout bot iteration (18 sets, contended host)

Report: `docs/tasks/m5-map-characters/notes/06-report.md` (read its "Host" section first). Sets `botrunN`, `botduelN`, `ffaN`: N = 1 is the
baseline layout, N = 6 the final layout (ec4700f; set 5 differs only by the Quad/shard commit). **No set ran on a quiet host** (three orphaned UE
clients of another worktree, load 1.7–3.8): every cost verdict is NOT JUDGED / contended, the FFA `budget_mean_us` 378–448 is not comparable,
and the behaviour verdicts carry the D40 caveat. ADR 0006 **E3** uses sets 5 and 6 only as the second basis of the band arithmetic
(`../0006-dm1-blockout-06b/e3_bands.py`); the valid acceptance of the final layout is task 06b's `../0006-dm1-blockout-06b/`.
`dm1run-final.out` is the `DM1Run` movement regression on the final layout (PASS, 7/7 checkpoints). `set6-*.txt` / `set1-*.txt` are the
stuck positions, long-dwell triage (`dwellwhy`) and link outcomes per layout record of the final and the baseline layout.
