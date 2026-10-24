// Velox - saved move, network move data, move response data and client prediction data.

#include "Movement/VeloxNetworkMoveData.h"

#include "Engine/NetSerialization.h"
#include "GameFramework/Character.h"
#include "Movement/VeloxMovementComponent.h"

// ---------------------------------------------------------------------------------------------------------------------
// FSavedMove_Velox

FSavedMove_Velox::FSavedMove_Velox()
	: bSavedJumpHeld(false)
	, bSavedHasForwardInput(false)
	, bSavedHasSideInput(false)
	, bSavedJumpLatched(false)
	, bSavedEnteredTrigger(false)
	, bSavedJumpedHeld(false)
{
}

void FSavedMove_Velox::Clear()
{
	Super::Clear();

	SavedPendingKnockback.Reset();
	SavedKnockbackGrace = 0.f;
	bSavedJumpHeld = false;
	bSavedHasForwardInput = false;
	bSavedHasSideInput = false;
	bSavedJumpLatched = false;
	bSavedEnteredTrigger = false;
	bSavedJumpedHeld = false;
}

uint8 FSavedMove_Velox::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();

	if (bSavedJumpHeld)
	{
		Result |= FLAG_JumpHeld;
	}
	if (bSavedHasForwardInput)
	{
		Result |= FLAG_HasForwardInput;
	}
	if (bSavedHasSideInput)
	{
		Result |= FLAG_HasSideInput;
	}

	return Result;
}

bool FSavedMove_Velox::CanCombineVeloxState(const FSavedMove_Velox& NewMove, bool bCombineAirMoves) const
{
	if (bSavedJumpHeld != NewMove.bSavedJumpHeld)
	{
		return false;
	}
	if (bSavedJumpLatched != NewMove.bSavedJumpLatched)
	{
		return false;
	}
	// A knockback is an instant velocity change; combining would re-simulate it from the older start location.
	if (SavedPendingKnockback.IsPending() || NewMove.SavedPendingKnockback.IsPending())
	{
		return false;
	}
	// The combined move would sample the grace once at its start (A7) and count it down over the longer dt.
	if (SavedKnockbackGrace > 0.f || NewMove.SavedKnockbackGrace > 0.f)
	{
		return false;
	}
	// Re-simulating a pending air move with a longer dt re-places the player mid-jump (ADR 0002 section 5).
	if (!bCombineAirMoves)
	{
		const UCharacterMovementComponent* Unpacker = GetDefault<UVeloxMovementComponent>();
		TEnumAsByte<EMovementMode> Mode;
		TEnumAsByte<EMovementMode> GroundMode;
		uint8 CustomMode = 0;
		Unpacker->UnpackNetworkMovementMode(StartPackedMovementMode, Mode, CustomMode, GroundMode);
		const bool bOldFalling = Mode == MOVE_Falling;
		Unpacker->UnpackNetworkMovementMode(NewMove.StartPackedMovementMode, Mode, CustomMode, GroundMode);
		const bool bNewFalling = Mode == MOVE_Falling;
		if (bOldFalling || bNewFalling)
		{
			return false;
		}
	}
	if (bSavedHasForwardInput != NewMove.bSavedHasForwardInput || bSavedHasSideInput != NewMove.bSavedHasSideInput)
	{
		return false;
	}
	return true;
}

bool FSavedMove_Velox::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	// Every saved move allocated by FNetworkPredictionData_Client_Velox is an FSavedMove_Velox.
	const FSavedMove_Velox* NewVeloxMove = static_cast<const FSavedMove_Velox*>(NewMove.Get());

	const UVeloxMovementComponent* MoveComp = InCharacter ? Cast<UVeloxMovementComponent>(InCharacter->GetCharacterMovement()) : nullptr;
	const bool bCombineAirMoves = MoveComp && MoveComp->GetSimParams().bCombineAirMoves;
	if (!CanCombineVeloxState(*NewVeloxMove, bCombineAirMoves))
	{
		return false;
	}

	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Velox::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	// ReplicateMoveToServer calls this before PerformMovement, so these are the move's start values.
	if (const UVeloxMovementComponent* MoveComp = Cast<UVeloxMovementComponent>(C->GetCharacterMovement()))
	{
		bSavedJumpHeld = MoveComp->IsJumpHeld();
		bSavedHasForwardInput = MoveComp->HasForwardInput();
		bSavedHasSideInput = MoveComp->HasSideInput();
		bSavedJumpLatched = MoveComp->IsJumpLatched();
		SavedKnockbackGrace = MoveComp->GetKnockbackGrace();
		SavedPendingKnockback = MoveComp->GetPendingKnockback();

		// Same idea as the engine's PendingLaunchVelocity rule: an instant velocity change is sent on its own.
		if (SavedPendingKnockback.IsPending())
		{
			bForceNoCombine = true;
		}
	}
}

