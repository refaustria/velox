// Velox - custom character movement component (ADR 0002 section 2 override table).

#include "Movement/VeloxMovementComponent.h"

#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "Components/CapsuleComponent.h"
#include "Core/VeloxLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Movement/VeloxMoveDebug.h"
#include "Movement/VeloxMovementSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxMovementComponent)

namespace VeloxMovementConstants
{
	// Input presence threshold per axis for the CPM forward-only / side-only rules (ADR 0002 section 2).
	constexpr float InputPresenceThreshold = 0.1f;

	// Dev-only sanity bound: a speed above this multiple of MaxGroundSpeed means a sim bug (ADR 0002 section 5).
	constexpr float SanityMaxSpeedFactor = 20.f;

	// Modes driven by Velox physics; anything else (Flying noclip, Swimming, Custom) gets engine values (ADR 0002 A15).
	FORCEINLINE bool IsVeloxMovementMode(const EMovementMode Mode)
	{
		return Mode == MOVE_Walking || Mode == MOVE_NavWalking || Mode == MOVE_Falling;
	}

	FORCEINLINE bool IsGroundMovementMode(const EMovementMode Mode)
	{
		return Mode == MOVE_Walking || Mode == MOVE_NavWalking;
	}
}

UVeloxMovementComponent::UVeloxMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWantsJumpHeld = false;
	bHasForwardInput = false;
	bHasSideInput = false;
	bJumpLatched = false;
	bApplyingCorrection = false;
	bJumpedThisMove = false;
	bInClientReplay = false;
	bGroundMeanVelocityActive = false;

	// FPS rotation: yaw comes from the controller via the character (bUseControllerRotationYaw), never from movement.
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;

	// Walking and falling only; MOVE_Flying is reserved for spectator/noclip later.
	NavAgentProps.bCanSwim = false;
	NavAgentProps.bCanFly = false;

	// Correction knobs (ADR 0002 section 5): engine defaults, set explicitly because lag compensation relies on them.
	NetworkMinTimeBetweenClientAdjustments = 0.1f;
	NetworkLargeClientCorrectionDistance = 15.f;
	NetworkSimulatedSmoothLocationTime = 0.1f;
	bNetworkSkipProxyPredictionOnNetUpdate = true;

	SetNetworkMoveDataContainer(VeloxNetworkMoveDataContainer);
	SetMoveResponseDataContainer(VeloxMoveResponseDataContainer);
}

const UVeloxMovementSettings* UVeloxMovementComponent::GetMovementSettings() const
{
	return MovementSettings;
}

void UVeloxMovementComponent::SetMovementSettings(const UVeloxMovementSettings* InMovementSettings)
{
	MovementSettings = InMovementSettings;
	if (!MovementSettings)
	{
		SimParams = FVeloxMoveParams();
		return;
	}

	SimParams = MovementSettings->BuildSimParams();

	// The only place engine movement fields are written (ADR 0002 section 2). Values are for engine readers
	// (navigation, AI, debug); the Velox hooks read SimParams.
	MaxWalkSpeed = SimParams.MaxGroundSpeed;
	MaxAcceleration = SimParams.MaxGroundSpeed;
	MaxStepHeight = SimParams.StepHeight;
	SetWalkableFloorAngle(SimParams.WalkableFloorAngle);
	PerchRadiusThreshold = SimParams.EdgePerchRadius;
	JumpZVelocity = SimParams.JumpVelocity;
	NavMovementProperties.bUseAccelerationForPaths = true;
	bEnablePhysicsInteraction = false;
	GravityScale = 1.f;
}

void UVeloxMovementComponent::SetLocalMoveInput(FVector2D ForwardRight)
{
	bHasForwardInput = FMath::Abs(ForwardRight.X) > VeloxMovementConstants::InputPresenceThreshold;
	bHasSideInput = FMath::Abs(ForwardRight.Y) > VeloxMovementConstants::InputPresenceThreshold;
}

FNetworkPredictionData_Client* UVeloxMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		// Same lazy-allocation pattern as UCharacterMovementComponent; the base class owns and deletes it.
		UVeloxMovementComponent* MutableThis = const_cast<UVeloxMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Velox(*this);
	}

	return ClientPredictionData;
}

// ---------------------------------------------------------------------------------------------------------------------
// Speed / acceleration / gravity encoding

