# 13 — Gameplay review fixes: pickup reset race, late-joiner take event, ready persistence, intermission for joiners, HasteRun in the soak list

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** done (merged; 74 tests)
**Depends on:** 01, 02, 03, 05 merged on `m3-gameloop`; `docs/reviews/m3-gameplay-review.md`; ADR 0004 C1–C10
**Estimate:** 2 h

## Goal
Close the findings of the architect review of M3-01/02/03/05 (`docs/reviews/m3-gameplay-review.md` §2, F1–F4 and the HasteRun soak-list note). One must-fix (an AI pawn standing in a pickup takes it during `ResetLevel`), three small rule fixes, and the HasteRun soak joins the default list. No design changes; every rule is already written in ADR 0004 (C5, C10) and the review.

## Scope — files/modules the agent may touch
- `Source/Velox/Items/VeloxPickup.cpp/.h` — F1: `Reset()` defers the standing-pawn check to the next tick; F2: stale-take rule in `HandleReplicatedState` (C10).
- `Source/Velox/Combat/VeloxCombatSettings.h/.cpp` — `PickupTakeEventMaxAgeSeconds` (default 1.0 s, cvar/bootstrap field like the other settings); `Tools/editor/create_bootstrap_content.py` only for that field.
- `Source/Velox/GameModes/VeloxGameModeMatch.cpp` — F3: ready persistence (C5); F4: `PostLogin`/`RequestJoin` during PostMatch enter the intermission.
- `Source/Velox/GameModes/VeloxPlayerController.h/.cpp` — F4: `IsInIntermission()` also true while the GameState phase is PostMatch.
- `Source/Velox/Tests/VeloxItemTests.cpp`, `Source/Velox/Tests/VeloxItemFunctionalTest.cpp`, `Source/Velox/Tests/VeloxMatchRulesTest.cpp` — the rows named below.
- `infra/ci/nettest.sh`, `infra/ci/README.md` — HasteRun in the default script list, its log lines documented.
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/reviews/m3-gameplay-review.md` §2 (defects F1–F4, ranked, with file:line) — the specification of this task.
- `docs/adr/0004-game-loop-items-modes-ui.md` C1, C3, C5, C8, C10 (and §1, §2, §5).
- `docs/adr/0002-movement-component-design.md` A37 (context only; no movement change here).

## Acceptance criteria
- [ ] **F1** `AVeloxPickup::Reset()` no longer calls `CheckOverlappingPawns` synchronously; it schedules it for the next tick (`SetTimerForNextTick`), and the timer is cleared in `EndPlay`. Test row (`Velox.Items.Pickup` with a test world, or `FT_ItemPickup`): a pawn with a valid PlayerState/ASC standing inside an available pickup, `GameMode->ResetLevel()` → `TakeCount` unchanged and the item available right after the reset; one tick later a *live* pawn still standing there takes it (the reset's destroyed pawns do not).
- [ ] **F2** On a client, the first replicated update of a placed pickup whose take is older than `PickupTakeEventMaxAgeSeconds` (age = `ServerNow − (RespawnServerTime − RespawnSeconds × ItemRespawnScale)`, respawn 0 = never stale) updates `SeenTakeCount`/`SeenRespawnServerTime` and the placeholder but does **not** broadcast `OnPickupTaken`; a fresh take (age below the threshold) still does. World-free helper `VeloxPickupRules::IsStaleTake(ServerNow, RespawnServerTime, RespawnSeconds, MaxAge)` with unit rows (fresh 0.1 s → false; 5 s → true; respawn 0 → false). Server-side broadcasts unchanged.
- [ ] **F3** `AVeloxGameMode::SetPhase` clears `bReady` only on entering Live, on entering PostMatch and on leaving PostMatch; Warmup ↔ Countdown keeps it in both directions. Headless Duel check (extend the 05 cheat sequence, `velox.match.*`): A and B ready → Countdown; B un-readies → Warmup with A still ready (log `VeloxMatch ready player=A ready=1` not re-sent, `IsReady()` true); B readies → Countdown without A pressing again.
- [ ] **F4** `AVeloxPlayerController::IsInIntermission()` is `bInIntermission || (GameState && GameState->GetPhase() == PostMatch)`; a controller that logs in or joins (`RequestJoin`) during PostMatch receives `EnterIntermission` + `ClientEnterIntermission` with the same view target as the others (`FindIntermissionCamera()`, else its own pawn — which it has none; the camera or null). Headless: a third client connecting during PostMatch logs `VeloxMatch intermission player=`.
- [ ] HasteRun is in `nettest.sh`'s default script list; `infra/ci/README.md` documents its log lines (`VeloxNetSoak event=haste/haste_server`, `VeloxHasteState`, `VeloxPowerupMoveTimes`) and its criteria (ADR 0004 C3).
- [ ] `infra/ci/build.sh` → `[ci] CI OK` (test count reported); `infra/ci/nettest.sh --scripts ItemRun,HasteRun --repeat 3` 3/3 each at the lead-assigned port on an idle machine.

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`, all `Velox.*` tests green, the new rows listed.
2. Headless `L_ItemTest -game -nullrhi` with `velox.items.FtItemPickup quit` (reset row) → PASS.
3. Headless Duel ready sequence (F3) and third-client PostMatch join (F4) with the `velox.match.*` cheats; paste the `VeloxMatch` lines.
4. `infra/ci/nettest.sh --scripts ItemRun,HasteRun --repeat 3 --port <N>` → 3/3 each.

