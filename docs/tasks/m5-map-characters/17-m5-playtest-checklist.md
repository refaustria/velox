# 17 — M5 human playtest checklist (Apple Silicon Mac)

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** done
**Depends on:** 15, 16 (map), 08–13 (character, weapons, audio); ADR 0006 §10

## Goal
`docs/playtests/m5-map-characters-checklist.md` lets the human sign off readability, feel, lighting, audio and frame time on the Apple Silicon Mac in under an hour, with the `stat` figures the ADR §8 needs, in the format of the M1–M4 checklists.

## Scope — files/modules the agent may touch
- `docs/playtests/m5-map-characters-checklist.md` (new), `docs/playtests/README.md` if one exists (index)

## Relevant ADRs / references
- ADR 0006 §10 (human criteria), §8 (what to measure: `stat unit`, `stat scenerendering`, `ProfileGPU`), §4.3 (skin forcing default), §9 (audio checks)
- `docs/playtests/m4-bots-checklist.md` (format, PIE setup with a dedicated server and two clients, 100 ms / 2 % emulation)

## Acceptance criteria
- [ ] Setup section: Mac prerequisites (question 26's list), `build-mac.sh`, `git lfs pull`, launching `L_DM1` as dedicated server + 2 clients with `?Bots=3`, `velox.vis.PlaceholderBody 0`, the console commands for stats.
- [ ] Readability table (≥ 10 rows): enemy at 40 m against each room's dominant surface, skin colours told apart at 20 m, forced enemy skin on/off, item silhouettes at 20 m (Mega vs 50 HP vs RA vs YA), view model not occluding the crosshair area, muzzle flash not blinding, pickup glow visible but not dominant.
- [ ] Feel table: strafe lanes C and D at full speed with no catch, all three pads land as in the blockout, both rocket-jump shortcuts reachable, stairs and ramps smooth, crouch under nothing unexpected, no visible seams where clip and kit meet.
- [ ] Lighting table: no black pits, no "lighting needs to be rebuilt", characters lit by the volumetric lightmap (no flat-black skins in shadowed rooms), reflections plausible.
- [ ] Audio table: direction of enemy fire identifiable with eyes closed, footsteps audible at 10 m, own weapon not drowning the enemy's, announcer intelligible, pickups distinguishable (health/armor/weapon).
- [ ] Performance table: `stat unit` and `stat scenerendering` at 1080p from 6 named positions (A ledge, B balcony, C mid, D lower, E top, E ground) recorded as draw calls / triangles / GPU ms, compared against ADR §8; one `ProfileGPU` capture saved.
- [ ] Sign-off block with date, build hash, pass/fail per table, free-text findings.

## Verification steps
1. Markdown lint by eye; links to the ADR sections resolve; the commands exist in the project (`grep` each cvar/console command in `Source/`).

## Report back
Standard format; path of the checklist.

## Notes / outcome
Done 2026-11-01 (dev, docs only): `docs/playtests/m5-map-characters-checklist.md` (51 rows in 7 tables plus sign-off). Commands checked against `Source/Velox` (`velox.vis.PlaceholderBody`, `velox.settings.Dump`, `velox.bots.*`, `velox.combat.*`, `velox.move.LogCorrections`, the "Force enemy skin" settings row; no teleport cheat exists). Bake and ServerDefaultMap are stated as task 16 part B.
