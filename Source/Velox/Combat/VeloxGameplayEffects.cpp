// Velox - native gameplay effects.

#include "Combat/VeloxGameplayEffects.h"

#include "AbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxDamageExecution.h"
#include "Combat/VeloxDamageMath.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "GameModes/VeloxMatchEffects.h"
#include "GameplayEffectComponents/AdditionalEffectsGameplayEffectComponent.h"
#include "GameplayEffectComponents/RemoveOtherGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameplayEffects)

namespace VeloxGameplayEffectsPrivate
{
	FSetByCallerFloat SetByCaller(const FGameplayTag& Tag)
	{
		FSetByCallerFloat Value;
		Value.DataTag = Tag;
		return Value;
	}

	void AddSetByCallerModifier(UGameplayEffect& Effect, const FGameplayAttribute& Attribute, EGameplayModOp::Type Op, const FGameplayTag& Tag)
	{
		FGameplayModifierInfo& Modifier = Effect.Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = Op;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller(Tag));
	}

	/** Granted-tag changes for a UTargetTagsGameplayEffectComponent. */
	FInheritedTagContainer GrantedTag(const FGameplayTag& Tag)
	{
		FInheritedTagContainer Tags;
		Tags.AddTag(Tag);
		return Tags;
	}
}

UVeloxGE_Damage::UVeloxGE_Damage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition& Execution = Executions.AddDefaulted_GetRef();
	Execution.CalculationClass = UVeloxDamageExecution::StaticClass();
}

UVeloxGE_AmmoCost::UVeloxGE_AmmoCost()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	for (const TPair<FGameplayTag, FGameplayAttribute>& Type : UVeloxAmmoSet::GetAmmoTypes())
	{
		VeloxGameplayEffectsPrivate::AddSetByCallerModifier(*this, Type.Value, EGameplayModOp::Additive, Type.Key);
	}
}

UVeloxGE_Heal::UVeloxGE_Heal()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	VeloxGameplayEffectsPrivate::AddSetByCallerModifier(*this, UVeloxHealthSet::GetHealthAttribute(), EGameplayModOp::Additive, VeloxGameplayTags::SetByCaller_Health);
}

UVeloxGE_ArmorGain::UVeloxGE_ArmorGain()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	VeloxGameplayEffectsPrivate::AddSetByCallerModifier(*this, UVeloxHealthSet::GetArmorAttribute(), EGameplayModOp::Additive, VeloxGameplayTags::SetByCaller_Armor);
}

UVeloxGE_SpawnInit::UVeloxGE_SpawnInit()
{
	using namespace VeloxGameplayEffectsPrivate;
	using namespace VeloxGameplayTags;

	DurationPolicy = EGameplayEffectDurationType::Instant;

	// Maxima first so the clamps of Health/Armor see the new caps.
	AddSetByCallerModifier(*this, UVeloxHealthSet::GetMaxHealthAttribute(), EGameplayModOp::Override, SetByCaller_MaxHealth);
	AddSetByCallerModifier(*this, UVeloxHealthSet::GetMaxArmorAttribute(), EGameplayModOp::Override, SetByCaller_MaxArmor);
	AddSetByCallerModifier(*this, UVeloxHealthSet::GetHealthAttribute(), EGameplayModOp::Override, SetByCaller_Health);
	AddSetByCallerModifier(*this, UVeloxHealthSet::GetArmorAttribute(), EGameplayModOp::Override, SetByCaller_Armor);
	for (const TPair<FGameplayTag, FGameplayAttribute>& Type : UVeloxAmmoSet::GetAmmoTypes())
	{
		AddSetByCallerModifier(*this, Type.Value, EGameplayModOp::Override, Type.Key);
	}

	// Components run in array order on application, after the modifiers/executions (ADR 0003 B7: ExecuteGameplayEffect
	// precedes Spec.Def->OnApplied): override the attributes, then clear Dead/SpawnProtected/old decay, then start the decay.
	URemoveOtherGameplayEffectComponent* Remove = CreateDefaultSubobject<URemoveOtherGameplayEffectComponent>(TEXT("RemoveDeadAndDecay"));
	GEComponents.Add(Remove);
	{
		FGameplayTagContainer StateTags;
		StateTags.AddTag(State_Dead);
		StateTags.AddTag(State_SpawnProtected);
		// ADR 0004 section 3: powerups are lost on death (the parent tag matches every Velox.Powerup.<Name>).
		StateTags.AddTag(Powerup);
		Remove->RemoveGameplayEffectQueries.Add(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(StateTags));
		FGameplayEffectQuery DecayQuery;
		DecayQuery.EffectDefinition = UVeloxGE_HealthDecay::StaticClass();
		Remove->RemoveGameplayEffectQueries.Add(DecayQuery);
	}

	UAdditionalEffectsGameplayEffectComponent* Additional = CreateDefaultSubobject<UAdditionalEffectsGameplayEffectComponent>(TEXT("StartHealthDecay"));
	GEComponents.Add(Additional);
	FConditionalGameplayEffect& Decay = Additional->OnApplicationGameplayEffects.AddDefaulted_GetRef();
	Decay.EffectClass = UVeloxGE_HealthDecay::StaticClass();
}

