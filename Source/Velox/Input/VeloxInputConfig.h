// Velox - input config: maps input actions to gameplay tags (Lyra tag-driven input idea).

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "VeloxInputConfig.generated.h"

class UInputAction;

USTRUCT(BlueprintType)
struct VELOX_API FVeloxInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Input")
	TObjectPtr<const UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Input", Meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
};

UCLASS(BlueprintType, Const)
class VELOX_API UVeloxInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Velox|Input")
	const UInputAction* FindNativeInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;

	UFUNCTION(BlueprintCallable, Category = "Velox|Input")
	const UInputAction* FindAbilityInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;

	/** Actions bound directly on the character (move, look, jump, crouch). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Input", Meta = (TitleProperty = "InputAction"))
	TArray<FVeloxInputAction> NativeInputActions;

	/** Actions routed to the ability system component by tag (fire, weapon slots, zoom, ...). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Input", Meta = (TitleProperty = "InputAction"))
	TArray<FVeloxInputAction> AbilityInputActions;
};
