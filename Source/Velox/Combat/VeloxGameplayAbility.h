// Velox - gameplay ability base (ADR 0003 section 1; Lyra activation-policy idea).

#pragma once

#include "Abilities/GameplayAbility.h"

#include "VeloxGameplayAbility.generated.h"

class UVeloxAbilitySystemComponent;

/** How input activates an ability (processed by UVeloxAbilitySystemComponent::ProcessAbilityInput). */
UENUM(BlueprintType)
enum class EVeloxAbilityActivationPolicy : uint8
{
	/** Try to activate on the input press. */
	OnInputTriggered,
	/** Re-try every frame while the input is held (automatic fire: one activation per shot). */
	WhileInputActive,
	/** Activate when granted / when an avatar is set (passives). */
	OnSpawn
};

/** Defaults: InstancedPerActor, LocalPredicted, ReplicateNo, server ignores remote cancellation. */
UCLASS(Abstract)
class VELOX_API UVeloxGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UVeloxGameplayAbility(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	EVeloxAbilityActivationPolicy GetActivationPolicy() const { return ActivationPolicy; }

	UFUNCTION(BlueprintPure, Category = "Velox|Ability")
	UVeloxAbilitySystemComponent* GetVeloxAbilitySystemComponentFromActorInfo() const;

	/** OnSpawn policy: activates on the side that owns the execution (local for predicted, server for server abilities). */
	void TryActivateAbilityOnSpawn(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) const;

	//~UGameplayAbility interface
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	//~End of UGameplayAbility interface

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ability Activation")
	EVeloxAbilityActivationPolicy ActivationPolicy = EVeloxAbilityActivationPolicy::OnInputTriggered;
};
