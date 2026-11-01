# 16c — `build.sh` map audit: exit hang and run time

**Milestone:** M5 First real map + characters
**Agent:** dev
**Status:** done
**Depends on:** 16 part A (`ci_audit` in `infra/ci/build.sh`, `Tools/editor/audit_map.py`), 15 (`L_DM1` exists)

## Goal
Since `L_DM1` landed, the `ci_audit` step of `infra/ci/build.sh` takes about 15 min on the art map, prints `[audit] LIGHTING FAIL` (expected: unbaked) and then the `UnrealEditor-Cmd` process hangs at exit, so `build.sh` never returns unless someone kills it (task 13b report). It also holds the shared soak lock for that time. After this task the audit of both DM1 maps finishes and `build.sh` returns on its own, in a bounded time.

## Scope — files/modules the agent may touch
- `infra/ci/build.sh` `ci_audit` — e.g. watch the log for the `[VeloxAudit] RESULT` line and terminate that one commandlet process (by its own PID) after a short grace period; a hard timeout with a clear WARNING/FAILED message; keep the fatal/non-fatal rule unchanged
- `Tools/editor/audit_map.py` — only if the cause of the slow run or the hang is in the script (e.g. an editor subsystem left open, a slow per-component pass that can be cached); no change to what is measured or to any budget
- `infra/ci/README.md` — document the timeout

## Acceptance criteria
- [x] Root cause of the hang identified (or stated as unknown with what was tried) — report it.
- [x] `infra/ci/build.sh` on the current head returns on its own with `CI OK`; audit lines for both maps printed; total audit time stated (target: the art-map audit ≤ 5 min if the cause allows, else justify).
- [x] No orphaned `UnrealEditor-Cmd` after the run (`pgrep` count of your own processes 0).
- [x] The audit verdicts (draw calls, triangles, collision diff, protrusion) are identical to `docs/adr/evidence/0006-dm1-art/` audit output.
- [x] Every UE step under the soak lock.

## Notes (done)
- Slow run: the `--protrusion` pass in `audit_map.py` (pure Python, per sample point a scan over all 150 clip boxes plus up to 26 downward probes x 150 boxes) took 15 min of the 15.5 min art-map run (map load, budgets and lighting probe: about 25 s). Now `ClipIndex` (2D spatial hash plus bounding-sphere rejects in front of the same exact box test) does it in about 20 s; art-map audit 44 s. Verdict identical (`_walkable_air_ref` / `_protrusion_depth_ref` are kept and the selftest compares them on a random scene with rotated boxes).
- Hang: not reproduced. A direct run of the same command line on this head exited by itself (`LogExit: Exiting.`) right after RESULT. The 13b "hang" was most likely the 15 min protrusion pass being read as a hang, possibly plus an editor shutdown stall that I could not trigger. Mitigation is in `build.sh` (grace then kill by PID, hard timeout), tested with fake processes (RESULT then sleep; no RESULT then sleep).
- `VELOX_AUDIT_GRACE` (60 s) and `VELOX_AUDIT_TIMEOUT` (1500 s) documented in `infra/ci/README.md`.
