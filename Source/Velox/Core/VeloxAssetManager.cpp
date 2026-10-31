// Velox - asset manager.

#include "Core/VeloxAssetManager.h"

#include "AbilitySystemGlobals.h"
#include "Core/VeloxCharacterCosmetics.h"
#include "Core/VeloxLog.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAssetManager)

void UVeloxAssetManager::StartInitialLoading()
{
	// Performs the primary asset scan; must run before anything below.
	Super::StartInitialLoading();

	// Initialise GAS global data (tag maps, cue manager, target data script structs) once at startup.
	UAbilitySystemGlobals::Get().InitGlobalData();
}

UVeloxAssetManager* UVeloxAssetManager::GetVelox()
{
	return UAssetManager::IsInitialized() ? Cast<UVeloxAssetManager>(&UAssetManager::Get()) : nullptr;
}

bool UVeloxAssetManager::IsCosmeticsFreeWorld(const UWorld* World)
{
	return IsRunningDedicatedServer() || (World && World->GetNetMode() == NM_DedicatedServer);
}

bool UVeloxAssetManager::LoadCharacterCosmetics(const UWorld* World, const TSoftObjectPtr<UVeloxCharacterCosmetics>& Cosmetics, TFunction<void(const UVeloxCharacterCosmetics*)> OnLoaded)
{
	if (IsCosmeticsFreeWorld(World) || !UAssetManager::IsInitialized())
	{
		return false;
	}
	if (Cosmetics.IsNull())
	{
		OnLoaded(nullptr);
		return true;
	}

	const FSoftObjectPath DataPath = Cosmetics.ToSoftObjectPath();
	FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
	Streamable.RequestAsyncLoad(DataPath, FStreamableDelegate::CreateLambda([Cosmetics, DataPath, OnLoaded]()
	{
		const UVeloxCharacterCosmetics* Data = Cosmetics.Get();
		if (!Data)
		{
			UE_LOG(LogVelox, Warning, TEXT("VeloxCosmetics: failed to load [%s]."), *DataPath.ToString());
			OnLoaded(nullptr);
			return;
		}
		TArray<FSoftObjectPath> Paths;
		Paths.Add(DataPath);
		Data->GetSoftReferences(Paths);
		UVeloxAssetManager* Manager = GetVelox();
		if (Manager)
		{
			if (const TSharedPtr<FStreamableHandle>* Existing = Manager->CosmeticsHandles.Find(DataPath); Existing && Existing->IsValid() && (*Existing)->HasLoadCompleted())
			{
				OnLoaded(Data);
				return;
			}
		}
		UE_LOG(LogVelox, Log, TEXT("VeloxCosmetics: loading %d asset(s) of %s."), Paths.Num() - 1, *DataPath.ToString());
		TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate::CreateLambda([Cosmetics, OnLoaded]()
		{
			OnLoaded(Cosmetics.Get());
		}));
		if (Manager && Handle.IsValid())
		{
			Manager->CosmeticsHandles.Add(DataPath, Handle);
		}
		else if (!Handle.IsValid())
		{
			// Nothing to request (already resident without a handle of ours): the delegate does not fire.
			OnLoaded(Data);
		}
	}));
	return true;
}
