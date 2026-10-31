// Velox - weapon switch (ADR 0003 section 4): one spec per PlayerState bound to InputTag.Weapon.Slot1..9/Next/Prev,
// OnInputTriggered, LocalPredicted. The press is resolved to a table index on the shooter's machine
// (PrepareInputActivation) and travels to the server as event data (EventMagnitude = EncodeSwitchEvent); the server
// validates ownership. Sequence: a wait (B42: max(refire wait, the running switch's remaining raise), Q3 weaponTime),
// then the drop, then the raise; GE_WeaponSwitching covers Wait + Drop + Raise (applied at activation, inside the
// prediction window). B18: every machine that runs the activation records the inventory's SwitchEndTime = Now + Wait +
// Drop + Raise; the shooter's machine fires only at or after it, the server gates a remote shooter by the GE's remaining
// time with the one resend tolerance FireRateJitterToleranceSeconds (B37, UVeloxGameplayAbility_Fire).
// B42 (STATUS q20, Q3 PM_FinishWeaponChange reads cmd.weapon at the drop end): the inventory owns the drop on every
// machine (PendingIndex + PendingDropTimer; server: SetCurrentIndex, owning client: PredictCurrentIndex) and the ability
// instance ends at activation, so a press during the own wait/drop reaches PrepareInputActivation and becomes a
// *retarget*: the pending target changes, drop end and switch end stay. The server applies a retarget to its pending
// switch, or - if its drop already ended within the resend tolerance - makes the target current at once, otherwise it
// runs a fresh switch (VeloxFireTiming::ClassifyRemoteRetarget). A press during a raise is a new switch whose wait is
// the remaining raise. B33's supersede check stays for a (non-retarget) switch that reaches the server during its drop.

#pragma once

#include "Combat/VeloxGameplayAbility.h"

#include "VeloxGameplayAbility_WeaponSwitch.generated.h"

class UVeloxWeaponInventoryComponent;

UCLASS()
class VELOX_API UVeloxGameplayAbility_WeaponSwitch : public UVeloxGameplayAbility
{
	GENERATED_BODY()

public:
	UVeloxGameplayAbility_WeaponSwitch(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Server (task 08b D4, via UVeloxWeaponInventoryComponent::CompletePendingSwitch): the switch was completed by an
	 * accepted shot of the pending weapon. Since B42 the instance ends at activation, so this only ends a still active
	 * instance (none in practice).
	 */
	void CompleteSwitchEarly();

	/**
	 * Switch timing on this machine: seconds until the current weapon may fire again (0 without bSwitchWaitsForRefire).
	 * Shooter's machine: its NextFireTime. Server for a remote shooter (B37): max(0, LastAcceptedReceiveTime + Interval
	 * - Now) of the current weapon's fire instance (VeloxFireTiming::RemoteRefireWait), never more than one interval;
	 * Interval is the Haste-scaled one (GetHasteFireInterval).
	 */
	static double RefireWait(const FGameplayAbilityActorInfo* ActorInfo, bool bSwitchWaitsForRefire);

	/**
	 * B42: seconds left of the running switch on this machine (0 without one). Shooter's machine: SwitchEndTime - Now;
	 * server for a remote shooter: the largest remaining GE_WeaponSwitching (its timeline, B18).
	 */
	static double RunningSwitchRemaining(const FGameplayAbilityActorInfo* ActorInfo);

	/** B42 event data: the table index, plus MaxWeapons for a retarget (a press during the own wait/drop). */
	static float EncodeSwitchEvent(int32 Index, bool bRetarget);
	/** False for anything that is not a finite encoded index (nit 11). */
	static bool DecodeSwitchEvent(float Magnitude, int32& OutIndex, bool& bOutRetarget);

	//~UVeloxGameplayAbility interface
	virtual EVeloxInputActivation PrepareInputActivation(const FGameplayTag& InputTag, const FGameplayAbilityActorInfo* ActorInfo, FGameplayEventData& OutEventData) const override;
	//~End of UVeloxGameplayAbility interface

	//~UGameplayAbility interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	//~End of UGameplayAbility interface

protected:
	/**
	 * B42: applies a retarget to Target. Returns false when the server must run it as a fresh switch instead
	 * (ERemoteRetarget::Fresh, or no pending switch on the shooter's machine).
	 */
	bool ApplyRetarget(const FGameplayAbilityActorInfo* ActorInfo, UVeloxWeaponInventoryComponent& Inventory, int32 Target) const;

	/** Owning client: the server's rejection of this activation rolls the local switch back (prediction key delegate). */
	void BindLocalRejection(const FGameplayAbilityActivationInfo& ActivationInfo, UVeloxWeaponInventoryComponent& Inventory) const;

	/** Server: refuses a confirmed activation (ClientRejectSwitch to a remote shooter) and ends the instance. */
	void RejectActivation(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo& ActivationInfo,
		UVeloxWeaponInventoryComponent* Inventory);

	int32 TargetIndex = INDEX_NONE;
};