float UVeloxMovementComponent::GetMaxSpeed() const
{
	if (HasVeloxPhysics() && VeloxMovementConstants::IsVeloxMovementMode(MovementMode))
	{
		// Crouch scale applies to ground wish speed only (Q3 PM_WalkMove, ADR 0002 A10).
		const bool bCrouchScaled = IsCrouching() && VeloxMovementConstants::IsGroundMovementMode(MovementMode);
		return SimParams.MaxGroundSpeed * (bCrouchScaled ? SimParams.CrouchSpeedScale : 1.f);
	}
	return Super::GetMaxSpeed();
}

float UVeloxMovementComponent::GetMaxAcceleration() const
{
	// ScaleInputAcceleration multiplies the clamped input by this, so Acceleration == Q3 wishvel (cm/s).
	return (HasVeloxPhysics() && VeloxMovementConstants::IsVeloxMovementMode(MovementMode))
		? SimParams.MaxGroundSpeed
		: Super::GetMaxAcceleration();
}

float UVeloxMovementComponent::GetMaxBrakingDeceleration() const
{
	// Braking is never used: friction lives in VeloxMoveSim.
	return (HasVeloxPhysics() && VeloxMovementConstants::IsVeloxMovementMode(MovementMode))
		? 0.f
		: Super::GetMaxBrakingDeceleration();
}

float UVeloxMovementComponent::GetGravityZ() const
{
	// Pawn gravity from the settings; ignores physics-volume gravity and GravityScale (ADR 0001).
	return HasVeloxPhysics() ? SimParams.GravityZ : Super::GetGravityZ();
}

// ---------------------------------------------------------------------------------------------------------------------
// Move / velocity integration

void UVeloxMovementComponent::PerformMovement(float DeltaSeconds)
{
	if (!HasVeloxPhysics())
	{
		Super::PerformMovement(DeltaSeconds);
		return;
	}

	// Defensive (A19): any correction teleport is over once a move runs.
	bApplyingCorrection = false;

	// One params build per move, reused by every CalcVelocity of this move (A8).
	SimParams = MovementSettings->BuildSimParams();
	RampExitVelocityZ = 0.f;

	// VQ3 clears PMF_JUMP_HELD on the first frame without jump (A12). The move's own flag, so the server and a replay
	// derive the same value. A jump started by CheckJumpInput before this call has held = true and keeps the latch.
	bJumpLatched = JumpLatchAtMoveStart(bJumpLatched, bWantsJumpHeld);

	bJumpedThisMove = false;

	ConsumePendingKnockback();
	MoveStartKnockbackGrace = KnockbackGrace;
	MoveElapsedTime = 0.f;
	bGroundMeanVelocityActive = false;

	Super::PerformMovement(DeltaSeconds);

	// A31 safety net: PhysWalking always ends the mean-velocity window through one of the hooks; a move must never end
	// with the mean velocity (saved moves, HUD, server error check and responses all expect the end velocity).
	const bool bMeanWindowWasOpen = RestoreGroundEndVelocity();
#if !UE_BUILD_SHIPPING
	ensureMsgf(!bMeanWindowWasOpen, TEXT("%s: A31 mean-velocity window still open at the end of PerformMovement (mode %s)"), *GetPathNameSafe(this), *GetMovementName());
#endif

	KnockbackGrace = FMath::Max(0.f, KnockbackGrace - DeltaSeconds);

#if !UE_BUILD_SHIPPING
	ensureMsgf(Velocity.SizeSquared() < FMath::Square(VeloxMovementConstants::SanityMaxSpeedFactor * SimParams.MaxGroundSpeed),
		TEXT("%s: velocity %s exceeds %.0f x MaxGroundSpeed, movement sim bug?"), *GetPathNameSafe(this), *Velocity.ToString(), VeloxMovementConstants::SanityMaxSpeedFactor);
#endif
}

void UVeloxMovementComponent::ConsumePendingKnockback()
{
	if (!PendingKnockback.IsPending())
	{
		return;
	}

	Velocity = VeloxMoveSim::ApplyKnockback(Velocity, PendingKnockback.Velocity, PendingKnockback.bOverride);
	KnockbackGrace = FMath::Max(KnockbackGrace, VeloxMoveSim::KnockbackGraceFor(PendingKnockback.Velocity, SimParams));

	// Lift off when pushed away from the floor; PhysWalking would otherwise flatten the velocity.
	if (IsMovingOnGround() && (Velocity | CurrentFloor.HitResult.ImpactNormal) > SimParams.RampSlideMinNormalSpeed)
	{
		SetMovementMode(MOVE_Falling);
	}
	bForceNextFloorCheck = true;
	PendingKnockback.Reset();
}

