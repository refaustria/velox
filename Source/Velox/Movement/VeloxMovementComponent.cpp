// Velox - custom character movement component (ADR 0002 section 2 override table).

#include "Movement/VeloxMovementComponent.h"

#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
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

	// One params build per move, reused by every CalcVelocity of this move (A8).
	SimParams = MovementSettings->BuildSimParams();
	RampExitVelocityZ = 0.f;

	ConsumePendingKnockback();
	MoveStartKnockbackGrace = KnockbackGrace;

	Super::PerformMovement(DeltaSeconds);

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

	Velocity = bOnGround
		? VeloxMoveSim::GroundMove(Velocity, Input, DeltaTime, MoveStartKnockbackGrace, SimParams)
		: VeloxMoveSim::AirMove(Velocity, Input, DeltaTime, SimParams);
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
	// Auto-hop: jump held on landing jumps in the landing frame, before any ground friction (Q3 checks the jump before
	// PM_Friction). NavWalking ground mode keeps the engine path (it validates the navmesh in ProcessLanded).
	const bool bAutoHop = HasVeloxPhysics() && SimParams.bAutoHop && bWantsJumpHeld && CharacterOwner
		&& GetGroundMovementMode() == MOVE_Walking && !bWantsToCrouch;
	if (!bAutoHop)
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
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ramps and steps

float UVeloxMovementComponent::ComputeRampExitVelocityZ(const FVector& FloorImpactNormal, const FVector& FloorContactNormal) const
{
	// Same conditions under which ComputeGroundMovementDelta moves along the ramp plane.
	const float RampMaxNormalZ = FMath::Cos(FMath::DegreesToRadians(SimParams.RampExitMinAngle));
	if (FloorImpactNormal.Z >= RampMaxNormalZ || FloorImpactNormal.Z <= UE_KINDA_SMALL_NUMBER || FloorContactNormal.Z <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0.0);
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
	const float ClimbRate = ComputeRampExitVelocityZ(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal);
	if (ClimbRate <= 0.f)
	{
		return false;
	}
	const FVector RampVelocity(Velocity.X, Velocity.Y, ClimbRate);
	return (RampVelocity | NewFloor.HitResult.ImpactNormal) > SimParams.RampSlideMinNormalSpeed;
}

void UVeloxMovementComponent::HandleWalkingOffLedge(const FVector& PreviousFloorImpactNormal, const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta)
{
	Super::HandleWalkingOffLedge(PreviousFloorImpactNormal, PreviousFloorContactNormal, PreviousLocation, TimeDelta);

	// Both PhysWalking fall paths (ShouldCatchAir and CheckFall) call this right before StartFalling. Velocity is still
	// the horizontal move velocity of this iteration.
	RampExitVelocityZ = (HasVeloxPhysics() && IsMovingOnGround())
		? ComputeRampExitVelocityZ(PreviousFloorImpactNormal, PreviousFloorContactNormal)
		: 0.f;
}

void UVeloxMovementComponent::StartFalling(int32 Iterations, float remainingTime, float timeTick, const FVector& Delta, const FVector& subLoc)
{
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
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	RampExitVelocityZ = 0.f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Network

void UVeloxMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	bWantsJumpHeld = (Flags & FSavedMove_Velox::FLAG_JumpHeld) != 0;
	// TODO(M1 task 04): bHasForwardInput / bHasSideInput from FLAG_Custom_1 / FLAG_Custom_2 once FSavedMove_Velox sends them.
}