UVeloxGE_HealthDecay::UVeloxGE_HealthDecay()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	// Q3 ClientTimerActions: once per second. A rule, not a tunable.
	Period = FScalableFloat(1.0f);
	bExecutePeriodicEffectOnApplication = false;
	FGameplayEffectExecutionDefinition& Execution = Executions.AddDefaulted_GetRef();
	Execution.CalculationClass = UVeloxDecayExecution::StaticClass();
}

UVeloxGE_WeaponSwitching::UVeloxGE_WeaponSwitching()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(VeloxGameplayEffectsPrivate::SetByCaller(VeloxGameplayTags::SetByCaller_Duration));
	// Native GE components must be default subobjects listed in GEComponents (UGameplayEffect::PostInitProperties).
	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GrantWeaponSwitching"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(VeloxGameplayEffectsPrivate::GrantedTag(VeloxGameplayTags::State_WeaponSwitching));
}

UVeloxGE_SpawnProtection::UVeloxGE_SpawnProtection()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(VeloxGameplayEffectsPrivate::SetByCaller(VeloxGameplayTags::SetByCaller_Duration));
	// Native GE components must be default subobjects listed in GEComponents (UGameplayEffect::PostInitProperties).
	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GrantSpawnProtected"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(VeloxGameplayEffectsPrivate::GrantedTag(VeloxGameplayTags::State_SpawnProtected));
}

UVeloxGE_Dead::UVeloxGE_Dead()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	// Native GE components must be default subobjects listed in GEComponents (UGameplayEffect::PostInitProperties).
	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GrantDead"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(VeloxGameplayEffectsPrivate::GrantedTag(VeloxGameplayTags::State_Dead));
}

