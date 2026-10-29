// Velox - ability set data asset (ADR 0001 section 2, ADR 0003 section 1; Lyra ability-set idea). Granted once per
// PlayerState (AVeloxPlayerState::GrantDefaultAbilitySets); weapon fire abilities are granted by the inventory (task 04).

#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "Engine/DataAsset.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"

#include "VeloxAbilitySet.generated.h"

class UGameplayEffect;
class UVeloxAbilitySystemComponent;
class UVeloxAttributeSet;
class UVeloxGameplayAbility;
class UAttributeSet;

USTRUCT(BlueprintType)
struct FVeloxAbilitySet_GameplayAbility
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Abilities")
	TSubclassOf<UVeloxGameplayAbility> Ability;

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Abilities")
	int32 Level = 1;

	/** Added to the spec's dynamic source tags; ability input is routed by it. */
	UPROPERTY(EditDefaultsOnly, Category = "Velox|Abilities", Meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
};

USTRUCT(BlueprintType)
struct FVeloxAbilitySet_GameplayEffect
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Abilities")
	TSubclassOf<UGameplayEffect> GameplayEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Abilities")
	float Level = 1.f;
};

/** Extra attribute sets (M3 powerups); the health and ammo sets are PlayerState subobjects and never granted. */
USTRUCT(BlueprintType)
struct FVeloxAbilitySet_AttributeSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Abilities")
	TSubclassOf<UVeloxAttributeSet> AttributeSet;
};

/** What one GiveToAbilitySystem call granted, so it can be taken back. */
USTRUCT(BlueprintType)
struct FVeloxAbilitySet_GrantedHandles
{
	GENERATED_BODY()

	void AddAbilitySpecHandle(const FGameplayAbilitySpecHandle& Handle);
	void AddGameplayEffectHandle(const FActiveGameplayEffectHandle& Handle);
	void AddAttributeSet(UAttributeSet* Set);

	/** Server: removes everything granted (abilities, effects, attribute sets) and resets the handles. */
	void TakeFromAbilitySystem(UVeloxAbilitySystemComponent* ASC);

	int32 NumAbilities() const { return AbilitySpecHandles.Num(); }
	int32 NumEffects() const { return GameplayEffectHandles.Num(); }
	int32 NumAttributeSets() const { return GrantedAttributeSets.Num(); }

protected:
	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;

	UPROPERTY()
	TArray<FActiveGameplayEffectHandle> GameplayEffectHandles;

	UPROPERTY()
	TArray<TObjectPtr<UAttributeSet>> GrantedAttributeSets;
};

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Ability Set"))
class VELOX_API UVeloxAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Server only. Grants everything in the set; OutGrantedHandles (optional) records it for TakeFromAbilitySystem. */
	void GiveToAbilitySystem(UVeloxAbilitySystemComponent* ASC, FVeloxAbilitySet_GrantedHandles* OutGrantedHandles, UObject* SourceObject = nullptr) const;

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Abilities", Meta = (TitleProperty = Ability))
	TArray<FVeloxAbilitySet_GameplayAbility> GrantedGameplayAbilities;

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Abilities", Meta = (TitleProperty = GameplayEffect))
	TArray<FVeloxAbilitySet_GameplayEffect> GrantedGameplayEffects;

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Abilities", Meta = (TitleProperty = AttributeSet))
	TArray<FVeloxAbilitySet_AttributeSet> GrantedAttributes;
};
