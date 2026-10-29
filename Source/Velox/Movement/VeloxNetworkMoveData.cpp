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
	, bSavedQueuedPredictedKnockback(false)
	, bSavedHasteActive(false)
	, bSavedHasShot(false)
{
}

void FSavedMove_Velox::Clear()
{
	Super::Clear();

	SavedPendingKnockback.Reset();
	SavedPredictedKnockback.Reset();
	SavedKnockbackGrace = 0.f;
	bSavedJumpHeld = false;
	bSavedHasForwardInput = false;
	bSavedHasSideInput = false;
	bSavedJumpLatched = false;
	bSavedEnteredTrigger = false;
	bSavedJumpedHeld = false;
	bSavedQueuedPredictedKnockback = false;
	bSavedHasteActive = false;
	bSavedHasShot = false;
	SavedMoveShot = FVeloxMoveShot();
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
	// Same for the recorded predicted self-knockback (ADR 0003 section 3): it must stay at its own move for the replay.
	if (SavedPredictedKnockback.IsPending() || NewMove.SavedPredictedKnockback.IsPending())
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
	// ADR 0002 A36: a combined move would start earlier than the shot's move; the fire move is sent on its own.
	if (bSavedHasShot || NewMove.bSavedHasShot)
	{
		return false;
	}
	// ADR 0004 section 3: the combined move would be simulated with the scale of its end; never across the Haste end.
	if (bSavedHasteActive != NewMove.bSavedHasteActive)
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
		SavedPredictedKnockback = MoveComp->GetPendingPredictedKnockback();
		// TimeStamp is this move's end stamp (ReplicateMoveToServer advanced it before SetMoveFor); the sim evaluates the
		// same stamp (UVeloxMovementComponent::GetPowerupClockNow).
		bSavedHasteActive = MoveComp->IsPowerupActiveAt(MoveComp->GetPowerupMoveTimes().HasteEnd, TimeStamp);

		// Same idea as the engine's PendingLaunchVelocity rule: an instant velocity change is sent on its own.
		if (SavedPendingKnockback.IsPending() || SavedPredictedKnockback.IsPending())
		{
			bForceNoCombine = true;
		}
	}

	// ADR 0002 A36: the projectile shot fired at the start of this move (queued by the fire ability in this frame's
	// PlayerTick, before the CMC tick created this move) rides this move to the server; the CMC slot is cleared.
	if (UVeloxMovementComponent* MutableMoveComp = C ? Cast<UVeloxMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		bSavedHasShot = MutableMoveComp->ConsumePendingMoveShot(SavedMoveShot);
		if (bSavedHasShot)
		{
			bForceNoCombine = true;
		}
	}
}

FVeloxPredictedKnockback FSavedMove_Velox::PredictedKnockbackForReplay(const FVeloxPredictedKnockback& Saved, uint8 ConfirmedSeq)
{
	if (Saved.IsPending() && UVeloxMovementComponent::ShouldReplayPredictedKnockback(Saved.Seq, ConfirmedSeq))
	{
		return Saved;
	}
	return FVeloxPredictedKnockback();
}

void FSavedMove_Velox::PrepMoveFor(ACharacter* C)
{
	// Nothing else Velox-specific is restored (A17). The input flags reach the replay through MoveAutonomous ->
	// UpdateFromCompressedFlags; KnockbackGrace, the jump latch and the pending knockback chain from the correction like
	// Velocity (restoring recorded predictions would re-inject mispredicted history).
	Super::PrepMoveFor(C);

	// The one exception (ADR 0003 section 3): the predicted self-knockback has no replayable source event. A replayed
	// move re-applies it while the server has not resolved its shot; once resolved, the server's state (corrected
	// velocity or chained pending knockback) already holds the real impulse, or correctly none.
	if (UVeloxMovementComponent* MoveComp = C ? Cast<UVeloxMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		MoveComp->SetReplayPredictedKnockback(PredictedKnockbackForReplay(SavedPredictedKnockback, MoveComp->GetConfirmedSelfKnockbackSeq()));
	}
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
			// A move-timed self detonation of a fake projectile queued its prediction at the end of this move.
			bSavedQueuedPredictedKnockback = MoveComp->GetPendingPredictedKnockback().IsPending();
		}
	}
}

