// Velox - one weapon's look and sound (ADR 0006 section 4.6 and the audio section). Assets:
// /Game/Weapons/DA_WeaponCosmetics_<Name>, referenced softly from UVeloxWeaponDefinition::Cosmetics. Every reference is
// soft: the dedicated server never loads any of it. Meshes and sounds may be empty (the art tasks fill them).

#pragma once

#include "Engine/DataAsset.h"
#include "Math/Transform.h"

#include "VeloxWeaponCosmetics.generated.h"

class UStaticMesh;
class USoundBase;

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Weapon Cosmetics"))
class VELOX_API UVeloxWeaponCosmetics : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Mesh in the pawn's right hand for everyone else (socket weapon_r). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Mesh")
	TSoftObjectPtr<UStaticMesh> WorldMesh;

	/** First-person mesh (may be the same asset as WorldMesh). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Mesh")
	TSoftObjectPtr<UStaticMesh> ViewMesh;

	/** View model placement relative to the camera (x forward, y right, z up, cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Mesh")
	FTransform ViewOffset = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Mesh", Meta = (ClampMin = "0.01"))
	float ViewScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Mesh")
	FName MuzzleSocket = TEXT("muzzle");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Audio")
	TSoftObjectPtr<USoundBase> FireSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Audio")
	TSoftObjectPtr<USoundBase> ImpactSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Audio")
	TSoftObjectPtr<USoundBase> BeamStart;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Audio")
	TSoftObjectPtr<USoundBase> BeamLoop;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Audio")
	TSoftObjectPtr<USoundBase> BeamStop;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Audio")
	TSoftObjectPtr<USoundBase> DryFireSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Audio")
	TSoftObjectPtr<USoundBase> SwitchSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Light")
	FLinearColor MuzzleLightColor = FLinearColor::White;

	/** 0 = no muzzle light. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Light", Meta = (ClampMin = "0"))
	float MuzzleLightIntensity = 0.f;
};
