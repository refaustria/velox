// Velox - custom character movement component (ADR 0001 sections 1 and 3, ADR 0002 section 2).
//
// Quake-style physics through the CMC velocity hooks: the engine keeps PhysWalking/PhysFalling, floor finding,
// stepping, substepping, saved moves and corrections; Velox replaces the velocity integration (CalcVelocity ->
// VeloxMoveSim), the jump, the landing frame (auto-hop) and the ramp/step/slope rules. All tunables come from
// UVeloxMovementSettings; engine fields are written only in SetMovementSettings().

#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/VeloxMoveSim.h"
#include "Movement/VeloxNetworkMoveData.h"

#include "VeloxMovementComponent.generated.h"

class UVeloxMovementSettings;

/** One server update as received by a simulated proxy: the replicated stamp and the world location it carried. */
struct FVeloxSmoothingAnchor
{
	double Stamp = 0.0;
	FVector Location = FVector::ZeroVector;
};

/**
 * World-free part of UVeloxMovementComponent::GetRenderedServerStamp (task 08b, D1). Under Linear smoothing a simulated
 * proxy is drawn at Capsule + MeshTranslationOffset: the capsule sits at the newest received server pose extrapolated
 * by SimulateMovement, and the offset lerps the previously drawn place onto it, so the drawn place is neither the pose at
 * SmoothingClientTimeStamp nor at the newest stamp. RenderedStamp returns the stamp of the point of the received server
 * trajectory nearest to Drawn: the polyline through Anchors (oldest first, increasing stamps) plus the extrapolation
 * segment from the newest anchor to Capsule, timed by ExtrapolationSeconds; stamps below MinStamp are not considered.
 * Nearest wins; on equal distance the newer point. This is exact while the server path is locally linear (the soak
 * measures |dt| < 1 ms) and degrades to the nearest received pose where the drawn place left the server path (the
 * extrapolation overshoot at a direction reversal).
 */
namespace VeloxProxySmoothing
{
	/** Anchors kept per proxy: 32 updates cover 0.53 s at 60 Hz, more than MaxClientSmoothingDeltaTime (0.5 s), the farthest SCT trails the newest stamp. */
	inline constexpr int32 MaxAnchors = 32;

	VELOX_API double RenderedStamp(TConstArrayView<FVeloxSmoothingAnchor> Anchors, const FVector& Capsule, double ExtrapolationSeconds, const FVector& Drawn,
		double MinStamp);
}

/**
 * Where a knockback comes from (ADR 0003 section 3, ADR 0002 A17). Overlap (pads) and Server (every server-side impulse:
 * damage, the server's own self-knockback) accumulate into the chained PendingKnockback. Predicted (the owning client's
 * self-knockback from its fake projectile) goes into the recorded PendingPredictedKnockback, accepted on the autonomous
 * proxy only.
 */
UENUM(BlueprintType)
enum class EVeloxKnockbackSource : uint8
{
	Overlap,
	Server,
	Predicted
};

