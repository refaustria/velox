# 04 — Headless content bootstrap (maps, input assets, data assets)

**Milestone:** M0 Infrastructure
**Agent:** dev
**Status:** done (2026-10-24)
**Depends on:** 03, ADR 0001, engine install (06)

## Goal
The minimal content the skeleton references exists: `Content/Maps/L_Empty.umap`, `Content/Input/IMC_Default` + `IA_Move/IA_Look/IA_Jump/IA_Fire`, `Content/Data/DA_PawnData_Default`, `Content/Data/DA_MovementSettings_Default`, created by an editor Python script run headlessly so it is reproducible.

## Scope — files/modules the agent may touch
- `Tools/editor/create_bootstrap_content.py` (new)
- `Content/Maps/`, `Content/Input/`, `Content/Data/` (generated assets, LFS)
- `docs/content-bootstrap.md` (new) — how to run it on Linux and on the Mac

## Acceptance criteria
- [ ] `UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=Tools/editor/create_bootstrap_content.py -nullrhi -unattended -nopause` creates all assets listed above and saves them.
- [ ] `L_Empty` has a floor, a `PlayerStart`, a directional light and a sky light with static mobility; the project's default game mode loads it without errors.
- [ ] Data assets reference the classes from task 03 and carry the initial tuning values from the ADR (or Q3-derived defaults documented in the ADR/task).
- [ ] Headless cook of `L_Empty` for the host platform succeeds.
- [ ] Mac fallback documented (same command with `UnrealEditor-Cmd` from the Launcher install path).

## Verification steps
1. Run the command above; `ls Content/Maps Content/Input Content/Data` shows the assets.
2. `git lfs ls-files` lists them.
