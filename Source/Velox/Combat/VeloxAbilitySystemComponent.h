// Velox - ability system component. Lives on AVeloxPlayerState, Mixed replication (ADR 0001 section 2, ADR 0003 section 1).

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
	 * Input routed by tag (Lyra three-list input processing). Press/release only mark the specs whose dynamic source tags
	 * contain InputTag; ProcessAbilityInput (called from AVeloxPlayerController::PlayerTick after UpdateRotation, ADR 0003
	 * B6; from the end of the bot controller tick in M4) activates and forwards.
	 */
	void AbilityInputTagPressed(const FGameplayTag& InputTag);
	void AbilityInputTagReleased(const FGameplayTag& InputTag);

	/**
	 * Once per frame: OnInputTriggered specs activate on the press, WhileInputActive specs re-try every frame while held,
	 * active specs get InputPressed/InputReleased (and the replicated generic events for WaitInputPress/Release tasks).
	 * While Velox.State.Dead is present all pending and held input is dropped and nothing activates.
	 */
	void ProcessAbilityInput(float DeltaTime, bool bGamePaused);

	/** Drops all pending and held input (death, menu, possession change). */
	void ClearAbilityInput();

	//~UAbilitySystemComponent interface
	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
	virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;
	//~End of UAbilitySystemComponent interface

	/**
	 * Input routing filter (ADR 0003 section 1): for InputTag.Fire only the spec whose SourceObject is the inventory's
	 * current weapon definition passes, so six granted fire abilities cost one TryActivateAbility per frame. Other
	 * specs always pass. Public for tests.
	 */
	virtual bool CanRouteInputToSpec(const FGameplayAbilitySpec& Spec) const;

protected:
	/** Specs whose input was pressed / released this frame, and specs whose input is held. */
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;
	/** The input tag of this frame's press per pressed spec (specs bound to several tags, e.g. the weapon switch). */
	TMap<FGameplayAbilitySpecHandle, FGameplayTag> InputPressedTags;
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;
};
