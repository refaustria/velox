// Velox - asset manager.

#include "Core/VeloxAssetManager.h"

#include "Core/VeloxServerKind.h"
#include "AbilitySystemGlobals.h"
#include "Core/VeloxCharacterCosmetics.h"
#include "Core/VeloxLog.h"
#include "Core/VeloxWeaponCosmetics.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Weapons/VeloxWeaponDefinition.h"

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
	return VeloxServerKind::IsServerLike(World); // dedicated, or a headless listen host (ADR 0007 task 02)
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

void UVeloxAssetManager::FinishWeaponCosmetics()
{
	bWeaponCosmeticsLoading = false;
	bWeaponCosmeticsResident = true;
	TArray<TFunction<void()>> Waiters = MoveTemp(WeaponCosmeticsWaiters);
	WeaponCosmeticsWaiters.Reset();
	for (const TFunction<void()>& Waiter : Waiters)
	{
		Waiter();
	}
}

bool UVeloxAssetManager::LoadWeaponCosmetics(const UWorld* World, TConstArrayView<const UVeloxWeaponDefinition*> Definitions, TFunction<void()> OnLoaded)
{
	UVeloxAssetManager* Manager = GetVelox();
	if (IsCosmeticsFreeWorld(World) || !Manager)
	{
		static bool bLoggedServerSkip = false;
		UE_CLOG(!bLoggedServerSkip && IsCosmeticsFreeWorld(World), LogVelox, Verbose, TEXT("VeloxCosmetics: weapon cosmetics skipped on dedicated server (nothing is loaded)."));
		bLoggedServerSkip = bLoggedServerSkip || IsCosmeticsFreeWorld(World);
		return false;
	}
	if (Manager->bWeaponCosmeticsResident)
	{
		OnLoaded();
		return true;
	}
	Manager->WeaponCosmeticsWaiters.Add(MoveTemp(OnLoaded));
	if (Manager->bWeaponCosmeticsLoading)
	{
		return true;
	}
	Manager->bWeaponCosmeticsLoading = true;

	TArray<FSoftObjectPath> Stage1;
	TArray<TSoftObjectPtr<UVeloxWeaponCosmetics>> Cosmetics;
	for (const UVeloxWeaponDefinition* Definition : Definitions)
	{
		if (Definition && !Definition->Cosmetics.IsNull())
		{
			Stage1.AddUnique(Definition->Cosmetics.ToSoftObjectPath());
			Cosmetics.Add(Definition->Cosmetics);
		}
	}
	if (Stage1.IsEmpty())
	{
		Manager->FinishWeaponCosmetics();
		return true;
	}

	// Stage 2 runs when the data assets are in memory; the resident handle covers them and everything they name.
	auto Stage2 = [Cosmetics, Stage1]()
	{
		UVeloxAssetManager* Manager = GetVelox();
		if (!Manager)
		{
			return;
		}
		TArray<FSoftObjectPath> Paths;
		for (const TSoftObjectPtr<UVeloxWeaponCosmetics>& Soft : Cosmetics)
		{
			if (const UVeloxWeaponCosmetics* Data = Soft.Get())
			{
				Data->GetSoftReferences(Paths);
			}
			else
			{
				UE_LOG(LogVelox, Warning, TEXT("VeloxCosmetics: failed to load [%s]."), *Soft.ToString());
			}
		}
		const int32 NumReferenced = Paths.Num();
		for (const FSoftObjectPath& Path : Stage1)
		{
			Paths.AddUnique(Path);
		}
		UE_LOG(LogVelox, Log, TEXT("VeloxCosmetics: loading %d asset(s) of WeaponCosmetics (%d cosmetics asset(s))."), NumReferenced, Stage1.Num());
		TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate::CreateLambda([]()
		{
			if (UVeloxAssetManager* Done = GetVelox())
			{
				Done->FinishWeaponCosmetics();
			}
		}));
		if (Handle.IsValid())
		{
			Manager->WeaponCosmeticsHandle = Handle;
		}
		else
		{
			Manager->FinishWeaponCosmetics();
		}
	};

	TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Stage1, FStreamableDelegate::CreateLambda(Stage2));
	if (!Handle.IsValid())
	{
		Stage2();
	}
	return true;
}
