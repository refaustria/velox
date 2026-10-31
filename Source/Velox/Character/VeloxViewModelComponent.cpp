// Velox - first-person weapon view model.

#include "Character/VeloxViewModelComponent.h"

#include "Camera/CameraTypes.h"
#include "Combat/VeloxCombatSettings.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/VeloxCollisionChannels.h"
#include "Core/VeloxLog.h"
#include "Core/VeloxViewModelSettings.h"
#include "Core/VeloxWeaponCosmetics.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapons/VeloxCombatFeedbackComponent.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxViewModelComponent)

UVeloxViewModelComponent::UVeloxViewModelComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After movement and the camera-relevant pawn updates of the frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(false);
	SettingsAsset = TSoftObjectPtr<UVeloxViewModelSettings>(FSoftObjectPath(TEXT("/Game/Characters/Velox/DA_ViewModelSettings_Default.DA_ViewModelSettings_Default")));
}

const UVeloxViewModelSettings& UVeloxViewModelComponent::GetSettings() const
{
	return LoadedSettings ? *LoadedSettings : *GetDefault<UVeloxViewModelSettings>();
}

void UVeloxViewModelComponent::Initialize()
{
	const AActor* Owner = GetOwner();
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer)
	{
		UE_LOG(LogVelox, Verbose, TEXT("VeloxViewModel: dedicated server, no view model content"));
		return;
	}
	if (ViewMesh)
	{
		return;
	}

	if (!SettingsAsset.IsNull())
	{
		LoadedSettings = SettingsAsset.LoadSynchronous();
	}
	if (!LoadedSettings)
	{
		UE_LOG(LogVelox, Verbose, TEXT("VeloxViewModel: settings asset [%s] not loaded, using class defaults"), *SettingsAsset.ToString());
	}

	// World-space tracking: the transform is set from the camera every tick.
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);

	ViewMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("ViewModelMesh"));
	ViewMesh->SetupAttachment(this);
	ViewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ViewMesh->SetCollisionResponseToChannel(Velox_TraceChannel_Weapon, ECR_Ignore);
	ViewMesh->SetGenerateOverlapEvents(false);
	ViewMesh->SetCastShadow(false);
	ViewMesh->SetOnlyOwnerSee(true);
	ViewMesh->bAffectDynamicIndirectLighting = false;
	ViewMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	ViewMesh->SetVisibility(false);
	ViewMesh->RegisterComponent();

	if (UVeloxWeaponInventoryComponent* Inv = UVeloxWeaponInventoryComponent::Find(Owner))
	{
		Inventory = Inv;
		WeaponChangedHandle = Inv->OnCurrentWeaponChanged.AddUObject(this, &ThisClass::OnWeaponChanged);
		ShowWeapon(Inv->GetCurrentIndex());
	}
	if (UVeloxCombatFeedbackComponent* Feedback = Owner->FindComponentByClass<UVeloxCombatFeedbackComponent>())
	{
		FireCueHandle = Feedback->OnFireCue.AddUObject(this, &ThisClass::OnFireCue);
	}
}

void UVeloxViewModelComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (UVeloxWeaponInventoryComponent* Inv = Inventory.Get())
	{
		Inv->OnCurrentWeaponChanged.Remove(WeaponChangedHandle);
	}
	if (const AActor* Owner = GetOwner())
	{
		if (UVeloxCombatFeedbackComponent* Feedback = Owner->FindComponentByClass<UVeloxCombatFeedbackComponent>())
		{
			Feedback->OnFireCue.Remove(FireCueHandle);
		}
	}
	if (ViewMesh)
	{
		ViewMesh->DestroyComponent();
		ViewMesh = nullptr;
	}
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UVeloxViewModelComponent::ApplyToView(FMinimalViewInfo& OutResult) const
{
	if (!ViewMesh || !ViewMesh->IsVisible())
	{
		return;
	}
	const UVeloxViewModelSettings& S = GetSettings();
	OutResult.bUseFirstPersonParameters = true;
	OutResult.FirstPersonFOV = S.FirstPersonFieldOfView;
	OutResult.FirstPersonScale = S.FirstPersonScale;
}

void UVeloxViewModelComponent::OnWeaponChanged(int32 OldIndex, int32 NewIndex)
{
	ShowWeapon(NewIndex);
}

void UVeloxViewModelComponent::OnFireCue()
{
	RecoilStrength = VeloxViewModelMath::AddRecoilImpulse(RecoilStrength);
}

