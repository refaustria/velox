// Velox - damage and decay executions (ADR 0003 section 2). Executions only run on the authority.

#pragma once

#include "GameplayEffectExecutionCalculation.h"

#include "VeloxDamageExecution.generated.h"

/**
 * GE_Damage: captures the target's Armor, reads SetByCaller.Damage / SetByCaller.PelletHits and the Velox context, runs
 * FVeloxDamageMath per pellet and writes IncomingArmorDamage, IncomingKnockback and (last, the commit attribute)
 * IncomingDamage. Self = context flag Self or Velox.Damage.Self on the spec (set explicitly by the
 * caller: environment damage is built on the victim's own ASC and must not count as self damage).
 * Protected = target has Velox.State.SpawnProtected. Dead targets take nothing.
 */
UCLASS()
class VELOX_API UVeloxDamageExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UVeloxDamageExecution();

protected:
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};

/** GE_HealthDecay (period 1 s): Health -1 while above MaxHealth, Armor -1 while above MaxHealth (Q3 ClientTimerActions). */
UCLASS()
class VELOX_API UVeloxDecayExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UVeloxDecayExecution();

protected:
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
