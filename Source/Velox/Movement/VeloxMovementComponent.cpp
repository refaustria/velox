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

	// ADR 0003 section 6: Linear smoothing draws simulated proxies on the server trajectory interpolated at
	// SmoothingClientTimeStamp (in that pawn's stamp domain), so a client can name the pose it aimed at
	// (GetRenderedServerStamp). The engine default (Exponential) has no such stamp.
	NetworkSmoothingMode = ENetworkSmoothingMode::Linear;

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
		// Haste scales the wish-speed cap like the sim (ADR 0004 section 3); navigation and AI read it here.
		return SimParams.MaxGroundSpeed * (bCrouchScaled ? SimParams.CrouchSpeedScale : 1.f) * GetHasteSpeedScale();
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

	// ADR 0004 section 3: Haste in the move timeline, evaluated at this move's time stamp on both ends. A replay recomputes
	// it from the replayed move's stamp and the end times of the response it replays from (nothing is restored per move).
	const float PowerupClock = GetPowerupClockNow();
	MoveSpeedScale = IsPowerupActiveAt(PowerupMoveTimes.HasteEnd, PowerupClock) && SimParams.HasteSpeedScale > 0.f ? SimParams.HasteSpeedScale : 1.f;
#if !UE_BUILD_SHIPPING
	const float StartSpeed2D = Velocity.Size2D();
#endif

	// VQ3 clears PMF_JUMP_HELD on the first frame without jump (A12). The move's own flag, so the server and a replay
	// derive the same value. A jump started by CheckJumpInput before this call has held = true and keeps the latch.
	bJumpLatched = JumpLatchAtMoveStart(bJumpLatched, bWantsJumpHeld);

	// bJumpedThisMove is not cleared here: a take-off jump of this move already happened in CheckJumpInput, right before
	// this call; ControlledCharacterMove / MoveAutonomous clear it before that (task M1-07).

#if !UE_BUILD_SHIPPING
	const bool bLogKnockbackConsumed = PendingKnockback.IsPending() && CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority
		&& CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy; // a remote client's move (MoveAutonomous set the stamp)
#endif
	ConsumePendingKnockback();
#if !UE_BUILD_SHIPPING
	// ADR 0003 review 05/06 defect 9 (nettest KnockbackRun/KnockbackGroundRun): the server move that consumed the chained
	// knockback, so the soak checks a response's grace exactly: grace - (ts_resp - ts_start). Logged here, not inside
	// ConsumePendingKnockback, because the move's DeltaSeconds gives its start stamp.
	if (bLogKnockbackConsumed)
	{
		UE_LOG(LogVeloxNet, Log, TEXT("VeloxKnockbackConsumed ts_start=%.6f ts_end=%.6f grace=%.6f"), LastMoveAutonomousTimeStamp - DeltaSeconds, LastMoveAutonomousTimeStamp, KnockbackGrace);
	}
#endif
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

	// Move-timed events (projectile self splash): live owning-client moves and server moves only. A replay re-applies the
	// recorded result instead (PrepMoveFor). A correction teleport is not a move and never reaches this point
	// (bApplyingCorrection is cleared at the top of every move, A19), so only the replay flag is tested.
	if (!bInClientReplay && MoveTimedEvents.Num() > 0 && CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		RunDueMoveTimedEvents(GetCurrentMoveTimeStamp());
	}
	if (!bInClientReplay && CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		DropStalePowerupMoveTimes(PowerupClock);
#if !UE_BUILD_SHIPPING
		LogHasteStateChange(PowerupClock, DeltaSeconds, StartSpeed2D);
#endif
	}

#if !UE_BUILD_SHIPPING
	ensureMsgf(Velocity.SizeSquared() < FMath::Square(VeloxMovementConstants::SanityMaxSpeedFactor * SimParams.MaxGroundSpeed),
		TEXT("%s: velocity %s exceeds %.0f x MaxGroundSpeed, movement sim bug?"), *GetPathNameSafe(this), *Velocity.ToString(), VeloxMovementConstants::SanityMaxSpeedFactor);
#endif
}