/** Powerups that live in the owner's move timeline (ADR 0004 section 3): FVeloxPowerupMoveTimes::HasteEnd / QuadEnd. */
enum class EVeloxMovePowerup : uint8
{
	Haste,
	Quad
};

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UVeloxMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Movement tunables. May be null until pawn data has been applied (then the component behaves like the stock CMC). */
	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	const UVeloxMovementSettings* GetMovementSettings() const;

	/**
	 * Must be called identically on server and owning client (the character does this in PostInitializeComponents).
	 * Rebuilds the cached sim params and is the only place engine movement fields are written.
	 */
	void SetMovementSettings(const UVeloxMovementSettings* InMovementSettings);

	/** Sim params of the current move (rebuilt at the start of every PerformMovement). Zeros without settings. */
	const FVeloxMoveParams& GetSimParams() const { return SimParams; }

	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	bool IsJumpHeld() const { return bWantsJumpHeld; }

	/** Locally controlled input state; replicated to the server through FSavedMove_Velox::FLAG_JumpHeld. */
	void SetJumpHeld(bool bHeld) { bWantsJumpHeld = bHeld; }

	/**
	 * VQ3 PMF_JUMP_HELD (ADR 0002 A12): set by a successful jump, cleared at the start of a move without jump held. With
	 * bAutoHop off, a landing jumps only when the latch is clear. Derived from the flag stream on the server.
	 */
	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	bool IsJumpLatched() const { return bJumpLatched; }

	/** Correction only (A17: the latch chains through replays, PrepMoveFor never restores it). */
	void SetJumpLatched(bool bLatched) { bJumpLatched = bLatched; }

	/** A12: the latch at the start of a move (cleared by a move without jump held). Shared with the JumpLatch test. */
	static bool JumpLatchAtMoveStart(bool bLatched, bool bJumpHeld) { return bLatched && bJumpHeld; }

	/** A12: whether a landing jumps in the landing frame (auto-hop, or VQ3 after a release). Shared with the JumpLatch test. */
	static bool WantsLandingJump(bool bJumpHeld, bool bAutoHop, bool bLatched) { return bJumpHeld && (bAutoHop || !bLatched); }

	/**
	 * Raw move input of this frame, X = forward, Y = right (player input or bot). Only its presence per axis is used
	 * (CPM side-only / forward-only rules); the wish vector itself always comes from Acceleration.
	 */
	UFUNCTION(BlueprintCallable, Category = "Velox|Movement")
	void SetLocalMoveInput(FVector2D ForwardRight);

	bool HasForwardInput() const { return bHasForwardInput; }
	bool HasSideInput() const { return bHasSideInput; }

	/**
	 * Queues a velocity change for the start of the next move (ADR 0002 section 4): rockets, jump pads, console tests.
	 * Adds Impulse to the velocity, or replaces the velocity when bOverrideVelocity (jump pads, Q3 trigger_push). Several
	 * calls before the next move accumulate (an override replaces everything queued before it). Sets a knockback grace
	 * (no ground friction) of KnockbackGraceFor(Impulse). Callable on the authority and the owning client (predicted
	 * sources such as jump pads call it on both); ignored on simulated proxies, which never run moves, and on the owning
	 * client while it applies a correction teleport (A19: the server's response already holds every entry it saw).
	 * Source Predicted forwards to AddPredictedKnockback(Impulse, PredictedSeq) (bOverrideVelocity is ignored there).
	 */
	UFUNCTION(BlueprintCallable, Category = "Velox|Movement")
	bool AddKnockback(FVector Impulse, bool bOverrideVelocity = false, EVeloxKnockbackSource Source = EVeloxKnockbackSource::Overlap, uint8 PredictedSeq = 0);

	/**
	 * Predicted (ADR 0003 section 3): PredictedSeq is the shot sequence of the fake projectile whose detonation this is.
	 * Not reflected; game code on the owning client only.
	 */
	bool AddPredictedKnockback(const FVector& Impulse, uint8 PredictedSeq);

	/**
	 * ADR 0003 section 3: a replayed move re-applies its recorded predicted self-knockback only while the server has not
	 * resolved that shot yet (wrap-aware uint8 compare: int8(Seq - Confirmed) > 0). Used by FSavedMove_Velox::PrepMoveFor.
	 */
	static bool ShouldReplayPredictedKnockback(uint8 Seq, uint8 ConfirmedSeq) { return static_cast<int8>(static_cast<uint8>(Seq - ConfirmedSeq)) > 0; }

	/** Wrap-aware newer of two shot sequences (int8 difference). */
	static uint8 NewerShotSeq(uint8 A, uint8 B) { return static_cast<int8>(static_cast<uint8>(B - A)) > 0 ? B : A; }

	/** Owning client: predicted self-knockback queued for the next move (recorded channel). */
	const FVeloxPredictedKnockback& GetPendingPredictedKnockback() const { return PendingPredictedKnockback; }

	/** Replay only (FSavedMove_Velox::PrepMoveFor): the recorded predicted impulse of the move about to be replayed. */
	void SetReplayPredictedKnockback(const FVeloxPredictedKnockback& InPredicted) { PendingPredictedKnockback = InPredicted; }

	/**
	 * Server: newest own shot whose self-knockback the server has resolved (applied, or found out of range). Client: the
	 * value of the last correction received (only read during the replay that follows it).
	 */
	uint8 GetConfirmedSelfKnockbackSeq() const { return ConfirmedSelfKnockbackSeq; }

	/**
	 * Server, remote owner: a projectile with self-knockback potential was fired. With no other shot unresolved every
	 * earlier shot is resolved, so the confirmed sequence moves up to Seq - 1 (keeps it within int8 range of new shots
	 * however many hitscan shots were fired in between).
	 */
	void ServerBeginSelfKnockbackShot(uint8 Seq);

	/** Server: the self-knockback of shot Seq is resolved; ConfirmedSelfKnockbackSeq = newer of it and Seq. */
	void ServerResolveSelfKnockbackShot(uint8 Seq);

	/**
	 * Server, remote owner (ADR 0003 B25): an accepted shot Seq without self-knockback potential (hitscan, a projectile
	 * without splash). While no self-knockback shot is unresolved, ConfirmedSelfKnockbackSeq moves up to the newer of
	 * itself and Seq, so int8(Seq - Confirmed) of the next rocket stays within the shots fired in one RTT however much
	 * hitscan fire preceded it. Ignored while a self-knockback shot is unresolved (an out-of-order confirmation would
	 * drop that shot's recorded prediction from a replay).
	 */
	void ServerNoteShot(uint8 Seq);

	/**
	 * Move-timed events (task 05: the owner's self splash of a projectile, evaluated in the owner's move timeline on both
	 * the owning client and the server so prediction and authority agree to the move). Callback runs at the end of the
	 * first live (owning client) or server-processed (authority) move whose time stamp is >= MoveTime, never in a replay.
	 * Returns an id > 0.
	 */
	using FVeloxMoveTimedCallback = TFunction<void(UVeloxMovementComponent&)>;
	int32 AddMoveTimedEvent(float MoveTime, FVeloxMoveTimedCallback&& Callback);
	/** Runs the event now if it is still pending (server deadline). */
	bool RunMoveTimedEventNow(int32 EventId);
	bool CancelMoveTimedEvent(int32 EventId);
	/** Drops every pending move-timed event without running it (death flow, ADR 0003 B25/B27); returns how many. */
	int32 CancelAllMoveTimedEvents();
	bool HasMoveTimedEvent(int32 EventId) const;
	int32 GetNumMoveTimedEvents() const { return MoveTimedEvents.Num(); }

	/**
	 * Whether an event at EventTime is due after a move ending at MoveTime. Time stamps wrap every ResetPeriod
	 * (MinTimeBetweenTimeStampResets) on both ends; an event more than half a period ahead was scheduled before a reset.
	 */
	static bool IsMoveTimeDue(float EventTime, float MoveTime, float ResetPeriod) { return MoveTime >= EventTime || EventTime - MoveTime > 0.5f * ResetPeriod; }

	/**
	 * Owning client: time stamp at the end of the last move performed (ClientData->CurrentTimeStamp), i.e. the start of
	 * the next one. A shot fired in PlayerTick happens there (B6: before the frame's move). 0 without prediction data.
	 */
	float GetClientMoveTimeStamp() const;

	/** Server: time stamp of the last client move processed (ServerData->CurrentClientTimeStamp); 0 without server data. */
	float GetServerClientMoveTimeStamp() const;

	const FVeloxPendingKnockback& GetPendingKnockback() const { return PendingKnockback; }

	/** A knockback is queued for the start of the next move (A28: the move that queued it is an important move). */
	bool HasPendingKnockback() const { return PendingKnockback.IsPending(); }

	/**
	 * DoJump succeeded in the current/last move (A29): the take-off jump CheckJumpInput starts right before PerformMovement
	 * or a landing jump inside it. Cleared where a move begins (ControlledCharacterMove, MoveAutonomous). With jump held it
	 * makes the move an event move (FSavedMove_Velox::bSavedJumpedHeld, task M1-07).
	 */
	bool JumpedThisMove() const { return bJumpedThisMove; }

	/**
	 * Time stamp of the move being simulated (A28(iii) instrumentation): on the server and during a client replay the
	 * ClientTimeStamp captured in MoveAutonomous, on the live owning client the prediction data's CurrentTimeStamp.
	 */
	float GetCurrentMoveTimeStamp() const;

	/** Where the current move runs, for the pad launch log (A28(iii)): live, replay, teleport (A19) or server. */
	const TCHAR* GetMoveSourceName() const;

	/** Correction only (A17: the pending knockback chains through replays); game code uses AddKnockback. */
	void SetPendingKnockback(const FVeloxPendingKnockback& InPendingKnockback) { PendingKnockback = InPendingKnockback; }

	/** Remaining ground-friction-free time after a knockback (s). */
	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	float GetKnockbackGrace() const { return KnockbackGrace; }

	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	float GetSpeed2D() const { return Velocity.Size2D(); }

	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	bool IsOnGround() const { return IsMovingOnGround(); }

	// Owning-client correction statistics (ADR 0002 section 5; HUD and net soak, task 05). Distance is between the
	// client's location after the corrected move and the server's.
	UFUNCTION(BlueprintPure, Category = "Velox|Movement|Net")
	int32 GetNumCorrections() const { return NumCorrections; }

	UFUNCTION(BlueprintPure, Category = "Velox|Movement|Net")
	float GetMaxCorrectionCm() const { return MaxCorrectionCm; }

	UFUNCTION(BlueprintPure, Category = "Velox|Movement|Net")
	float GetSumCorrectionCm() const { return SumCorrectionCm; }

	UFUNCTION(BlueprintPure, Category = "Velox|Movement|Net")
	float GetLastCorrectionCm() const { return LastCorrectionCm; }

	/** World time (s) of the last correction; negative if none yet. */
	UFUNCTION(BlueprintPure, Category = "Velox|Movement|Net")
	double GetLastCorrectionTime() const { return LastCorrectionTime; }

	UFUNCTION(BlueprintCallable, Category = "Velox|Movement|Net")
	void ResetCorrectionStats();

	/** Owning client: RPCs that carried a resent OldMove (A32), and all ServerMovePacked RPCs sent. */
	UFUNCTION(BlueprintPure, Category = "Velox|Movement|Net")
	int32 GetNumResentMoves() const { return NumResentMoves; }

	UFUNCTION(BlueprintPure, Category = "Velox|Movement|Net")
	int32 GetNumServerMoveRPCs() const { return NumServerMoveRPCs; }

	/** Owning client: RPCs that also carried an extra old move (task M1-07, A32's deferred cl_packetdup 2). */
	UFUNCTION(BlueprintPure, Category = "Velox|Movement|Net")
	int32 GetNumExtraResentMoves() const { return NumExtraResentMoves; }

	/** The `showdebug VeloxMovement` block (non-shipping; empty in shipping). */
	void DisplayVeloxDebug(class UCanvas* Canvas, float& YL, float& YPos) const;

	/**
	 * Simulated proxy: the server stamp of the pose currently drawn, in the domain of GetServerLastTransformUpdateTimeStamp
	 * on the server. The lag-comp hint of ADR 0003 section 6. Not SmoothingClientTimeStamp: that only times the mesh
	 * offset's lerp, while the capsule underneath is extrapolated from the newest server pose, so the drawn pose is
	 * up to ~80 ms newer than it (task 08b, D1). VeloxProxySmoothing::RenderedStamp over the anchors recorded in
	 * SmoothCorrection, no older than SmoothingClientTimeStamp (the drawn stamp's lower bound). 0 when the component has no
	 * client prediction data (server); SmoothingClientTimeStamp before the first update.
	 */
	double GetRenderedServerStamp() const;

	/** Server: Velox state snapshot for the correction queued by the last failed error check (A18). */
	const FVeloxCorrectionState& GetServerCorrectionState() const { return ServerCorrectionState; }

	// ---- Powerups in the move timeline (ADR 0004 section 3) ---------------------------------------------------------

	/**
	 * Signed seconds from MoveTime to End in a time-stamp domain that wraps every ResetPeriod
	 * (MinTimeBetweenTimeStampResets): End - MoveTime reduced into (-ResetPeriod / 2, ResetPeriod / 2]. Positive while the
	 * end lies ahead of the move. Exact across a time-stamp reset in either order (End stored before or after the reset),
	 * for durations below half a period.
	 */
	static float PowerupTimeLeft(float End, float MoveTime, float ResetPeriod);

	/** A powerup with end End (0 = none) is active in a move ending at MoveTime: End != 0 && PowerupTimeLeft > 0. */
	static bool IsPowerupActive(float End, float MoveTime, float ResetPeriod) { return End != 0.f && PowerupTimeLeft(End, MoveTime, ResetPeriod) > 0.f; }

	/** IsPowerupActive with this component's MinTimeBetweenTimeStampResets. */
	bool IsPowerupActiveAt(float End, float MoveTime) const { return IsPowerupActive(End, MoveTime, MinTimeBetweenTimeStampResets); }

	/**
	 * The end time of a powerup with RemainingSeconds left at Now: (Now + Remaining) normalised to (0, ResetPeriod];
	 * Remaining is clamped to PowerupMaxRemainingSeconds so the end stays unambiguous (PowerupTimeLeft). 0 for Remaining <= 0.
	 */
	static float MakePowerupEnd(double Now, float RemainingSeconds, float ResetPeriod);

	/** Longest remaining time the move timeline can represent: 0.45 x the reset period (108 s at the engine's 240 s). */
	static float PowerupMaxRemainingSeconds(float ResetPeriod) { return 0.45f * ResetPeriod; }

	/**
	 * Seconds after the end at which a non-replayed move drops a stale end time (a quarter period): long after every
	 * in-flight shot or move that could still refer to it, long before PowerupTimeLeft wraps to positive again.
	 */
	static float PowerupStaleSeconds(float ResetPeriod) { return 0.25f * ResetPeriod; }

	/**
	 * The move-time clock of this pawn: the move being simulated (server: the client stamp in MoveAutonomous, or the last
	 * one processed; owning client: the live move's CurrentTimeStamp or the replayed move's stamp; outside a move the end
	 * of the last move, where B6 shots happen), or world time for a pawn whose moves carry no client stamps (bots, listen
	 * host, standalone; ADR 0003 B12). Normalised into [0, MinTimeBetweenTimeStampResets) for world time.
	 */
	float GetPowerupClockNow() const;

	/** IsPowerupActiveAt(end of Powerup, GetPowerupClockNow()). */
	bool IsPowerupActiveNow(EVeloxMovePowerup Powerup) const;

	/** Haste active in the move timeline right now (HUD, AI, tests). */
	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	bool IsHasteActive() const { return IsPowerupActiveNow(EVeloxMovePowerup::Haste); }

	/** SimParams.HasteSpeedScale while Haste is active at GetPowerupClockNow(), else 1 (GetMaxSpeed, the sim). */
	float GetHasteSpeedScale() const;

	/** Speed scale of the move being simulated (computed at the start of PerformMovement). */
	float GetMoveSpeedScale() const { return MoveSpeedScale; }

	const FVeloxPowerupMoveTimes& GetPowerupMoveTimes() const { return PowerupMoveTimes; }

	/** Owning client (response receipt) and tests: replaces the end times. Game code on the server uses ServerSetPowerupRemaining. */
	void SetPowerupMoveTimes(const FVeloxPowerupMoveTimes& InTimes) { PowerupMoveTimes = InTimes; }

	/**
	 * Server: the powerup has RemainingSeconds left now (its GE's remaining time): End = MakePowerupEnd(GetPowerupClockNow(),
	 * RemainingSeconds); 0 when RemainingSeconds <= 0 (a premature removal). Sent with the next response.
	 */
	void ServerSetPowerupRemaining(EVeloxMovePowerup Powerup, float RemainingSeconds);

	//~UCharacterMovementComponent interface
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual float GetGravityZ() const override;
	virtual bool CanAttemptJump() const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;

	/**
	 * A25: ignores a good-move ack while a correction is pending replay (ClientData->bUpdatePosition). The engine acks
	 * without checking that flag, so an ack processed in the same net receive as a correction would cull the saved moves
	 * (ts_corr, ts_ack] that the replay of the correction still needs. Dropping the ack is lossless: the correction already
	 * acked up to ts_corr and the next good-move ack follows within NetworkMinTimeBetweenClientAckGoodMoves.
	 */
	virtual void ClientAckGoodMove_Implementation(float TimeStamp) override;
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode) override;
	/** Simulated proxy: records the server stamp and location of the update as an anchor of GetRenderedServerStamp (D1). */
	virtual void SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation) override;
	//~End of UCharacterMovementComponent interface

	// The single-argument DoJump is deprecated in the engine and forwards to the two-argument one.
	using Super::DoJump;

	// The deprecated UPrimitiveComponent overload forwards to the FMovementBaseInterfaceData one above.
	using Super::ServerMoveHandleClientError;

