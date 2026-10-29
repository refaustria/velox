// Velox - item registry.

#include "Items/VeloxItemRegistrySubsystem.h"

#include "Engine/World.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxGameState.h"
#include "Items/VeloxItemDefinition.h"
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
		Pickups.AddUnique(Pickup);
	}
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
