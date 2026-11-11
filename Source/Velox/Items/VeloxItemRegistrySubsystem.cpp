// Velox - item registry.

#include "Items/VeloxItemRegistrySubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxPlayerState.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemLog.h"
#include "Items/VeloxPickup.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxItemRegistrySubsystem)

UVeloxItemRegistrySubsystem* UVeloxItemRegistrySubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UVeloxItemRegistrySubsystem>() : nullptr;
}

bool UVeloxItemRegistrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FVeloxItemModeRules UVeloxItemRegistrySubsystem::MakeModeRules(const UVeloxGameModeDefinition* Definition)
{
	FVeloxItemModeRules Rules;
	if (Definition)
	{
		Rules.ItemRespawnScale = Definition->ItemRespawnScale;
		Rules.WeaponRespawnSeconds = Definition->WeaponRespawnSeconds;
		Rules.bWeaponStay = Definition->bWeaponStay;
	}
	return Rules;
}

void UVeloxItemRegistrySubsystem::RefreshModeRules()
{
	const UWorld* World = GetWorld();
	const AVeloxGameState* State = World ? World->GetGameState<AVeloxGameState>() : nullptr;
	ModeRules = MakeModeRules(State ? State->GetGameModeDefinition() : nullptr);
	// The timer rule comes from the same definition.
	RefreshAllItemTimers();
}

EVeloxItemTimerVisibility UVeloxItemRegistrySubsystem::GetItemTimerVisibility() const
{
	const UWorld* World = GetWorld();
	const AVeloxGameState* State = World ? World->GetGameState<AVeloxGameState>() : nullptr;
	const UVeloxGameModeDefinition* Definition = State ? State->GetGameModeDefinition() : nullptr;
	return Definition ? Definition->ItemTimers : EVeloxItemTimerVisibility::Spectators;
}

void UVeloxItemRegistrySubsystem::RegisterPickup(AVeloxPickup* Pickup)
{
	if (Pickup)
	{
		if (Pickup->HasAuthority() && !Pickups.Contains(Pickup))
		{
			// ADR 0007 section 5.5: the key of the owner-only timer entries; replicated initial-only, so set before the
			// first replication (BeginPlay).
			Pickup->SetItemIndex(NextItemIndex);
			++NextItemIndex;
			if (NextItemIndex == 0)
			{
				UE_LOG(LogVeloxItems, Warning, TEXT("VeloxItemIndexWrap: more than 255 registered pickups; item indices repeat (0 stays reserved for none)"));
				NextItemIndex = 1;
			}
		}
		Pickups.AddUnique(Pickup);
	}
}

AVeloxPickup* UVeloxItemRegistrySubsystem::FindPickupByIndex(uint8 ItemIndex) const
{
	if (ItemIndex == 0)
	{
		return nullptr;   // 0 = "no index" (review F2)
	}
	for (const TWeakObjectPtr<AVeloxPickup>& Entry : Pickups)
	{
		AVeloxPickup* Pickup = Entry.Get();
		if (Pickup && Pickup->GetItemIndex() == ItemIndex)
		{
			return Pickup;
		}
	}
	return nullptr;
}

bool UVeloxItemRegistrySubsystem::IsEntitledToItemTimers(const APlayerState* PlayerState) const
{
	return PlayerState && VeloxItemTimerRules::IsEntitled(GetItemTimerVisibility(), PlayerState->IsOnlyASpectator(), PlayerState->IsABot());
}

void UVeloxItemRegistrySubsystem::BuildTimerSet(TArray<FVeloxItemTimerEntry>& OutTimers) const
{
	OutTimers.Reset();
	for (const AVeloxPickup* Pickup : GetMajorPickups())
	{
		FVeloxItemTimerEntry& Entry = OutTimers.AddDefaulted_GetRef();
		Entry.ItemIndex = Pickup->GetItemIndex();
		Entry.RespawnServerTime = Pickup->GetRespawnServerTime();
	}
}