bool FSavedMove_Velox::IsImportantMove(const FSavedMovePtr& LastAckedMove) const
{
	if (IsEventMove())
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

const FSavedMove_Character* FSavedMove_Velox::ChooseExtraOldMove(const TArray<FSavedMovePtr>& SavedMoves, const FSavedMovePtr& LastAckedMove,
	const FSavedMove_Character* NewMove, const FSavedMove_Character* PendingMove, const FSavedMove_Character* OldMove)
{
	if (!OldMove || !LastAckedMove.IsValid())
	{
		return nullptr;
	}
	// Same eligibility as ChooseOldMove; the server simulates the extra move first, so it must be older than OldMove.
	for (int32 Index = SavedMoves.Num() - 1; Index >= 0; --Index)
	{
		const FSavedMove_Character* Move = SavedMoves[Index].Get();
		if (!Move || Move == NewMove || Move == PendingMove || Move == OldMove || Move->bOldTimeStampBeforeReset
			|| Move->TimeStamp <= LastAckedMove->TimeStamp || Move->TimeStamp >= OldMove->TimeStamp)
		{
			continue;
		}
		return Move;
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// FVeloxNetworkMoveData

void FVeloxNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);

	// Every saved move allocated by FNetworkPredictionData_Client_Velox is an FSavedMove_Velox (ADR 0002 A36).
	const FSavedMove_Velox& VeloxMove = static_cast<const FSavedMove_Velox&>(ClientMove);
	bHasShot = VeloxMove.bSavedHasShot;
	Shot = bHasShot ? VeloxMove.SavedMoveShot : FVeloxMoveShot();
}

bool FVeloxNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	const bool bSuccess = Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType);

	// ADR 0002 A36: after the engine's fields, one presence bit, then the shot (every move type).
	Ar.SerializeBits(&bHasShot, 1);
	bool bShotOk = true;
	if (bHasShot)
	{
		bShotOk = Shot.Serialize(Ar);
	}
	else if (Ar.IsLoading())
	{
		Shot = FVeloxMoveShot();
	}

	return bSuccess && bShotOk && !Ar.IsError();
}

// ---------------------------------------------------------------------------------------------------------------------
// FVeloxMoveShot

bool FVeloxMoveShot::Serialize(FArchive& Ar)
{
	Ar << ShotSeq;
	uint32 Index = WeaponIndex;
	Ar.SerializeInt(Index, static_cast<uint32>(MaxWeaponIndex) + 1u); // 4 bits
	WeaponIndex = static_cast<uint8>(FMath::Min<uint32>(Index, MaxWeaponIndex));
	// Full precision: both ends schedule the owner's self splash at FireMoveTimeStamp + flight (ADR 0003 B25).
	Ar << FireMoveTimeStamp;
	bool bOriginOk = true;
	bool bDirOk = true;
	Origin.NetSerialize(Ar, nullptr, bOriginOk);
	Dir.NetSerialize(Ar, nullptr, bDirOk);
	return bOriginOk && bDirOk && !Ar.IsError();
}

// ---------------------------------------------------------------------------------------------------------------------
// FVeloxNetworkMoveDataContainer

FVeloxNetworkMoveDataContainer::FVeloxNetworkMoveDataContainer()
{
	NewMoveData = &VeloxMoveData[0];
	PendingMoveData = &VeloxMoveData[1];
	OldMoveData = &VeloxMoveData[2];
}

