// Velox - fire ability base (ADR 0003 section 5). One activation = one shot, ending in the same frame; automatic fire is
// the WhileInputActive re-try. Granted per owned weapon by UVeloxWeaponInventoryComponent with SourceObject = the
// UVeloxWeaponDefinition, which supplies every number.
//
// Fire rate: no cooldown GE. Each side keeps NextFireTime in its own world time (the shooter's machine: client, listen
// host, bot server) with Q3's one-frame carry (B19, VeloxFireTiming); the server for a remote shooter validates with
// FVeloxFireRateBucket (token bucket + minimum spacing) on receive time instead, since receive jitter would otherwise
// reject legitimate shots.
// Switch timing (B18): the shooter's machine fires only at or after the inventory's local SwitchEndTime; the server for
// a remote shooter accepts a shot while GE_WeaponSwitching has <= SwitchTimingToleranceSeconds left, also for the
// pending weapon, whose accepted shot finalises the switch. Velox.State.WeaponSwitching does not block activation.

#pragma once

#include "Combat/VeloxGameplayAbility.h"
#include "Weapons/VeloxFireRateBucket.h"
#include "Weapons/VeloxShotValidation.h"

#include "VeloxGameplayAbility_Fire.generated.h"

class AController;
class APawn;
class UVeloxWeaponDefinition;
class UVeloxWeaponInventoryComponent;
struct FGameplayCueParameters;

/**
 * Test tooling (task 08, scripted input "shots" rows): number of shots this process fired as the shooter's machine
 * (predicting client, listen host, bots), counted by the fire abilities. Process-wide; meaningful in a client process.
 */
namespace VeloxFireDebug
{
	VELOX_API int32 GetLocalShotCount();
	void NoteLocalShot();
}

/** Summary of the last shot this instance resolved on the server (logs, tests, ClientConfirmHit). */
struct FVeloxServerShotInfo
{
	uint8 ShotSeq = 0;
	int32 NumHits = 0;
	int32 PelletHits = 0;
	int32 TotalDamage = 0;
	bool bKill = false;
	bool bRejected = false;
};

UCLASS(Abstract)
class VELOX_API UVeloxGameplayAbility_Fire : public UVeloxGameplayAbility
{
	GENERATED_BODY()

public:
	UVeloxGameplayAbility_Fire(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** The weapon of the spec (its SourceObject). */
	static const UVeloxWeaponDefinition* GetDefinition(const FGameplayAbilitySpec* Spec);
	const UVeloxWeaponDefinition* GetCurrentDefinition() const;

	/** Local world time of the next allowed shot on this machine (also read by the switch's refire wait). */
	double GetNextFireTime() const { return NextFireTime; }

	const FVeloxServerShotInfo& GetLastServerShot() const { return LastServerShot; }

	/** True on the server for a shooter controlled by a remote client (validation by bucket + spacing, switch by tolerance). */
	static bool IsServerForRemoteShooter(const FGameplayAbilityActorInfo* ActorInfo);

	/** Seconds left on ASC's longest active GE_WeaponSwitching (Velox.State.WeaponSwitching); 0 without one. */
	static double GetSwitchRemaining(const UAbilitySystemComponent* ASC);

	/**
	 * ADR 0004 section 3: Haste for the fire interval. Shooter's machine (owning client, listen host, bots): the move
	 * timeline (UVeloxMovementComponent::IsHasteActive); server for a remote shooter: its Velox.Powerup.Haste tag.
	 */
	static bool IsShooterHasteActive(const FGameplayAbilityActorInfo* ActorInfo);

	/** Interval / HasteFireRateScale while IsShooterHasteActive (VeloxFireTiming::HasteFireInterval), else Interval. */
	float GetHasteFireInterval(const FGameplayAbilityActorInfo* ActorInfo, float Interval) const;

	//~UGameplayAbility interface
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	/** Ammo >= AmmoPerShot (the ammo attribute of the definition's AmmoTag). The cost itself is a separate GE_AmmoCost. */
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	//~End of UGameplayAbility interface

protected:
	/** B19, Q3 weapon time: a shot within one frame (this frame's delta) of its schedule carries, a later one restarts. */
	void AdvanceNextFireTime(double Now, float Interval);

	/** Server receive-time rate check only (bucket + minimum spacing, B19); logs nothing. */
	bool ConsumeServerFireToken(double Now, const UVeloxWeaponDefinition& Definition);

	/** Server for a remote shooter: VeloxShotValidation::Validate with this weapon's bucket and the combat settings. */
	EVeloxShotReject ValidateServerShot(const FGameplayAbilityTargetData* Raw, double Now, const UVeloxWeaponDefinition& Definition, FVeloxValidatedShot& OutShot);

	/** Server, accepted shot (B18): if Definition is the pending weapon of a running switch, it becomes current now. */
	void FinalisePendingSwitch(const UVeloxWeaponDefinition& Definition) const;

	/** GE_AmmoCost of one shot on the own ASC (predicted inside the activation's window on the client). */
	void ApplyAmmoCost(const UVeloxWeaponDefinition& Definition) const;

	/** Cue parameters: Location = End, Normal, context origin = Start, Velox context shot sequence, instigator = avatar. */
	FGameplayCueParameters MakeCueParameters(const FVector& Start, const FVector& End, const FVector& Normal, uint8 ShotSeq) const;

	/** The shooter pawn and its controller (B6: aim from the controller, origin from the pawn's view location). */
	APawn* GetShooterPawn() const;
	AController* GetShooterController() const;
	UVeloxWeaponInventoryComponent* GetInventory() const;

	double NextFireTime = -1.0e9;
	FVeloxFireRateBucket ServerBucket;
	FVeloxServerShotInfo LastServerShot;
};
