# 16c — `build.sh` map audit: exit hang and run time

**Milestone:** M5 First real map + characters
**Agent:** dev
**Status:** todo
**Depends on:** 16 part A (`ci_audit` in `infra/ci/build.sh`, `Tools/editor/audit_map.py`), 15 (`L_DM1` exists)

## Goal
Since `L_DM1` landed, the `ci_audit` step of `infra/ci/build.sh` takes about 15 min on the art map, prints `[audit] LIGHTING FAIL` (expected: unbaked) and then the `UnrealEditor-Cmd` process hangs at exit, so `build.sh` never returns unless someone kills it (task 13b report). It also holds the shared soak lock for that time. After this task the audit of both DM1 maps finishes and `build.sh` returns on its own, in a bounded time.

## Scope — files/modules the agent may touch
- `infra/ci/build.sh` `ci_audit` — e.g. watch the log for the `[VeloxAudit] RESULT` line and terminate that one commandlet process (by its own PID) after a short grace period; a hard timeout with a clear WARNING/FAILED message; keep the fatal/non-fatal rule unchanged
- `Tools/editor/audit_map.py` — only if the cause of the slow run or the hang is in the script (e.g. an editor subsystem left open, a slow per-component pass that can be cached); no change to what is measured or to any budget
- `infra/ci/README.md` — document the timeout

## Acceptance criteria
- [ ] Root cause of the hang identified (or stated as unknown with what was tried) — report it.
- [ ] `infra/ci/build.sh` on the current head returns on its own with `CI OK`; audit lines for both maps printed; total audit time stated (target: the art-map audit ≤ 5 min if the cause allows, else justify).
- [ ] No orphaned `UnrealEditor-Cmd` after the run (`pgrep` count of your own processes 0).
- [ ] The audit verdicts (draw calls, triangles, collision diff, protrusion) are identical to `docs/adr/evidence/0006-dm1-art/` audit output.
- [ ] Every UE step under the soak lock.