## Standing rules
- The nettest port is assigned by the lead; pass it via `nettest.sh --port <N>`. Do not pick your own. Soak only on an idle machine.
- Unity-build safe names: no file-scope `using namespace`, no duplicate file-local helper names across `.cpp` files.
- Tunables live in settings/data assets, never as magic numbers (`PickupTakeEventMaxAgeSeconds` on `UVeloxCombatSettings`).
- Every change is covered by a test. No commit by the agent. Report ADR disagreements instead of deviating.

## Report back
Standard format: result, what changed, verification (commands + output tail), open issues.

## Notes / outcome
- Why F1 is real: `AGameModeBase::ResetLevel` resets controllers first (`APlayerController::Reset` unpossesses; `AController::Reset`/`AAIController::Reset` do not — Controller.cpp:442, AIController.cpp:106), then every actor in iteration order; `APawn::Reset` (Pawn.cpp:934) destroys a pawn only when its own turn comes. A dummy or M4 bot whose pawn is iterated after the pickup passes `TryPickup` and the item is down for a full respawn at Live start.
- F2 is harmless for the current HUD (nothing subscribes to `OnPickupTaken` on clients yet); it matters for 08's "took" messages and spectator feed.

## Added by the lead (2026-10-29)
- **F5 player name**: the server ignores `?Name=` (`UPendingNetGame` overrides it with `ULocalPlayer::GetNickname()`, M3-10 report). Add a `ULocalPlayer` subclass overriding `GetGameLoginOptions()` → `Name=<UVeloxGameUserSettings PlayerName>` (sanitised), set `LocalPlayerClassName` in `DefaultEngine.ini`; verify on a headless server that `VeloxKill`/scoreboard show the configured name.
- **F6 stale comments**: "bucket + spacing" remnants in `Weapons/VeloxGameplayAbility_Fire.h` (l.7/65/91), `VeloxGameplayAbility_FireHitscan.cpp:250`, `VeloxGameplayAbility_FireHitscan.h:9`, `VeloxGameplayAbility_FireProjectile.cpp:243` → B32 wording (comments only; 08h may touch these files — keep hunks comment-only).

## Outcome (2026-10-29)
build.sh 74 tests (+ `Velox.Items.PickupReset`), 0 failures (`20261029-224521-4031786`, worktree). F1 reset re-check on next tick (test); F2 `IsStaleTake` + `PickupTakeEventMaxAgeSeconds` 1 s (unit rows); F3 headless Duel: ready survives Countdown→Warmup abort; F4 PostMatch joiner gets `VeloxMatch intermission` (`IsInIntermission()` also checks the phase); F5 server log `Login request: ?Name=Anarki` via `UVeloxLocalPlayer::GetGameLoginOptions` (`LocalPlayerClassName`); F6 B32 comments; HasteRun documented in `infra/ci/README.md`. ItemRun + HasteRun 3/3 under load. Extra: `velox.match.Ready [0|1] [idx=N|all]`.
