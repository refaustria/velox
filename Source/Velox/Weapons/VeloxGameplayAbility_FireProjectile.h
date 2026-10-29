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
// ADR 0003 B35 (task 08f): the predicting client also queues the shot into its fire move (QueueMoveShot); the server's
//   inventory registers the self splash from the move stream (UVeloxWeaponInventoryComponent::HandleServerMoveShot).
//   The target data then adopts that record (no bucket token, no second schedule; source=move), or waits for the fire
//   move when it overtook it (pending, resumed by the inventory's per-move hook), or schedules from its own data when the
//   move stream passed the fire stamp without a move-shot (source=rpc, VeloxShotLate when late).

#pragma once

#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxShotTargetData.h"

#include "VeloxGameplayAbility_FireProjectile.generated.h"

class AVeloxProjectile;
class UVeloxWeaponDefinition;
struct FGameplayAbilityTargetDataHandle;
struct FVeloxMoveShotRecord;

/**
 * Projectile shot: the hitscan payload (origin, direction, view time, sequence, FireMoveTimeStamp). Its own type so the
 * server tells a projectile shot from a hitscan one (B26); since B36 the move time stamp lives in the base struct.
 */
USTRUCT()
struct VELOX_API FGameplayAbilityTargetData_VeloxProjectileShot : public FGameplayAbilityTargetData_VeloxShot
{
	GENERATED_BODY()

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

	/** "move" or "rpc": where the self splash of the last server-side remote shot came from (tests, logs); "" otherwise. */
	const TCHAR* GetLastServerShotSource() const { return LastServerShotSource; }

	/**
	 * ADR 0003 B35: how far (ms) the owner's processed move stream (LastMoveStamp) is past a shot's fire stamp, wrap-aware
	 * over ResetPeriod (MinTimeBetweenTimeStampResets). The late_ms of the VeloxProjectileShot line.
	 */
	static double ComputeShotLateMs(float LastMoveStamp, float FireMoveTimeStamp, float ResetPeriod);

	/** A source=rpc shot later than one 60 Hz move + 40 ms jitter is reported as VeloxShotLate (B35). */
	static bool IsShotLate(double LateMs) { return LateMs > 1000.0 / 60.0 + 40.0; }

	/** Server: a validated fire RPC is waiting for its fire move (ADR 0003 B35). */
	bool HasPendingServerShot() const { return bHasPendingServerShot; }

	/**
	 * Server (UVeloxWeaponInventoryComponent::HandleServerMoveProcessed, the pending timeout): launches the waiting shot,
	 * adopting the move-shot record of its sequence if there is one (source=move), else from its own data (source=rpc),
	 * then ends the ability. No-op without a pending shot.
	 */
	void ResumePendingServerShot();

	/** Server: drops a waiting shot without launching it and ends the ability cancelled (inventory reset). */
	void CancelPendingServerShot();

protected:
	void OnServerTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);

	/**
	 * Server: the real projectile from a validated shot. bRemote: the owner predicts (forward simulation, move-timed
	 * self splash at FireMoveTimeStamp); else the owner's splash is resolved at the explosion.
	 */
	void LaunchServerShot(const UVeloxWeaponDefinition& Definition, const FVector& Origin, const FVector& Dir, uint8 ShotSeq, bool bRemote, float FireMoveTimeStamp,
		const FVeloxMoveShotRecord* Record = nullptr);

	/** Launches the pending shot (ResumePendingServerShot without EndAbility); clears the pending state first. */
	void LaunchPendingServerShot();

	void OnPendingServerShotTimeout();

	void Reject(uint8 ShotSeq, const TCHAR* Reason);

	FDelegateHandle TargetDataDelegateHandle;
	TWeakObjectPtr<AVeloxProjectile> LastServerProjectile;
	const TCHAR* LastServerShotSource = TEXT("");

	/** ADR 0003 B35: the validated fire RPC waiting for its fire move. */
	bool bHasPendingServerShot = false;
	FVeloxValidatedShot PendingServerShot;
	TWeakObjectPtr<const UVeloxWeaponDefinition> PendingServerShotDefinition;
};