void UVeloxItemRegistrySubsystem::PushTimersTo(AVeloxPlayerState* PlayerState) const
{
	const UWorld* World = GetWorld();
	if (!PlayerState || !World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const bool bEntitled = IsEntitledToItemTimers(PlayerState);
	if (bEntitled)
	{
		TArray<FVeloxItemTimerEntry> Timers;
		BuildTimerSet(Timers);
		PlayerState->SetItemTimers(MoveTemp(Timers));
	}
	else
	{
		PlayerState->ClearItemTimers();
	}
	// local=1: the listen host's own PlayerState (nothing leaves the machine for it). Names are not unique (the host and a
	// client both default to "Player"), so the nettest ItemGate evaluator tells the rows apart by this flag.
	const APlayerController* PC = Cast<APlayerController>(PlayerState->GetOwner());
	UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemTimerEntitlement player=%s pid=%d entitled=%d only_spectator=%d local=%d rule=%s n=%d"), *AVeloxPlayerState::VeloxPlayerLabel(PlayerState),
		PlayerState->GetPlayerId(), bEntitled ? 1 : 0, PlayerState->IsOnlyASpectator() ? 1 : 0, (PC && PC->IsLocalController()) ? 1 : 0, *UEnum::GetValueAsString(GetItemTimerVisibility()),
		PlayerState->GetItemTimers().Num());
}

void UVeloxItemRegistrySubsystem::RefreshAllItemTimers() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	if (!State || World->GetNetMode() == NM_Client)
	{
		return;
	}
	for (APlayerState* PS : State->PlayerArray)
	{
		PushTimersTo(Cast<AVeloxPlayerState>(PS));
	}
}

void UVeloxItemRegistrySubsystem::HandleItemTimerChanged(const AVeloxPickup& Pickup) const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	if (!State || World->GetNetMode() == NM_Client)
	{
		return;
	}
	int32 NumEntitled = 0;
	int32 NumRemote = 0;
	FString Names;
	for (APlayerState* PS : State->PlayerArray)
	{
		AVeloxPlayerState* VeloxPS = Cast<AVeloxPlayerState>(PS);
		if (!VeloxPS)
		{
			continue;
		}
		if (!IsEntitledToItemTimers(VeloxPS))
		{
			// Normally already empty; a stale set (rule or flag changed without a push) must not keep travelling.
			VeloxPS->ClearItemTimers();
			continue;
		}
		VeloxPS->SetItemTimer(Pickup.GetItemIndex(), Pickup.GetRespawnServerTime());
		++NumEntitled;
		const APlayerController* PC = Cast<APlayerController>(VeloxPS->GetOwner());
		// The listen host's own spectator-only PlayerState is entitled but local: nothing leaves the machine for it.
		NumRemote += (PC && !PC->IsLocalController()) ? 1 : 0;
		Names += (Names.IsEmpty() ? TEXT("") : TEXT(",")) + FString::Printf(TEXT("%s#%d"), *AVeloxPlayerState::VeloxPlayerLabel(VeloxPS), VeloxPS->GetPlayerId());
	}
	UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemTimerPush item=%s index=%u respawn_t=%.3f entitled=%d remote=%d to=[%s]"), *GetNameSafe(Pickup.GetDefinition()), Pickup.GetItemIndex(),
		Pickup.GetRespawnServerTime(), NumEntitled, NumRemote, *Names);
}

void UVeloxItemRegistrySubsystem::UnregisterPickup(AVeloxPickup* Pickup)
{
	Pickups.RemoveAll([Pickup](const TWeakObjectPtr<AVeloxPickup>& Entry) { return !Entry.IsValid() || Entry.Get() == Pickup; });
}

TArray<AVeloxPickup*> UVeloxItemRegistrySubsystem::GetPickups() const
{
	TArray<AVeloxPickup*> Result;
	Result.Reserve(Pickups.Num());
	for (const TWeakObjectPtr<AVeloxPickup>& Entry : Pickups)
	{
		if (AVeloxPickup* Pickup = Entry.Get())
		{
			Result.Add(Pickup);
		}
	}
	return Result;
}

TArray<AVeloxPickup*> UVeloxItemRegistrySubsystem::GetMajorPickups() const
{
	TArray<AVeloxPickup*> Result = GetPickups();
	Result.RemoveAll([](const AVeloxPickup* Pickup) { return !Pickup->GetDefinition() || !Pickup->GetDefinition()->bMajorItem; });
	return Result;
}

TArray<AVeloxPickup*> UVeloxItemRegistrySubsystem::FindByTag(const FGameplayTag& ItemTag) const
{
	TArray<AVeloxPickup*> Result = GetPickups();
	Result.RemoveAll([&ItemTag](const AVeloxPickup* Pickup) { return !ItemTag.IsValid() || !Pickup->GetDefinition() || !Pickup->GetDefinition()->ItemTag.MatchesTag(ItemTag); });
	return Result;
}
