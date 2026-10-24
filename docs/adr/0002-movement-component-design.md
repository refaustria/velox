# ADR 0002 — Movement component design (M1)

**Status:** Accepted (2026-10-24, project lead) (architect, 2026-10-24) → accepted by the lead when the M1 task files are written.
**Scope:** everything `dev-hard` needs to turn the M0 skeleton (`Source/Velox/Movement/*`, `Character/VeloxCharacter.*`) into Quake-feel, predicted, tunable movement. Builds on ADR 0001 §3 (saved-move path, `FVeloxMoveSim`, `AddKnockback`); does not replace the skeleton. Weapon knockback sources and self-knockback prediction are M2 (next ADR); bots are M4 but §8 fixes what they will drive.
**Engine facts verified against UE 5.8.4 source** (`CharacterMovementComponent.{h,cpp}`, `Character.cpp`, `CharacterMovementReplication.h`, `GameNetworkManager.cpp`); line references below are to that cpp.

## Context

CLAUDE.md non-negotiable #1: strafe jumping, bunny hopping with no landing speed loss, Q3 friction, rocket jumps, jump pads, ramp sliding, as a `UCharacterMovementComponent` (CMC) subclass. ROADMAP M1 adds: all tunables in `UVeloxMovementSettings`, full prediction with no visible jitter at 100 ms / 2 % loss, `L_MovementTest`, automation tests, human sign-off.

What the stock CMC does that fights Q3 feel (read from source): `CalcVelocity` (l.3863) applies braking only when input is zero and clamps speed to `MaxSpeed` every tick (kills strafe-jump gain); `GetFallingLateralAcceleration` (l.4827) scales input by `AirControl` and `PhysFalling` re-runs `CalcVelocity` to limit air control on wall hits (l.4887ff); `HandleSlopeBoosting` (l.3749) forbids gaining height when sliding on a ramp in the air; `ProcessLanded` (l.6373) → `SetPostLandedPhysics` → `StartNewPhysics` → `PhysWalking` applies ground friction in the landing frame and a jump happens only in the *next* tick's `CheckJumpInput`; `PhysWalking` recomputes `Velocity = (NewLoc − OldLoc)/dt` after each iteration (l.5873) and `StepUp` leaves `bJustTeleported=false` with `bMaintainHorizontalGroundVelocity=true` (l.7778), so a partially blocked step-up loses speed; a ramp's vertical velocity is discarded by `MaintainHorizontalGroundVelocity` (l.5635) and the pawn is snapped to the floor on ramp exits (`ShouldCatchAir` l.6282 is `false` by default). Everything else (floor finding, stepping, substepping `MaxSimulationTimeStep`, saved moves, packed RPCs, corrections) is exactly what we want to keep.

## Decision

### 1. Physics model: Q3 vanilla core, CPM features as toggles, fixed-step velocity integration

Formulas (Q3 `bg_pmove.c`), all in UE units (cm, s), horizontal unless noted. `WishDir` = unit XY, `WishSpeed` ∈ [0, MaxGroundSpeed] (analog scale):

```
Friction(V, dt):   s=|V_xy|; if s<ε → V_xy=0; control=max(s, StopSpeed); drop=control·GroundFriction·dt;
                   V_xy *= max(s−drop, 0)/s                                         (skipped while KnockbackGrace>0)
Accelerate(V, WishDir, WishSpeed, Accel, dt):  cur=V·WishDir; add=WishSpeed−cur; if add≤0 return;
                   a=min(Accel·dt·WishSpeed, add); V += a·WishDir        (the dot-product clamp = strafe jumping)
Ground:            Friction; Accelerate(GroundAcceleration, WishSpeed)
Air (vanilla):     Accelerate(AirAcceleration, min(WishSpeed, AirMaxWishSpeed))
Air (CPM, bAirControl): accel = (V·WishDir<0) ? AirStopAcceleration : AirAcceleration;
                   if side-only input: WishSpeed=min(WishSpeed, AirStrafeMaxWishSpeed), accel=AirStrafeAcceleration;
                   Accelerate(...); if forward/back-only input: AirControl():
                     z=V.z; V.z=0; s=|V|; V/=s; dot=V·WishDir; k=AirControlScale·AirControl·dot²·dt;
                     if dot>0: V = normalize(V·s + WishDir·k);  V*=s; V.z=z         (turns without speed change)
Jump:              V.z = JumpVelocity (set, not max); mode=Falling.  Auto-hop: jump-held on landing → jump in the
                   landing frame before any friction (Q3 PM_WalkMove checks jump before PM_Friction).
Gravity:           V.z += GravityZ·dt (engine midpoint integration, apex substep kept)
Ramp slide:        landing hit with V.z>0 and V·N > RampSlideMinNormalSpeed is not a landing → slide (Q3 PM_GroundTrace)
Ramp exit:         leaving a ramp upward keeps the ramp's vertical velocity (Q3 clips V to the ground plane, so V.z>0)
ClipVelocity:      V − N·(V·N)  (UE VectorPlaneProject; Q3 OVERCLIP 1.001 is unnecessary with swept moves)
Knockback:         V += Impulse (or V = Impulse if override); KnockbackGrace = clamp(|Impulse|·KnockbackGracePerSpeed,
                   KnockbackGraceMin, KnockbackGraceMax); ground friction off while >0 (Q3 PMF_TIME_KNOCKBACK)
```

