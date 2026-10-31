# 18 — Architect design review of tasks 08–10 (cosmetics, view model, audio hooks)

**Milestone:** M5 Map + characters
**Agent:** architect
**Status:** todo
**Depends on:** 08, 09, 10 merged to the M5 branch

## Goal
One review pass over the code that touched the replicated pawn, the PlayerState, the cue handler and the pickup path, before art lands on top of it: defects ranked by severity with file/function and fix, amendments to ADR 0006 as E-items where the implementation taught something.

## Scope — files/modules the agent may touch
- `docs/adr/0006-first-map-and-characters.md` (amendments), `docs/adr/reviews/0006-cosmetics-review.md` (new)

## Relevant ADRs / references
- ADR 0006 §1 (soft refs, server never loads), §4.3 (SkinIndex, forcing), §4.5–4.6, §9 (hooks); ADR 0003 §6/§10 (hitbox untouched, placeholder body), ADR 0004 §1 (pickup cosmetics path), ADR 0002 (no movement changes)

## Review checklist (what the pass must answer)
- [ ] Does any code path load a cosmetic asset on the dedicated server (grep for `LoadSynchronous`, `TryLoad`, `StaticLoadObject` in the new files; the `CosmeticsSoftRefs` test cannot catch a sync load)?
- [ ] `SkinIndex` rename: replication unchanged (`DOREPLIFETIME_WITH_PARAMS_FAST`, push model), assignment order and HUD colours stable, corpses keep the skin.
- [ ] Anim instance: no per-frame allocations, proxies use the smoothed velocity, `bFiring` cleared each frame, sequence playback does not restart the same sequence on repeated state.
- [ ] View model: created only for the local pawn, destroyed on unpossess/death, no effect on the camera transform the server validates (B-series eye-origin rules in ADR 0003), recoil does not move the aim.
- [ ] Audio: the own-weapon 2D path never double-plays with the cue's 3D path; `MulticastLaunchCue` dedupe against the predicted launch; footstep cadence identical for bots; announcer rate limits; no audio in `Movement/`.
- [ ] Settings: `bForceEnemySkin` applied on late-joining pawns and on skin changes.
- [ ] Tests: the four new unit tests test rules, not constants.

## Report back
Defects ranked (severity, file/function, fix), E-amendments written, tasks recommended (title + acceptance criterion each).

## Notes / outcome
