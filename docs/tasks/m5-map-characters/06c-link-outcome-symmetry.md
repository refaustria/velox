# 06c — Link outcome symmetry: `air_start` only for foreign flights, `ok`-on-relaunch bounded (review D1, D2)

**Milestone:** M5 First real map + characters
**Agent:** dev-hard
**Status:** done (2026-11-01)
**Depends on:** 06b (merged, f62dd90), `docs/adr/reviews/0006-06b-review.md` (D1, D2), ADR 0006 E0.3 (b), E3

## Goal
The link-success invariant (ADR 0006 §7, E0.3 (b)) counts every traversal the bot chose, success or miss alike. After this task a Drop (or pad / rocket jump) segment that becomes current while the bot is in one of its own **hops** is an ordinary attempt (`ok` or `missed`), `air_start` is returned only when the traversal began inside a **flight** that was not the bot's (a pad launch, a drop, knockback: airborne longer than the bot's own hop), and an external impulse mid-flight finishes `ok` only next to the end (within 2 × `EndAcceptanceCm`), otherwise `relaunched`. The maneuver core stays world-free; unit rows cover every branch; no soak is needed (both clauses fired 0 times in the 06b sets, so no committed verdict changes).

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotManeuver.{h,cpp}` — `FVeloxManeuverInput::bBeganInFlight` (set once at `Begin`, stored as `bBeganInFlight` on the maneuver), the `AirStart` condition, the `Ok`-on-impulse horizontal bound
- `Source/Velox/Bots/VeloxBotPathFollowingComponent.{h,cpp}` — track `AirborneSince` from `Movement->IsOnGround()` in `TickManeuver`/`Tick` (the follower's own copy; do not read the controller's state), compute `HopAirSeconds = 2 × JumpVelocity / |g| + StuckProgressFlightSlackSeconds` exactly as `AVeloxBotController::Navigate` does (move that expression into a shared `VeloxBotRules::HopAirSeconds(JumpVelocity, GravityZ, Slack)` so both call sites use one function), fill `In.bBeganInFlight` at `BeginManeuver`
- `Source/Velox/Bots/VeloxBotController.{h,cpp}` — only the call to the shared `HopAirSeconds` helper (no behaviour change)
- `Source/Velox/Tests/VeloxBotTests.cpp` (`Velox.Bots.Maneuver`), `Source/Velox/Tests/VeloxBotFunctionalTest.cpp` (`Velox.Bots.ControllerRules`: the helper)
- `infra/ci/README.md` — one sentence under "not an attempt": `air_start` = begun in a foreign flight

## Relevant ADRs / references
- `docs/adr/reviews/0006-06b-review.md` D1, D2 (the defects and the fixes, with the unit rows named)
- `docs/adr/0006-first-map-and-characters.md` E0.3 (b), E3 (link rulings), ADR 0005 D28 (not-an-attempt semantics)
- `docs/tasks/m5-map-characters/06b-dm1-bot-defects.md` finding 3 (the rule being corrected)

## Acceptance criteria
- [x] `FVeloxBotManeuver::TickAirborne`: a landing away from End is `AirStart` only when the traversal began in a flight (`bBeganInFlight`) and saw neither a ground tick nor its own launch; a hop-started traversal (airborne ≤ `HopAirSeconds` at `Begin`) landing away is `Missed`; both landing at End are `Ok`.
- [x] A non-own impulse finishes `Ok` only when `DistToEnd <= EndAcceptanceCm`, or (Drop) `HeightAboveEndCm <= EndAcceptanceCm / 2` **and** `DistToEnd <= 2 × EndAcceptanceCm`; otherwise `Relaunched`.
- [x] `VeloxBotRules::HopAirSeconds` exists, is used by the controller's flight test and the follower's `bBeganInFlight`, and a unit row asserts 0.825 s for 675 cm/s, −2000 cm/s², 0.15 s.
- [x] `Velox.Bots.Maneuver` rows: (a) drop, `bBeganInFlight=false`, no ground tick, landing 400 cm off → `Missed`; (b) drop, `bBeganInFlight=true`, landing 400 cm off → `AirStart`; (c) both landing at End → `Ok`; (d) drop, own ground start, impulse at end level 400 cm from End → `Relaunched`; (e) same at 150 cm (acceptance 80) → `Ok`; the existing 06b rows pass unchanged except the row "drop begun in the air, landing away → AirStart", which now sets `bBeganInFlight=true`.
- [x] `infra/ci/build.sh` → `CI OK`, all tests; `infra/ci/fts.sh --port 8201` → `OK 8/8`.
- [x] No soak: the task report states that `air_start` and the end-level `ok` clause have 0 occurrences in `docs/adr/evidence/0006-dm1-blockout-06b/` (grep `reason=air_start` over the server logs and the FFA logs), so no stored verdict changes.

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`; `Velox.Bots.Maneuver` and `Velox.Bots.ControllerRules` listed as passed.
2. `infra/ci/fts.sh --port 8201` → `OK 8/8`.
3. `zcat docs/adr/evidence/0006-dm1-blockout-06b/*/logs/*/*-server.log.gz docs/adr/evidence/0006-dm1-blockout-06b/dm1-ffa/logs/ffa-*.log.gz | grep -c 'reason=air_start'` → `0` (quoted in the report).

