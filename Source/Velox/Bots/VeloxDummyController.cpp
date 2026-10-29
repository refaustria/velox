// Velox - strafing target dummy controller.

#include "Bots/VeloxDummyController.h"

#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxGameplayEffects.h"
#include "GameModes/VeloxPlayerState.h"
#include "Core/VeloxLog.h"
#include "Engine/World.h"
#include "Movement/VeloxMovementComponent.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxDummyController)

AVeloxDummyController::AVeloxDummyController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	// A PlayerState gives the dummy an ability system component and a scoreboard entry, like a real combatant.
	bWantsPlayerState = true;
}

void AVeloxDummyController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (InPawn)
	{
		// A fixed respawn restarts the strafe at the spawner, wherever the pawn was adjusted to.
		StrafeOrigin = bHasFixedRespawn ? RespawnTransform.GetLocation() : InPawn->GetActorLocation();
		StrafeDirection = 1.f;
		// Strafing is along world Y.
		SetControlRotation(FRotator(0.f, FacingYaw, 0.f));

		// After Super::OnPossess: GE_SpawnInit (run by PossessedBy) removes SpawnProtected, so apply this last.
		AVeloxPlayerState* VeloxPlayerState = bInvulnerable ? GetPlayerState<AVeloxPlayerState>() : nullptr;
		if (UVeloxAbilitySystemComponent* ASC = VeloxPlayerState ? VeloxPlayerState->GetVeloxAbilitySystemComponent() : nullptr)
		{
			const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDurationSpec(ASC, UVeloxGE_SpawnProtection::StaticClass(), UGameplayEffect::INFINITE_DURATION);
			if (Spec.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			}
		}
	}

	if (LogInterval > 0.f && GetWorld())
	{
		GetWorldTimerManager().SetTimer(LogTimer, this, &AVeloxDummyController::LogPosition, LogInterval, true);
	}
}

void AVeloxDummyController::SetFixedRespawn(const FTransform& Transform, TSubclassOf<AVeloxCharacter> PawnClass)
{
	RespawnTransform = Transform;
	RespawnPawnClass = PawnClass;
	bHasFixedRespawn = true;
}

void AVeloxDummyController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(LogTimer);
	Super::OnUnPossess();
}

void AVeloxDummyController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AVeloxCharacter* Dummy = Cast<AVeloxCharacter>(GetPawn());
	if (!Dummy)
	{
		return;
	}

	const float OffsetY = Dummy->GetActorLocation().Y - StrafeOrigin.Y;
	if (OffsetY >= StrafeHalfWidth)
	{
		StrafeDirection = -1.f;
	}
	else if (OffsetY <= -StrafeHalfWidth)
	{
		StrafeDirection = 1.f;
	}

	const float Scale = FMath::Clamp(StrafeSpeedScale, 0.f, 1.f);
	// Same entry points as player input (ADR 0002 section 8): world-Y wish direction, side-only raw input.
	Dummy->AddMovementInput(FVector::YAxisVector, StrafeDirection * Scale);
	if (UVeloxMovementComponent* Movement = Dummy->GetVeloxMovementComponent())
	{
		Movement->SetLocalMoveInput(FVector2D(0.f, StrafeDirection * Scale));
		Movement->SetJumpHeld(false);
	}
}

void AVeloxDummyController::LogPosition() const
{
	if (const APawn* P = GetPawn())
	{
		UE_LOG(LogVelox, Log, TEXT("Dummy %s at %s velocity %s (origin Y %.0f, half width %.0f, dir %+.0f)"), *GetName(),
			*P->GetActorLocation().ToCompactString(), *P->GetVelocity().ToCompactString(), StrafeOrigin.Y, StrafeHalfWidth,
			StrafeDirection);
	}
}
