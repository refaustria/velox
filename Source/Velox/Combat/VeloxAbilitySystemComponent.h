// Velox - ability system component. Lives on AVeloxPlayerState, Mixed replication (ADR 0001 section 2).

#pragma once

#include "AbilitySystemComponent.h"

#include "VeloxAbilitySystemComponent.generated.h"

struct FGameplayTag;

UCLASS()
class VELOX_API UVeloxAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UVeloxAbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Input routed by tag (Lyra tag-driven ability input). Abilities are matched by their spec's dynamic source tags.
	 * M0: press activates or forwards the press, release forwards the release. M2 replaces this with
	 * activation-policy processing (OnInputTriggered / WhileInputActive / OnSpawn).
	 */
	void AbilityInputTagPressed(const FGameplayTag& InputTag);
	void AbilityInputTagReleased(const FGameplayTag& InputTag);
};
