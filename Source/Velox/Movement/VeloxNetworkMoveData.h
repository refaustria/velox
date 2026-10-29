// Velox - saved move, network move data, move response data and client prediction data for UVeloxMovementComponent
// (ADR 0001 section 3, ADR 0002 sections 4 and 5, amendments A12, A17 and A18).
// All Velox movement input reaches the server through these types; never through ad-hoc RPCs.

#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"

class ACharacter;
class UVeloxMovementComponent;

/**
 * External velocity change waiting to be applied at the start of the next PerformMovement (ADR 0002 section 4).
 * Filled by UVeloxMovementComponent::AddKnockback; consumed at move start, chained through replays (A17), saved per move
 * for CanCombineWith only, and sent in corrections.
 */
struct FVeloxPendingKnockback
{
	/** Velocity to add, or to set when bOverride. */
	FVector Velocity = FVector::ZeroVector;
	/** Replace the current velocity instead of adding to it (jump pads). */
	bool bOverride = false;

	/** An override with zero velocity ("stop") is still pending. */
	bool IsPending() const { return bOverride || !Velocity.IsZero(); }
	void Reset() { *this = FVeloxPendingKnockback(); }

	/**
	 * Accumulates another knockback of the same move: an override replaces everything before it, an additive one adds to
	 * the pending velocity and keeps an earlier override (set, then add).
	 */
	void Add(const FVector& InVelocity, bool bInOverride)
	{
		if (bInOverride)
		{
			Velocity = InVelocity;
			bOverride = true;
		}
		else
		{
			Velocity += InVelocity;
		}
	}

	bool operator==(const FVeloxPendingKnockback& Other) const { return Velocity == Other.Velocity && bOverride == Other.bOverride; }
	bool operator!=(const FVeloxPendingKnockback& Other) const { return !(*this == Other); }
};

/**
 * Client-predicted self-knockback (ADR 0003 section 3, ADR 0002 A17 recorded channel): the impulse the owning client
 * predicts when its fake projectile detonates, keyed by the shot sequence of that projectile. Accepted only on the
 * autonomous proxy, saved per move and re-applied in replays until the server has resolved the shot
 * (UVeloxMovementComponent::ShouldReplayPredictedKnockback). Never sent to the server.
 */
struct FVeloxPredictedKnockback
{
	/** Velocity to add (never an override). */
	FVector Velocity = FVector::ZeroVector;
	/** Shot sequence of the newest predicted detonation in this impulse. */
	uint8 Seq = 0;

	bool IsPending() const { return !Velocity.IsZero(); }
	void Reset() { *this = FVeloxPredictedKnockback(); }

	/** Accumulates another prediction of the same move; the sequence becomes the newer of the two (wrap-aware). */
	void Add(const FVector& InVelocity, uint8 InSeq)
	{
		if (!IsPending() || static_cast<int8>(static_cast<uint8>(InSeq - Seq)) > 0)
		{
			Seq = InSeq;
		}
		Velocity += InVelocity;
	}

	bool operator==(const FVeloxPredictedKnockback& Other) const { return Velocity == Other.Velocity && Seq == Other.Seq; }
	bool operator!=(const FVeloxPredictedKnockback& Other) const { return !(*this == Other); }
};

/**
 * Powerup end times in the owner's move-time domain (ADR 0004 section 3): the client move time stamp (server:
 * ServerData->CurrentClientTimeStamp, owning client: the saved move's TimeStamp) at which the powerup ends, normalised to
 * [0, MinTimeBetweenTimeStampResets); 0 = not active. Bots and listen hosts use world time (ADR 0003 B12). Set by the
 * server (UVeloxMovementComponent::ServerSetPowerupRemaining), sent in every move response (acks and corrections) and
 * snapshotted per correction (A18); evaluated per move with UVeloxMovementComponent::IsPowerupActive on both ends.
 */
struct FVeloxPowerupMoveTimes
{
	float HasteEnd = 0.f;
	float QuadEnd = 0.f;

	bool operator==(const FVeloxPowerupMoveTimes& Other) const { return HasteEnd == Other.HasteEnd && QuadEnd == Other.QuadEnd; }
	bool operator!=(const FVeloxPowerupMoveTimes& Other) const { return !(*this == Other); }
};