void FSavedMove_Velox::PrepMoveFor(ACharacter* C)
{
	// Nothing Velox-specific is restored (A17). The input flags reach the replay through MoveAutonomous ->
	// UpdateFromCompressedFlags; KnockbackGrace, the jump latch and the pending knockback chain from the correction like
	// Velocity (restoring recorded predictions would re-inject mispredicted history). Kept as an explicit override so the
	// rule has one place to live; M2's recorded knockback channel (A17) will be assigned here.
	Super::PrepMoveFor(C);
}

void FSavedMove_Velox::PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode)
{
	Super::PostUpdate(C, PostUpdateMode);

	// Only when recording the live move: a replay must not change which moves the client resends.
	if (PostUpdateMode == PostUpdate_Record)
	{
		if (const UVeloxMovementComponent* MoveComp = C ? Cast<UVeloxMovementComponent>(C->GetCharacterMovement()) : nullptr)
		{
			// The move's own end-of-scope overlap has already queued the knockback; the next move consumes it (A28).
			bSavedEnteredTrigger = MoveComp->HasPendingKnockback();
			bSavedJumpedHeld = MoveComp->JumpedThisMove() && bSavedJumpHeld;
		}
	}
}

bool FSavedMove_Velox::IsImportantMove(const FSavedMovePtr& LastAckedMove) const
{
	if (bSavedEnteredTrigger || bSavedJumpedHeld)
	{
		return true;
	}
	return Super::IsImportantMove(LastAckedMove);
}

const FSavedMove_Character* FSavedMove_Velox::ChooseOldMove(const TArray<FSavedMovePtr>& SavedMoves, const FSavedMovePtr& LastAckedMove,
	const FSavedMove_Character* NewMove, const FSavedMove_Character* PendingMove, const FSavedMove_Character* EngineOldMove, bool* bOutIsEventMove)
{
	const FSavedMove_Character* Newest = nullptr;
	const FSavedMove_Character* Result = EngineOldMove;
	bool bEvent = false;
	if (LastAckedMove.IsValid())
	{
		// Newest first: the first event move found is the newest one; the first candidate at all is the previous RPC's move.
		for (int32 Index = SavedMoves.Num() - 1; Index >= 0; --Index)
		{
			const FSavedMove_Character* Move = SavedMoves[Index].Get();
			if (!Move || Move == NewMove || Move == PendingMove || Move->bOldTimeStampBeforeReset || Move->TimeStamp <= LastAckedMove->TimeStamp)
			{
				continue;
			}
			if (static_cast<const FSavedMove_Velox*>(Move)->IsEventMove())
			{
				Newest = Move;
				bEvent = true;
				break;
			}
			if (!Newest)
			{
				Newest = Move;
			}
		}
	}
	if (Newest)
	{
		Result = Newest;
	}
	else if (EngineOldMove)
	{
		bEvent = static_cast<const FSavedMove_Velox*>(EngineOldMove)->IsEventMove();
	}
	if (bOutIsEventMove)
	{
		*bOutIsEventMove = bEvent;
	}
	return Result;
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
// FVeloxMoveResponseDataContainer

void FVeloxMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	Super::ServerFillResponseData(CharacterMovement, PendingAdjustment);

	KnockbackGrace = 0.f;
	bJumpLatched = false;
	PendingKnockback.Reset();

	// Server state after the acked move: what the client must start its replay from. The adjustment was queued in
	// ServerMoveHandleClientError, possibly before further moves of this frame ran, so read the snapshot taken there (A18).
	if (const UVeloxMovementComponent* MoveComp = Cast<UVeloxMovementComponent>(&CharacterMovement))
	{
		const FVeloxCorrectionState& Snapshot = MoveComp->GetServerCorrectionState();
		if (!PendingAdjustment.bAckGoodMove && Snapshot.TimeStamp == PendingAdjustment.TimeStamp)
		{
			KnockbackGrace = Snapshot.KnockbackGrace;
			bJumpLatched = Snapshot.bJumpLatched;
			PendingKnockback = Snapshot.PendingKnockback;
		}
		else
		{
			// Defensive fallback (no snapshot for this adjustment): the live values.
			KnockbackGrace = MoveComp->GetKnockbackGrace();
			bJumpLatched = MoveComp->IsJumpLatched();
			PendingKnockback = MoveComp->GetPendingKnockback();
		}
	}
}

bool FVeloxMoveResponseDataContainer::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	if (!Super::Serialize(CharacterMovement, Ar, PackageMap))
	{
		return false;
	}

	// Acks carry nothing extra; the Velox fields only matter for a replay.
	if (IsCorrection())
	{
		const bool bIsSaving = Ar.IsSaving();
		// Full precision on purpose: the client replays from these values and must match the server bit for bit.
		SerializeOptionalValue<float>(bIsSaving, Ar, KnockbackGrace, 0.f);
		Ar.SerializeBits(&bJumpLatched, 1);
		SerializeOptionalValue<FVector>(bIsSaving, Ar, PendingKnockback.Velocity, FVector::ZeroVector);
		Ar.SerializeBits(&PendingKnockback.bOverride, 1);
	}

	return !Ar.IsError();
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