bool UVeloxMovementComponent::AddKnockback(FVector Impulse, bool bOverrideVelocity)
{
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Simulated proxies never run moves; the result arrives through replication.
		return false;
	}
	if (bApplyingCorrection && CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy)
	{
		// The correction teleport fired an overlap (A19). The server's state already contains every entry it saw, consumed
		// into the corrected velocity or pending in the response; the replay re-fires overlaps the server has not seen yet.
		UE_LOG(LogVeloxMovement, Verbose, TEXT("%s: AddKnockback %s (override %d) ignored during correction teleport"), *GetPathNameSafe(this), *Impulse.ToString(), bOverrideVelocity ? 1 : 0);
		return false;
	}
	PendingKnockback.Add(Impulse, bOverrideVelocity);
	return true;
}

float UVeloxMovementComponent::GetCurrentMoveTimeStamp() const
{
	const bool bAuthority = CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority;
	if (bAuthority || bInClientReplay)
	{
		return LastMoveAutonomousTimeStamp;
	}
	if (bApplyingCorrection)
	{
		return PendingCorrectionTimeStamp;
	}
	const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	return ClientData ? ClientData->CurrentTimeStamp : 0.f;
}

const TCHAR* UVeloxMovementComponent::GetMoveSourceName() const
{
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority)
	{
		return TEXT("server");
	}
	if (bApplyingCorrection)
	{
		return TEXT("teleport");
	}
	return bInClientReplay ? TEXT("replay") : TEXT("live");
}

void UVeloxMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	LastMoveAutonomousTimeStamp = ClientTimeStamp;
#if !UE_BUILD_SHIPPING
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority)
	{
		// A34(i): regular client moves are 1/120 or 1/60 s; a server move longer than 20 ms (catches the 25 ms merge of a
		// 1/60 + 1/120 pair) filled in the time of lost RPCs. A slow client frame can also produce one: diagnostic only.
		constexpr float MoveGapLogThreshold = 0.020f;
		if (DeltaTime > MoveGapLogThreshold)
		{
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxMoveGap ts=%.6f dt=%.6f"), ClientTimeStamp, DeltaTime);
		}
		// A32: an old move that passed VerifyClientTimeStamp (MoveAutonomous only runs then) was lost in its own RPC and is
		// now simulated in its original partition.
		const FCharacterNetworkMoveData* MoveData = GetCurrentNetworkMoveData();
		if (MoveData && MoveData->NetworkMoveType == FCharacterNetworkMoveData::ENetworkMoveType::OldMove)
		{
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxMoveRepaired ts=%.6f dt=%.6f"), ClientTimeStamp, DeltaTime);
		}
	}
#endif
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UVeloxMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (!HasVeloxPhysics() || !(IsMovingOnGround() || IsFalling()))
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}

	// Same early-outs as the engine: simulated proxies get their velocity replicated.
	if (!HasValidData() || HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME
		|| (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy && !bWasSimulatingRootMotion))
	{
		return;
	}

	// Friction, BrakingDeceleration, bFluid, RequestedVelocity and the MaxSpeed clamp are deliberately ignored.
	// Acceleration is the Q3 wishvel (GetMaxAcceleration encoding); PhysFalling has swapped in FallAcceleration and
	// zeroed Velocity.Z around this call, PhysWalking has made Velocity horizontal.
	const bool bOnGround = IsMovingOnGround();

	FVeloxMoveInput Input;
	Input.WishDir = Acceleration.GetSafeNormal2D();
	Input.WishSpeed = FMath::Min(static_cast<float>(Acceleration.Size2D()), SimParams.MaxGroundSpeed);
	if (bOnGround && IsCrouching())
	{
		// Q3 PM_WalkMove applies pm_duckScale on the ground only.
		Input.WishSpeed *= SimParams.CrouchSpeedScale;
	}
	Input.bHasForwardInput = bHasForwardInput;
	Input.bHasSideInput = bHasSideInput;
	Input.bJumpHeld = bWantsJumpHeld;

	// A27: the grace remaining at the start of this iteration, not of the move. GetSimulationTimeStep splits moves longer
	// than MaxSimulationTimeStep (a server move that merged lost client moves) into several PhysWalking iterations.
	const float GraceNow = FMath::Max(0.f, MoveStartKnockbackGrace - MoveElapsedTime);
	if (!bOnGround)
	{
		// PhysFalling integrates the trapezoid 0.5 * (OldVelocity + Velocity) * dt itself.
		Velocity = VeloxMoveSim::AirMove(Velocity, Input, DeltaTime, SimParams);
	}
	else if (MovementMode == MOVE_Walking)
	{
		// A31 Fix D: PhysWalking moves Velocity * dt (rectangle rule, first-order partition-dependent). Hand it the
		// substep-trapezoid mean for the displacement; the hooks that follow (MaintainHorizontalGroundVelocity after
		// "reflect actual move", HandleWalkingOffLedge, StartFalling, OnMovementModeChanged) put the end velocity back.
		FVector Mean;
		GroundEndVelocity = VeloxMoveSim::GroundMove(Velocity, Input, DeltaTime, GraceNow, SimParams, &Mean);
		GroundMeanVelocity = Mean;
		Velocity = Mean;
		bGroundMeanVelocityActive = true;
	}
	else
	{
		// NavWalking has no restore hook after its move (no "reflect actual move"): keep the end velocity.
		Velocity = VeloxMoveSim::GroundMove(Velocity, Input, DeltaTime, GraceNow, SimParams);
	}
	// The grace counts down regardless of the mode.
	MoveElapsedTime += DeltaTime;
}

