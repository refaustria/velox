# 18 — Architect design review of tasks 08–10 (cosmetics, view model, audio hooks)

**Milestone:** M5 Map + characters
**Agent:** architect
**Status:** done
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

Done (architect, 2026-10-31, reading review of be73053). **Verdict: APPROVE WITH FOLLOW-UPS** — `docs/adr/reviews/0006-cosmetics-review.md`; rulings recorded as ADR 0006 **E1**. Checklist: no cosmetic load reachable on the dedicated server (path by path); `SkinIndex` replication/order/colours/corpses correct; anim instance clean (no allocations, one-frame `bFiring`, no sequence restart); view model local-only, destroyed on death, camera transform untouched; pickup-sound prediction per ADR 0004 §1; jump-pad dedupe and bandwidth fine; shared files merged semantically; tests test rules. Defects: **D1 High** — every "own pawn" audio test uses `IsLocallyControlled()`, true for bots on a listen server/standalone (bot fire/pain/death/pad play 2D in the host's ears); **D2 Medium** — simulated proxies never fire `Landed`, so other players' landings are silent on clients; **D3 Medium** — dry fire unhooked (`AbilityFailedCallbacks` + `ActivateFailCostTag` is the hook); **D4 Medium** — weapon sounds duplicated between `UVeloxAudioSettings::CueSounds` and `UVeloxWeaponCosmetics`, weapon meshes `LoadSynchronous` at swaps; D5–D7 Low (corpse-skin guard, announcer name matching, settings rows + UI click). Follow-ups: `18b-cosmetics-review-fixes.md` (D1–D3, D5–D7, dev; before the Mac playtest) and `18c-weapon-cosmetics-consolidation.md` (D4, dev; before task 13). Rocket light deferred to task 13.
