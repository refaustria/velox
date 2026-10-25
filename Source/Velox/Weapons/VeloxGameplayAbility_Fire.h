// Velox - fire ability base (ADR 0003 section 5). One activation = one shot, ending in the same frame; automatic fire is
// the WhileInputActive re-try. Granted per owned weapon by UVeloxWeaponInventoryComponent with SourceObject = the
// UVeloxWeaponDefinition, which supplies every number.
//
// Fire rate: no cooldown GE. Each side keeps NextFireTime in its own world time (the shooter's machine: client, listen
// host, bot server); the server for a remote shooter validates with FVeloxFireRateBucket on receive time instead, since
// receive jitter would otherwise reject legitimate shots.

#pragma once

#include "Combat/VeloxGameplayAbility.h"
#include "Weapons/VeloxFireRateBucket.h"

#include "VeloxGameplayAbility_Fire.generated.h"

class AController;
class APawn;
class UVeloxWeaponDefinition;
class UVeloxWeaponInventoryComponent;
struct FGameplayCueParameters;

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

	/** True on the server for a shooter controlled by a remote client (validation by bucket, switch timing trusted). */
	static bool IsServerForRemoteShooter(const FGameplayAbilityActorInfo* ActorInfo);

	//~UGameplayAbility interface
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	/** Ammo >= AmmoPerShot (the ammo attribute of the definition's AmmoTag). The cost itself is a separate GE_AmmoCost. */
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	/**
	 * ActivationBlockedTags = {Dead, WeaponSwitching} on the shooter's machine. The server for a remote shooter ignores
	 * WeaponSwitching: its switch timers run one-way-latency (plus jitter) late, so a shot fired right after the raise
	 * would be rejected; the switch timing is trusted like the fire timing, bounded by the token bucket.
	 */
	virtual bool DoesAbilitySatisfyTagRequirements(const UAbilitySystemComponent& AbilitySystemComponent, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	//~End of UGameplayAbility interface

protected:
	/** Q3 weapon time: held fire keeps the exact rate on frame boundaries (the remainder carries), a pause restarts it. */
	void AdvanceNextFireTime(double Now, float Interval);

	/** Server receive-time validation; logs nothing. */
	bool ConsumeServerFireToken(double Now, const UVeloxWeaponDefinition& Definition);

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