/**
 * Saved move with Velox state (ADR 0002 section 5).
 * Compressed flag allocation (FSavedMove_Character::FLAG_Custom_0..3 belong to Velox):
 *   FLAG_Custom_0 = jump held (sampled every move; auto-hop and the jump latch)
 *   FLAG_Custom_1 = forward/back input present (CPM rules)
 *   FLAG_Custom_2 = side input present (CPM rules)
 *   FLAG_Custom_3 = reserved
 *
 * Replay rule (A17, supersedes A12/A14 restore clauses): PrepMoveFor restores nothing Velox-specific except the recorded
 * predicted self-knockback (ADR 0003 section 3). Inputs reach the replay through the compressed flags (MoveAutonomous ->
 * UpdateFromCompressedFlags). KnockbackGrace, the jump latch and the pending knockback are derived state: they chain from
 * the correction (UVeloxMovementComponent::OnClientCorrectionReceived) through the replayed moves exactly like Velocity,
 * and replayed pad overlaps re-fire on their own. The saved copies below exist for CanCombineWith/bForceNoCombine and
 * debugging. The one exception, SavedPredictedKnockback, has no replayable source event (the fake projectile is not
 * re-simulated): PrepMoveFor assigns it iff the server has not resolved its shot yet.
 */
class VELOX_API FSavedMove_Velox : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;

	static constexpr uint8 FLAG_JumpHeld = FSavedMove_Character::FLAG_Custom_0;
	static constexpr uint8 FLAG_HasForwardInput = FSavedMove_Character::FLAG_Custom_1;
	static constexpr uint8 FLAG_HasSideInput = FSavedMove_Character::FLAG_Custom_2;

	FSavedMove_Velox();

	//~FSavedMove_Character interface
	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;
	virtual void PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode) override;

	/**
	 * Engine rule plus the Velox event moves (A28/A29): a move at whose end a pad overlap queued a knockback, and a move
	 * with a held-jump landing jump. An important move is resent as OldMove until acked, so a lost event move is simulated
	 * by the server in its original partition instead of being merged into the next move (which shifts the event).
	 */
	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override;
	//~End of FSavedMove_Character interface

	/**
	 * The Velox part of CanCombineWith (ADR 0002 section 5), without the engine rules. Public and world-free for the
	 * SavedMoveCombine test. bCombineAirMoves comes from the movement settings of the character.
	 */
	bool CanCombineVeloxState(const FSavedMove_Velox& NewMove, bool bCombineAirMoves) const;

	/**
	 * Knockback that the move consumes at its start (captured in SetMoveFor before PerformMovement). CanCombineWith,
	 * bForceNoCombine and debugging only; never restored on replay (A17).
	 */
	FVeloxPendingKnockback SavedPendingKnockback;

	/**
	 * Predicted self-knockback the move consumes at its start (captured in SetMoveFor). The recorded channel: assigned in
	 * PrepMoveFor while UVeloxMovementComponent::ShouldReplayPredictedKnockback(Seq, ConfirmedSelfKnockbackSeq) holds.
	 */
	FVeloxPredictedKnockback SavedPredictedKnockback;

	/** KnockbackGrace at the start of the move. CanCombineWith and debugging only; never restored (A14, A17). */
	float SavedKnockbackGrace = 0.f;

	/** Jump button held during this move. */
	uint8 bSavedJumpHeld : 1;

	/** Forward/back input present during this move. */
	uint8 bSavedHasForwardInput : 1;

	/** Side input present during this move. */
	uint8 bSavedHasSideInput : 1;

	/** Jump latch at the start of the move (A12). CanCombineWith and debugging only; never restored (A17). */
	uint8 bSavedJumpLatched : 1;

	/** A knockback was queued during this move, e.g. a pad trigger entry (A28). Recorded in PostUpdate_Record. */
	uint8 bSavedEnteredTrigger : 1;

	/**
	 * A jump happened in this move with jump held (A29): the held-jump landing jump inside the move, or the take-off jump
	 * CheckJumpInput starts at the move's beginning (task M1-07: its start boundary must reach the server, see
	 * ChooseExtraOldMove). Recorded in PostUpdate_Record.
	 */
	uint8 bSavedJumpedHeld : 1;

	/**
	 * A predicted self-knockback was queued at the end of this move (the client's move-timed self detonation of a fake
	 * projectile); the server evaluates its own self splash at the end of the same move, so the move's partition must
	 * survive loss (A29). Recorded in PostUpdate_Record.
	 */
	uint8 bSavedQueuedPredictedKnockback : 1;

	/**
	 * Haste active in this move (ADR 0004 section 3), evaluated at the move's TimeStamp in SetMoveFor. CanCombineWith only:
	 * moves on different sides of the Haste end never combine. Never restored; a replay recomputes the scale from the
	 * move's time stamp and the chained FVeloxPowerupMoveTimes.
	 */
	uint8 bSavedHasteActive : 1;

	/** A28/A29 event move: a lost one costs 10-15 cm, anything else <= 2 cm, so it wins the single OldMove slot (A32). */
	bool IsEventMove() const { return bSavedEnteredTrigger || bSavedJumpedHeld || bSavedQueuedPredictedKnockback; }

	/**
	 * ADR 0003 section 3 PrepMoveFor rule, world-free: the predicted impulse a replay of a move recorded with Saved must
	 * apply, given the last ConfirmedSelfKnockbackSeq received from the server (empty once the server resolved the shot).
	 */
	static FVeloxPredictedKnockback PredictedKnockbackForReplay(const FVeloxPredictedKnockback& Saved, uint8 ConfirmedSeq);

	/**
	 * A32 (Quake cl_packetdup 1): the OldMove to send with an RPC carrying NewMove (and PendingMove, may be null).
	 * Candidates are the saved moves that are neither NewMove nor PendingMove, newer than LastAckedMove and not from before
	 * a time stamp reset (bOldTimeStampBeforeReset). Returns (1) the newest candidate that is an event move, else (2) the
	 * newest candidate (the previous RPC's NewMove), else (3) EngineOldMove. Without a LastAckedMove (nothing acked yet)
	 * the engine's choice is kept, as the engine does. Every saved move must be an FSavedMove_Velox. Pure, no world.
	 */
	static const FSavedMove_Character* ChooseOldMove(const TArray<FSavedMovePtr>& SavedMoves, const FSavedMovePtr& LastAckedMove,
		const FSavedMove_Character* NewMove, const FSavedMove_Character* PendingMove, const FSavedMove_Character* EngineOldMove,
		bool* bOutIsEventMove = nullptr);

	/**
	 * Task M1-07 (A32's deferred second slot, Quake cl_packetdup 2): the extra old move sent with an RPC whose OldMove is
	 * OldMove: the newest candidate (same eligibility as ChooseOldMove) older than OldMove, i.e. the move right before it.
	 * The server simulates it before OldMove, so (a) a lost dual RPC is repaired including its pending half, (b) a burst
	 * of two lost RPCs is repaired, and (c) an event move travels together with the move that ends at its start (a
	 * take-off jump begins at the start of its move: merging the move before it into it moves the take-off earlier).
	 * Returns nullptr without OldMove, without a LastAckedMove, or when nothing older is eligible. Pure, no world.
	 */
	static const FSavedMove_Character* ChooseExtraOldMove(const TArray<FSavedMovePtr>& SavedMoves, const FSavedMovePtr& LastAckedMove,
		const FSavedMove_Character* NewMove, const FSavedMove_Character* PendingMove, const FSavedMove_Character* OldMove);
};