namespace VeloxEffects
{
	FGameplayEffectSpecHandle MakeDamageSpec(const UAbilitySystemComponent* SourceASC, const FVeloxDamageSpecParams& Params)
	{
		if (!SourceASC)
		{
			return FGameplayEffectSpecHandle();
		}

		FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
		if (Params.Causer)
		{
			ContextHandle.AddInstigator(SourceASC->GetOwnerActor(), Params.Causer);
		}
		if (FVeloxGameplayEffectContext* Context = FVeloxGameplayEffectContext::Get(ContextHandle))
		{
			Context->HitDirection = Params.Direction.GetSafeNormal();
			Context->HitLocation = Params.HitLocation;
			Context->KnockbackScale = Params.KnockbackScale;
			Context->ShotSeq = Params.ShotSeq;
			if (Params.bSelf) { Context->AddFlag(EVeloxDamageFlags::Self); }
			if (Params.bRadius) { Context->AddFlag(EVeloxDamageFlags::Radius); }
			if (Params.bNoArmor) { Context->AddFlag(EVeloxDamageFlags::NoArmor); }
			if (Params.bNoKnockback) { Context->AddFlag(EVeloxDamageFlags::NoKnockback); }
		}

		FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UVeloxGE_Damage::StaticClass(), 1.f, ContextHandle);
		if (FGameplayEffectSpec* Data = Spec.Data.Get())
		{
			Data->SetSetByCallerMagnitude(VeloxGameplayTags::SetByCaller_Damage, static_cast<float>(FVeloxDamageMath::ScaleDamage(Params.Damage, Params.DamageScale)));
			Data->SetSetByCallerMagnitude(VeloxGameplayTags::SetByCaller_PelletHits, static_cast<float>(FMath::Max(Params.PelletHits, 1)));
			if (Params.DamageTag.IsValid())
			{
				Data->AddDynamicAssetTag(Params.DamageTag);
			}
			if (Params.bSelf)
			{
				Data->AddDynamicAssetTag(VeloxGameplayTags::Damage_Self);
			}
		}
		return Spec;
	}

	FGameplayEffectSpecHandle MakeSpawnInitSpec(const UAbilitySystemComponent* ASC, const UVeloxCombatSettings& Settings)
	{
		if (!ASC)
		{
			return FGameplayEffectSpecHandle();
		}
		using namespace VeloxGameplayTags;
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UVeloxGE_SpawnInit::StaticClass(), 1.f, ASC->MakeEffectContext());
		if (FGameplayEffectSpec* Data = Spec.Data.Get())
		{
			Data->SetSetByCallerMagnitude(SetByCaller_MaxHealth, Settings.MaxHealth);
			Data->SetSetByCallerMagnitude(SetByCaller_MaxArmor, Settings.MaxArmor);
			Data->SetSetByCallerMagnitude(SetByCaller_Health, Settings.SpawnHealth);
			Data->SetSetByCallerMagnitude(SetByCaller_Armor, Settings.SpawnArmor);
			for (const TPair<FGameplayTag, FGameplayAttribute>& Type : UVeloxAmmoSet::GetAmmoTypes())
			{
				// ADR 0003 B5: one source of truth, the spawn loadout (clamped to the cap by UVeloxAmmoSet).
				Data->SetSetByCallerMagnitude(Type.Key, static_cast<float>(Settings.GetSpawnAmmo(Type.Key)));
			}
		}
		return Spec;
	}

	FGameplayEffectSpecHandle MakeAmmoCostSpec(const UAbilitySystemComponent* ASC, const FGameplayTag& AmmoTag, int32 Cost)
	{
		if (!ASC)
		{
			return FGameplayEffectSpecHandle();
		}
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UVeloxGE_AmmoCost::StaticClass(), 1.f, ASC->MakeEffectContext());
		if (FGameplayEffectSpec* Data = Spec.Data.Get())
		{
			for (const TPair<FGameplayTag, FGameplayAttribute>& Type : UVeloxAmmoSet::GetAmmoTypes())
			{
				Data->SetSetByCallerMagnitude(Type.Key, Type.Key == AmmoTag ? -static_cast<float>(Cost) : 0.f);
			}
		}
		return Spec;
	}

	FGameplayEffectSpecHandle MakeAmmoGainSpec(const UAbilitySystemComponent* ASC, const FGameplayTag& AmmoTag, int32 Amount)
	{
		return MakeAmmoCostSpec(ASC, AmmoTag, -Amount);
	}

	/** A spec of an Instant GE with one SetByCaller magnitude (heal / armor gain). */
	FGameplayEffectSpecHandle MakeSingleMagnitudeSpec(const UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass, const FGameplayTag& Tag, int32 Amount)
	{
		if (!ASC)
		{
			return FGameplayEffectSpecHandle();
		}
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.f, ASC->MakeEffectContext());
		if (FGameplayEffectSpec* Data = Spec.Data.Get())
		{
			Data->SetSetByCallerMagnitude(Tag, static_cast<float>(Amount));
		}
		return Spec;
	}

	FGameplayEffectSpecHandle MakeHealSpec(const UAbilitySystemComponent* ASC, int32 Amount)
	{
		return MakeSingleMagnitudeSpec(ASC, UVeloxGE_Heal::StaticClass(), VeloxGameplayTags::SetByCaller_Health, Amount);
	}

	FGameplayEffectSpecHandle MakeArmorGainSpec(const UAbilitySystemComponent* ASC, int32 Amount)
	{
		return MakeSingleMagnitudeSpec(ASC, UVeloxGE_ArmorGain::StaticClass(), VeloxGameplayTags::SetByCaller_Armor, Amount);
	}

	FGameplayEffectSpecHandle MakeDurationSpec(const UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass, float Duration)
	{
		if (!ASC || !EffectClass)
		{
			return FGameplayEffectSpecHandle();
		}
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.f, ASC->MakeEffectContext());
		if (FGameplayEffectSpec* Data = Spec.Data.Get())
		{
			Data->SetSetByCallerMagnitude(VeloxGameplayTags::SetByCaller_Duration, Duration);
		}
		return Spec;
	}

	FGameplayTag GetMatchFrozenTag()
	{
		return VeloxMatchTags::State_MatchFrozen;
	}
}
