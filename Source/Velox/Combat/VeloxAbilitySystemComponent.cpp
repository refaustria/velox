// Velox - ability system component.

#include "Combat/VeloxAbilitySystemComponent.h"

#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxGameplayAbility.h"
#include "Core/VeloxGameplayTags.h"
#include "GameplayTagContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAbilitySystemComponent)

UVeloxAbilitySystemComponent::UVeloxAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UVeloxAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	const AActor* OldAvatar = AbilityActorInfo.IsValid() ? AbilityActorInfo->AvatarActor.Get() : nullptr;
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);

	if (OldAvatar != InAvatarActor)
	{
		// Held input belongs to the old pawn; OnSpawn abilities activate through UVeloxGameplayAbility::OnAvatarSet.
		ClearAbilityInput();
		UE_LOG(LogVeloxCombat, Log, TEXT("%s: ability actor info owner=%s avatar %s -> %s (%d abilities)"), *GetNameSafe(GetOwner()), *GetNameSafe(InOwnerActor),
			*GetNameSafe(OldAvatar), *GetNameSafe(InAvatarActor), GetActivatableAbilities().Num());
	}
}

void UVeloxAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}
	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			InputPressedSpecHandles.AddUnique(Spec.Handle);
			InputHeldSpecHandles.AddUnique(Spec.Handle);
		}
	}
}

void UVeloxAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}
	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			InputReleasedSpecHandles.AddUnique(Spec.Handle);
			InputHeldSpecHandles.Remove(Spec.Handle);
		}
	}
}

void UVeloxAbilitySystemComponent::ProcessAbilityInput(float DeltaTime, bool bGamePaused)
{
	// Input-blocked gate (Lyra Gameplay.AbilityInputBlocked): dead players neither fire nor queue a press for the respawn.
	if (HasMatchingGameplayTag(VeloxGameplayTags::State_Dead))
	{
		ClearAbilityInput();
		return;
	}

	static TArray<FGameplayAbilitySpecHandle> AbilitiesToActivate;
	AbilitiesToActivate.Reset();

	// Held: WhileInputActive specs that are not running (automatic fire re-tries every frame).
	for (const FGameplayAbilitySpecHandle& Handle : InputHeldSpecHandles)
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		if (!Spec || !Spec->Ability || Spec->IsActive() || !CanRouteInputToSpec(*Spec))
		{
			continue;
		}
		const UVeloxGameplayAbility* AbilityCDO = Cast<UVeloxGameplayAbility>(Spec->Ability);
		if (AbilityCDO && AbilityCDO->GetActivationPolicy() == EVeloxAbilityActivationPolicy::WhileInputActive)
		{
			AbilitiesToActivate.AddUnique(Handle);
		}
	}

	// Pressed this frame: forward to running specs, activate OnInputTriggered ones.
	for (const FGameplayAbilitySpecHandle& Handle : InputPressedSpecHandles)
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		if (!Spec || !Spec->Ability || !CanRouteInputToSpec(*Spec))
		{
			continue;
		}
		Spec->InputPressed = true;
		if (Spec->IsActive())
		{
			AbilitySpecInputPressed(*Spec);
			continue;
		}
		const UVeloxGameplayAbility* AbilityCDO = Cast<UVeloxGameplayAbility>(Spec->Ability);
		if (AbilityCDO && AbilityCDO->GetActivationPolicy() == EVeloxAbilityActivationPolicy::OnInputTriggered)
		{
			AbilitiesToActivate.AddUnique(Handle);
		}
	}

	// Activate after the press pass so a press and a hold of the same frame activate once.
	for (const FGameplayAbilitySpecHandle& Handle : AbilitiesToActivate)
	{
		TryActivateAbility(Handle);
	}

	// Released this frame (filter not applied: a release must always reach a running ability).
	for (const FGameplayAbilitySpecHandle& Handle : InputReleasedSpecHandles)
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		if (!Spec || !Spec->Ability)
		{
			continue;
		}
		Spec->InputPressed = false;
		if (Spec->IsActive())
		{
			AbilitySpecInputReleased(*Spec);
		}
	}

	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
}

void UVeloxAbilitySystemComponent::ClearAbilityInput()
{
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
}

void UVeloxAbilitySystemComponent::AbilitySpecInputPressed(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputPressed(Spec);

	// Replicated generic event (not sent here) so WaitInputPress tasks work; bReplicateInputDirectly is not used.
	if (Spec.IsActive())
	{
		if (const UGameplayAbility* Instance = Spec.GetPrimaryInstance())
		{
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Spec.Handle, Instance->GetCurrentActivationInfo().GetActivationPredictionKey());
		}
	}
}

void UVeloxAbilitySystemComponent::AbilitySpecInputReleased(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputReleased(Spec);

	if (Spec.IsActive())
	{
		if (const UGameplayAbility* Instance = Spec.GetPrimaryInstance())
		{
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Spec.Handle, Instance->GetCurrentActivationInfo().GetActivationPredictionKey());
		}
	}
}
