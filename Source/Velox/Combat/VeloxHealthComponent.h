// Velox - health component on the pawn (ADR 0001 section 2, ADR 0003 section 2; Lyra health-component idea). UI and the
// game mode use this, never the ASC. Bound to the PlayerState's ASC while the pawn is its avatar.

#pragma once

#include "Components/ActorComponent.h"
#include "Delegates/IDelegateInstance.h"

#include "VeloxHealthComponent.generated.h"

class UVeloxAbilitySystemComponent;
class UVeloxHealthSet;
struct FOnAttributeChangeData;
struct FVeloxDamageEvent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FVeloxHealthAttributeChanged, UVeloxHealthComponent*, HealthComponent, float, OldValue, float, NewValue, AActor*, Instigator);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVeloxHealthDeathEvent, AActor*, OwningActor);
DECLARE_MULTICAST_DELEGATE_OneParam(FVeloxHealthDamageEvent, const FVeloxDamageEvent& /*Event*/);

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVeloxHealthComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "Velox|Health")
	static UVeloxHealthComponent* FindHealthComponent(const AActor* Actor);

	/** Binds to ASC (server and clients). Called by AVeloxCharacter after InitAbilityActorInfo; rebinding is a no-op. */
	void InitializeWithAbilitySystem(UVeloxAbilitySystemComponent* InASC);

	/** Unbinds (unpossess, EndPlay). */
	void UninitializeFromAbilitySystem();

	UFUNCTION(BlueprintPure, Category = "Velox|Health")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "Velox|Health")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "Velox|Health")
	float GetArmor() const;

	UFUNCTION(BlueprintPure, Category = "Velox|Health")
	float GetMaxArmor() const;

	UFUNCTION(BlueprintPure, Category = "Velox|Health")
	bool IsDeadOrDying() const { return bDeathStarted; }

	/** Task 06 death flow: broadcast OnDeathStarted / OnDeathFinished once each. */
	void StartDeath();
	void FinishDeath();

	/** Health/armor changed (server and clients, from replication too). */
	UPROPERTY(BlueprintAssignable)
	FVeloxHealthAttributeChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable)
	FVeloxHealthAttributeChanged OnArmorChanged;

	UPROPERTY(BlueprintAssignable)
	FVeloxHealthDeathEvent OnDeathStarted;

	UPROPERTY(BlueprintAssignable)
	FVeloxHealthDeathEvent OnDeathFinished;

	/** Server: a damage execution hit this pawn. */
	FVeloxHealthDamageEvent OnDamaged;

	/** Server: health reached 0 (AVeloxCharacter::HandleDeath in task 06). */
	FVeloxHealthDamageEvent OnOutOfHealth;

protected:
	virtual void OnUnregister() override;

	void HandleHealthChanged(const FOnAttributeChangeData& Data);
	void HandleArmorChanged(const FOnAttributeChangeData& Data);
	void HandleDamaged(const FVeloxDamageEvent& Event);
	void HandleOutOfHealth(const FVeloxDamageEvent& Event);

	UPROPERTY(Transient)
	TObjectPtr<UVeloxAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(Transient)
	TObjectPtr<const UVeloxHealthSet> HealthSet;

private:
	FDelegateHandle HealthChangedHandle;
	FDelegateHandle ArmorChangedHandle;
	FDelegateHandle DamagedHandle;
	FDelegateHandle OutOfHealthHandle;
	bool bDeathStarted = false;
	bool bDeathFinished = false;
};