bool UVeloxMovementComponent::RestoreGroundEndVelocity()
{
	if (!bGroundMeanVelocityActive)
	{
		return false;
	}
	bGroundMeanVelocityActive = false;
	// Tolerance: Velocity = (NewLocation - OldLocation) / dt is Mean up to double -> float noise when nothing clipped the
	// move (horizontal part; MaintainHorizontalGroundVelocity has already removed the ramp's vertical part).
	constexpr float MeanVelocityTolerance = 0.05f;   // cm/s
	if (Velocity.Equals(GroundMeanVelocity, MeanVelocityTolerance))
	{
		Velocity = GroundEndVelocity;
	}
	return true;
}

void UVeloxMovementComponent::MaintainHorizontalGroundVelocity()
{
	Super::MaintainHorizontalGroundVelocity();
	RestoreGroundEndVelocity();
}

FVector UVeloxMovementComponent::GetFallingLateralAcceleration(float DeltaTime)
{
	// Full input in the air: no AirControl scaling, boost or clamp (Q3 air acceleration happens in CalcVelocity).
	return HasVeloxPhysics() ? ProjectToGravityFloor(Acceleration) : Super::GetFallingLateralAcceleration(DeltaTime);
}

bool UVeloxMovementComponent::ShouldLimitAirControl(float DeltaTime, const FVector& FallAcceleration) const
{
	// Wall hits in the air become plain slides (no VelocityNoAirControl re-simulation).
	return HasVeloxPhysics() ? false : Super::ShouldLimitAirControl(DeltaTime, FallAcceleration);
}

FVector UVeloxMovementComponent::HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const
{
	// Airborne ramp slides may gain height (Q3 PM_ClipVelocity).
	return HasVeloxPhysics() ? SlideResult : Super::HandleSlopeBoosting(SlideResult, Delta, Time, Normal, Hit);
}

// ---------------------------------------------------------------------------------------------------------------------
// Landing / jumping

bool UVeloxMovementComponent::IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const
{
	// Q3 PM_GroundTrace: moving up and away from the surface faster than this is a slide/kick-off, not a landing.
	if (HasVeloxPhysics() && Velocity.Z > 0.f && (Velocity | Hit.ImpactNormal) > SimParams.RampSlideMinNormalSpeed)
	{
		return false;
	}
	return Super::IsValidLandingSpot(CapsuleLocation, Hit);
}

void UVeloxMovementComponent::ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations)
{
	// Landing jump: jump held on landing jumps in the landing frame, before any ground friction (Q3 checks the jump
	// before PM_Friction). With bAutoHop off only if the jump was released since the last jump (VQ3 PMF_JUMP_HELD, A12).
	// NavWalking ground mode keeps the engine path (it validates the navmesh in ProcessLanded).
	const bool bLandingJump = HasVeloxPhysics() && WantsLandingJump(bWantsJumpHeld, SimParams.bAutoHop, bJumpLatched) && CharacterOwner
		&& GetGroundMovementMode() == MOVE_Walking && !bWantsToCrouch;
	if (!bLandingJump)
	{
		Super::ProcessLanded(Hit, remainingTime, Iterations);
		return;
	}

	// Mirrors UCharacterMovementComponent::ProcessLanded with the jump inserted before StartNewPhysics.
	if (CharacterOwner->ShouldNotifyLanded(Hit))
	{
		CharacterOwner->Landed(Hit);
	}
	if (IsFalling())
	{
		SetPostLandedPhysics(Hit);
	}
	if (IPathFollowingAgentInterface* PFAgent = GetPathFollowingAgent())
	{
		PFAgent->OnLanded();
	}

	// Landing reset the jump state (ACharacter::OnMovementModeChanged), so CanJump() sees a fresh first jump.
	if (IsMovingOnGround() && DoJump(CharacterOwner->bClientUpdating, remainingTime))
	{
		CharacterOwner->JumpCurrentCount++;
		CharacterOwner->OnJumped();
	}

	// After a jump the rest of the tick runs as PhysFalling (air acceleration, no friction).
	StartNewPhysics(remainingTime, Iterations);
}

