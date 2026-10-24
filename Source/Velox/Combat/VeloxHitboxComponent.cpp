// Velox - pawn hit volumes.

#include "Combat/VeloxHitboxComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxHitboxComponent)

UVeloxHitboxComponent::UVeloxHitboxComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

UVeloxHitboxComponent* UVeloxHitboxComponent::FindHitboxComponent(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVeloxHitboxComponent>() : nullptr;
}

ACharacter* UVeloxHitboxComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

FVeloxHitboxPose UVeloxHitboxComponent::GetCurrentPose() const
{
	FVeloxHitboxPose Pose;
	GetCurrentPose(Pose);
	return Pose;
}

void UVeloxHitboxComponent::GetCurrentPose(FVeloxHitboxPose& OutPose) const
{
	OutPose.Shapes.Reset();
	const ACharacter* Character = GetCharacter();
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return;
	}
	// The capsule is kept upright (yaw-only rotation), so the shape axis is world up. Simulated proxies carry the
	// engine's NetProxyShrink (0.01 cm), which is below every tolerance that matters here.
	OutPose.Shapes.Emplace(Capsule->GetComponentLocation(), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
}

FVector UVeloxHitboxComponent::GetRenderedOffset() const
{
	const ACharacter* Character = GetCharacter();
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	const USceneComponent* MeshParent = Mesh ? Mesh->GetAttachParent() : nullptr;
	if (!MeshParent)
	{
		return FVector::ZeroVector;
	}
	// Linear smoothing puts the mesh at Parent * (BaseTranslationOffset + unrotated smoothing offset) and rotates the
	// capsule to the smoothed rotation, so the difference to the unsmoothed mesh place is the world smoothing offset.
	return Mesh->GetComponentLocation() - MeshParent->GetComponentTransform().TransformPosition(Character->GetBaseTranslationOffset());
}

FVeloxHitboxPose UVeloxHitboxComponent::GetRenderedPose() const
{
	FVeloxHitboxPose Pose;
	GetRenderedPose(Pose);
	return Pose;
}

void UVeloxHitboxComponent::GetRenderedPose(FVeloxHitboxPose& OutPose) const
{
	GetCurrentPose(OutPose);
	const FVector Offset = GetRenderedOffset();
	for (FVeloxHitShape& Shape : OutPose.Shapes)
	{
		Shape.Centre += Offset;
	}
}

void UVeloxHitboxComponent::BeginPlay()
{
	Super::BeginPlay();
	// Task 03: register with UVeloxLagCompensationSubsystem here when the owner has authority (the subsystem exists
	// only when NetMode != NM_Client).
}

void UVeloxHitboxComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Task 03: unregister from UVeloxLagCompensationSubsystem (death unregisters earlier, task 06).
	Super::EndPlay(EndPlayReason);
}
