# 07 (H2) — AVeloxHazardVolume, UVeloxHazardSubsystem, L_HazardTest, FtHazard

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H1, P4
**Wave:** hazard track

## Goal
Hazard actor with lava tick and void kill through `MakeDamageSpec` (no armor, no knockback, credited attacker's ASC), `HasHazards()` subsystem, the `VeloxHazard ...` log line, `veloxmap.hazard()`, the `L_HazardTest` fixture map and `FtHazard`.

## Scope — files/modules the agent may touch
- `Source/Velox/Combat/VeloxHazardVolume.*` (new)
- `Source/Velox/Combat/VeloxHazardSubsystem.*` (new)
- `Tools/editor/veloxmap.py` (`hazard()`)
- `Tools/editor/create_hazard_test_map.py` (new)
- `Source/Velox/Tests/`
- `infra/ci/fts.sh`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §3.1, §3.3, §3.7
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `FtHazard` green, all six cases of §3.7 (incl. lava 25 per 0.25 s with death at the computed time and armor untouched; a dummy rocketed into the void gives credit)
- [ ] Default soak/FT list 13/13
- [ ] `FellOutOfWorld` unchanged and never credits anyone
- [ ] `L_HazardTest` committed only with its generator script
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