bool UVeloxMovementComponent::CanAttemptJump() const
{
	if (!HasVeloxPhysics())
	{
		return Super::CanAttemptJump();
	}
	// No air jumps.
	return IsJumpAllowed() && !bWantsToCrouch && IsMovingOnGround();
}

bool UVeloxMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (!HasVeloxPhysics())
	{
		return Super::DoJump(bReplayingMoves, DeltaTime);
	}

	// CheckJumpInput calls this without checking CanJump() first.
	if (CharacterOwner && CharacterOwner->CanJump())
	{
		// Q3 sets Z (no max with the current Z, no jump hold force).
		Velocity = VeloxMoveSim::Jump(Velocity, SimParams);
		SetMovementMode(MOVE_Falling);
		bJumpLatched = true;
		bJumpedThisMove = true;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ramps and steps

float UVeloxMovementComponent::ComputeRampExitVelocityZ(const FVector& FloorImpactNormal, const FVector& FloorContactNormal) const
{
	return ComputeRampExitVelocityZ(FloorImpactNormal, FloorContactNormal, Velocity);
}

float UVeloxMovementComponent::ComputeRampExitVelocityZ(const FVector& FloorImpactNormal, const FVector& FloorContactNormal, const FVector& InVelocity) const
{
	// Same conditions under which ComputeGroundMovementDelta moves along the ramp plane.
	const float RampMaxNormalZ = FMath::Cos(FMath::DegreesToRadians(SimParams.RampExitMinAngle));
	if (FloorImpactNormal.Z >= RampMaxNormalZ || FloorImpactNormal.Z <= UE_KINDA_SMALL_NUMBER || FloorContactNormal.Z <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	const FVector HorizontalVelocity(InVelocity.X, InVelocity.Y, 0.0);
	const float ClimbRate = -(HorizontalVelocity | FloorImpactNormal) / FloorImpactNormal.Z;
	return FMath::Max(ClimbRate, 0.f);
}

bool UVeloxMovementComponent::ShouldCatchAir(const FFindFloorResult& OldFloor, const FFindFloorResult& NewFloor)
{
	if (!HasVeloxPhysics())
	{
		return Super::ShouldCatchAir(OldFloor, NewFloor);
	}

	// Leave the ground at the top of a ramp instead of snapping down: Q3 PM_GroundTrace kick-off rule, the same
	// criterion as IsValidLandingSpot (ADR 0002 A9). The pawn moves at (V_xy, Zc) along the ramp; it stays on the new
	// floor only if that velocity does not point away from it faster than RampSlideMinNormalSpeed. The climb rate itself
	// is recomputed by HandleWalkingOffLedge, which the engine calls next on this path.
	if (!SimParams.bRampExitCatchAir || OldFloor.bLineTrace)
	{
		return false;
	}
	// A31: decided on the end velocity of the ground update, as before Fix D (the engine has not restored it yet here).
	const FVector& EndVelocity = GetGroundEndVelocity();
	const float ClimbRate = ComputeRampExitVelocityZ(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal, EndVelocity);
	if (ClimbRate <= 0.f)
	{
		return false;
	}
	const FVector RampVelocity(EndVelocity.X, EndVelocity.Y, ClimbRate);
	return (RampVelocity | NewFloor.HitResult.ImpactNormal) > SimParams.RampSlideMinNormalSpeed;
}

void UVeloxMovementComponent::HandleWalkingOffLedge(const FVector& PreviousFloorImpactNormal, const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta)
{
	// A31: the fall paths leave PhysWalking before "reflect actual move"; the ramp exit reads the end velocity.
	RestoreGroundEndVelocity();

	Super::HandleWalkingOffLedge(PreviousFloorImpactNormal, PreviousFloorContactNormal, PreviousLocation, TimeDelta);

	// Both PhysWalking fall paths (ShouldCatchAir and CheckFall) call this right before StartFalling. Velocity is still
	// the horizontal move velocity of this iteration.
	RampExitVelocityZ = (HasVeloxPhysics() && IsMovingOnGround())
		? ComputeRampExitVelocityZ(PreviousFloorImpactNormal, PreviousFloorContactNormal)
		: 0.f;
}

void UVeloxMovementComponent::StartFalling(int32 Iterations, float remainingTime, float timeTick, const FVector& Delta, const FVector& subLoc)
{
	// A31: normally already restored by HandleWalkingOffLedge (both engine call sites call it first).
	RestoreGroundEndVelocity();

	// Carry the ramp's vertical speed into PhysFalling (Q3 keeps the ground-plane-clipped velocity).
	if (RampExitVelocityZ > 0.f)
	{
		Velocity.Z = RampExitVelocityZ;
	}
	RampExitVelocityZ = 0.f;

	Super::StartFalling(Iterations, remainingTime, timeTick, Delta, subLoc);
}

void UVeloxMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	// A31: a mode change inside PhysWalking (step-up-and-over, root motion) leaves before "reflect actual move".
	RestoreGroundEndVelocity();

	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	RampExitVelocityZ = 0.f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Network

void UVeloxMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	bWantsJumpHeld = (Flags & FSavedMove_Velox::FLAG_JumpHeld) != 0;
	bHasForwardInput = (Flags & FSavedMove_Velox::FLAG_HasForwardInput) != 0;
	bHasSideInput = (Flags & FSavedMove_Velox::FLAG_HasSideInput) != 0;
}

void UVeloxMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection)
{
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewMovementBaseInterfaceData, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);

	// Statistics: client location after the corrected move vs the server's (same measure as p.NetShowCorrections).
	const FVector ClientLocation = ClientData.LastAckedMove.IsValid() ? ClientData.LastAckedMove->SavedLocation : UpdatedComponent->GetComponentLocation();
	LastCorrectionCm = static_cast<float>(FVector::Dist(ClientLocation, NewLocation));
	++NumCorrections;
	SumCorrectionCm += LastCorrectionCm;
	MaxCorrectionCm = FMath::Max(MaxCorrectionCm, LastCorrectionCm);
	LastCorrectionTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	PendingCorrectionTimeStamp = TimeStamp;

	// Velox state the replay starts from. The container is ours (constructor); it is only filled on the packed-RPC path,
	// so the time stamp guards against stale data from the legacy RPCs.
	const FVeloxMoveResponseDataContainer& Response = static_cast<const FVeloxMoveResponseDataContainer&>(GetMoveResponseDataContainer());
	if (!Response.IsCorrection() || Response.ClientAdjustment.TimeStamp != TimeStamp)
	{
		return;
	}

	// Derived state chains from here through the replayed moves, like Velocity (A17); PrepMoveFor restores none of it.
	KnockbackGrace = Response.KnockbackGrace;
	bJumpLatched = Response.bJumpLatched;
	PendingKnockback = Response.PendingKnockback;
