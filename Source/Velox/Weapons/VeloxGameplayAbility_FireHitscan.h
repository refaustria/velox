// Velox - hitscan fire (MG, SG, LG, RG), ADR 0003 section 5 and the call sequence of the task-03 review.
//
// Shooter's machine (client, listen host, bot server), in ActivateAbility after CommitAbility:
//   Origin = Pawn->GetPawnViewLocation(), Dir = Controller->GetControlRotation().Vector() (B6); BuildShotTiming once with
//   the centre ray (B14). Client: cosmetic resolution against the rendered proxy capsules + world (never the
//   subsystem, B10) of the centre ray only, target data -> CallServerSetReplicatedTargetData, predicted GE_AmmoCost, the
//   predicted fire cue (no impact cue, B21), NextFireTime, EndAbility (not replicated). Listen host / bot:
//   ResolveServerShot directly (no RPC).
// Server for a remote client: AbilityTargetDataSetDelegate -> OnServerTargetData: VeloxShotValidation (B32 token bucket,
//   then type and finite checks; VeloxShotRejected seq= reason=, throttled), the pending switch finalised, ResolveServerShot:
//   the server's own pattern seed (B21), TraceShot, one MakeDamageSpec per victim, GE_AmmoCost, the fire cue with the
//   activation's prediction key (the predicting client skips it), one impact cue with no prediction key (every client,
//   the owner included, receives it, ~RTT late at the true location as in Q3), the LG beam cue, ClientConfirmHit,
//   LastAcceptedReceiveTime (B37, no NextFireTime on this path), EndAbility.
// Ray directions: server only, VeloxShotDirs (pellets / Q3 MG spread from FRandomStream(ServerPatternSeq ^ PlayerId)).
// Both hitscan cues carry PatternSeq and the centre direction (HitDirection) in the Velox context, so a receiver can
// rebuild the pattern (M5 FX).

#pragma once

#include "Combat/VeloxHitMath.h"
#include "Engine/TimerHandle.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxWeaponDefinition.h"

#include "VeloxGameplayAbility_FireHitscan.generated.h"

class UVeloxWeaponDefinition;
struct FGameplayAbilityTargetDataHandle;

namespace VeloxShotDirs
{
	/**
	 * Ray directions of one shot around the unit centre direction Dir: PelletCount > 1 -> Q3 ShotgunPattern; else a
	 * spread angle -> one Q3 Bullet_Fire direction (MG); else Dir. Deterministic for Seed (server rays = any receiver's
	 * rebuild from the cue's PatternSeq).
	 */
	template <typename AllocatorType>
	void Build(const UVeloxWeaponDefinition& Definition, const FVector& Dir, int32 Seed, TArray<FVector, AllocatorType>& OutDirs)
	{
		OutDirs.Reset();
		if (Definition.IsPelletWeapon())
		{
			FVeloxHitMath::PelletDirections(Dir, Definition.PelletCount, Definition.SpreadAngleDeg, Seed, OutDirs);
		}
		else if (Definition.SpreadAngleDeg > 0.f)
		{
			FRandomStream Stream(Seed);
			OutDirs.Add(FVeloxHitMath::BulletSpreadDirection(Dir, Definition.SpreadAngleDeg, Stream));
		}
		else
		{
			OutDirs.Add(Dir);
		}
	}

	/** ADR 0003 B21: FRandomStream(ServerPatternSeq ^ PlayerId); PatternSeq is the server's, never the client's ShotSeq. */
	inline int32 MakeSeed(uint8 PatternSeq, int32 PlayerId) { return static_cast<int32>(PatternSeq) ^ PlayerId; }
}

UCLASS()
class VELOX_API UVeloxGameplayAbility_FireHitscan : public UVeloxGameplayAbility_Fire
{
	GENERATED_BODY()

public:
	UVeloxGameplayAbility_FireHitscan(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UGameplayAbility interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;
	//~End of UGameplayAbility interface

	/** Server: the pattern seed and ray directions of the last shot this instance resolved (B21; logs, tests). */
	uint8 GetLastServerPatternSeq() const { return LastServerPatternSeq; }
	const TArray<FVector, TInlineAllocator<1>>& GetLastServerDirs() const { return LastServerDirs; }

protected:
	struct FShotInput
	{
		FVector Origin = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		double ViewServerTime = 0.0;
		TWeakObjectPtr<AActor> HintTarget;
		float HintTargetStamp = 0.f;
		uint8 ShotSeq = 0;
		/** B36: the payload's move time stamp; set for a remote shooter only (origin checked against the eye at it). */
		float FireMoveTimeStamp = 0.f;
		bool bHasFireMoveTimeStamp = false;
	};

	void OnServerTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);

	/** Server (remote shooter after validation, or local shooter directly): steps 2-5 of the review. */
	void ResolveServerShot(const UVeloxWeaponDefinition& Definition, const FShotInput& Shot);

	/** Client: the centre ray against rendered proxies + world, the predicted fire cue only (B21: no impact cue). */
	void PlayLocalShotCosmetics(const UVeloxWeaponDefinition& Definition, const FShotInput& Shot) const;

	void Reject(uint8 ShotSeq, const TCHAR* Reason);

	FDelegateHandle TargetDataDelegateHandle;

	uint8 LastServerPatternSeq = 0;
	TArray<FVector, TInlineAllocator<1>> LastServerDirs;
};