/**
 * Per-move network payload. Velox adds no serialised fields in M1 (ADR 0002 section 5): wish direction travels in
 * Acceleration and inputs in CompressedMoveFlags. Add fields here only when a move needs data neither can express.
 */
struct VELOX_API FVeloxNetworkMoveData : public FCharacterNetworkMoveData
{
	using Super = FCharacterNetworkMoveData;

	//~FCharacterNetworkMoveData interface
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
	//~End of FCharacterNetworkMoveData interface
};

/**
 * Container that makes the CMC use FVeloxNetworkMoveData for new, pending and old moves, plus the Velox extra old move
 * (task M1-07, A32's deferred cl_packetdup 2): serialised after the engine's fields (1 bit when absent), simulated by
 * UVeloxMovementComponent::ServerMove_HandleMoveData before the engine's old move.
 */
struct VELOX_API FVeloxNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	using Super = FCharacterNetworkMoveDataContainer;

	FVeloxNetworkMoveDataContainer();

	// The base pointers point into this object; copying would leave them dangling.
	FVeloxNetworkMoveDataContainer(const FVeloxNetworkMoveDataContainer&) = delete;
	FVeloxNetworkMoveDataContainer& operator=(const FVeloxNetworkMoveDataContainer&) = delete;

	//~FCharacterNetworkMoveDataContainer interface
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character* ClientNewMove, const FSavedMove_Character* ClientPendingMove, const FSavedMove_Character* ClientOldMove) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap) override;
	//~End of FCharacterNetworkMoveDataContainer interface

	/** Client: the extra old move the next ClientFillNetworkMoveData packs (nullptr = none). Cleared by the fill. */
	void SetClientExtraOldMove(const FSavedMove_Character* InExtraOldMove) { ClientExtraOldMove = InExtraOldMove; }

	/** The received (server) or packed (client) extra old move, nullptr when the RPC carries none. */
	FVeloxNetworkMoveData* GetExtraOldMoveData() { return bHasExtraOldMove ? &ExtraOldMoveData : nullptr; }
	const FVeloxNetworkMoveData* GetExtraOldMoveData() const { return bHasExtraOldMove ? &ExtraOldMoveData : nullptr; }

	/** Test access: the storage of the extra old move and its presence bit. */
	FVeloxNetworkMoveData& GetExtraOldMoveStorage() { return ExtraOldMoveData; }
	void SetHasExtraOldMove(bool bInHasExtraOldMove) { bHasExtraOldMove = bInHasExtraOldMove; }

