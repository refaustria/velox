// Velox - damage and decay executions.

#include "Combat/VeloxDamageExecution.h"

#include "AbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxDamageMath.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "GameModes/VeloxPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxDamageExecution)

namespace VeloxDamageExecutionPrivate
{
	struct FDamageStatics
	{
		FGameplayEffectAttributeCaptureDefinition ArmorDef;

		FDamageStatics()
		{
			ArmorDef = FGameplayEffectAttributeCaptureDefinition(UVeloxHealthSet::GetArmorAttribute(), EGameplayEffectAttributeCaptureSource::Target, false);
		}
	};

	struct FDecayStatics
	{
		FGameplayEffectAttributeCaptureDefinition HealthDef;
		FGameplayEffectAttributeCaptureDefinition MaxHealthDef;
		FGameplayEffectAttributeCaptureDefinition ArmorDef;

		FDecayStatics()
		{
			HealthDef = FGameplayEffectAttributeCaptureDefinition(UVeloxHealthSet::GetHealthAttribute(), EGameplayEffectAttributeCaptureSource::Target, false);
			MaxHealthDef = FGameplayEffectAttributeCaptureDefinition(UVeloxHealthSet::GetMaxHealthAttribute(), EGameplayEffectAttributeCaptureSource::Target, false);
			ArmorDef = FGameplayEffectAttributeCaptureDefinition(UVeloxHealthSet::GetArmorAttribute(), EGameplayEffectAttributeCaptureSource::Target, false);
		}
	};

	static const FDamageStatics& DamageStatics()
	{
		static FDamageStatics Statics;
		return Statics;
	}

	static const FDecayStatics& DecayStatics()
	{
		static FDecayStatics Statics;
		return Statics;
	}

	static const UVeloxCombatSettings& SettingsFor(const UAbilitySystemComponent* TargetASC)
	{
		const AVeloxPlayerState* PlayerState = TargetASC ? Cast<AVeloxPlayerState>(TargetASC->GetOwnerActor()) : nullptr;
		return UVeloxCombatSettings::GetOrDefault(PlayerState ? PlayerState->GetCombatSettings() : nullptr);
	}
}

UVeloxDamageExecution::UVeloxDamageExecution()
{
	RelevantAttributesToCapture.Add(VeloxDamageExecutionPrivate::DamageStatics().ArmorDef);
}

void UVeloxDamageExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	using namespace VeloxDamageExecutionPrivate;

	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
	const FVeloxGameplayEffectContext* Context = FVeloxGameplayEffectContext::Get(Spec.GetEffectContext());

	FAggregatorEvaluateParameters EvalParams;
	EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float Armor = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().ArmorDef, EvalParams, Armor);

	FGameplayTagContainer SpecTags;
	Spec.GetAllAssetTags(SpecTags);

	FVeloxDamageInput Input;
	Input.Damage = FMath::RoundToInt32(Spec.GetSetByCallerMagnitude(VeloxGameplayTags::SetByCaller_Damage, true, 0.f));
	Input.Armor = FMath::Max(FMath::RoundToInt32(Armor), 0);
	Input.bSelf = (Context && Context->HasFlag(EVeloxDamageFlags::Self)) || SpecTags.HasTagExact(VeloxGameplayTags::Damage_Self);
	Input.bNoArmor = Context && Context->HasFlag(EVeloxDamageFlags::NoArmor);
	// Q3: no direction, no knockback.
	Input.bNoKnockback = !Context || Context->HasFlag(EVeloxDamageFlags::NoKnockback) || Context->HitDirection.IsNearlyZero();
	Input.bProtected = TargetASC && TargetASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected);
	Input.KnockbackScale = Context ? Context->KnockbackScale : 1.f;
	const int32 PelletHits = FMath::Max(FMath::RoundToInt32(Spec.GetSetByCallerMagnitude(VeloxGameplayTags::SetByCaller_PelletHits, false, 1.f)), 1);

	const bool bDead = TargetASC && TargetASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead);
	const FVeloxCombatParams Params = SettingsFor(TargetASC).BuildCombatParams();
	const FVeloxDamageResult Result = bDead ? FVeloxDamageResult() : FVeloxDamageMath::ApplyPellets(Input, PelletHits, Params);

	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDamageCalc target=%s dmg=%d pellets=%d armor=%d self=%d protected=%d noarmor=%d dead=%d -> health_loss=%d armor_loss=%d knockback=%.1f cm/s"),
		*GetNameSafe(TargetASC ? TargetASC->GetOwnerActor() : nullptr), Input.Damage, PelletHits, Input.Armor, Input.bSelf ? 1 : 0, Input.bProtected ? 1 : 0,
		Input.bNoArmor ? 1 : 0, bDead ? 1 : 0, Result.HealthLoss, Result.ArmorLoss, Result.KnockbackSpeed);

	// IncomingDamage last: UVeloxHealthSet commits all three metas when it sees IncomingDamage.
	if (Result.ArmorLoss > 0)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UVeloxHealthSet::GetIncomingArmorDamageAttribute(), EGameplayModOp::Additive, static_cast<float>(Result.ArmorLoss)));
	}
	if (Result.KnockbackSpeed > 0.f)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UVeloxHealthSet::GetIncomingKnockbackAttribute(), EGameplayModOp::Additive, Result.KnockbackSpeed));
	}
	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UVeloxHealthSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, static_cast<float>(Result.HealthLoss)));
}

UVeloxDecayExecution::UVeloxDecayExecution()
{
	using namespace VeloxDamageExecutionPrivate;
	RelevantAttributesToCapture.Add(DecayStatics().HealthDef);
	RelevantAttributesToCapture.Add(DecayStatics().MaxHealthDef);
	RelevantAttributesToCapture.Add(DecayStatics().ArmorDef);
}

void UVeloxDecayExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	using namespace VeloxDamageExecutionPrivate;

	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	FAggregatorEvaluateParameters EvalParams;
	EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float Health = 0.f;
	float MaxHealth = 0.f;
	float Armor = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DecayStatics().HealthDef, EvalParams, Health);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DecayStatics().MaxHealthDef, EvalParams, MaxHealth);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DecayStatics().ArmorDef, EvalParams, Armor);

	const bool bDecayHealth = Health > MaxHealth;
	const bool bDecayArmor = Armor > MaxHealth;
	if (bDecayHealth)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UVeloxHealthSet::GetHealthAttribute(), EGameplayModOp::Additive, -1.f));
	}
	if (bDecayArmor)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UVeloxHealthSet::GetArmorAttribute(), EGameplayModOp::Additive, -1.f));
	}
	if (bDecayHealth || bDecayArmor)
	{
		const UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
		UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxDecay target=%s health %.0f->%.0f armor %.0f->%.0f (max health %.0f)"),
			*GetNameSafe(TargetASC ? TargetASC->GetOwnerActor() : nullptr), Health, bDecayHealth ? Health - 1.f : Health, Armor, bDecayArmor ? Armor - 1.f : Armor, MaxHealth);
	}
}
