// Velox - weapon switch (ADR 0003 section 4): one spec per PlayerState bound to InputTag.Weapon.Slot1..9/Next/Prev,
// OnInputTriggered, LocalPredicted. The press is resolved to a table index on the shooter's machine
// (PrepareInputActivation) and travels to the server as event data (EventMagnitude = index); the server validates
// ownership. Sequence: if bSwitchWaitsForRefire wait for the current weapon's NextFireTime, then GE_WeaponSwitching for
// Drop + Raise (applied at activation with Wait + Drop + Raise, inside the prediction window), after Wait + Drop the
// current index changes (server: authoritative; client: predicted until OnRep). B18: every machine that runs the
// activation records the inventory's SwitchEndTime = Now + Wait + Drop + Raise; the shooter's machine fires only at or
// after it, the server gates a remote shooter by the GE's remaining time with a tolerance (UVeloxGameplayAbility_Fire).
// The tag stays on the GE for animation/UI and the server gate; it does not block fire activation.
// Task 08b (D4): a switch activation that reaches the server while its previous switch instance is still running (that
// instance's drop end is shifted by the latency) supersedes it (bRetriggerInstancedAbility); the shooter's machine still
// ignores a switch press during its own drop (PrepareInputActivation skips while the spec is active), as before.

#pragma once

#include "Combat/VeloxGameplayAbility.h"

#include "VeloxGameplayAbility_WeaponSwitch.generated.h"

UCLASS()
class VELOX_API UVeloxGameplayAbility_WeaponSwitch : public UVeloxGameplayAbility
{
	GENERATED_BODY()

public:
	UVeloxGameplayAbility_WeaponSwitch(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Server (task 08b D4, via UVeloxWeaponInventoryComponent::CompletePendingSwitch): the switch was completed by an
	 * accepted shot of the pending weapon before this instance's drop end; ends it without touching SwitchEndTime.
	 */
	void CompleteSwitchEarly();

	/** Switch timing on this machine: seconds until the current weapon may fire (0 without bSwitchWaitsForRefire). */
	static double RefireWait(const FGameplayAbilityActorInfo* ActorInfo, bool bSwitchWaitsForRefire);

	//~UVeloxGameplayAbility interface
	virtual EVeloxInputActivation PrepareInputActivation(const FGameplayTag& InputTag, const FGameplayAbilityActorInfo* ActorInfo, FGameplayEventData& OutEventData) const override;
	//~End of UVeloxGameplayAbility interface

	//~UGameplayAbility interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	/** Ended before the drop finished (rejected by the server, cancelled): the switch did not happen, SwitchEndTime is cleared. */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;
	//~End of UGameplayAbility interface

protected:
	UFUNCTION()
	void OnDropFinished();

	int32 TargetIndex = INDEX_NONE;

	/** This activation set the inventory's SwitchEndTime and has not reached its drop end yet. */
	bool bSwitchTimingPending = false;
};
