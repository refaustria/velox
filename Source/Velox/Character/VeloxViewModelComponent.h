// Velox - first-person weapon view model (ADR 0006 section 4.6): a static mesh in front of the camera, no arms, moved
// procedurally by bob, sway, recoil and the weapon switch. Created only for the locally controlled player pawn of a
// non-dedicated world (AVeloxCharacter::EnsureViewModel); the dedicated server never creates it or loads any cosmetics.
//
// Rendering uses UE 5.8's first-person primitive path: the mesh is an EFirstPersonPrimitiveType::FirstPerson primitive
// (Components/PrimitiveComponent.h), and the pawn's camera result sets FMinimalViewInfo::bUseFirstPersonParameters,
// FirstPersonFOV and FirstPersonScale (Camera/CameraTypes.h) from UVeloxViewModelSettings (ApplyToView).

#pragma once

#include "Components/SceneComponent.h"
#include "Character/VeloxViewModelMath.h"

#include "VeloxViewModelComponent.generated.h"

class UStaticMeshComponent;
class UVeloxViewModelSettings;
class UVeloxWeaponInventoryComponent;
struct FMinimalViewInfo;

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxViewModelComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UVeloxViewModelComponent();

	/** Binds the owner's weapon inventory and feedback component, creates the mesh child and shows the current weapon. */
	void Initialize();

	/** Camera result: first-person FOV and scale for the view model primitive. */
	void ApplyToView(FMinimalViewInfo& OutResult) const;

	const UVeloxViewModelSettings& GetSettings() const;
	UStaticMeshComponent* GetViewMeshComponent() const { return ViewMesh; }

	/** The weapon table index currently shown (INDEX_NONE: none). */
	int32 GetShownIndex() const { return ShownIndex; }

	/** Shows weapon Index (mesh and offset from its cosmetics, resident Get() only); AVeloxCharacter calls it again when the weapon-cosmetics load completes. */
	void ShowWeapon(int32 Index);

	//~UActorComponent interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	//~End of UActorComponent interface

protected:
	/** Tunables; the C++ defaults of UVeloxViewModelSettings apply when the asset cannot be loaded. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|ViewModel")
	TSoftObjectPtr<UVeloxViewModelSettings> SettingsAsset;

private:
	void OnWeaponChanged(int32 OldIndex, int32 NewIndex);
	void OnFireCue();
	/** Current camera transform of the owner pawn (same source as the pawn's camera). */
	bool GetCameraTransform(FTransform& OutCamera, FRotator& OutViewRotation) const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ViewMesh;

	UPROPERTY(Transient)
	TObjectPtr<const UVeloxViewModelSettings> LoadedSettings;

	TWeakObjectPtr<UVeloxWeaponInventoryComponent> Inventory;
	FDelegateHandle WeaponChangedHandle;
	FDelegateHandle FireCueHandle;
	int32 ShownIndex = INDEX_NONE;

	/** Placement of the shown weapon relative to the camera (UVeloxWeaponCosmetics::ViewOffset). */
	FTransform ShownOffset = FTransform::Identity;

	double BobPhase = 0.0;
	float RecoilStrength = 0.f;
	VeloxViewModelMath::FSwayState Sway;
	FRotator LastViewRotation = FRotator::ZeroRotator;
	bool bHasLastViewRotation = false;
};
