// Velox - asset manager. Set via AssetManagerClassName in Config/DefaultEngine.ini.

#pragma once

#include "Engine/AssetManager.h"
#include "Templates/Function.h"

#include "VeloxAssetManager.generated.h"

class UVeloxCharacterCosmetics;
class UVeloxWeaponDefinition;
class UWorld;

UCLASS(Config = Game)
class VELOX_API UVeloxAssetManager : public UAssetManager
{
	GENERATED_BODY()

public:
	/** The project's asset manager, null when AssetManagerClassName is not ours (e.g. a stripped commandlet). */
	static UVeloxAssetManager* GetVelox();

	/** True for worlds that must never load cosmetics: dedicated server processes and dedicated-server worlds (PIE). */
	static bool IsCosmeticsFreeWorld(const UWorld* World);

	/**
	 * ADR 0006 section 1: async-loads the cosmetics data asset, then every soft reference in it (mesh, anim class, skin
	 * materials, sequences), and calls OnLoaded on the game thread with the data asset (null when it is unset or failed
	 * to load). The loaded set stays resident for the session (one handle per data asset), so later pawns resolve at once.
	 * Returns false without loading anything (and without calling OnLoaded) on a cosmetics-free world.
	 */
	static bool LoadCharacterCosmetics(const UWorld* World, const TSoftObjectPtr<UVeloxCharacterCosmetics>& Cosmetics, TFunction<void(const UVeloxCharacterCosmetics*)> OnLoaded);

	/**
	 * Review 0006 D4: async-loads the cosmetics asset of every definition (stage 1), then every soft reference of those
	 * (meshes, fire / impact / beam sounds; stage 2) and calls OnLoaded on the game thread when the set is resident. One
	 * resident handle ("WeaponCosmetics") for the process: later calls (other pawns, respawns) call OnLoaded at once, calls
	 * during the load wait for it. Returns false without loading anything (and without calling OnLoaded) on a cosmetics-free world.
	 */
	static bool LoadWeaponCosmetics(const UWorld* World, TConstArrayView<const UVeloxWeaponDefinition*> Definitions, TFunction<void()> OnLoaded);

	/** True once LoadWeaponCosmetics has made the set resident (tests, logs). */
	bool IsWeaponCosmeticsResident() const { return bWeaponCosmeticsResident; }

protected:
	//~UAssetManager interface
	virtual void StartInitialLoading() override;
	//~End of UAssetManager interface

private:
	/** Keeps every loaded cosmetics set resident (keyed by the data asset path). */
	TMap<FSoftObjectPath, TSharedPtr<FStreamableHandle>> CosmeticsHandles;

	/** The one weapon-cosmetics set of this process (key "WeaponCosmetics"). */
	TSharedPtr<FStreamableHandle> WeaponCosmeticsHandle;
	bool bWeaponCosmeticsLoading = false;
	bool bWeaponCosmeticsResident = false;
	TArray<TFunction<void()>> WeaponCosmeticsWaiters;

	void FinishWeaponCosmetics();
};