private:
	FVeloxNetworkMoveData VeloxMoveData[3];
	FVeloxNetworkMoveData ExtraOldMoveData;
	bool bHasExtraOldMove = false;
	const FSavedMove_Character* ClientExtraOldMove = nullptr;
};

/**
 * Server-side snapshot of the Velox state a correction must carry, taken in
 * UVeloxMovementComponent::ServerMoveHandleClientError when the error check queues a correction (A18). The response is
 * serialised later, at replication time, after further ServerMove RPCs of the frame may have changed the live state.
 */
struct FVeloxCorrectionState
{
	/** Client time stamp of the move whose check queued the correction (FClientAdjustment::TimeStamp); 0 = none. */
	float TimeStamp = 0.f;
	float KnockbackGrace = 0.f;
	bool bJumpLatched = false;
	FVeloxPendingKnockback PendingKnockback;
	/** ADR 0003 section 3: newest own shot whose self-knockback the server has resolved, at check time (A18). */
	uint8 ConfirmedSelfKnockbackSeq = 0;
	/** ADR 0004 section 3: powerup end times at check time (A18), so the replay sees the values of its check time. */
	FVeloxPowerupMoveTimes PowerupMoveTimes;
};

/**
 * Server -> owning client move response with the Velox state a replay needs (ADR 0002 section 4): the server's
 * KnockbackGrace and jump latch after the acked move, any knockback the server has not consumed yet, and the newest own
 * shot whose self-knockback the server has resolved (ADR 0003 section 3). Values are the
 * server's state at the time of the error check (snapshot), see UVeloxMovementComponent::GetServerCorrectionState (A18).
 * Only corrections carry it; each field costs one bit when zero/false. Applied in
 * UVeloxMovementComponent::OnClientCorrectionReceived.
 * ADR 0004 section 3: the powerup end times travel in acks and corrections alike (one bit each while 0; the engine
 * serialises both through this one container, FCharacterMoveResponseDataContainer::Serialize). Acks carry the live values
 * (the ack's time stamp is the last move processed), corrections the A18 snapshot. Applied in
 * UVeloxMovementComponent::OnClientCorrectionReceived and ClientAckGoodMove_Implementation.
 */
struct VELOX_API FVeloxMoveResponseDataContainer : public FCharacterMoveResponseDataContainer
{
	using Super = FCharacterMoveResponseDataContainer;

	//~FCharacterMoveResponseDataContainer interface
	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap) override;
	//~End of FCharacterMoveResponseDataContainer interface

	float KnockbackGrace = 0.f;
	bool bJumpLatched = false;
	FVeloxPendingKnockback PendingKnockback;
	/** ADR 0003 section 3: one byte, corrections only (optional against 0). */
	uint8 ConfirmedSelfKnockbackSeq = 0;
	/** ADR 0004 section 3: acks and corrections, each float optional against 0. */
	FVeloxPowerupMoveTimes PowerupMoveTimes;
};

/** Client prediction data that allocates FSavedMove_Velox. */
class VELOX_API FNetworkPredictionData_Client_Velox : public FNetworkPredictionData_Client_Character
{
public:
	using Super = FNetworkPredictionData_Client_Character;

	explicit FNetworkPredictionData_Client_Velox(const UCharacterMovementComponent& ClientMovement);

	//~FNetworkPredictionData_Client_Character interface
	virtual FSavedMovePtr AllocateNewMove() override;
	//~End of FNetworkPredictionData_Client_Character interface
};