void FVeloxNetworkMoveDataContainer::ClientFillNetworkMoveData(const FSavedMove_Character* ClientNewMove, const FSavedMove_Character* ClientPendingMove, const FSavedMove_Character* ClientOldMove)
{
	Super::ClientFillNetworkMoveData(ClientNewMove, ClientPendingMove, ClientOldMove);

	// Only together with an engine old move (the server simulates the extra one right before it).
	bHasExtraOldMove = ClientExtraOldMove != nullptr && ClientOldMove != nullptr;
	if (bHasExtraOldMove)
	{
		ExtraOldMoveData.ClientFillNetworkMoveData(*ClientExtraOldMove, FCharacterNetworkMoveData::ENetworkMoveType::OldMove);
	}
	ClientExtraOldMove = nullptr;
}

bool FVeloxNetworkMoveDataContainer::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	if (!Super::Serialize(CharacterMovement, Ar, PackageMap))
	{
		return false;
	}

	// Appended after the engine's fields: one bit when absent. Serialised as an old move (no client error check on it).
	Ar.SerializeBits(&bHasExtraOldMove, 1);
	if (bHasExtraOldMove)
	{
		if (!ExtraOldMoveData.Serialize(CharacterMovement, Ar, PackageMap, FCharacterNetworkMoveData::ENetworkMoveType::OldMove))
		{
			return false;
		}
	}

	return !Ar.IsError();
}

const FVeloxNetworkMoveData* FVeloxNetworkMoveDataContainer::AsVeloxMoveData(const FCharacterNetworkMoveData* MoveData) const
{
	if (!MoveData)
	{
		return nullptr;
	}
	for (const FVeloxNetworkMoveData& Data : VeloxMoveData)
	{
		if (MoveData == &Data)
		{
			return &Data;
		}
	}
	return MoveData == &ExtraOldMoveData ? &ExtraOldMoveData : nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// FVeloxMoveResponseDataContainer

void FVeloxMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	Super::ServerFillResponseData(CharacterMovement, PendingAdjustment);

	KnockbackGrace = 0.f;
	bJumpLatched = false;
	PendingKnockback.Reset();
	ConfirmedSelfKnockbackSeq = 0;
	PowerupMoveTimes = FVeloxPowerupMoveTimes();

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
			ConfirmedSelfKnockbackSeq = Snapshot.ConfirmedSelfKnockbackSeq;
			PowerupMoveTimes = Snapshot.PowerupMoveTimes;
		}
		else
		{
			// Acks (the ack's time stamp is the last move processed, so the live state is the state after it) and the
			// defensive fallback for a correction without a snapshot: the live values.
			KnockbackGrace = MoveComp->GetKnockbackGrace();
			bJumpLatched = MoveComp->IsJumpLatched();
			PendingKnockback = MoveComp->GetPendingKnockback();
			ConfirmedSelfKnockbackSeq = MoveComp->GetConfirmedSelfKnockbackSeq();
			PowerupMoveTimes = MoveComp->GetPowerupMoveTimes();
		}
	}
}

bool FVeloxMoveResponseDataContainer::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	if (!Super::Serialize(CharacterMovement, Ar, PackageMap))
	{
		return false;
	}

	// The replay state only matters for a correction.
	if (IsCorrection())
	{
		const bool bIsSaving = Ar.IsSaving();
		// Full precision on purpose: the client replays from these values and must match the server bit for bit.
		SerializeOptionalValue<float>(bIsSaving, Ar, KnockbackGrace, 0.f);
		Ar.SerializeBits(&bJumpLatched, 1);
		SerializeOptionalValue<FVector>(bIsSaving, Ar, PendingKnockback.Velocity, FVector::ZeroVector);
		Ar.SerializeBits(&PendingKnockback.bOverride, 1);
		SerializeOptionalValue<uint8>(bIsSaving, Ar, ConfirmedSelfKnockbackSeq, 0);
	}

	// ADR 0004 section 3: acks and corrections. Full precision: both ends compare move stamps against these exactly.
	SerializeOptionalValue<float>(Ar.IsSaving(), Ar, PowerupMoveTimes.HasteEnd, 0.f);
	SerializeOptionalValue<float>(Ar.IsSaving(), Ar, PowerupMoveTimes.QuadEnd, 0.f);

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
