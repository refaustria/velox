// Velox - projectile fire (RL, PG), ADR 0003 sections 5 and 7, task 05.
//
// Shooter's machine, in ActivateAbility after CommitAbility (B6: origin = GetPawnViewLocation, dir = control rotation):
//   Predicting client: target data (the hitscan payload plus FireMoveTimeStamp), quantized exactly as the server will
//   read it; the owner's self splash scheduled in its move timeline at FireMoveTimeStamp + world flight time; the fake
//   projectile; CallServerSetReplicatedTargetData; predicted GE_AmmoCost; local fire cue; NextFireTime; EndAbility.
//   Listen host / bot / standalone: the real projectile directly (no forward simulation, the owner's splash at the
//   explosion like everyone else's).
// Server for a remote client: AbilityTargetDataSetDelegate -> bucket, payload checks, origin validation (B15 rules),
//   FireMoveTimeStamp clamped to the owner's processed move time +- MaxRewindSeconds, the owner's self splash scheduled
//   in its move stream, the real projectile with forward simulation, GE_AmmoCost, fire cue (the predicting client skips
//   it), EndAbility.

#pragma once

#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxShotTargetData.h"

#include "VeloxGameplayAbility_FireProjectile.generated.h"

class AVeloxProjectile;
class UVeloxWeaponDefinition;
struct FGameplayAbilityTargetDataHandle;

/**
 * Projectile shot: the hitscan payload (origin, direction, view time, sequence) plus the shooter's move time stamp at
 * fire time, so the server can evaluate the owner's self splash at the same move as the predicting client (~4 bytes).
 */
USTRUCT()
struct VELOX_API FGameplayAbilityTargetData_VeloxProjectileShot : public FGameplayAbilityTargetData_VeloxShot
{
	GENERATED_BODY()

	/** UVeloxMovementComponent::GetClientMoveTimeStamp() at fire time (end of the last move, B6: before the frame's move). */
	UPROPERTY()
	float FireMoveTimeStamp = 0.f;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	/** Round trip through NetSerialize without a package map: the values the server will read (no hint). */
	void Quantize();

	//~FGameplayAbilityTargetData interface
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	virtual FString ToString() const override;
	//~End of FGameplayAbilityTargetData interface
};

template <>
struct TStructOpsTypeTraits<FGameplayAbilityTargetData_VeloxProjectileShot> : public TStructOpsTypeTraitsBase2<FGameplayAbilityTargetData_VeloxProjectileShot>
{
	enum
	{
		WithNetSerializer = true,
	};
};

UCLASS()
class VELOX_API UVeloxGameplayAbility_FireProjectile : public UVeloxGameplayAbility_Fire
{
	GENERATED_BODY()

public:
	UVeloxGameplayAbility_FireProjectile(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UGameplayAbility interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;
	//~End of UGameplayAbility interface

	/** The projectile spawned by the last server-side shot of this instance (tests; may be destroyed already). */
	AVeloxProjectile* GetLastServerProjectile() const { return LastServerProjectile.Get(); }

protected:
	void OnServerTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);

	/**
	 * Server: the real projectile from a validated shot. bRemote: the owner predicts (forward simulation, move-timed
	 * self splash at FireMoveTimeStamp); else the owner's splash is resolved at the explosion.
	 */
	void LaunchServerShot(const UVeloxWeaponDefinition& Definition, const FVector& Origin, const FVector& Dir, uint8 ShotSeq, bool bRemote, float FireMoveTimeStamp);

	void Reject(uint8 ShotSeq, const TCHAR* Reason);

	FDelegateHandle TargetDataDelegateHandle;
	TWeakObjectPtr<AVeloxProjectile> LastServerProjectile;
};