void UVeloxMovementComponent::ConsumePendingKnockback()
{
	if (!PendingKnockback.IsPending() && !PendingPredictedKnockback.IsPending())
	{
		return;
	}

	// ADR 0003 section 3: the recorded predicted channel first, then the chained one; both set the grace. On the server
	// and in every client move without a prediction this is exactly the M1 single-channel computation.
	if (PendingPredictedKnockback.IsPending())
	{
		Velocity = VeloxMoveSim::ApplyKnockback(Velocity, PendingPredictedKnockback.Velocity, false);
		KnockbackGrace = FMath::Max(KnockbackGrace, VeloxMoveSim::KnockbackGraceFor(PendingPredictedKnockback.Velocity, SimParams));
		PendingPredictedKnockback.Reset();
	}
	if (PendingKnockback.IsPending())
	{
		Velocity = VeloxMoveSim::ApplyKnockback(Velocity, PendingKnockback.Velocity, PendingKnockback.bOverride);
		KnockbackGrace = FMath::Max(KnockbackGrace, VeloxMoveSim::KnockbackGraceFor(PendingKnockback.Velocity, SimParams));
	}

	// Lift off when pushed away from the floor; PhysWalking would otherwise flatten the velocity.
	if (IsMovingOnGround() && (Velocity | CurrentFloor.HitResult.ImpactNormal) > SimParams.RampSlideMinNormalSpeed)
	{
		SetMovementMode(MOVE_Falling);
	}
	bForceNextFloorCheck = true;
	PendingKnockback.Reset();
}

