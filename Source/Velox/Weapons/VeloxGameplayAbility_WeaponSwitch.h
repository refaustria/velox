// Velox - weapon switch (ADR 0003 section 4): one spec per PlayerState bound to InputTag.Weapon.Slot1..9/Next/Prev,
// OnInputTriggered, LocalPredicted. The press is resolved to a table index on the shooter's machine
// (PrepareInputActivation) and travels to the server as event data (EventMagnitude = index); the server validates
// ownership. Sequence: if bSwitchWaitsForRefire wait for the current weapon's NextFireTime, then GE_WeaponSwitching for
// Drop + Raise (applied at activation with Wait + Drop + Raise, inside the prediction window), after Wait + Drop the
// current index changes (server: authoritative; client: predicted until OnRep). Fire is blocked by the tag until the
// raise ends.

#pragma once

#include "Combat/VeloxGameplayAbility.h"

#include "VeloxGameplayAbility_WeaponSwitch.generated.h"

UCLASS()
class VELOX_API UVeloxGameplayAbility_WeaponSwitch : public UVeloxGameplayAbility
{
	GENERATED_BODY()

public:
	UVeloxGameplayAbility_WeaponSwitch(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Switch timing on this machine: seconds until the current weapon may fire (0 without bSwitchWaitsForRefire). */
	static double RefireWait(const FGameplayAbilityActorInfo* ActorInfo, bool bSwitchWaitsForRefire);

	//~UVeloxGameplayAbility interface
	virtual EVeloxInputActivation PrepareInputActivation(const FGameplayTag& InputTag, const FGameplayAbilityActorInfo* ActorInfo, FGameplayEventData& OutEventData) const override;
	//~End of UVeloxGameplayAbility interface

	//~UGameplayAbility interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	//~End of UGameplayAbility interface

protected:
	UFUNCTION()
	void OnDropFinished();

	int32 TargetIndex = INDEX_NONE;
};