## Report back
Standard dev-hard report; name the two conditions as implemented (one line each) and the unit-row names.

## Notes / outcome

**Done 2026-11-01 (dev-hard).**
- D1, `FVeloxBotManeuver::TickAirborne`: a landing away from End is `AirStart` iff `bBeganInFlight && !bGroundSeen && !bLaunchImpulseSeen`, else `Missed`; at End `Ok`. `bBeganInFlight` is `FVeloxManeuverInput::bBeganInFlight`, stored by the new `Begin(Params, Start, End, const FVeloxManeuverInput&)` overload (the `Begin(..., Time)` form clears it).
- D2, non-own impulse: `Ok` iff `DistToEnd <= EndAcceptanceCm` or (Drop) `HeightAboveEndCm <= EndAcceptanceCm / 2 && DistToEnd <= 2 x EndAcceptanceCm`; else `Relaunched`.
- `VeloxBotRules::HopAirSeconds(JumpVelocity, GravityZ, Slack)` used by `AVeloxBotController` (progress rule) and by the follower, which tracks its own `AirborneSince` in `StepFollow` and sets `In.bBeganInFlight = IsFlight(...)` at `BeginManeuver` (begin log line gains `flight=0|1`).
- Unit rows: `06c (a)`..`06c (e)`, Begin store / reset rows, a 60 cm-above-end-level row (`Relaunched`); `ControllerRules`: `HopAirSeconds(675, -2000, 0.15) = 0.825 s`, no-gravity row, 0.3 s / 1.0 s threshold row. The 06b row "drop re-launched at the end level -> Ok" (400 cm off) became row (d) (`Relaunched`) as the review's D2 demands; the AirStart row now sets `bBeganInFlight=true`.
- Verification: `infra/ci/build.sh` -> `CI OK`, 119 tests, 0 not successful (Maneuver, ControllerRules passed); `infra/ci/fts.sh --port 8211` -> `OK 8/8` (port 8211, the agent's assigned range, instead of 8201).
- No soak: `zcat docs/adr/evidence/0006-dm1-blockout-06b/*/logs/*/*-server.log.gz .../dm1-ffa/logs/ffa-*.log.gz | grep -c 'reason=air_start'` -> `0` (566 `VeloxBotLink` lines: ok 487, relaunched 57, missed 14, replaced 5, below_start 3). The end-level `ok` clause is not separately logged (`reason=ok`); the review records 0 occurrences. No stored verdict changes.