**Fixed-step integration.** Velocity updates (friction, accelerate, air control) run in substeps of `FixedStep` (default 1/125 s = Q3 `pmove_fixed 1, pmove_msec 8`): `n = ceil(dt/FixedStep)`, each substep `dt/n`. Position integration stays per move (CMC). Rationale: the strafe gain per second otherwise depends on the client frame rate (the Q3 125 fps advantage); with substeps a 60 fps and a 240 fps client gain the same. `bFixedStepIntegration=false` turns it off for A/B tests. Determinism (ADR 0001 §4) only requires identical inputs and dt per move, which the server replays anyway.

**Units.** `UnitScale = 2.5 cm per Q3 unit` (Q3 player 56 u → 140 cm, speed 320 → 800 cm/s, step 18 → 45 cm = UE default `MaxStepHeight`). Round, keeps numbers close to UE defaults, maps built from scratch. Alternatives (2.54 "inch", 3.2 "180 cm human") in Alternatives. Assets store UE units; `UnitScale` is for HUD display (ups) and the `velox.move.Preset` helper only.

**Initial tuning table** (`DA_MovementSettings_Default`; Q3 value → UE). Fields marked * are new or renamed in `UVeloxMovementSettings`; `RampSlideMaxAngle` is renamed to `WalkableFloorAngle`.

| Field | Q3 / CPM | UE default | Note |
|---|---|---|---|
| `UnitScale` | — | 2.5 | cm per Q3 unit |
| `MaxGroundSpeed` | g_speed 320 | 800 | also `MaxWalkSpeed`, `MaxAcceleration` (§2 encoding) |
| `GroundAcceleration` | pm_accelerate 10 (CPM 15) | 10 | 1/s |
| `GroundFriction` | pm_friction 6 | 6 | 1/s |
| `StopSpeed`* | pm_stopspeed 100 | 250 | cm/s |
| `AirAcceleration` | pm_airaccelerate 1 | 1 | 1/s |
| `AirMaxWishSpeed` | = g_speed | 800 | vanilla air wish cap |
| `bAirControl`* | CPM on / VQ3 off | false | enables the three CPM rules below (open question) |
| `AirStopAcceleration`* | pm_airstopaccelerate 2.5 | 2.5 | |
| `AirStrafeAcceleration`* | pm_strafeaccelerate 70 | 70 | |
| `AirStrafeMaxWishSpeed`* | pm_wishspeed 30 | 75 | cm/s |
| `AirControl`*, `AirControlScale`* | pm_aircontrol 150, k 32 | 150, 32 | |
| `JumpVelocity` | JUMP_VELOCITY 270 | 675 | cm/s |
| `bAutoHop`* | CPM/defrag | true | jump while held re-jumps on landing |
| `GravityZ` | g_gravity 800 | −2000 | cm/s² |
| `StepHeight` | STEPSIZE 18 | 45 | → `MaxStepHeight` |
| `WalkableFloorAngle`* | MIN_WALK_NORMAL 0.7 | 45.5° | → `SetWalkableFloorAngle` |
| `RampSlideMinNormalSpeed`* | 10 ups | 25 | cm/s, see §2 `IsValidLandingSpot` |
| `RampExitMinAngle`*, `bRampExitCatchAir`* | implicit | 5°, true | floors steeper than this are "ramps" |
| `EdgePerchRadius`* | AABB edges | 15 | → `PerchRadiusThreshold` (capsule stands on edges like a box) |
| `CrouchSpeedScale`* | pm_duckScale 0.25 | 0.25 | engine crouch kept; no crouch-slide |
| `KnockbackGraceMin/Max`*, `KnockbackGracePerSpeed`* | 50–200 ms, 2 ms/knockback | 0.05, 0.2, 0.00016 s per cm/s | Q3: 100 dmg rocket ≈ 1250 cm/s → 200 ms |
| `FixedStep`*, `bFixedStepIntegration`* | pmove_msec 8 | 0.008, true | |
| `bCombineAirMoves`* | — | false | §5 |
| `TerminalVelocity` (project) | none | 10000 | `[/Script/Engine.PhysicsSettings] DefaultTerminalVelocity`; engine default 4000 would cap long falls |

Pawn geometry goes into `UVeloxPawnData` (same asset on both ends, applied in `PostInitializeComponents`): `CapsuleRadius 37.5`, `CapsuleHalfHeight 70` (Q3 box 30×30×56), `EyeHeight 55` above capsule centre (Q3 viewheight 26 + origin 24 = 50 u above feet). Character: `bUseControllerRotationYaw=true`, CMC `bOrientRotationToMovement=false`, `bUseControllerDesiredRotation=false` (FPS yaw from the controller, pitch never applied to the capsule). Swimming/flying disabled (`NavAgentProps.bCanSwim/bCanFly=false`); `MOVE_Flying` reserved for spectator/noclip later.

### 2. Where in the CMC: velocity hooks, not phys-function rewrites