#if !UE_BUILD_SHIPPING
	// Net soak (infra/ci/nettest.sh): what the server's response container delivered, before the replay consumes it.
	UE_LOG(LogVeloxMovement, Log, TEXT("VeloxCorrectionResponse t=%.3f ts=%.6f grace=%.6f latch=%d svel=%.1f"), LastCorrectionTime, TimeStamp, KnockbackGrace, bJumpLatched ? 1 : 0, NewVelocity.Size());
#endif

	// ClientAdjustPosition teleports the pawn after this call; its overlaps must not queue knockback (A19).
	bApplyingCorrection = true;
}

bool UVeloxMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	// The correction teleport is over; overlaps during the replay must fire again (A19).
	bApplyingCorrection = false;

	// The replay sets each saved move's inputs (MoveAutonomous -> UpdateFromCompressedFlags); give the live inputs back
	// afterwards, like the engine does for bPressedJump and bWantsToCrouch. Input events between the last saved move and
	// now would otherwise be lost. Derived state (grace, latch, pending knockback) is not restored: the values at the end
	// of the replay are the correct live ones (A17).
	const bool bRealJumpHeld = bWantsJumpHeld;
	const bool bRealForwardInput = bHasForwardInput;
	const bool bRealSideInput = bHasSideInput;

#if !UE_BUILD_SHIPPING
	// Net soak A25 regression check (infra/ci/nettest.sh): the replay must start at the move right after the correction.
	// Measured against the correction's own TimeStamp, not LastAckedMove, which a culling ack would already have advanced.
	if (const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
	{
		if (ClientData->bUpdatePosition && ClientData->SavedMoves.Num() > 0)
		{
			const FSavedMove_Character& FirstMove = *ClientData->SavedMoves[0];
			UE_LOG(LogVeloxMovement, Log, TEXT("VeloxReplay t=%.3f start=%.6f expected=%.6f moves=%d"), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0,
				FirstMove.TimeStamp, PendingCorrectionTimeStamp + FirstMove.DeltaTime, ClientData->SavedMoves.Num());
		}
	}
#endif

	bInClientReplay = true;
	const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();
	bInClientReplay = false;

	bWantsJumpHeld = bRealJumpHeld;
	bHasForwardInput = bRealForwardInput;
	bHasSideInput = bRealSideInput;
	return bResult;
}

