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

/**
 * Where a knockback comes from (ADR 0003 section 3, ADR 0002 A17). Overlap (pads) and Server (every server-side impulse:
 * damage, the server's own self-knockback) accumulate into the chained PendingKnockback. Predicted (client self-knockback
 * from a fake projectile) needs the recorded channel of task 05 and is rejected until then.
 */
UENUM(BlueprintType)
enum class EVeloxKnockbackSource : uint8
{
	Overlap,
	Server,
	Predicted
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
	 */
	UFUNCTION(BlueprintCallable, Category = "Velox|Movement")
	bool AddKnockback(FVector Impulse, bool bOverrideVelocity = false, EVeloxKnockbackSource Source = EVeloxKnockbackSource::Overlap);

	/**
	 * ADR 0003 section 3: a replayed move re-applies its recorded predicted self-knockback only while the server has not
	 * resolved that shot yet (wrap-aware uint8 compare: int8(Seq - Confirmed) > 0). Used by task 05's PrepMoveFor.
	 */
	static bool ShouldReplayPredictedKnockback(uint8 Seq, uint8 ConfirmedSeq) { return static_cast<int8>(static_cast<uint8>(Seq - ConfirmedSeq)) > 0; }

	const FVeloxPendingKnockback& GetPendingKnockback() const { return PendingKnockback; }

	/** A knockback is queued for the start of the next move (A28: the move that queued it is an important move). */
	bool HasPendingKnockback() const { return PendingKnockback.IsPending(); }

	/** DoJump succeeded during the current/last PerformMovement (A29: a held-jump landing jump makes the move important). */
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

	/** The `showdebug VeloxMovement` block (non-shipping; empty in shipping). */
	void DisplayVeloxDebug(class UCanvas* Canvas, float& YL, float& YPos) const;

	/** Server: Velox state snapshot for the correction queued by the last failed error check (A18). */
	const FVeloxCorrectionState& GetServerCorrectionState() const { return ServerCorrectionState; }

	//~UCharacterMovementComponent interface
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual float GetGravityZ() const override;
	virtual bool CanAttemptJump() const override;
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

	/**
	 * A31: PhysWalking calls this right after "make velocity reflect actual move" (and at the top/end of every iteration);
	 * restores the end velocity of the ground update after the engine integrated the mean velocity.
	 */
	virtual void MaintainHorizontalGroundVelocity() override;

	/**
	 * A32 (Quake cl_packetdup 1): replaces the engine's OldMove by FSavedMove_Velox::ChooseOldMove before sending, so the
	 * previous RPC's move (or an unacked event move) rides along with every RPC and a single lost RPC is repaired in its
	 * original move partition. Non-shipping: logs VeloxResend / VeloxSend (LogVeloxNet) per RPC.
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

	/** DoJump succeeded in the current move; cleared at the start of PerformMovement (A29). */
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

	/**
	 * Owning client: set in OnClientCorrectionReceived, before ClientAdjustPosition teleports the pawn; cleared at the
	 * start of ClientUpdatePositionAfterServerUpdate and of PerformMovement. While set, AddKnockback drops impulses so the
	 * teleport's pad BeginOverlap cannot launch on top of the server's state (A19).
	 */
	uint8 bApplyingCorrection : 1;

	/** Server: Velox state at the error check that queued the pending correction (A18). */
	FVeloxCorrectionState ServerCorrectionState;

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

private:
	/** Persistent storage installed via SetNetworkMoveDataContainer in the constructor. */
	FVeloxNetworkMoveDataContainer VeloxNetworkMoveDataContainer;

	/** Persistent storage installed via SetMoveResponseDataContainer in the constructor. */
	FVeloxMoveResponseDataContainer VeloxMoveResponseDataContainer;
};