`PhysWalking`/`PhysFalling`/`MoveAlongFloor`/`FindFloor`/`StepUp` core/`SlideAlongSurface`/`ComputeSlideVector` stay engine. `ApplyVelocityBraking` becomes dead code (never called from our `CalcVelocity`). Overrides, all in `UVeloxMovementComponent`:

| Virtual (5.8 signature) | Velox behaviour | Why |
|---|---|---|
| `CalcVelocity(float Dt, float Friction, bool bFluid, float BrakingDecel)` | ignore params; `IsMovingOnGround()` → `Velocity = Sim::GroundMove(...)`; `IsFalling()` → `Velocity.xy = Sim::AirMove(...)` (Z untouched; `PhysFalling` zeroes/restores Z around the call); else `Super` | single velocity-integration point for both phys modes; bypasses braking, max-speed clamp, RVO, `RequestedVelocity` |
| `GetMaxSpeed()` | Walking/Falling: `MaxGroundSpeed × (IsCrouching() ? CrouchSpeedScale : 1)`; else `Super` | |
| `GetMaxAcceleration()` | `MaxGroundSpeed` | `ScaleInputAcceleration` then makes `Acceleration` **= Q3 wishvel** (dir × wishspeed, cm/s); `RoundAcceleration` quantises to 0.1 (`QuantizeVector(10)`), identical on both ends; `MoveAutonomous` clamps to it on the server (l.9980) |
| `GetMaxBrakingDeceleration()` | 0 | unused |
| `GetGravityZ()` | `Settings->GravityZ` | ignores volume gravity and `GravityScale` (ADR 0001) |
| `GetFallingLateralAcceleration(float Dt)` | `ProjectToGravityFloor(Acceleration)` | no `AirControl` scaling/boost/clamp |
| `ShouldLimitAirControl(...)` | `false` | wall hits in the air become plain `ClipVelocity` (skips the `VelocityNoAirControl` re-simulation) |
| `HandleSlopeBoosting(...)` | return `SlideResult` | airborne ramp slides may gain height (Q3 PM_ClipVelocity) |
| `IsValidLandingSpot(const FVector&, const FHitResult& Hit)` | `false` if `Velocity.Z > 0 && (Velocity | Hit.ImpactNormal) > RampSlideMinNormalSpeed`; else `Super` | ramp sliding; also covers the `ShouldCheckForValidLandingSpot` floor path (same virtual) |
| `ProcessLanded(const FHitResult& Hit, float Remaining, int32 Iterations)` | if `bAutoHop && bWantsJumpHeld && CanAttemptJumpAfterLanding()`: `CharacterOwner->Landed(Hit)`, `SetPostLandedPhysics(Hit)`, `DoJump(CharacterOwner->bClientUpdating, 0)`, `CharacterOwner->OnJumped()`, `StartNewPhysics(Remaining, Iterations)`; else `Super` | zero-friction landing frame; remainder of the tick continues in `PhysFalling` |
| `DoJump(bool bReplaying, float Dt)` | `Velocity.Z = JumpVelocity; SetMovementMode(MOVE_Falling); return true` when `CharacterOwner->CanJump()` | Q3 sets Z; engine `JumpZVelocity`/`bDontFallBelowJumpZVelocityDuringJump` ignored. Character defaults `JumpMaxHoldTime=0`, `JumpMaxCount=1` |
| `CanAttemptJump()` | `IsJumpAllowed() && !bWantsToCrouch && IsMovingOnGround()` | no air jumps |
| `MaintainHorizontalGroundVelocity()` | cache `RampExitVelocityZ = Velocity.Z` if `CurrentFloor` normal.Z < cos(RampExitMinAngle) and Z > 0, else 0; then `Super` | `Velocity.Z` here is the measured climb rate along the ramp |
| `ShouldCatchAir(const FFindFloorResult& Old, const FFindFloorResult& New)` | `bRampExitCatchAir && RampExitVelocityZ > 0 && Old is ramp && New.ImpactNormal.Z > Old.ImpactNormal.Z + 0.01` | leave the ground at the top of a ramp instead of snapping down |
| `StartFalling(int32, float, float, const FVector&, const FVector&)` | if `RampExitVelocityZ > 0`: `Velocity.Z = RampExitVelocityZ`; clear; `Super` | carries ramp Z into `PhysFalling` for both the catch-air and the walked-off-edge paths |
| `StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult& Hit, FStepDownResult* Out)` | `Super`; on success `bJustTeleported = true` | skips the displacement-based velocity recompute (l.5873) → no speed loss on stairs |
| `PerformMovement(float Dt)` | `ConsumePendingKnockback()` (§4); `Super`; `KnockbackGrace = max(0, KnockbackGrace − Dt)` | same slot as the engine's `ApplyAccumulatedForces`/`HandlePendingLaunch` |
| `OnMovementModeChanged(...)` | `Super`; `RampExitVelocityZ = 0` | |
| `UpdateFromCompressedFlags(uint8)` | existing + `FLAG_Custom_1/2` (§5) | |
| `OnClientCorrectionReceived(...)` | read `FVeloxMoveResponseDataContainer` (§4) then `Super` | |
| `DisplayDebug(UCanvas*, const FDebugDisplayInfo&, float&, float&)` | Velox lines under `showdebug VeloxMovement` (§6) | |