bool UVeloxMovementComponent::AddKnockback(FVector Impulse, bool bOverrideVelocity, EVeloxKnockbackSource Source, uint8 PredictedSeq)
{
	if (Source == EVeloxKnockbackSource::Predicted)
	{
		// The recorded channel (ADR 0003 section 3); additive only.
		return AddPredictedKnockback(Impulse, PredictedSeq);
	}
	if (Source == EVeloxKnockbackSource::Server && CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_Authority)
	{
		// Server impulses reach clients through the movement correction only; nothing reads client-side damage knockback.
		UE_LOG(LogVeloxMovement, Warning, TEXT("%s: AddKnockback(Server) called without authority; impulse %s dropped"), *GetPathNameSafe(this), *Impulse.ToString());
		return false;
	}
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

bool UVeloxMovementComponent::AddPredictedKnockback(const FVector& Impulse, uint8 PredictedSeq)
{
	if (!CharacterOwner || CharacterOwner->GetLocalRole() != ROLE_AutonomousProxy)
	{
		// Authority resolves its own explosions through GE_Damage -> AddKnockback(Server); proxies never move.
		UE_LOG(LogVeloxMovement, Verbose, TEXT("%s: predicted knockback %s (seq %u) ignored outside the autonomous proxy"), *GetPathNameSafe(this), *Impulse.ToString(), PredictedSeq);
		return false;
	}
	if (Impulse.IsZero())
	{
		return false;
	}
	PendingPredictedKnockback.Add(Impulse, PredictedSeq);
	UE_LOG(LogVeloxMovement, Log, TEXT("VeloxPredictedKnockback t=%.3f ts=%.6f seq=%u impulse=%s"), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0,
		GetCurrentMoveTimeStamp(), PredictedSeq, *Impulse.ToCompactString());
	return true;
}

void UVeloxMovementComponent::ServerBeginSelfKnockbackShot(uint8 Seq)
{
	if (ServerUnresolvedSelfKnockbackSeqs.IsEmpty())
	{
		ConfirmedSelfKnockbackSeq = static_cast<uint8>(Seq - 1);
	}
	ServerUnresolvedSelfKnockbackSeqs.Add(Seq);
}

void UVeloxMovementComponent::ServerResolveSelfKnockbackShot(uint8 Seq)
{
	ServerUnresolvedSelfKnockbackSeqs.RemoveSingle(Seq);
	ConfirmedSelfKnockbackSeq = NewerShotSeq(ConfirmedSelfKnockbackSeq, Seq);
}

void UVeloxMovementComponent::ServerNoteShot(uint8 Seq)
{
	if (ServerUnresolvedSelfKnockbackSeqs.IsEmpty())
	{
		ConfirmedSelfKnockbackSeq = NewerShotSeq(ConfirmedSelfKnockbackSeq, Seq);
	}
}

bool UVeloxMovementComponent::QueueMoveShot(const FVeloxMoveShot& Shot)
{
	if (!CharacterOwner || CharacterOwner->GetLocalRole() != ROLE_AutonomousProxy)
	{
		// Listen host, bots, standalone: no move stream; the fire RPC path is unchanged there (ADR 0003 B35).
		return false;
	}
	if (bHasPendingMoveShot)
	{
		UE_LOG(LogVeloxNet, Warning, TEXT("VeloxMoveShotOverwritten seq=%u by seq=%u (two shots before one move; the first one falls back to the fire RPC)"),
			PendingMoveShot.ShotSeq, Shot.ShotSeq);
	}
	PendingMoveShot = Shot;
	bHasPendingMoveShot = true;
	return true;
}

bool UVeloxMovementComponent::ConsumePendingMoveShot(FVeloxMoveShot& OutShot)
{
	if (!bHasPendingMoveShot)
	{
		return false;
	}
	OutShot = PendingMoveShot;
	PendingMoveShot = FVeloxMoveShot();
	bHasPendingMoveShot = false;
	return true;
}

int32 UVeloxMovementComponent::AddMoveTimedEvent(float MoveTime, FVeloxMoveTimedCallback&& Callback)
{
	FMoveTimedEvent& Event = MoveTimedEvents.AddDefaulted_GetRef();
	Event.Id = NextMoveTimedEventId++;
	Event.MoveTime = MoveTime;
	Event.Callback = MoveTemp(Callback);
	return Event.Id;
}

bool UVeloxMovementComponent::RunMoveTimedEventNow(int32 EventId)
{
	const int32 Index = MoveTimedEvents.IndexOfByPredicate([EventId](const FMoveTimedEvent& E) { return E.Id == EventId; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	FVeloxMoveTimedCallback Callback = MoveTemp(MoveTimedEvents[Index].Callback);
	MoveTimedEvents.RemoveAt(Index);
	if (Callback)
	{
		Callback(*this);
	}
	return true;
}

bool UVeloxMovementComponent::CancelMoveTimedEvent(int32 EventId)
{
	return MoveTimedEvents.RemoveAll([EventId](const FMoveTimedEvent& E) { return E.Id == EventId; }) > 0;
}

int32 UVeloxMovementComponent::CancelAllMoveTimedEvents()
{
	const int32 Count = MoveTimedEvents.Num();
	MoveTimedEvents.Reset();
	return Count;
}

bool UVeloxMovementComponent::GetMoveTimedEventTime(int32 EventId, float& OutMoveTime) const
{
	const FMoveTimedEvent* Event = MoveTimedEvents.FindByPredicate([EventId](const FMoveTimedEvent& E) { return E.Id == EventId; });
	OutMoveTime = Event ? Event->MoveTime : 0.f;
	return Event != nullptr;
}

bool UVeloxMovementComponent::HasMoveTimedEvent(int32 EventId) const
{
	return MoveTimedEvents.ContainsByPredicate([EventId](const FMoveTimedEvent& E) { return E.Id == EventId; });
}

void UVeloxMovementComponent::RunDueMoveTimedEvents(float MoveTime)
{
	// Take the due callbacks out first: a callback may add or cancel events (or apply damage that ends the pawn's life).
	TArray<FVeloxMoveTimedCallback, TInlineAllocator<2>> Due;
	for (int32 Index = 0; Index < MoveTimedEvents.Num();)
	{
		if (IsMoveTimeDue(MoveTimedEvents[Index].MoveTime, MoveTime, MinTimeBetweenTimeStampResets))
		{
			Due.Add(MoveTemp(MoveTimedEvents[Index].Callback));
			MoveTimedEvents.RemoveAt(Index);
		}
		else
		{
			++Index;
		}
	}
	for (FVeloxMoveTimedCallback& Callback : Due)
	{
		if (Callback)
		{
			Callback(*this);
		}
	}
}

float UVeloxMovementComponent::GetClientMoveTimeStamp() const
{
	const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	return ClientData ? ClientData->CurrentTimeStamp : 0.f;
}

float UVeloxMovementComponent::GetServerClientMoveTimeStamp() const
{
	const FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character();
	return ServerData ? ServerData->CurrentClientTimeStamp : 0.f;
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

// ---------------------------------------------------------------------------------------------------------------------
// Powerups in the move timeline (ADR 0004 section 3)

float UVeloxMovementComponent::PowerupTimeLeft(float End, float MoveTime, float ResetPeriod)
{
	double Diff = static_cast<double>(End) - static_cast<double>(MoveTime);
	if (ResetPeriod > 0.f)
	{
		const double Period = ResetPeriod;
		Diff = FMath::Fmod(Diff, Period);
		if (Diff > 0.5 * Period)
		{
			Diff -= Period;
		}
		else if (Diff <= -0.5 * Period)
		{
			Diff += Period;
		}
	}
	return static_cast<float>(Diff);
}

float UVeloxMovementComponent::MakePowerupEnd(double Now, float RemainingSeconds, float ResetPeriod)
{
	if (RemainingSeconds <= 0.f)
	{
		return 0.f;
	}
	const float Remaining = ResetPeriod > 0.f ? FMath::Min(RemainingSeconds, PowerupMaxRemainingSeconds(ResetPeriod)) : RemainingSeconds;
	double End = Now + Remaining;
	if (ResetPeriod > 0.f)
	{
		End = FMath::Fmod(End, static_cast<double>(ResetPeriod));
		if (End <= 0.0)
		{
			// 0 means "none"; the same instant one period later is equivalent in the wrapped domain.
			End += ResetPeriod;
		}
	}
	return static_cast<float>(End);
}

float UVeloxMovementComponent::GetPowerupClockNow() const
{
	if (!CharacterOwner)
	{
		return 0.f;
	}
	const ENetRole Role = CharacterOwner->GetLocalRole();
	const bool bRemoteOwner = Role == ROLE_Authority && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy && !CharacterOwner->IsLocallyControlled();
	if (Role == ROLE_AutonomousProxy || bRemoteOwner)
	{
		// Client stamps (B25 domain): the move being simulated, or the end of the last one.
		return GetCurrentMoveTimeStamp();
	}
	// Bots, listen host, standalone, simulated proxies: no client stamps, world time (B12), wrapped like client stamps.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	return MinTimeBetweenTimeStampResets > 0.f ? static_cast<float>(FMath::Fmod(Now, static_cast<double>(MinTimeBetweenTimeStampResets))) : static_cast<float>(Now);
}

bool UVeloxMovementComponent::IsPowerupActiveNow(EVeloxMovePowerup Powerup) const
{
	const float End = Powerup == EVeloxMovePowerup::Haste ? PowerupMoveTimes.HasteEnd : PowerupMoveTimes.QuadEnd;
	return End != 0.f && IsPowerupActiveAt(End, GetPowerupClockNow());
}

float UVeloxMovementComponent::GetHasteSpeedScale() const
{
	return HasVeloxPhysics() && SimParams.HasteSpeedScale > 0.f && IsPowerupActiveNow(EVeloxMovePowerup::Haste) ? SimParams.HasteSpeedScale : 1.f;
}

void UVeloxMovementComponent::ServerSetPowerupRemaining(EVeloxMovePowerup Powerup, float RemainingSeconds)
{
	if (!CharacterOwner || CharacterOwner->GetLocalRole() != ROLE_Authority)
	{
		return;
	}
	const float Clock = GetPowerupClockNow();
	const float End = MakePowerupEnd(Clock, RemainingSeconds, MinTimeBetweenTimeStampResets);
	float& Slot = Powerup == EVeloxMovePowerup::Haste ? PowerupMoveTimes.HasteEnd : PowerupMoveTimes.QuadEnd;
	if (Slot != End)
	{
		Slot = End;
		UE_LOG(LogVeloxMovement, Log, TEXT("VeloxPowerupMoveTime powerup=%s pawn=%s t=%.3f clock=%.6f remaining=%.3f end=%.6f"), Powerup == EVeloxMovePowerup::Haste ? TEXT("haste") : TEXT("quad"),
			*GetNameSafe(CharacterOwner), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0, Clock, RemainingSeconds, End);
	}
}

void UVeloxMovementComponent::ApplyReceivedPowerupMoveTimes(const FVeloxPowerupMoveTimes& InTimes, float TimeStamp, const TCHAR* Kind)
{
	if (InTimes == PowerupMoveTimes)
	{
		return;
	}
	PowerupMoveTimes = InTimes;
	UE_LOG(LogVeloxMovement, Log, TEXT("VeloxPowerupMoveTimes recv=%s t=%.3f ts=%.6f haste_end=%.6f quad_end=%.6f"), Kind, GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0, TimeStamp,
		PowerupMoveTimes.HasteEnd, PowerupMoveTimes.QuadEnd);
}

void UVeloxMovementComponent::DropStalePowerupMoveTimes(float MoveClock)
{
	const float Stale = -PowerupStaleSeconds(MinTimeBetweenTimeStampResets);
	for (float* End : { &PowerupMoveTimes.HasteEnd, &PowerupMoveTimes.QuadEnd })
	{
		if (*End != 0.f && PowerupTimeLeft(*End, MoveClock, MinTimeBetweenTimeStampResets) < Stale)
		{
			*End = 0.f;
		}
	}
}

#if !UE_BUILD_SHIPPING
void UVeloxMovementComponent::LogHasteStateChange(float MoveClock, float DeltaSeconds, float StartSpeed2D)
{
	const bool bActive = MoveSpeedScale != 1.f;
	if (bActive)
	{
		SoakHastePeak2D = FMath::Max(SoakHastePeak2D, static_cast<float>(Velocity.Size2D()));
	}
	if (bActive == bSoakHasteActive)
	{
		return;
	}
	bSoakHasteActive = bActive;
	const bool bServer = CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority;
	// dt: a combined client move or a merged server move starts at ts - dt; the boundary both ends must share.
	UE_LOG(LogVeloxNet, Log, TEXT("VeloxHasteState role=%s t=%.3f ts=%.6f dt=%.6f active=%d speed2d=%.1f peak2d=%.1f end=%.6f scale=%.3f"), bServer ? TEXT("server") : TEXT("client"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0, MoveClock, DeltaSeconds, bActive ? 1 : 0, StartSpeed2D, bActive ? 0.f : SoakHastePeak2D, PowerupMoveTimes.HasteEnd, MoveSpeedScale);
	if (bActive)
	{
		SoakHastePeak2D = static_cast<float>(Velocity.Size2D());
	}
}
#endif

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

void UVeloxMovementComponent::ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds)
{
	// A new live move begins; its CheckJumpInput (take-off) runs inside Super before PerformMovement (task M1-07).
	bJumpedThisMove = false;
	Super::ControlledCharacterMove(InputVector, DeltaSeconds);
}

void UVeloxMovementComponent::ServerMove_HandleMoveData(const FCharacterNetworkMoveDataContainer& MoveDataContainer)
{
	// Task M1-07: the extra old move is older than the engine's old move; simulate it first. Every container this
	// component receives is its own FVeloxNetworkMoveDataContainer (SetNetworkMoveDataContainer in the constructor).
	if (&MoveDataContainer == &VeloxNetworkMoveDataContainer)
	{
		if (FVeloxNetworkMoveData* ExtraOldMove = VeloxNetworkMoveDataContainer.GetExtraOldMoveData())
		{
			SetCurrentNetworkMoveData(ExtraOldMove);
			ServerMove_PerformMovement(*ExtraOldMove);
			SetCurrentNetworkMoveData(nullptr);
		}
	}
	Super::ServerMove_HandleMoveData(MoveDataContainer);
}

void UVeloxMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	LastMoveAutonomousTimeStamp = ClientTimeStamp;
	// A new move begins (server, client replay); Super runs CheckJumpInput (take-off) before PerformMovement (task M1-07).
	bJumpedThisMove = false;
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
			// Task M1-08 (A35): which old-move slot repaired the loss. The extra slot is the move before the OldMove.
			const bool bExtraSlot = MoveData == static_cast<const FCharacterNetworkMoveData*>(VeloxNetworkMoveDataContainer.GetExtraOldMoveData());
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxMoveRepaired ts=%.6f dt=%.6f slot=%s"), ClientTimeStamp, DeltaTime, bExtraSlot ? TEXT("extra") : TEXT("old"));
		}
	}
#endif
	// ADR 0002 A36 / ADR 0003 B35: the fire move's shot is registered before the move is simulated, so an event due inside
	// it (a point-blank plasma) runs at its end like on the client. Only moves of this component's own container carry one.
	const bool bServerMove = CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority;
	if (bServerMove && OnServerMoveShot.IsBound())
	{
		const FVeloxNetworkMoveData* VeloxMoveData = VeloxNetworkMoveDataContainer.AsVeloxMoveData(GetCurrentNetworkMoveData());
		if (VeloxMoveData && VeloxMoveData->bHasShot)
		{
			OnServerMoveShot.Broadcast(VeloxMoveData->Shot, ClientTimeStamp, DeltaTime);
		}
	}
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
	if (bServerMove)
	{
		// ADR 0003 B36 / ADR 0002 A36: the eye after this move, keyed by its end stamp, for the shot-origin validation.
		// Pushed before OnServerMoveProcessed so a pending fire RPC resumed there finds this move's eye (B35).
		ServerEyeRing.Push(ClientTimeStamp, CharacterOwner->GetPawnViewLocation());
		OnServerMoveProcessed.Broadcast(ClientTimeStamp);
	}
}

void FVeloxEyeRing::Push(float Stamp, const FVector& Eye)
{
	if (Count > 0)
	{
		FVeloxEyeSample& Newest = Samples[(Head + Count - 1) % Capacity];
		if (Stamp == Newest.Stamp)
		{
			Newest.Eye = Eye;
			return;
		}
		if (Stamp < Newest.Stamp)
		{
			Reset();
		}
	}
	if (Count == Capacity)
	{
		Head = (Head + 1) % Capacity;
		--Count;
	}
	Samples[(Head + Count) % Capacity] = FVeloxEyeSample{Stamp, Eye};
	++Count;
}

bool VeloxEyeRing::Lookup(const FVeloxEyeRing& Ring, float Stamp, FVector& OutEye)
{
	const int32 Num = Ring.Num();
	if (Num == 0 || !FMath::IsFinite(Stamp) || Stamp > Ring.Get(Num - 1).Stamp)
	{
		return false;
	}
	if (Stamp <= Ring.Get(0).Stamp)
	{
		OutEye = Ring.Get(0).Eye;
		return true;
	}
	// First entry with a stamp >= Stamp (exists: the newest is >= Stamp); entry 0 is < Stamp, so Low >= 1.
	int32 Low = 1;
	int32 High = Num - 1;
	while (Low < High)
	{
		const int32 Mid = (Low + High) / 2;
		if (Ring.Get(Mid).Stamp < Stamp)
		{
			Low = Mid + 1;
		}
		else
		{
			High = Mid;
		}
	}
	const FVeloxEyeSample& After = Ring.Get(Low);
	if (After.Stamp == Stamp)
	{
		OutEye = After.Eye;
		return true;
	}
	const FVeloxEyeSample& Before = Ring.Get(Low - 1);
	const float Alpha = (Stamp - Before.Stamp) / (After.Stamp - Before.Stamp);
	OutEye = FMath::Lerp(Before.Eye, After.Eye, static_cast<double>(Alpha));
	return true;
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
	// Acceleration encodes the input fraction x MaxGroundSpeed (GetMaxAcceleration, never scaled, so a client cannot
	// raise its own cap); Haste scales the wish speed and the caps by the move's own scale (Q3 PM_CmdScale x ps->speed).
	Input.WishSpeed = FMath::Min(static_cast<float>(Acceleration.Size2D()), SimParams.MaxGroundSpeed) * MoveSpeedScale;
	Input.SpeedScale = MoveSpeedScale;
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

bool UVeloxMovementComponent::CanCrouchInCurrentState() const
{
	// ADR 0003 section 6 (A11 option (c)): ground only. A mid-air capsule shrink would become a +20 cm crouch-jump that
	// Q3 does not have (Q3's air duck is cosmetic). Leaving the ground while crouched uncrouches (engine,
	// UpdateCharacterStateBeforeMovement) where there is room.
	return Super::CanCrouchInCurrentState() && IsMovingOnGround();
}

double VeloxProxySmoothing::RenderedStamp(TConstArrayView<FVeloxSmoothingAnchor> Anchors, const FVector& Capsule, double ExtrapolationSeconds, const FVector& Drawn,
	double MinStamp)
{
	if (Anchors.IsEmpty())
	{
		return 0.0;
	}
	double BestDistSq = TNumericLimits<double>::Max();
	double BestStamp = Anchors.Last().Stamp;
	// Segment A(SA) -> B(SB); the part before MinStamp is skipped. Later segments win ties (<=).
	auto Consider = [&](const FVector& A, double SA, const FVector& B, double SB)
	{
		if (SB < MinStamp)
		{
			return;
		}
		const double Span = SB - SA;
		const double MinU = (Span > 0.0 && SA < MinStamp) ? (MinStamp - SA) / Span : 0.0;
		const FVector AB = B - A;
		const double LenSq = AB.SizeSquared();
		const double U = LenSq > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(FVector::DotProduct(Drawn - A, AB) / LenSq, MinU, 1.0) : 1.0;
		const double DistSq = FVector::DistSquared(Drawn, A + AB * U);
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			BestStamp = SA + Span * U;
		}
	};
	for (int32 Index = 1; Index < Anchors.Num(); ++Index)
	{
		Consider(Anchors[Index - 1].Location, Anchors[Index - 1].Stamp, Anchors[Index].Location, Anchors[Index].Stamp);
	}
	const FVeloxSmoothingAnchor& Newest = Anchors.Last();
	Consider(Newest.Location, Newest.Stamp, Capsule, Newest.Stamp + FMath::Max(ExtrapolationSeconds, 0.0));
	return BestStamp;
}

double UVeloxMovementComponent::GetRenderedServerStamp() const
{
	if (!HasPredictionData_Client())
	{
		return 0.0;
	}
	const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	if (!ClientData)
	{
		return 0.0;
	}
	if (SmoothingAnchorCount == 0 || !UpdatedComponent || NetworkSmoothingMode != ENetworkSmoothingMode::Linear)
	{
		return ClientData->SmoothingClientTimeStamp;
	}
	TArray<FVeloxSmoothingAnchor, TInlineAllocator<VeloxProxySmoothing::MaxAnchors>> Anchors;
	for (int32 Index = 0; Index < SmoothingAnchorCount; ++Index)
	{
		Anchors.Add(SmoothingAnchors[(SmoothingAnchorHead + Index) % VeloxProxySmoothing::MaxAnchors]);
	}
	// Drawn place = capsule (newest anchor + SimulateMovement extrapolation) + the smoother's world mesh offset. The
	// extrapolation is timed by distance over the replicated speed, at most the time since the update (a blocked capsule
	// stops early).
	const FVector Capsule = UpdatedComponent->GetComponentLocation();
	const FVector Drawn = Capsule + ClientData->MeshTranslationOffset;
	const UWorld* World = GetWorld();
	const double SinceUpdate = World ? FMath::Max(World->GetTimeSeconds() - ClientData->LastCorrectionTime, 0.0) : 0.0;
	const double Speed = Velocity.Size();
	const double Extrapolation = Speed > UE_KINDA_SMALL_NUMBER ? FMath::Min(FVector::Dist(Capsule, Anchors.Last().Location) / Speed, SinceUpdate) : 0.0;
	// Lower bound SmoothingClientTimeStamp: the lerp starts at the previously drawn place and SCT advances with its alpha
	// towards the newest stamp while the capsule end is at or past it, so the drawn stamp never falls below SCT on a
	// locally linear path. Without the bound a ping-pong path (strafe reversal) offers the same place on the outgoing arm
	// ~0.25 s earlier.
	return VeloxProxySmoothing::RenderedStamp(Anchors, Capsule, Extrapolation, Drawn, ClientData->SmoothingClientTimeStamp);
}

void UVeloxMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation)
{
	Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
	const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	if (!CharacterOwner || CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy || !ClientData)
	{
		return;
	}
	// Super set SmoothingServerTimeStamp to this update's stamp.
	const double Stamp = ClientData->SmoothingServerTimeStamp;
	constexpr int32 Max = VeloxProxySmoothing::MaxAnchors;
	if (SmoothingAnchorCount > 0)
	{
		FVeloxSmoothingAnchor& Newest = SmoothingAnchors[(SmoothingAnchorHead + SmoothingAnchorCount - 1) % Max];
		if (Stamp == Newest.Stamp)
		{
			Newest.Location = NewLocation; // same server move resent (or moved without a new stamp): keep one point
			return;
		}
		if (Stamp < Newest.Stamp)
		{
			SmoothingAnchorCount = 0; // stamp reset (new pawn life, time-stamp wrap): the old path is another domain
		}
	}
	if (SmoothingAnchorCount == Max)
	{
		SmoothingAnchorHead = (SmoothingAnchorHead + 1) % Max;
		--SmoothingAnchorCount;
	}
	SmoothingAnchors[(SmoothingAnchorHead + SmoothingAnchorCount) % Max] = {Stamp, NewLocation};
	++SmoothingAnchorCount;
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
	// The replay's PrepMoveFor drops recorded predictions the server has resolved (ADR 0003 section 3).
	ConfirmedSelfKnockbackSeq = Response.ConfirmedSelfKnockbackSeq;
	// ADR 0004 section 3: the replay evaluates every move against the end times of the correction's check time (A18).
	ApplyReceivedPowerupMoveTimes(Response.PowerupMoveTimes, TimeStamp, TEXT("correction"));
#if !UE_BUILD_SHIPPING
	// Net soak (infra/ci/nettest.sh): what the server's response container delivered, before the replay consumes it.
	UE_LOG(LogVeloxMovement, Log, TEXT("VeloxCorrectionResponse t=%.3f ts=%.6f grace=%.6f latch=%d svel=%.1f kseq=%u haste_end=%.6f"), LastCorrectionTime, TimeStamp, KnockbackGrace, bJumpLatched ? 1 : 0,
		NewVelocity.Size(), ConfirmedSelfKnockbackSeq, PowerupMoveTimes.HasteEnd);
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

	// A predicted impulse queued after the last saved move (a self detonation at the end of the last live move) belongs
	// to the next live move, not to the replay; each replayed move gets its recorded one from PrepMoveFor.
	const FVeloxPredictedKnockback LivePredictedKnockback = PendingPredictedKnockback;
	PendingPredictedKnockback.Reset();

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
	PendingPredictedKnockback = LivePredictedKnockback;
	return bResult;
}

