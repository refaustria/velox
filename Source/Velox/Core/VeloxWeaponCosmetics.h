// Velox - one weapon's look and sound: the single source (fire, impact and beam sounds, muzzle light, meshes; review 0006 D4) (ADR 0006 section 4.6 and the audio section). Assets:
// /Game/Weapons/DA_WeaponCosmetics_<Name>, referenced softly from UVeloxWeaponDefinition::Cosmetics. Every reference is
// soft: the dedicated server never loads any of it. Meshes and sounds may be empty (the art tasks fill them).

#pragma once

#include "Engine/DataAsset.h"
#include "Core/VeloxAudioSettings.h"
#include "Math/Transform.h"

#include "VeloxWeaponCosmetics.generated.h"

class UStaticMesh;
class USoundBase;
class UMaterialInterface;

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

	/** Attenuation of ImpactSound (Impact for bullets, Explosion for the rocket and plasma). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Audio")
	EVeloxAudioAttenuation ImpactAttenuation = EVeloxAudioAttenuation::Impact;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Light")
	FLinearColor MuzzleLightColor = FLinearColor::White;

	/** 0 = no muzzle light. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Light", Meta = (ClampMin = "0"))
	float MuzzleLightIntensity = 0.f;

	/** Projectile weapons: colour of the light that rides the projectile in flight (task M5-13). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Light")
	FLinearColor ProjectileLightColor = FLinearColor(1.f, 0.55f, 0.2f);

	/** 0 = no flight light. Counted against UVeloxAudioSettings::MaxMovableLights together with the muzzle flashes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Light", Meta = (ClampMin = "0"))
	float ProjectileLightIntensity = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Light", Meta = (ClampMin = "0", Units = "cm"))
	float ProjectileLightRadius = 400.f;

	/** Projectile weapons: the mesh that flies (unlit, bright; task M5-13b). Empty = the projectile shows only its light. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Projectile")
	TSoftObjectPtr<UStaticMesh> ProjectileMesh;

	/** Length of the trail behind a projectile (cm, a stretched TracerMesh in TracerColor / TracerWidth); 0 = no trail. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Projectile", Meta = (ClampMin = "0", Units = "cm"))
	float ProjectileTrailLength = 0.f;

	/** Unit beam (100 cm along +X, 1 cm radius; SM_Fx_Tracer) stretched per shot into the hitscan tracer and the projectile trail. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Tracer")
	TSoftObjectPtr<UStaticMesh> TracerMesh;

	/** Unlit material with a `Color` vector parameter (M_Tracer_Unlit). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Tracer")
	TSoftObjectPtr<UMaterialInterface> TracerMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Tracer")
	FLinearColor TracerColor = FLinearColor::White;

	/** Radius scale of the unit beam (1 = 1 cm radius). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Tracer", Meta = (ClampMin = "0"))
	float TracerWidth = 0.5f;

	/** Seconds a hitscan tracer stays (rail ~0.5, MG / shotgun ~0.05); 0 = this weapon draws no tracer (projectile weapons). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon|Tracer", Meta = (ClampMin = "0", Units = "s"))
	float TracerLifetime = 0.f;

	/** Every soft reference of this asset (meshes, sounds), for the asset manager's resident load (UVeloxAssetManager::LoadWeaponCosmetics). */
	void GetSoftReferences(TArray<FSoftObjectPath>& OutPaths) const;
};
