// Velox - ability set data asset.

#include "Combat/VeloxAbilitySet.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAttributeSet.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxGameplayAbility.h"
#include "GameplayEffect.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAbilitySet)

void FVeloxAbilitySet_GrantedHandles::AddAbilitySpecHandle(const FGameplayAbilitySpecHandle& Handle)
{
	if (Handle.IsValid())
	{
		AbilitySpecHandles.Add(Handle);
	}
}

void FVeloxAbilitySet_GrantedHandles::AddGameplayEffectHandle(const FActiveGameplayEffectHandle& Handle)
{
	if (Handle.IsValid())
	{
		GameplayEffectHandles.Add(Handle);
	}
}

void FVeloxAbilitySet_GrantedHandles::AddAttributeSet(UAttributeSet* Set)
{
	GrantedAttributeSets.Add(Set);
}

void FVeloxAbilitySet_GrantedHandles::TakeFromAbilitySystem(UVeloxAbilitySystemComponent* ASC)
{
	if (!ASC || !ASC->IsOwnerActorAuthoritative())
	{
		return;
	}
	for (const FGameplayAbilitySpecHandle& Handle : AbilitySpecHandles)
	{
		if (Handle.IsValid())
		{
			ASC->ClearAbility(Handle);
		}
	}
	for (const FActiveGameplayEffectHandle& Handle : GameplayEffectHandles)
	{
		if (Handle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	for (UAttributeSet* Set : GrantedAttributeSets)
	{
		if (Set)
		{
			ASC->RemoveSpawnedAttribute(Set);
		}
	}
	AbilitySpecHandles.Reset();
	GameplayEffectHandles.Reset();
	GrantedAttributeSets.Reset();
}

void UVeloxAbilitySet::GiveToAbilitySystem(UVeloxAbilitySystemComponent* ASC, FVeloxAbilitySet_GrantedHandles* OutGrantedHandles, UObject* SourceObject) const
{
	if (!ASC || !ASC->IsOwnerActorAuthoritative())
	{
		return;
	}

	// Attribute sets first so effects and abilities granted below can use them.
	for (int32 Index = 0; Index < GrantedAttributes.Num(); ++Index)
	{
		const FVeloxAbilitySet_AttributeSet& Entry = GrantedAttributes[Index];
		if (!Entry.AttributeSet)
		{
			UE_LOG(LogVeloxCombat, Error, TEXT("%s: GrantedAttributes[%d] is empty."), *GetNameSafe(this), Index);
			continue;
		}
		UAttributeSet* NewSet = NewObject<UAttributeSet>(ASC->GetOwner(), Entry.AttributeSet);
		ASC->AddAttributeSetSubobject(NewSet);
		if (OutGrantedHandles)
		{
			OutGrantedHandles->AddAttributeSet(NewSet);
		}
	}

	for (int32 Index = 0; Index < GrantedGameplayAbilities.Num(); ++Index)
	{
		const FVeloxAbilitySet_GameplayAbility& Entry = GrantedGameplayAbilities[Index];
		if (!Entry.Ability)
		{
			UE_LOG(LogVeloxCombat, Error, TEXT("%s: GrantedGameplayAbilities[%d] is empty."), *GetNameSafe(this), Index);
			continue;
		}
		FGameplayAbilitySpec Spec(Entry.Ability, Entry.Level, INDEX_NONE, SourceObject);
		if (Entry.InputTag.IsValid())
		{
			Spec.GetDynamicSpecSourceTags().AddTag(Entry.InputTag);
		}
		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
		if (OutGrantedHandles)
		{
			OutGrantedHandles->AddAbilitySpecHandle(Handle);
		}
	}

	for (int32 Index = 0; Index < GrantedGameplayEffects.Num(); ++Index)
	{
		const FVeloxAbilitySet_GameplayEffect& Entry = GrantedGameplayEffects[Index];
		if (!Entry.GameplayEffect)
		{
			UE_LOG(LogVeloxCombat, Error, TEXT("%s: GrantedGameplayEffects[%d] is empty."), *GetNameSafe(this), Index);
			continue;
		}
		const UGameplayEffect* Effect = Entry.GameplayEffect->GetDefaultObject<UGameplayEffect>();
		const FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectToSelf(Effect, Entry.Level, ASC->MakeEffectContext());
		if (OutGrantedHandles)
		{
			OutGrantedHandles->AddGameplayEffectHandle(Handle);
		}
	}
}
