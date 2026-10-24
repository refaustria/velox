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
 * Saved move with Velox state (ADR 0002 section 5).
 * Compressed flag allocation (FSavedMove_Character::FLAG_Custom_0..3 belong to Velox):
 *   FLAG_Custom_0 = jump held (sampled every move; auto-hop and the jump latch)
 *   FLAG_Custom_1 = forward/back input present (CPM rules)
 *   FLAG_Custom_2 = side input present (CPM rules)
 *   FLAG_Custom_3 = reserved
 *
 * Replay rule (A17, supersedes A12/A14 restore clauses): PrepMoveFor restores nothing Velox-specific (it is Super only).
 * Inputs reach the replay through the compressed flags (MoveAutonomous -> UpdateFromCompressedFlags). KnockbackGrace,
 * the jump latch and the pending knockback are derived state: they chain from the correction
 * (UVeloxMovementComponent::OnClientCorrectionReceived) through the replayed moves exactly like Velocity, and replayed pad
 * overlaps re-fire on their own. The saved copies below exist for CanCombineWith/bForceNoCombine and debugging.
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

	/** A jump happened in this move with jump held, e.g. the held-jump landing jump (A29). Recorded in PostUpdate_Record. */
	uint8 bSavedJumpedHeld : 1;

	/** A28/A29 event move: a lost one costs 10-15 cm, anything else <= 2 cm, so it wins the single OldMove slot (A32). */
	bool IsEventMove() const { return bSavedEnteredTrigger || bSavedJumpedHeld; }

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

/** Container that makes the CMC use FVeloxNetworkMoveData for new, pending and old moves. */
struct VELOX_API FVeloxNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FVeloxNetworkMoveDataContainer();

	// The base pointers point into this object; copying would leave them dangling.
	FVeloxNetworkMoveDataContainer(const FVeloxNetworkMoveDataContainer&) = delete;
	FVeloxNetworkMoveDataContainer& operator=(const FVeloxNetworkMoveDataContainer&) = delete;

private:
	FVeloxNetworkMoveData VeloxMoveData[3];
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
};

/**
 * Server -> owning client move response with the Velox state a replay needs (ADR 0002 section 4): the server's
 * KnockbackGrace and jump latch after the acked move, and any knockback the server has not consumed yet. Values are the
 * server's state at the time of the error check (snapshot), see UVeloxMovementComponent::GetServerCorrectionState (A18).
 * Only corrections carry it; each field costs one bit when zero/false. Applied in
 * UVeloxMovementComponent::OnClientCorrectionReceived.
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
