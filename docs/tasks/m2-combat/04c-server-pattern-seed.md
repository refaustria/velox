# 04c — Server-authoritative pattern seed and server-only impact cues

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** done (merged; report in notes/04c-report.md)
**Depends on:** 04, 04b (review `docs/reviews/m2-04-weapons-hitscan-review.md` defect 4, ADR 0003 Amendment B21). Must land before 09; 05 adopts the unkeyed impact-cue rule from the start, 08 does not depend on it.

## Goal
Close the pattern-selection exploit: today the server seeds the MG spread and the SG pellet pattern from the client-supplied `ShotSeq` (`FRandomStream(ShotSeq ^ PlayerId)`), so a modified client can pick per shot the seed whose pattern best covers the target (spread-free MG, 11-of-11 SG). After this task the server draws the seed from its own per-PlayerState counter, carries it to clients in the Velox effect context of the impact cue, and impact cues are executed by the server only (unkeyed, so the owner receives them ≈ RTT late at the true location, as in Q3); the owner predicts the fire cue only.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/VeloxWeaponInventoryComponent.h/.cpp` — `uint8 AllocateServerPatternSeq()` (server-side counter, not replicated)
- `Source/Velox/Weapons/VeloxGameplayAbility_FireHitscan.h/.cpp` — `ResolveServerShot` seeds from `MakeSeed(Inventory->AllocateServerPatternSeq(), PlayerId)`; impact cue executed inside `FScopedPredictionWindow(ASC, FPredictionKey(), false)` (fire cue stays keyed); `PlayLocalShotCosmetics` draws the fire cue with the centre ray only and no impact cues (remove the per-pellet loop and the `VeloxShotDirs::Build` call on the client); `MakeCueParameters` writes `PatternSeq`
- `Source/Velox/Combat/VeloxGameplayEffectContext.h/.cpp` — `uint8 PatternSeq` field, `NetSerialize` (one byte), `Duplicate`
- `Source/Velox/Weapons/VeloxShotTargetData.h` — comment only: `ShotSeq` names the shot in logs and matches fake/real projectiles; it never seeds anything on the server
- `Source/Velox/Weapons/VeloxCombatFeedbackComponent.cpp` — none required (one impact sphere at the cue location as today); optional: log `PatternSeq` at Verbose
- `Source/Velox/Tests/VeloxWeaponTests.cpp`, `Source/Velox/Tests/VeloxCombatTests.cpp` (context serialisation test if one exists; else add the round trip to `TargetDataSerialize`)
- `docs/content-bootstrap.md` — no change expected

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` — §5, Amendment B21 (seed, cue policy), B23 (beam cue unchanged)
- `docs/reviews/m2-04-weapons-hitscan-review.md` — §2 defect 4
- Engine: `AbilitySystemComponent_Abilities.cpp` l.4007–4030 (the target-data RPC runs inside the client's prediction window — hence the explicit unkeyed window for the impact cue), `GameplayPrediction.cpp` l.387–406 (server-side `FScopedPredictionWindow` with an explicit key)

## Acceptance criteria
- [ ] `Velox.Combat.HitscanDummy` extended: two SG shots with different client `ShotSeq` but the same server counter state produce identical `Request.Dirs` (expose the last request's directions on `FVeloxServerShotInfo` or compare `PelletHits`-independent ray ends logged by the test); the server counter advances by one per accepted shot and not on a rejected one.
- [ ] `VeloxShotDirs::Build` is unchanged and still deterministic for a seed (existing `HitMath` pellet determinism test green).
- [ ] `FVeloxGameplayEffectContext` round-trips `PatternSeq` through `NetSerialize` (test).
- [ ] The owner's client executes no impact cue in `PlayLocalShotCosmetics`; the server's impact cue is executed with an invalid prediction key (unit-check via a listen-world test that the shooter's own `UVeloxCombatFeedbackComponent` sees exactly one `.Impact` cue per shot — in a single game world the server-local path already receives its own cues; assert count == 1 for the RG shot, not 2).
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected.

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`.
2. Headless listen run: `velox.combat.FireTest SG` → server `VeloxShot` line shows `rays=11`; the shooter's feedback log shows one `.Fire` and one `.Impact` cue.

## Report back
Standard dev-hard format. State explicitly how `PatternSeq` reaches a receiver and what M5 needs to rebuild the SG pattern (origin, `HitDirection`, `PatternSeq`, weapon definition by cue tag).

## Notes / outcome
(Filled by the agent or the lead after completion.)
