// Velox - dummy spawner.

#include "Bots/VeloxDummySpawner.h"

#include "AIController.h"
#include "Bots/VeloxDummyController.h"
#include "Character/VeloxCharacter.h"
#include "Components/SceneComponent.h"
#include "Core/VeloxLog.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxDummySpawner)

AVeloxDummySpawner::AVeloxDummySpawner(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	DummyClass = AVeloxCharacter::StaticClass();
	ControllerClass = AVeloxDummyController::StaticClass();
}

void AVeloxDummySpawner::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoSpawn && HasAuthority())
	{
		SpawnDummy();
	}
}

AVeloxCharacter* AVeloxDummySpawner::SpawnDummy()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || !DummyClass || !ControllerClass)
	{
		return nullptr;
	}
	if (SpawnedDummy && IsValid(SpawnedDummy))
	{
		return SpawnedDummy;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.Owner = this;
	AVeloxCharacter* Dummy = World->SpawnActor<AVeloxCharacter>(DummyClass, GetActorLocation(), GetActorRotation(), Params);
	if (!Dummy)
	{
		UE_LOG(LogVelox, Warning, TEXT("%s: failed to spawn dummy pawn."), *GetName());
		return nullptr;
	}

	FActorSpawnParameters ControllerParams;
	ControllerParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAIController* Controller = World->SpawnActor<AAIController>(ControllerClass, FTransform::Identity, ControllerParams);
	if (!Controller)
	{
		UE_LOG(LogVelox, Warning, TEXT("%s: failed to spawn dummy controller."), *GetName());
		Dummy->Destroy();
		return nullptr;
	}
	if (AVeloxDummyController* DummyController = Cast<AVeloxDummyController>(Controller))
	{
		DummyController->StrafeHalfWidth = StrafeHalfWidth;
		DummyController->StrafeSpeedScale = StrafeSpeedScale;
		DummyController->SetFixedRespawn(GetActorTransform(), DummyClass);
		DummyController->SetInvulnerable(bInvulnerable);
	}
	Controller->Possess(Dummy);
	SpawnedDummy = Dummy;
	UE_LOG(LogVelox, Log, TEXT("%s: spawned dummy %s at %s."), *GetName(), *Dummy->GetName(), *Dummy->GetActorLocation().ToCompactString());
	return Dummy;
}