void UVeloxMovementComponent::CallServerMovePacked(const FSavedMove_Character* NewMove, const FSavedMove_Character* PendingMove, const FSavedMove_Character* OldMove)
{
	// A32: the server processes OldMove before PendingMove and NewMove (ServerMove_HandleMoveData). Sending the previous
	// RPC's move every time means a single lost RPC is simulated in its original partition by the next one (no merged
	// move, no partition-dependent integration, no trigger crossing shifted by one move); an unacked event move (pad
	// entry, held-jump landing) takes precedence. A duplicate is rejected by VerifyClientTimeStamp (Log-level line).
	const FSavedMove_Character* ChosenOldMove = OldMove;
	bool bEventMove = false;
	if (const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
	{
		ChosenOldMove = FSavedMove_Velox::ChooseOldMove(ClientData->SavedMoves, ClientData->LastAckedMove, NewMove, PendingMove, OldMove, &bEventMove);
	}

	++NumServerMoveRPCs;
	if (ChosenOldMove)
	{
		++NumResentMoves;
	}
#if !UE_BUILD_SHIPPING
	// Net soak (A34): one line per RPC. Event resends at Log (the per-pad proof), the rest at Verbose.
	const double NewStamp = NewMove ? NewMove->TimeStamp : -1.0;
	const double PendingStamp = PendingMove ? PendingMove->TimeStamp : -1.0;
	if (ChosenOldMove)
	{
		if (bEventMove)
		{
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxResend old=%.6f new=%.6f pending=%.6f event=1"), ChosenOldMove->TimeStamp, NewStamp, PendingStamp);
		}
		else
		{
			UE_LOG(LogVeloxNet, Verbose, TEXT("VeloxResend old=%.6f new=%.6f pending=%.6f event=0"), ChosenOldMove->TimeStamp, NewStamp, PendingStamp);
		}
	}
	else
	{
		UE_LOG(LogVeloxNet, Verbose, TEXT("VeloxSend new=%.6f pending=%.6f"), NewStamp, PendingStamp);
	}
#endif

	Super::CallServerMovePacked(NewMove, PendingMove, ChosenOldMove);
}

void UVeloxMovementComponent::ClientAckGoodMove_Implementation(float TimeStamp)
{
	// A25: a correction received earlier in this frame's net receive has not been replayed yet. Acking now would remove
	// the saved moves between the correction and this ack, and the replay would start from the corrected state with
	// those moves missing. Drop the ack; no replay or teleport here (overlaps must not fire inside net dispatch).
	const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	if (ClientData && ClientData->bUpdatePosition)
	{
		UE_LOG(LogVeloxMovement, Verbose, TEXT("ClientAckGoodMove %.3f ignored: correction pending replay (correction ts %.3f)"), TimeStamp, PendingCorrectionTimeStamp);
		return;
	}
	Super::ClientAckGoodMove_Implementation(TimeStamp);
}

void UVeloxMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation, ClientMovementBaseInterfaceData, ClientBaseBoneName, ClientMovementMode);

	// The engine snapshots NewLoc/NewVel/MovementMode into PendingAdjustment here but serialises it at replication time,
	// after any later ServerMove RPC of the frame. Snapshot the Velox state alongside it (A18).
	const FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character();
	if (ServerData && !ServerData->PendingAdjustment.bAckGoodMove && ServerData->PendingAdjustment.TimeStamp == ClientTimeStamp)
	{
		ServerCorrectionState.TimeStamp = ClientTimeStamp;
		ServerCorrectionState.KnockbackGrace = KnockbackGrace;
		ServerCorrectionState.bJumpLatched = bJumpLatched;
		ServerCorrectionState.PendingKnockback = PendingKnockback;
	}
}