void UVeloxMovementComponent::CallServerMovePacked(const FSavedMove_Character* NewMove, const FSavedMove_Character* PendingMove, const FSavedMove_Character* OldMove)
{
	// A32: the server processes OldMove before PendingMove and NewMove (ServerMove_HandleMoveData). Sending the previous
	// RPC's move every time means a single lost RPC is simulated in its original partition by the next one (no merged
	// move, no partition-dependent integration, no trigger crossing shifted by one move); an unacked event move (pad
	// entry, held-jump landing) takes precedence. A duplicate is
	// rejected in ServerMove_PerformMovement (LogNetPlayerMovement "ServerMove: TimeStamp expired", Log level; a Warning only
	// beyond net.NetServerMoveTimestampExpiredWarningThreshold; visible with "log LogNetPlayerMovement Log").
	const FSavedMove_Character* ChosenOldMove = OldMove;
	const FSavedMove_Character* ExtraOldMove = nullptr;
	bool bEventMove = false;
	if (const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
	{
		ChosenOldMove = FSavedMove_Velox::ChooseOldMove(ClientData->SavedMoves, ClientData->LastAckedMove, NewMove, PendingMove, OldMove, &bEventMove);
		// Task M1-07 (A32's deferred cl_packetdup 2): the move before the OldMove rides along too (pending half of a lost
		// dual RPC, two-RPC bursts, the start boundary of a take-off jump).
		ExtraOldMove = FSavedMove_Velox::ChooseExtraOldMove(ClientData->SavedMoves, ClientData->LastAckedMove, NewMove, PendingMove, ChosenOldMove);
	}
	VeloxNetworkMoveDataContainer.SetClientExtraOldMove(ExtraOldMove);
	if (ExtraOldMove)
	{
		++NumExtraResentMoves;
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
	const double ExtraStamp = ExtraOldMove ? ExtraOldMove->TimeStamp : -1.0;
	if (ChosenOldMove)
	{
		if (bEventMove)
		{
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxResend old=%.6f new=%.6f pending=%.6f event=1 extra=%.6f"), ChosenOldMove->TimeStamp, NewStamp, PendingStamp, ExtraStamp);
		}
		else
		{
			UE_LOG(LogVeloxNet, Verbose, TEXT("VeloxResend old=%.6f new=%.6f pending=%.6f event=0 extra=%.6f"), ChosenOldMove->TimeStamp, NewStamp, PendingStamp, ExtraStamp);
		}
	}
	else
	{
		UE_LOG(LogVeloxNet, Verbose, TEXT("VeloxSend new=%.6f pending=%.6f"), NewStamp, PendingStamp);
	}
#endif

	Super::CallServerMovePacked(NewMove, PendingMove, ChosenOldMove);
	// The fill clears it; also when Super returned before filling (never keep a pointer into SavedMoves).
	VeloxNetworkMoveDataContainer.SetClientExtraOldMove(nullptr);
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
	// ADR 0004 section 3: an ack carries the server's live end times after the acked move. Every later move (live, or a
	// replay after a later correction, which brings its own values) is newer than the acked one. Skipped with the ack
	// above: the pending replay must use the correction's snapshot. The container holds this ack on the packed path only.
	const FVeloxMoveResponseDataContainer& Response = static_cast<const FVeloxMoveResponseDataContainer&>(GetMoveResponseDataContainer());
	if (Response.IsGoodMove() && Response.ClientAdjustment.TimeStamp == TimeStamp)
	{
		ApplyReceivedPowerupMoveTimes(Response.PowerupMoveTimes, TimeStamp, TEXT("ack"));
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
		ServerCorrectionState.ConfirmedSelfKnockbackSeq = ConfirmedSelfKnockbackSeq;
		ServerCorrectionState.PowerupMoveTimes = PowerupMoveTimes;
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
	NumExtraResentMoves = 0;
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