`SetMovementSettings()` (exists) additionally applies engine fields: `MaxWalkSpeed = MaxAcceleration = MaxGroundSpeed`, `MaxStepHeight = StepHeight`, `SetWalkableFloorAngle(WalkableFloorAngle)`, `PerchRadiusThreshold = EdgePerchRadius`, `JumpZVelocity = JumpVelocity` (for engine readers), `bUseAccelerationForPaths = true` (§8), `bEnablePhysicsInteraction = false`, `GravityScale = 1`. Engine defaults kept: `bMaintainHorizontalGroundVelocity = true` (ramps preserve horizontal speed; `ComputeGroundMovementDelta` keeps XY), `MaxSimulationTimeStep 0.05`, `MaxSimulationIterations 8`, `bCanWalkOffLedges`. **Never call** `Launch()`/`LaunchCharacter()`/`AddImpulse()`; all external velocity goes through `AddKnockback` (§4).

Inputs: wish direction = `Acceleration` (engine); jump-held = `bWantsJumpHeld` (exists); new: `SetLocalMoveInput(FVector2D ForwardRight)` called from `AVeloxCharacter::Input_Move` (and bots) each frame, giving `bHasForwardInput`/`bHasSideInput` (|component| > 0.1) for the CPM rules; crouch = engine `bWantsToCrouch`. No other input.

### 3. World-free simulation core `FVeloxMoveSim`

`Movement/VeloxMoveSim.h/.cpp`: includes only `CoreMinimal.h`; no UObject, no world, no logging. Pure functions over plain structs; the CMC calls them from `CalcVelocity`/`ProcessLanded`/`ConsumePendingKnockback`, tests call them directly.

```cpp
struct FVeloxMoveParams { /* POD copy of every UVeloxMovementSettings number above, built per move by
    UVeloxMovementSettings::BuildSimParams() which also applies velox.move.* cvar overrides (non-shipping) */ };
struct FVeloxMoveInput  { FVector WishDir = FVector::ZeroVector; /* unit XY or zero */ float WishSpeed = 0.f;
    bool bHasForwardInput = false, bHasSideInput = false, bJumpHeld = false; };
struct FVeloxMoveState  { FVector Velocity = FVector::ZeroVector; bool bOnGround = false; float KnockbackGrace = 0.f; };
namespace VeloxMoveSim {
  int32   NumSubsteps(float Dt, const FVeloxMoveParams& P);                                   // 1 if fixed step off
  FVector ApplyFriction(const FVector& V, float Dt, const FVeloxMoveParams& P);               // XY only
  FVector Accelerate(const FVector& V, const FVector& WishDir, float WishSpeed, float Accel, float Dt);
  FVector AirControl(const FVector& V, const FVector& WishDir, float Dt, const FVeloxMoveParams& P);
  FVector GroundMove(const FVector& V, const FVeloxMoveInput& In, float Dt, float KnockbackGrace, const FVeloxMoveParams& P); // substepped
  FVector AirMove(const FVector& V, const FVeloxMoveInput& In, float Dt, const FVeloxMoveParams& P);                         // substepped, no gravity
  FVector ApplyGravity(const FVector& V, float Dt, const FVeloxMoveParams& P);               // tests/bots only; CMC uses NewFallVelocity
  FVector Jump(const FVector& V, const FVeloxMoveParams& P);                                 // Z = JumpVelocity
  FVector ClipVelocity(const FVector& V, const FVector& Normal);
  FVector ApplyKnockback(const FVector& V, const FVector& Impulse, bool bOverride);
  float   KnockbackGraceFor(const FVector& Impulse, const FVeloxMoveParams& P);
  FVector JumpPadLaunchVelocity(const FVector& From, const FVector& Target, float GravityZ);  // Q3 AimAtTarget
  void    StepFrame(FVeloxMoveState& S, const FVeloxMoveInput& In, float Dt, const FVeloxMoveParams& P); // reference frame:
          // ground: GroundMove, then Jump if bJumpHeld; air: AirMove + ApplyGravity. No collision. Tests and bot planning only.
}
```
`StepFrame` is a reference model, not what the CMC executes (the CMC adds sweeps, floor logic, apex substep); the determinism and feel tests run on it, the CMC-level determinism test (§7) covers the rest.

### 4. Knockback and jump pads through the predicted path

