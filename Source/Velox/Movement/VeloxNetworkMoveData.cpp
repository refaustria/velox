// Velox - saved move, network move data and client prediction data.

#include "Movement/VeloxNetworkMoveData.h"

#include "GameFramework/Character.h"
#include "Movement/VeloxMovementComponent.h"

// ---------------------------------------------------------------------------------------------------------------------
// FSavedMove_Velox

void FSavedMove_Velox::Clear()
{
	Super::Clear();

	bSavedJumpHeld = false;
}

uint8 FSavedMove_Velox::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();

	if (bSavedJumpHeld)
	{
		Result |= FLAG_JumpHeld;
	}

	return Result;
}

bool FSavedMove_Velox::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	// Every saved move allocated by FNetworkPredictionData_Client_Velox is an FSavedMove_Velox.
	const FSavedMove_Velox* NewVeloxMove = static_cast<const FSavedMove_Velox*>(NewMove.Get());

	if (bSavedJumpHeld != NewVeloxMove->bSavedJumpHeld)
	{
		return false;
	}

	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Velox::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	if (const UVeloxMovementComponent* MoveComp = Cast<UVeloxMovementComponent>(C->GetCharacterMovement()))
	{
		bSavedJumpHeld = MoveComp->IsJumpHeld();
	}
}

void FSavedMove_Velox::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	// Restore input state before the move is replayed after a server correction.
	if (UVeloxMovementComponent* MoveComp = Cast<UVeloxMovementComponent>(C->GetCharacterMovement()))
	{
		MoveComp->SetJumpHeld(bSavedJumpHeld);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FVeloxNetworkMoveData

void FVeloxNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);

	// Copy extra FSavedMove_Velox payload here once FVeloxNetworkMoveData gains serialised fields:
	// const FSavedMove_Velox& VeloxMove = static_cast<const FSavedMove_Velox&>(ClientMove);
}

bool FVeloxNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	const bool bSuccess = Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType);

	// Serialise extra fields here (same order on save and load).

	return bSuccess && !Ar.IsError();
}

// ---------------------------------------------------------------------------------------------------------------------
// FVeloxNetworkMoveDataContainer

FVeloxNetworkMoveDataContainer::FVeloxNetworkMoveDataContainer()
{
	NewMoveData = &VeloxMoveData[0];
	PendingMoveData = &VeloxMoveData[1];
	OldMoveData = &VeloxMoveData[2];
}

// ---------------------------------------------------------------------------------------------------------------------
// FNetworkPredictionData_Client_Velox

FNetworkPredictionData_Client_Velox::FNetworkPredictionData_Client_Velox(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr FNetworkPredictionData_Client_Velox::AllocateNewMove()
{
	return MakeShared<FSavedMove_Velox>();
}
