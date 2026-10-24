// Velox - saved move, network move data and client prediction data for UVeloxMovementComponent (ADR 0001 section 3).
// All Velox movement input reaches the server through these types; never through ad-hoc RPCs.

#pragma once

#include "GameFramework/CharacterMovementComponent.h"

class ACharacter;

/**
 * Saved move with Velox flags.
 * Compressed flag allocation (FSavedMove_Character::FLAG_Custom_0..3 belong to Velox):
 *   FLAG_Custom_0 = jump held (sampled every move; drives auto-hop in M1)
 *   FLAG_Custom_1..3 = reserved
 */
class VELOX_API FSavedMove_Velox : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;

	static constexpr uint8 FLAG_JumpHeld = FSavedMove_Character::FLAG_Custom_0;

	//~FSavedMove_Character interface
	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;
	//~End of FSavedMove_Character interface

	/** Jump button held during this move. */
	uint8 bSavedJumpHeld : 1 = false;
};

/**
 * Per-move network payload. Velox adds no serialised fields yet: wish direction travels in Acceleration and
 * inputs in CompressedMoveFlags. Add fields here only when a move needs data neither can express.
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
