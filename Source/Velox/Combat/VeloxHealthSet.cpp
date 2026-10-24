// Velox - health and armor.

#include "Combat/VeloxHealthSet.h"

#include "Character/VeloxCharacter.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "GameplayEffect.h"
#include "GameplayEffectExtension.h"
#include "Movement/VeloxMovementComponent.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxHealthSet)

UVeloxHealthSet::UVeloxHealthSet()
{
	// Class defaults of the settings (the asset is not reachable from a CDO); GE_SpawnInit sets the real values.
	const UVeloxCombatSettings& Defaults = *GetDefault<UVeloxCombatSettings>();
	InitHealth(Defaults.MaxHealth);
	InitMaxHealth(Defaults.MaxHealth);
	InitArmor(0.f);
	InitMaxArmor(Defaults.MaxArmor);
	InitIncomingDamage(0.f);
	InitIncomingArmorDamage(0.f);
	InitIncomingKnockback(0.f);
	InitIncomingHealing(0.f);
}

float UVeloxHealthSet::ClampAttribute(const FGameplayAttribute& Attribute, float NewValue) const
{
	const UVeloxCombatSettings& Settings = GetCombatSettings();
	const float HardCap = FMath::Max(Settings.HealthHardCap, 1.f);
	if (Attribute == GetHealthAttribute())
	{
		return FMath::Clamp(FMath::RoundToFloat(NewValue), 0.f, HardCap);
	}
	if (Attribute == GetMaxHealthAttribute())
	{
		return FMath::Clamp(FMath::RoundToFloat(NewValue), 1.f, HardCap);
	}
	if (Attribute == GetArmorAttribute())
	{
		return FMath::Clamp(FMath::RoundToFloat(NewValue), 0.f, FMath::Max(GetMaxArmor(), 0.f));
	}
	if (Attribute == GetMaxArmorAttribute())
	{
		return FMath::Max(FMath::RoundToFloat(NewValue), 0.f);
	}
	return NewValue;
}

void UVeloxHealthSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	NewValue = ClampAttribute(Attribute, NewValue);
}

void UVeloxHealthSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	NewValue = ClampAttribute(Attribute, NewValue);
}

void UVeloxHealthSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	if (Attribute == GetHealthAttribute() && NewValue > 0.f)
	{
		bOutOfHealth = false;
	}

	const bool bMeta = Attribute == GetIncomingDamageAttribute() || Attribute == GetIncomingArmorDamageAttribute() || Attribute == GetIncomingKnockbackAttribute() || Attribute == GetIncomingHealingAttribute();
	if (!bMeta)
	{
		UE_LOG(LogVeloxCombat, Verbose, TEXT("%s: %s %.0f -> %.0f"), *GetNameSafe(GetOwningActor()), *Attribute.GetName(), OldValue, NewValue);
	}
}

void UVeloxHealthSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// Executions only run on the authority; the meta attributes are server scratch values.
	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		ApplyIncomingDamage(Data);
	}
	// IncomingArmorDamage / IncomingKnockback are committed together with IncomingDamage, which the execution writes last.
}

void UVeloxHealthSet::ApplyIncomingDamage(const FGameplayEffectModCallbackData& Data)
{
	const float HealthLoss = FMath::Max(GetIncomingDamage(), 0.f);
	const float ArmorLoss = FMath::Max(GetIncomingArmorDamage(), 0.f);
	const float KnockbackSpeed = FMath::Max(GetIncomingKnockback(), 0.f);
	SetIncomingDamage(0.f);
	SetIncomingArmorDamage(0.f);
	SetIncomingKnockback(0.f);

	const FGameplayEffectContextHandle& ContextHandle = Data.EffectSpec.GetEffectContext();
	const FVeloxGameplayEffectContext* Context = FVeloxGameplayEffectContext::Get(ContextHandle);

	FVeloxDamageEvent Event;
	Event.Instigator = ContextHandle.GetOriginalInstigator();
	Event.Causer = ContextHandle.GetEffectCauser();
	Data.EffectSpec.GetAllAssetTags(Event.DamageTags);
	Event.OldHealth = GetHealth();
	Event.OldArmor = GetArmor();
	Event.HealthLoss = HealthLoss;
	Event.ArmorLoss = ArmorLoss;

	// Armor first (Q3 CheckArmor), then health.
	if (ArmorLoss > 0.f)
	{
		SetArmor(ClampAttribute(GetArmorAttribute(), GetArmor() - ArmorLoss));
	}
	if (HealthLoss > 0.f)
	{
		SetHealth(ClampAttribute(GetHealthAttribute(), GetHealth() - HealthLoss));
	}
	Event.NewHealth = GetHealth();
	Event.NewArmor = GetArmor();

	// The single knockback entry point (ADR 0002 section 4, ADR 0003 section 2): server source, chained channel (A17).
	bool bKnockbackAccepted = false;
	if (KnockbackSpeed > 0.f && Context && !Context->HitDirection.IsNearlyZero())
	{
		AActor* Avatar = Data.Target.AbilityActorInfo.IsValid() ? Data.Target.AbilityActorInfo->AvatarActor.Get() : nullptr;
		const AVeloxCharacter* Character = Cast<AVeloxCharacter>(Avatar);
		if (UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr)
		{
			Event.Knockback = FVector(Context->HitDirection).GetSafeNormal() * KnockbackSpeed;
			bKnockbackAccepted = Movement->AddKnockback(Event.Knockback, false, EVeloxKnockbackSource::Server);
		}
	}

	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDamage target=%s instigator=%s tags=[%s] health %.0f->%.0f armor %.0f->%.0f knockback=%s (%.0f cm/s, AddKnockback(Server) %s)"),
		*GetNameSafe(GetOwningActor()), *GetNameSafe(Event.Instigator.Get()), *Event.DamageTags.ToStringSimple(), Event.OldHealth, Event.NewHealth,
		Event.OldArmor, Event.NewArmor, *Event.Knockback.ToCompactString(), KnockbackSpeed,
		KnockbackSpeed <= 0.f ? TEXT("none") : (bKnockbackAccepted ? TEXT("accepted") : TEXT("not applied")));

	OnDamaged.Broadcast(Event);

	if (GetHealth() <= 0.f && !bOutOfHealth)
	{
		bOutOfHealth = true;
		OnOutOfHealth.Broadcast(Event);
	}
}

void UVeloxHealthSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Everyone sees health/armor (scoreboard, spectating); push model, GAS marks the properties dirty.
	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	Params.RepNotifyCondition = REPNOTIFY_Always;
	DOREPLIFETIME_WITH_PARAMS_FAST(UVeloxHealthSet, Health, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UVeloxHealthSet, MaxHealth, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UVeloxHealthSet, Armor, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UVeloxHealthSet, MaxArmor, Params);
}

void UVeloxHealthSet::OnRep_Health(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UVeloxHealthSet, Health, OldValue); }
void UVeloxHealthSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UVeloxHealthSet, MaxHealth, OldValue); }
void UVeloxHealthSet::OnRep_Armor(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UVeloxHealthSet, Armor, OldValue); }
void UVeloxHealthSet::OnRep_MaxArmor(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UVeloxHealthSet, MaxArmor, OldValue); }