`UVeloxMovementComponent::AddKnockback(FVector Impulse, bool bOverrideVelocity)` (ADR 0001) accumulates into `FVeloxPendingKnockback { FVector Velocity; bool bOverride; }` (own field, not the engine's `PendingImpulseToApply`/`PendingLaunchVelocity`, so it can be saved, replayed and sent in corrections). `ConsumePendingKnockback()` at the start of `PerformMovement`: `Velocity = Sim::ApplyKnockback(...)`; `KnockbackGrace = max(KnockbackGrace, Sim::KnockbackGraceFor(...))`; if `IsMovingOnGround() && (Velocity | CurrentFloor.HitResult.ImpactNormal) > RampSlideMinNormalSpeed` → `SetMovementMode(MOVE_Falling)`; `bForceNextFloorCheck = true`; clear pending. Callable on any role; the authority decides what is legitimate:

- **Server-side impulses (rockets, M2):** server calls `AddKnockback` in the explosion; the owning client learns of it through the standard correction (`ServerCheckClientError` fails at the next move → `ClientAdjustPosition`). To keep the replay consistent, the correction carries Velox state: `FVeloxMoveResponseDataContainer : FCharacterMoveResponseDataContainer` (installed with `SetMoveResponseDataContainer` in the constructor) overrides `ServerFillResponseData(const UCharacterMovementComponent&, const FClientAdjustment&)` to copy `KnockbackGrace` and any unconsumed pending knockback, and `Serialize(UCharacterMovementComponent&, FArchive&, UPackageMap*)` with `SerializeOptionalValue` (1 bit when zero). `OnClientCorrectionReceived` applies them before `ClientUpdatePositionAfterServerUpdate` replays. Without this the client replays with friction on while the server skips it for up to 200 ms → a correction every `NetworkMinTimeBetweenClientAdjustments` after every hit.
- **Jump pads (predicted, no replication):** `AVeloxJumpPad : AActor`, map-placed, not replicated, `UBoxComponent` trigger (`Overlap` with Pawn only) + editor-only arrow/mesh. Data: `EVeloxJumpPadMode { AimAtTarget, FixedVelocity }`, `TObjectPtr<AActor> Target` (any actor, usually `ATargetPoint`), `FVector LaunchVelocity`, `bool bOverrideVelocity = true` (Q3 trigger_push sets velocity; `false` adds). `OnComponentBeginOverlap` on both server and owning client: `MoveComp->AddKnockback(ComputeLaunchVelocity(MoveComp->GetGravityZ()), bOverrideVelocity)`; `ComputeLaunchVelocity` is `Sim::JumpPadLaunchVelocity(TriggerCentre, TargetLocation, GravityZ)` for `AimAtTarget`. Deterministic: same actor data, same gravity from the character's settings, same overlap during the same move on both sides; override semantics make a re-trigger during replay idempotent. One-move latency (consumed at the next `PerformMovement`) is accepted; simulated proxies get the result via replication. Nav-link metadata for bots is M4.
- **Rocket-jump self-knockback prediction** (client-side `AddKnockback` when the fake projectile detonates) is specified in the M2 ADR; the plumbing above already supports it. M1 ships a non-shipping console command `velox.move.TestKnockback X Y Z [override]` (server-executed in PIE) to exercise the path.

### 5. Prediction, correction, validation

`FSavedMove_Velox` gains: `SavedPendingKnockback` (captured in `SetMoveFor` **before** the move consumes it — `ReplicateMoveToServer` calls `SetMoveFor` before `PerformMovement`, l.8961/9024 — and restored in `PrepMoveFor`), `SavedKnockbackGrace` (start-of-move value), `SavedRampExitVelocityZ`, `bSavedHasForwardInput`, `bSavedHasSideInput`. Compressed flags: `FLAG_Custom_0` jump held (exists), `FLAG_Custom_1` has-forward-input, `FLAG_Custom_2` has-side-input, `FLAG_Custom_3` reserved. `Clear()` resets all. `CanCombineWith` returns `false` when: jump-held differs (exists); either move has a pending knockback; either `SavedKnockbackGrace > 0`; either `StartPackedMovementMode` is Falling and `!bCombineAirMoves`; input-presence flags differ; then `Super`. Rationale for not combining air moves: combining re-simulates the pending move with a longer dt from its start location, visibly re-placing the player mid-jump; the cost is more RPCs for clients above 60 fps (the packed RPC carries pending + new move, ≈ 40 B each; at 240 fps ≈ 5 KB/s up, 16 players ≈ 80 KB/s on the server). `IsImportantMove` stays engine.

`FVeloxNetworkMoveData`: **no new serialised fields in M1**. `Acceleration` (wishvel), `CompressedMoveFlags` and `ControlRotation` express every input; the move-data subclass stays for the day something does not fit (e.g. a per-move weapon-fire timestamp in M2).

Server validation: the client never sends speed or position intent, only wishvel + flags; the server simulates, so no speed cap is needed beyond what the engine does (`MoveAutonomous` clamps `Acceleration` to `GetMaxAcceleration()`; `GetServerMoveDeltaTime` clamps dt to `MaxMoveDeltaTime × p.NetServerMaxMoveDeltaTimeScalar`). Enable the engine's time-discrepancy (speedhack) guard. `Config/DefaultGame.ini` (the `GameNetworkManager` block lives there because `AGameNetworkManager` is `config=Game`; extend it with the time-discrepancy settings below):
```ini
[/Script/Engine.GameNetworkManager]
ClientNetSendMoveDeltaTime=0.0166
MaxMoveDeltaTime=0.125
bMovementTimeDiscrepancyDetection=True
bMovementTimeDiscrepancyResolution=True
MovementTimeDiscrepancyMaxTimeMargin=0.25
MovementTimeDiscrepancyMinTimeMargin=-0.25
MAXPOSITIONERRORSQUARED=3.0        ; engine default (1.73 cm); kept — our sim is deterministic, so corrections are rare and tiny
[/Script/Engine.PhysicsSettings]
DefaultTerminalVelocity=10000
```
CMC correction knobs (defaults are right; set explicitly in the constructor): `NetworkMinTimeBetweenClientAdjustments 0.1`, `NetworkLargeClientCorrectionDistance 15`, `NetworkSimulatedSmoothLocationTime 0.1` (ADR 0001 lag-comp depends on it), `bNetworkSkipProxyPredictionOnNetUpdate = true`, pawn `NetUpdateFrequency 60 / MinNetUpdateFrequency 30`. Owning-client corrections are snap-and-replay (no smoothing exists for the autonomous proxy in 5.8); "no visible jitter" therefore means few and small corrections, which determinism provides. Expected residual sources: 2 % lost moves (server fills the gap with its own dt → mm-scale), x86 vs Apple-silicon float differences (sub-mm drift), server-side knockback (intended). A dev-only sanity `ensureMsgf(|Velocity| < 20 × MaxGroundSpeed)` in `PerformMovement` catches sim bugs.

Headless jitter measurement (what is automatable): `infra/ci/nettest.sh` starts `Velox L_MovementTest -server -nullrhi -log -port=7777` and one client `Velox 127.0.0.1:7777 -game -nullrhi -nosound -unattended -UseFixedTimeStep -FPS=120 -VeloxScript=StrafeRun -ini:Engine:[PacketSimulationSettings]:PktLag=100,[PacketSimulationSettings]:PktLagVariance=20,[PacketSimulationSettings]:PktLoss=2` (dev verifies `FPacketSimulationSettings::LoadConfig` picks up the section under `DO_ENABLE_NET_TEST`; fallback: the scripted component execs `Net PktLag=100` after possession). `UVeloxScriptedInputComponent` (dev-only, on the character) replays a named input script (`Content/Data/MoveScripts/*.json`: rows of `{duration, forward, right, yawRate, jump}`) through `SetLocalMoveInput`/`AddMovementInput`/`SetJumpHeld`, while the CMC counts corrections in `OnClientCorrectionReceived` (`NumCorrections`, `MaxCorrectionCm`, `SumCorrectionCm`). At script end the client logs `VeloxNetSoak: moves=N corrections=C max=X mean=Y` and quits; the script fails when `C > 3 × expected lost moves`, `X > 30 cm` or `Y > 5 cm` (initial thresholds; tune after the first real run). PIE with two clients remains the human gate.

### 6. Debug and tuning tooling

- `AVeloxHUD` canvas block (toggle `velox.hud.Movement`, default on in non-shipping): speed XY in cm/s and ups, velocity XYZ, mode, jump-held, knockback grace, wish-vs-velocity angle, last correction (age, cm), ping, fps.
- `showdebug VeloxMovement` → `UVeloxMovementComponent::DisplayDebug`: effective params (asset vs cvar override), substeps this move, pending knockback, ramp Z cache, saved-move count, combine denials.
- `FVeloxMoveRecorder` (non-shipping): `velox.move.Record <name>` / `velox.move.StopRecord` writes `Saved/Velox/Runs/<name>.csv` (time, x, y, z, speed, speed_ups, onground, wishangle, corrections) per move on the owning client; `Tools/analyze_run.py` plots speed over distance for a strafe run.
- Cvars mirror every settings field: `velox.move.<FieldName>` (`TAutoConsoleVariable<float>`, sentinel `-1e30` = use asset), applied in `BuildSimParams()` and in `SetMovementSettings()` for the engine fields, `!UE_BUILD_SHIPPING` only. In PIE all instances share one cvar state, so server and clients stay in sync; on real servers overrides must be set on both ends or they are just corrections. `velox.move.DumpSettings` prints effective values; `velox.move.Preset Q3|CPM` applies the two reference sets. Editing `DA_MovementSettings_Default` in the editor is live: params are rebuilt from the asset every move (a ~30-float copy).

### 7. Test plan

Unit (`Tests/VeloxMoveSimTest.cpp`, `Velox.Movement.*`, world-free, `dt = 1/125` unless stated):
- `Friction`: from 800 cm/s with no input, N ticks: above `StopSpeed` speed equals `v0·(1−f·dt)^N` within 1e-3 relative; below it decays by `StopSpeed·f·dt` per tick and reaches exactly 0, never negative.
- `GroundAccel`: from rest with forward input the speed converges to `MaxGroundSpeed` (±1e-3) and never exceeds it.
- `AirAccel`: single tick, V=(800,0): wish parallel → zero gain (the clamp); wish perpendicular → gain `> 0`; gain per tick ≤ `AirAcceleration·dt·AirMaxWishSpeed` (|ΔV| bound).
- `StrafeJump`: 125 ticks with the optimal steering angle (`cos θ = (WishSpeed − a)/|V|`, `a = AirAcceleration·dt·WishSpeed`): total gain `> 0.25 × AirAcceleration·AirMaxWishSpeed·T` and `≤ AirAcceleration·AirMaxWishSpeed·T`; holding forward only: gain `== 0`.
- `FixedStepInvariance`: same per-1/60-s wish sequence stepped at dt 1/60 and 1/240 → final speeds within 0.5 % with fixed step on; the test also asserts they differ with it off (documents why the feature exists).
- `AirControl` (CPM on): forward-only input while airborne rotates V towards WishDir with `|V|` unchanged within 1e-3; side input does not.
- `BunnyHop`: airborne at 1000 cm/s, `StepFrame` with ground contact and jump held → XY bitwise unchanged, Z == `JumpVelocity`; without jump held → exactly one friction tick applied.
- `Knockback`: on the ground, impulse 1250 cm/s → grace 0.2 s, no friction for 25 ticks, friction resumes on tick 26; override vs additive semantics.
- `JumpPad`: `JumpPadLaunchVelocity` from (0,0,0) to (800,0,320): analytic flight lands within 1 cm; Z > 0; target below the pad also solvable.
- `Determinism`: 600-tick mixed script run twice through `StepFrame` → bitwise-equal state.
- `SavedMoveFlags` (exists) + `SavedMoveCombine`: the `CanCombineWith` rules of §5 on hand-built moves.

Functional / integration (headless `-game -nullrhi` on the VM, falls back to the Mac if unreliable):
- `FT_MovementDeterminism` (`AFunctionalTest` placed in `L_MovementTest`, runs under `Automation RunTests Velox.Functional`): spawns a character with `UVeloxScriptedInputComponent`, runs `StrafeRun` twice under `-UseFixedTimeStep -FPS=120` from the same spawn, asserts identical end positions (CMC + collision determinism) and that peak speed exceeds `1.4 × MaxGroundSpeed` (strafe jumping works in the real component) and that the pad script lands on its platform.
- `infra/ci/nettest.sh` soak (§5), optional CI step (`--net-soak`).

`L_MovementTest` spec (`Tools/editor/create_movement_test_map.py`, unlit checker material, no light build): spawn at origin; strafe corridor +X, 5120 cm long (2048 u) × 1000 wide, 2 m walls, `ATextRenderActor` markers every 320 cm (128 u) labelled in units and metres; ramp bank: 10°, 20°, 30°, 45° (walkable) and 50° (not), each 1600 cm run-up onto a 400 cm platform with a flat exit for ramp-exit tests; stairs: three flights of 12 steps with 20/40/60 cm risers (60 must block); jump pads: A to a platform 800 cm away / 320 cm up, B flat 1600 cm, C vertical 640 cm, each with a landing marker; bunny-hop gaps of 500/700/900 cm over a 100 cm drop (jump range `0.675 s × speed` at the defaults: 540 cm at 800, 945 at 1400); `APlayerStart`s at the corridor start and the pad bank. `DefaultEngine.ini` default maps switch to `L_MovementTest`; `+MapsToCook`.

### 8. What bots (M4) will drive

Bots are `AAIController`-possessed `AVeloxCharacter`s with the same component and settings asset. Guarantees from M1: input enters only via `AddMovementInput`/`SetLocalMoveInput`, `SetJumpHeld`, `AddKnockback`; `bUseAccelerationForPaths = true` so `UPathFollowingComponent` → `RequestPathMove` → `AddInputVector`, never `RequestDirectMove`/velocity requests (which our `CalcVelocity` ignores); no teleports, no `Launch`. Read API for planning: `GetSimParams()`, `GetSpeed2D()`, `IsOnGround()`, `GetKnockbackGrace()`, `VeloxMoveSim::StepFrame`/`ApplyGravity`/`JumpPadLaunchVelocity` (so M4 can simulate a strafe-jump or pad hop ahead of time for nav links). `AVeloxJumpPad` exposes `ComputeLaunchVelocity` and `GetLandingPoint()` for nav-link generation.

## Consequences

- Hooking `CalcVelocity` and the landing/ramp/step virtuals keeps ~600 lines of engine phys code and its substepping, floor logic and replication untouched; the price is that the hooks must be re-verified on engine upgrades (the table in §2 is the checklist).
- Fixed-step velocity integration makes strafe gain frame-rate independent and the feel reproducible in tests; it is a deliberate deviation from vanilla Q3 (matches CPMA `pmove_fixed`).
- No air-move combining raises upstream RPC rate for high-fps clients (bounded, see §5) in exchange for a stable local simulation.
- The move-response subclass makes knockback a first-class predicted quantity; M2 self-knockback prediction becomes a dedup problem only.
- Everything tunable is in the asset and mirrored by cvars; `UnitScale = 2.5` fixes map scale for M3+ content — changing it later means rebuilding every map.
- Headless CI gets deterministic unit and functional coverage plus a two-process net soak; human PIE is still the M1 gate.

## Alternatives considered

- **Mover 2.0 / NetworkPrediction plugin**: rejected in ADR 0001 (CMC mandated; AI path following; maturity). Unchanged.
- **GAS-driven movement** (jump/knockback as abilities with prediction keys): the GAS prediction window is not the move replay; a predicted ability applying velocity outside `PerformMovement` desyncs on every correction. Movement state stays in the saved-move path; GAS only calls `AddKnockback`.
- **Root motion**: irrelevant to a physics-driven FPS; `bAllowPhysicsRotationDuringAnimRootMotion` and root-motion sources are never used.
- **Overriding `PhysWalking`/`PhysFalling` wholesale** (copy and edit): maximal control, but ~600 lines of engine code to maintain across versions, and every feature in §1 was expressible through the smaller hooks. Revisit only if a feel bug cannot be fixed at the hook level.
- **Expressing Q3 air physics with engine parameters** (`AirControl`, `BrakingFriction`, `MaxAcceleration`): the stock `CalcVelocity` clamps to `MaxSpeed` and lacks the dot-product rule; impossible without the override.
- **Engine `Launch()`/`AddImpulse()` for knockback and pads**: works for pads, but two paths, mass semantics, no saved/correction state; one `AddKnockback` is simpler.
- **Combining air moves** (engine default): lower RPC rate, visible re-simulation of the player's own jump; rejected by default, kept as a toggle.
- **Per-frame integration (vanilla Q3)**: faithful but rewards high frame rates; fixed step chosen.
- **Unit scale 2.54 (inch) or 3.2 (180 cm pawn)**: 2.54 gains nothing over 2.5 and loses round numbers; 3.2 makes a 180 cm pawn but Q3 proportions (jump height ≈ 80 % of body height, 320 ups) then produce a 2.6 m jump-height feel mismatch with any realistic prop scale. Maps are ours, so proportions beat realism.
- **CPM physics on by default**: better duel movement for experts, harder to learn; vanilla core with CPM toggles lets the human decide after playing both (open question).

## Implementation notes for dev-hard (ordered tasks, 1–3 h each)

1. **`01-move-sim`** (dev-hard) — `Movement/VeloxMoveSim.{h,cpp}` per §3; extend/rename `UVeloxMovementSettings` fields per the §1 table with `BuildSimParams()`; all `Velox.Movement.*` unit tests of §7 except `SavedMoveCombine`. Files: `Movement/VeloxMoveSim.*`, `Movement/VeloxMovementSettings.h`, `Tests/VeloxMoveSimTest.cpp`. Accept: headless `Automation RunTests Velox.Movement` green; `VeloxMoveSim.h` includes only `CoreMinimal.h`.
2. **`02-cmc-quake-physics`** (dev-hard) — every override in §2, `SetMovementSettings` engine-field application, `SetLocalMoveInput`, character changes (capsule/eye from `UVeloxPawnData`, `bUseControllerRotationYaw`, jump defaults), `DefaultEngine.ini` blocks of §5, `DA_MovementSettings_Default` and `DA_PawnData_Default` values via the headless data-asset script. Files: `Movement/VeloxMovementComponent.*`, `Character/VeloxCharacter.*`, `Core/VeloxPawnData.h`, `Config/DefaultEngine.ini`, `Tools/editor/create_data_assets.py`. Accept: Editor + Game compile; unit tests green; `Velox L_MovementTest -game -nullrhi -unattended` (after task 3) runs 10 s with no `LogCharacterMovement` warnings; `architect` design review of the override table.
3. **`03-movement-test-map`** (dev, parallel with 2) — `Tools/editor/create_movement_test_map.py` building `L_MovementTest` per §7, unlit greybox material, markers, default-map switch, `+MapsToCook`. Accept: script runs headless and asserts actor counts; map loads in `-game -nullrhi`.
4. **`04-prediction-knockback-jumppads`** (dev-hard) — `FSavedMove_Velox` fields/flags/`CanCombineWith` (§5), `FVeloxMoveResponseDataContainer` + `OnClientCorrectionReceived`, `AddKnockback`/`ConsumePendingKnockback`/grace, `AVeloxJumpPad`, correction counters, `velox.move.TestKnockback`, `SavedMoveCombine` + `JumpPad` tests. Files: `Movement/VeloxNetworkMoveData.*`, `Movement/VeloxMovementComponent.*`, `Movement/VeloxJumpPad.*`, `Tests/VeloxSavedMoveTest.cpp`. Accept: compile; tests green; pads placed by task 3 launch a scripted character onto their platforms in `FT_MovementDeterminism` (task 5) once it exists; `architect` review.
5. **`05-debug-tooling-and-net-soak`** (dev) — HUD block, `DisplayDebug`, recorder + `Tools/analyze_run.py`, `velox.move.*` cvars/`DumpSettings`/`Preset`, `UVeloxScriptedInputComponent` + `Content/Data/MoveScripts/StrafeRun.json`, `FT_MovementDeterminism`, `infra/ci/nettest.sh` (+ `--net-soak` in `build.sh`). Files: `UI/VeloxHUD.*`, `Movement/VeloxMoveDebug.*`, `Movement/VeloxScriptedInputComponent.*`, `Tests/`, `infra/ci/`. Accept: `nettest.sh` runs server + client headless on the VM at 100 ms / 2 % and prints the `VeloxNetSoak` line within thresholds; `FT_MovementDeterminism` green.
6. **`06-human-playtest`** (dev writes checklist, human runs) — Mac PIE, dedicated server + 2 clients, `Net PktLag=100 PktLoss=2`: strafe corridor to ≥ 1.4× ground speed, bunny hop across the three gaps, all pads, ramp slide on 30°/45°, stairs at full speed, `TestKnockback` rocket-jump height; sign-off recorded in `STATUS.md`.

Rules for all tasks: never bypass the saved-move path; no engine-field magic numbers outside `SetMovementSettings`; report ADR disagreements instead of deviating.