protected:
	//~UCharacterMovementComponent interface
	virtual void PerformMovement(float DeltaSeconds) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual FVector GetFallingLateralAcceleration(float DeltaTime) override;
	virtual bool ShouldLimitAirControl(float DeltaTime, const FVector& FallAcceleration) const override;
	virtual FVector HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const override;
	virtual bool IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const override;
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;
	virtual bool ShouldCatchAir(const FFindFloorResult& OldFloor, const FFindFloorResult& NewFloor) override;
	virtual void HandleWalkingOffLedge(const FVector& PreviousFloorImpactNormal, const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta) override;
	virtual void StartFalling(int32 Iterations, float remainingTime, float timeTick, const FVector& Delta, const FVector& subLoc) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection) override;

	/**
	 * Captures the move's ClientTimeStamp (server and client replay); non-shipping on the authority: logs merged server
	 * moves (VeloxMoveGap, A28(iv)/A34(i)) and old moves that filled a gap (VeloxMoveRepaired, A32).
	 */
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;

	/** Clears bJumpedThisMove before the engine's CheckJumpInput of the live (locally controlled) move. */
	virtual void ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds) override;

	/**
	 * Task M1-07 (A32's deferred cl_packetdup 2): simulates the Velox extra old move of the RPC, if any, before the engine
	 * handles old, pending and new move. It goes through ServerMove_PerformMovement as an old move (time stamp check, no
	 * client error check), so a duplicate is rejected like a duplicate OldMove.
	 */
	virtual void ServerMove_HandleMoveData(const FCharacterNetworkMoveDataContainer& MoveDataContainer) override;

	/**
	 * A31: PhysWalking calls this right after "make velocity reflect actual move" (and at the top/end of every iteration);
	 * restores the end velocity of the ground update after the engine integrated the mean velocity.
	 */
	virtual void MaintainHorizontalGroundVelocity() override;

	/**
	 * A32 (Quake cl_packetdup 1): replaces the engine's OldMove by FSavedMove_Velox::ChooseOldMove before sending, so the
	 * previous RPC's move (or an unacked event move) rides along with every RPC and a single lost RPC is repaired in its
	 * original move partition. Task M1-07: also packs FSavedMove_Velox::ChooseExtraOldMove (the move before the OldMove)
	 * into the container's extra slot. Non-shipping: logs VeloxResend / VeloxSend (LogVeloxNet) per RPC.
	 */
	virtual void CallServerMovePacked(const FSavedMove_Character* NewMove, const FSavedMove_Character* PendingMove, const FSavedMove_Character* OldMove) override;
	//~End of UCharacterMovementComponent interface

	// The deprecated UPrimitiveComponent overload forwards to the one above.
	using Super::OnClientCorrectionReceived;

	/** True when Velox physics are active (settings assigned). Without settings every hook defers to the engine. */
	bool HasVeloxPhysics() const { return MovementSettings != nullptr; }

	/** Applies PendingKnockback (velocity, grace, lift-off) at the start of a move (ADR 0002 section 4). */
	void ConsumePendingKnockback();

	/**
	 * A31 Fix D: ends the mean-velocity window opened by the ground branch of CalcVelocity. If the engine's velocity is
	 * still the mean (the move was not clipped: PhysWalking set Velocity = displacement / dt == Mean), the end velocity of
	 * the ground update is put back; otherwise the engine's clipped velocity is kept (walls, steps; A13/A23 unchanged).
	 * Returns whether the window was open.
	 */
	bool RestoreGroundEndVelocity();

	/** The ground update's end velocity while the A31 mean-velocity window is open, else Velocity. */
	const FVector& GetGroundEndVelocity() const { return bGroundMeanVelocityActive ? GroundEndVelocity : Velocity; }

	/**
	 * Vertical speed with which the pawn leaves a ramp with floor normal ImpactNormal at the current horizontal velocity:
	 * the climb rate of the engine's ramp movement (ComputeGroundMovementDelta, bMaintainHorizontalGroundVelocity),
	 * -(V_xy . N) / N.z. Zero for floors flatter than RampExitMinAngle and for descending motion.
	 */
	float ComputeRampExitVelocityZ(const FVector& FloorImpactNormal, const FVector& FloorContactNormal) const;

	/** ComputeRampExitVelocityZ for a given horizontal velocity (A31: ShouldCatchAir decides on the end velocity). */
	float ComputeRampExitVelocityZ(const FVector& FloorImpactNormal, const FVector& FloorContactNormal, const FVector& InVelocity) const;

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Movement")
	TObjectPtr<const UVeloxMovementSettings> MovementSettings;

	/** Jump button is held this move (sampled every move, unlike ACharacter::bPressedJump). */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Movement")
	uint8 bWantsJumpHeld : 1;

	/** Forward/back input present this frame (CPM rules). Sent to the server as FSavedMove_Velox::FLAG_HasForwardInput. */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Movement")
	uint8 bHasForwardInput : 1;

	/** Side input present this frame (CPM rules). Sent to the server as FSavedMove_Velox::FLAG_HasSideInput. */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Movement")
	uint8 bHasSideInput : 1;

	/** Jump latch (VQ3 PMF_JUMP_HELD, A12); see IsJumpLatched. */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Movement")
	uint8 bJumpLatched : 1;

	/** Ground-friction-free time left (s); counted down once per PerformMovement. */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Movement")
	float KnockbackGrace = 0.f;

	/** KnockbackGrace at the start of the current move (ADR 0002 A7, A22). */
	float MoveStartKnockbackGrace = 0.f;

	/**
	 * Simulated time of the current move consumed by earlier CalcVelocity calls (A27): reset in PerformMovement, advanced
	 * after every CalcVelocity, so each PhysWalking iteration of a split move gets the grace remaining at its own start.
	 */
	float MoveElapsedTime = 0.f;

	/** ClientTimeStamp of the last MoveAutonomous (server, client replay); A28(iii) instrumentation. */
	float LastMoveAutonomousTimeStamp = 0.f;

	/** DoJump succeeded in the current move (take-off or landing jump); cleared where a move begins (A29, task M1-07). */
	uint8 bJumpedThisMove : 1;

	/** Owning client: inside Super::ClientUpdatePositionAfterServerUpdate (the replay of saved moves); A28(iii). */
	uint8 bInClientReplay : 1;

	/**
	 * A31: CalcVelocity (MOVE_Walking) has set Velocity to GroundMeanVelocity for the engine's displacement
	 * Delta = Velocity * dt; RestoreGroundEndVelocity puts GroundEndVelocity back. Never open across a move.
	 */
	uint8 bGroundMeanVelocityActive : 1;

	/** A31: end velocity of the last ground velocity update (VeloxMoveSim::GroundMove). */
	FVector GroundEndVelocity = FVector::ZeroVector;

	/** A31: substep-trapezoid mean velocity of the last ground velocity update. */
	FVector GroundMeanVelocity = FVector::ZeroVector;

	/**
	 * Ramp vertical speed handed from HandleWalkingOffLedge to StartFalling within one call chain of PhysWalking.
	 * Never survives a move (cleared at move start, by StartFalling and on mode changes), so it needs no saved-move slot.
	 */
	float RampExitVelocityZ = 0.f;

	/** Velocity change to apply at the start of the next move. */
	FVeloxPendingKnockback PendingKnockback;

	/** Recorded channel (ADR 0003 section 3): predicted self-knockback for the start of the next move (owning client). */
	FVeloxPredictedKnockback PendingPredictedKnockback;

	/** See GetConfirmedSelfKnockbackSeq. */
	uint8 ConfirmedSelfKnockbackSeq = 0;

	/** Server: own projectile shots whose self-knockback is not resolved yet (ServerBeginSelfKnockbackShot). */
	TArray<uint8, TInlineAllocator<4>> ServerUnresolvedSelfKnockbackSeqs;

	struct FMoveTimedEvent
	{
		int32 Id = 0;
		float MoveTime = 0.f;
		FVeloxMoveTimedCallback Callback;
	};

	/** Pending move-timed events, in insertion order. */
	TArray<FMoveTimedEvent> MoveTimedEvents;
	int32 NextMoveTimedEventId = 1;

	/** Runs every due event (end of a live or server move). */
	void RunDueMoveTimedEvents(float MoveTime);

	/**
	 * Owning client: set in OnClientCorrectionReceived, before ClientAdjustPosition teleports the pawn; cleared at the
	 * start of ClientUpdatePositionAfterServerUpdate and of PerformMovement. While set, AddKnockback drops impulses so the
	 * teleport's pad BeginOverlap cannot launch on top of the server's state (A19).
	 */
	uint8 bApplyingCorrection : 1;

	/** Server: Velox state at the error check that queued the pending correction (A18). */
	FVeloxCorrectionState ServerCorrectionState;

	/** ADR 0004 section 3: powerup end times in the move-time domain (server: authoritative; owning client: last response). */
	FVeloxPowerupMoveTimes PowerupMoveTimes;

	/** FVeloxMoveInput::SpeedScale of the current move (Haste), set at the start of every PerformMovement. */
	float MoveSpeedScale = 1.f;

	/**
	 * Owning client: applies the powerup end times of a response (ack or correction) before any replay; non-shipping logs
	 * a change (VeloxPowerupMoveTimes).
	 */
	void ApplyReceivedPowerupMoveTimes(const FVeloxPowerupMoveTimes& InTimes, float TimeStamp, const TCHAR* Kind);

	/** Non-replayed moves (server, live owning client, bots): drops end times more than PowerupStaleSeconds in the past. */
	void DropStalePowerupMoveTimes(float MoveClock);

