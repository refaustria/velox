// Velox - saved move, network move data, move response data and client prediction data for UVeloxMovementComponent
// (ADR 0001 section 3, ADR 0002 sections 4 and 5, amendments A12 and A14).
// All Velox movement input reaches the server through these types; never through ad-hoc RPCs.

#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"

class ACharacter;
class UVeloxMovementComponent;

/**
 * External velocity change waiting to be applied at the start of the next PerformMovement (ADR 0002 section 4).
 * Filled by UVeloxMovementComponent::AddKnockback; saved per move, assigned on replay and sent in corrections.
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
 * Replay rule (A14): PrepMoveFor restores inputs only (flags, pending knockback by assignment, jump latch). Derived state
 * (Velocity, KnockbackGrace) chains from the correction through the replayed moves.
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
	//~End of FSavedMove_Character interface

	/**
	 * The Velox part of CanCombineWith (ADR 0002 section 5), without the engine rules. Public and world-free for the
	 * SavedMoveCombine test. bCombineAirMoves comes from the movement settings of the character.
	 */
	bool CanCombineVeloxState(const FSavedMove_Velox& NewMove, bool bCombineAirMoves) const;

	/** Knockback that the move consumes at its start (captured in SetMoveFor before PerformMovement). Input-like. */
	FVeloxPendingKnockback SavedPendingKnockback;

	/** KnockbackGrace at the start of the move. CanCombineWith and debugging only; never restored (A14). */
	float SavedKnockbackGrace = 0.f;

	/** Jump button held during this move. */
	uint8 bSavedJumpHeld : 1;

	/** Forward/back input present during this move. */
	uint8 bSavedHasForwardInput : 1;

	/** Side input present during this move. */
	uint8 bSavedHasSideInput : 1;

	/** Jump latch at the start of the move (A12); restored in PrepMoveFor. */
	uint8 bSavedJumpLatched : 1;
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
 * Server -> owning client move response with the Velox state a replay needs (ADR 0002 section 4): the server's
 * KnockbackGrace and jump latch after the acked move, and any knockback the server has not consumed yet. Only
 * corrections carry it; each field costs one bit when zero/false. Applied in UVeloxMovementComponent::OnClientCorrectionReceived.
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
