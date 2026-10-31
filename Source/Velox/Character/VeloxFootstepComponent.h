// Velox - character sounds that do not come from a weapon: footsteps, jump, landing, pain, death (ADR 0006 section 9).
// On every AVeloxCharacter, on all clients, bots and proxies included: no animation notifies, so everything is derived from
// the pawn's own motion and the health component, and a proxy sounds like the owning player. The server plays nothing.

#pragma once

#include "Components/ActorComponent.h"
#include "Character/VeloxFootstepMath.h"

#include "VeloxFootstepComponent.generated.h"

class AVeloxCharacter;
class UVeloxHealthComponent;

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxFootstepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVeloxFootstepComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~End of UActorComponent interface

	/** AVeloxCharacter::Landed: soft / hard landing by the impact speed. Ignored while the owning client replays moves. */
	void HandleLanded(float ImpactVz);

	/** AVeloxCharacter::OnJumped: the jump grunt. Ignored while the owning client replays moves. */
	void HandleJumped();

	/** The pawn became a corpse (server death flow or the clients' OnRep_IsCorpse): the death sound, once. */
	void HandleCorpse();

	/** Footsteps played since BeginPlay (tests, logs). */
	int32 GetNumSteps() const { return NumSteps; }

private:
	AVeloxCharacter* GetCharacter() const;
	/** Where a foot sound comes from: the foot socket, else the actor location offset sideways. */
	FVector GetFootLocation(bool bRight) const;
	void PlayStep(bool bRight);

	UFUNCTION()
	void OnHealthChanged(UVeloxHealthComponent* HealthComponent, float OldValue, float NewValue, AActor* Instigator);

	UFUNCTION()
	void OnDeathStarted(AActor* OwningActor);

	VeloxFootstepMath::FState StepState;
	FVector LastLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;
	bool bWasGrounded = true;
	float LastFallingVz = 0.f;
	int32 NumSteps = 0;
	int32 StepVariation = 0;
	bool bBoundHealth = false;
	bool bDeathPlayed = false;
};
