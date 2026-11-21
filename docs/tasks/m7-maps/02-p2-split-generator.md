# 02 (P2) — Split the generator, shim, --golden

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done (engine gate deferred)
**Depends on:** P1
**Wave:** 0 pipeline

## Goal
One generator for all maps: record types move to `maps/records.py`, `L` becomes a parameter everywhere, CLI `create_map.py --map <slug> [--mode] [--validate-only] [--golden check|write] [--selftest]`, `create_dm1_map.py` becomes a shim, `VELOX_DM1_MODE` is still honoured.

## Scope — files/modules the agent may touch
- `Tools/editor/mapgen.py`
- `Tools/editor/create_map.py` (new)
- `Tools/editor/create_dm1_map.py` (becomes a 10-line shim)
- `Tools/editor/maps/records.py` (new)
- `Tools/editor/maps/dm1_layout.py` (import line only)
- `Tools/editor/README.md`
- `infra/ci/build.sh` (golden check before the build)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.1, §1.2, §1.5
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [x] `create_map.py --map dm1 --golden check` byte-identical to P1's golden
- [x] Regenerated DM1 maps: `--diff-collision` empty (0 differences, blocking 150/150, gameplay actors 68/68); committed `.umap` unchanged in git (regenerated files discarded). `--dump-actors` diff vs P1 evidence is NOT empty as-is: 70 lines per map, all extra `mesh=None` NO_COLLISION component lines on the 35 `VeloxPickup` actors (engine gate 2026-11-12, see Notes). Regenerated vs committed `.umap` dumped with the same binaries: 0 diff, so the generator output is unchanged.
- [x] `infra/ci/build.sh` runs the golden check for every map that has a golden file
- [x] Engine run (regeneration) under the soak lock (2026-11-12, UE 5.8.4: `create_map.py --map dm1 --mode blockout` and `--mode art`, both "done")
- [x] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
Pure-Python part done without the engine (/opt/UnrealEngine-5.8 absent). `mapgen.py` = old generator body with `L` as first parameter everywhere (mechanical AST transform; `_Cover` keeps `self.L`), `bind(L)` facade for `golden_dump._api()`; `maps/records.py` holds the record types; `create_map.py` CLI; `create_dm1_map.py` shim; `build.sh` runs `ci_golden_check` before `ci_main`. Checked: `--golden check` OK, `create_dm1_map.py --validate-only` output identical to before, `--mode art --validate-only` plan unchanged (518 instances, 0 protrusions), `--selftest` OK. `unreal`/`veloxmap` imports are guarded. No .umap touched.
Deferred engine gate (lead): regenerate L_DM1_Blockout (`create_map.py --map dm1 --mode blockout`) and L_DM1 (`--mode art`) under the soak lock, dump with `audit_map.py --dump-actors` and diff against docs/adr/evidence/0008-dm1-golden/*.actors.txt (empty), `--diff-collision` empty, committed .umap unchanged. The spawn/build functions of mapgen (build_map, build_art_map, spawn_*) were only transformed, never run.

## Engine gate (lead, 2026-11-12, UE 5.8.4 CL 58902052, reinstalled engine)
- `infra/ci/build.sh`: Editor and Game build Succeeded, 142/142 automation tests passed (incl. Velox.Combat.HazardRules, Velox.Combat.LastAttacker); golden check OK; VeloxServer/VeloxClient SKIPPED (installed engine).
- `create_map.py --map dm1 --mode blockout` and `--mode art` under UnrealEditor-Cmd: both ended with `[VeloxDM1] done`; `audit_map.py --dump-actors`: 224 / 777 actors (same counts as the evidence). `--diff-collision L_DM1_Blockout L_DM1`: 0 differences.
- `diff` against `docs/adr/evidence/0008-dm1-golden/*.actors.txt`: 70 lines each, ALL additions (evidence side has none): one extra `mesh=None mats=[] ... collision=NO_COLLISION` component line per `VeloxPickup` (35 actors x 2 lines). The committed `.umap` files dumped with the current binaries show the identical 70-line diff and `diff committed regenerated` is empty, so the delta comes from the C++ `AVeloxPickup` (changed after the evidence was dumped with older binaries), not from the generator. The evidence files need re-dumping from the current binaries (lead decision); not changed here.
- Regenerated `.umap` files were discarded (checkout of Content); the working tree was clean for Content afterwards.