void UVeloxMovementComponent::DisplayVeloxDebug(UCanvas* Canvas, float& YL, float& YPos) const
{
#if !UE_BUILD_SHIPPING
	VeloxMoveDebug::DrawDebug(Canvas, *this, YL, YPos);
#endif
}

void UVeloxMovementComponent::ResetCorrectionStats()
{
	NumCorrections = 0;
	MaxCorrectionCm = 0.f;
	SumCorrectionCm = 0.f;
	LastCorrectionCm = 0.f;
	LastCorrectionTime = -1.0;
	NumResentMoves = 0;
	NumServerMoveRPCs = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Console

#if !UE_BUILD_SHIPPING
namespace VeloxMovementConsole
{
	/** Velox characters the command applies to in an authority world: the one of PlayerId if given, else all player pawns. */
	void ApplyKnockbackInWorld(UWorld* World, TOptional<int32> PlayerId, const FVector& Impulse, bool bOverride)
	{
		int32 NumApplied = 0;
		for (TActorIterator<ACharacter> It(World); It; ++It)
		{
			UVeloxMovementComponent* MoveComp = Cast<UVeloxMovementComponent>(It->GetCharacterMovement());
			const APlayerState* PlayerState = It->GetPlayerState();
			if (!MoveComp || !PlayerState || (PlayerId.IsSet() && PlayerState->GetPlayerId() != PlayerId.GetValue()))
			{
				continue;
			}
			MoveComp->AddKnockback(Impulse, bOverride);
			++NumApplied;
			UE_LOG(LogVeloxMovement, Log, TEXT("TestKnockback: %s %s on %s (%s)"), bOverride ? TEXT("set") : TEXT("add"), *Impulse.ToString(), *GetNameSafe(*It), *World->GetName());
		}
		if (NumApplied == 0)
		{
			UE_LOG(LogVeloxMovement, Warning, TEXT("TestKnockback: no matching Velox character in %s"), *World->GetName());
		}
	}

	void TestKnockback(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 3)
		{
			UE_LOG(LogVeloxMovement, Warning, TEXT("Usage: velox.move.TestKnockback X Y Z [override 0|1] (cm/s)"));
			return;
		}
		const FVector Impulse(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		const bool bOverride = Args.IsValidIndex(3) && FCString::Atoi(*Args[3]) != 0;

		if (World->GetNetMode() != NM_Client)
		{
			// Server or standalone: the local player's pawn if there is one, else every player pawn (dedicated server).
			const APlayerController* LocalPC = World->GetFirstPlayerController();
			const APlayerState* LocalPS = (LocalPC && LocalPC->IsLocalController()) ? LocalPC->PlayerState.Get() : nullptr;
			ApplyKnockbackInWorld(World, LocalPS ? TOptional<int32>(LocalPS->GetPlayerId()) : TOptional<int32>(), Impulse, bOverride);
			return;
		}

		// Client: server-side knockback exercises the correction path. Only possible when the server runs in-process (PIE).
		const APlayerController* LocalPC = World->GetFirstPlayerController();
		const APlayerState* LocalPS = LocalPC ? LocalPC->PlayerState.Get() : nullptr;
		if (!LocalPS)
		{
			UE_LOG(LogVeloxMovement, Warning, TEXT("TestKnockback: no local player state yet"));
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* ServerWorld = Context.World();
			if (Context.WorldType == EWorldType::PIE && ServerWorld && ServerWorld != World && ServerWorld->GetNetMode() != NM_Client && ServerWorld->GetNetMode() != NM_Standalone)
			{
				ApplyKnockbackInWorld(ServerWorld, LocalPS->GetPlayerId(), Impulse, bOverride);
				return;
			}
		}
		UE_LOG(LogVeloxMovement, Warning, TEXT("TestKnockback: no in-process server world; run the command on the server"));
	}

	FAutoConsoleCommandWithWorldAndArgs TestKnockbackCommand(
		TEXT("velox.move.TestKnockback"),
		TEXT("velox.move.TestKnockback X Y Z [override 0|1]: server-side knockback (cm/s) on the local player's pawn through AddKnockback. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TestKnockback));
}
#endif // !UE_BUILD_SHIPPING