#if !UE_BUILD_SHIPPING
	/** Net soak (HasteRun): Haste state of the previous non-replayed move, and the peak XY speed while active. */
	bool bSoakHasteActive = false;
	float SoakHastePeak2D = 0.f;
	/** Logs VeloxHasteState when the Haste state of a non-replayed move (stamp MoveClock, length DeltaSeconds) differs from the previous one. */
	void LogHasteStateChange(float MoveClock, float DeltaSeconds, float StartSpeed2D);
#endif

	/** FVeloxMoveParams of the current move: built in SetMovementSettings and at the start of every PerformMovement (A8). */
	FVeloxMoveParams SimParams;

	// Correction statistics (owning client).
	int32 NumCorrections = 0;
	float MaxCorrectionCm = 0.f;
	float SumCorrectionCm = 0.f;
	float LastCorrectionCm = 0.f;
	double LastCorrectionTime = -1.0;

	/** TimeStamp of the last correction received (owning client); the replay must start at the move after it (A25). */
	float PendingCorrectionTimeStamp = -1.f;

	/** Owning client: RPCs sent with an OldMove (A32 resend), and RPCs sent in total. Net soak / debugging. */
	int32 NumResentMoves = 0;
	int32 NumServerMoveRPCs = 0;

	/** Owning client: RPCs that also carried an extra old move (task M1-07). Net soak / debugging. */
	int32 NumExtraResentMoves = 0;

	/** Simulated proxy: ring of received server updates (D1); SmoothingAnchors[(SmoothingAnchorHead + i) % MaxAnchors], i < SmoothingAnchorCount, oldest first. */
	FVeloxSmoothingAnchor SmoothingAnchors[VeloxProxySmoothing::MaxAnchors];
	int32 SmoothingAnchorHead = 0;
	int32 SmoothingAnchorCount = 0;

private:
	/** Persistent storage installed via SetNetworkMoveDataContainer in the constructor. */
	FVeloxNetworkMoveDataContainer VeloxNetworkMoveDataContainer;

	/** Persistent storage installed via SetMoveResponseDataContainer in the constructor. */
	FVeloxMoveResponseDataContainer VeloxMoveResponseDataContainer;
};