void UVeloxViewModelComponent::ShowWeapon(int32 Index)
{
	ShownIndex = Index;
	if (!ViewMesh)
	{
		return;
	}
	const UVeloxWeaponInventoryComponent* Inv = Inventory.Get();
	const UVeloxWeaponDefinition* Definition = Inv ? Inv->GetDefinition(Index) : nullptr;
	const UVeloxWeaponCosmetics* Cosmetics = (Definition && !Definition->Cosmetics.IsNull()) ? Definition->Cosmetics.LoadSynchronous() : nullptr;
	UStaticMesh* Mesh = (Cosmetics && !Cosmetics->ViewMesh.IsNull()) ? Cosmetics->ViewMesh.LoadSynchronous() : nullptr;

	ShownOffset = FTransform::Identity;
	if (Cosmetics)
	{
		ShownOffset = Cosmetics->ViewOffset;
		ShownOffset.SetScale3D(ShownOffset.GetScale3D() * Cosmetics->ViewScale);
	}
	ViewMesh->SetStaticMesh(Mesh);
	ViewMesh->SetRelativeTransform(ShownOffset);
	ViewMesh->SetVisibility(Mesh != nullptr);
	UE_LOG(LogVelox, Verbose, TEXT("VeloxViewModel weapon=%s mesh=%s"), *GetNameSafe(Definition), *GetNameSafe(Mesh));
}

bool UVeloxViewModelComponent::GetCameraTransform(FTransform& OutCamera, FRotator& OutViewRotation) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	FVector Location;
	FRotator Rotation;
	Owner->GetActorEyesViewPoint(Location, Rotation);
	OutCamera = FTransform(Rotation, Location);
	OutViewRotation = Rotation;
	return true;
}

void UVeloxViewModelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!ViewMesh)
	{
		return;
	}
	FTransform Camera;
	FRotator ViewRotation;
	if (!GetCameraTransform(Camera, ViewRotation))
	{
		return;
	}
	const UVeloxViewModelSettings& S = GetSettings();

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const bool bOnGround = Movement && Movement->IsMovingOnGround();
	const float GroundSpeed = Movement ? Movement->Velocity.Size2D() : 0.f;

	// Bob: distance-driven phase, ground only.
	BobPhase = VeloxViewModelMath::AdvanceBobPhase(BobPhase, GroundSpeed, bOnGround, DeltaTime, S);
	const FVector Bob = VeloxViewModelMath::BobOffset(BobPhase, GroundSpeed, bOnGround, S);

	// Sway: lag of this frame's view rotation delta.
	FVector2D ViewDelta = FVector2D::ZeroVector;
	if (bHasLastViewRotation)
	{
		ViewDelta.X = FRotator::NormalizeAxis(ViewRotation.Yaw - LastViewRotation.Yaw);
		ViewDelta.Y = FRotator::NormalizeAxis(ViewRotation.Pitch - LastViewRotation.Pitch);
	}
	LastViewRotation = ViewRotation;
	bHasLastViewRotation = true;
	VeloxViewModelMath::StepSway(Sway, ViewDelta, DeltaTime, S);

	RecoilStrength = VeloxViewModelMath::DecayRecoil(RecoilStrength, DeltaTime, S);

	// Raise / lower follows the (predicted) inventory switch.
	float Lower = 0.f;
	if (const UVeloxWeaponInventoryComponent* Inv = Inventory.Get())
	{
		const UWorld* World = GetWorld();
		const UVeloxCombatSettings* Combat = Inv->GetCombatSettings();
		const bool bDropping = Inv->GetPendingIndex() != INDEX_NONE;
		const double SwitchRemaining = Inv->GetSwitchEndTime() > 0.0 && World ? Inv->GetSwitchEndTime() - World->GetTimeSeconds() : 0.0;
		const float Drop = Combat ? Combat->WeaponDropTime : 0.f;
		const float Raise = Combat ? Combat->WeaponRaiseTime : 0.f;
		Lower = VeloxViewModelMath::LowerAlpha(bDropping, static_cast<float>(Inv->GetPendingDropRemaining()), Drop, static_cast<float>(SwitchRemaining), Raise);
	}

	const FVector Local = Bob + VeloxViewModelMath::SwayOffset(Sway, S) + FVector(-S.RecoilKickCm * RecoilStrength, 0.0, -S.LowerCm * Lower);
	const FRotator LocalRotation(S.RecoilPitchDeg * RecoilStrength + Sway.Angles.Y, Sway.Angles.X, 0.0);
	const FTransform Motion(LocalRotation, Local);
	SetWorldTransform(Motion * Camera);
}
