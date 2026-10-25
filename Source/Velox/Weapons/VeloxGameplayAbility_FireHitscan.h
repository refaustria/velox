// Velox - hitscan fire (MG, SG, LG, RG), ADR 0003 section 5 and the call sequence of the task-03 review.
//
// Shooter's machine (client, listen host, bot server), in ActivateAbility after CommitAbility:
//   Origin = Pawn->GetPawnViewLocation(), Dir = Controller->GetControlRotation().Vector() (B6); BuildShotTiming once with
//   the centre ray (B14). Client: cosmetic resolution against the rendered proxy capsules + world (never the
//   subsystem, B10), target data -> CallServerSetReplicatedTargetData, predicted GE_AmmoCost, local fire/impact cues,
//   NextFireTime, EndAbility (not replicated). Listen host / bot: ResolveServerShot directly (no RPC).
// Server for a remote client: AbilityTargetDataSetDelegate -> OnServerTargetData: bucket first, then type and finite
//   checks (VeloxShotRejected seq= reason=), ResolveServerShot: TraceShot, one MakeDamageSpec per victim, GE_AmmoCost,
//   fire cue + impact cue with the activation's prediction key (the predicting client skips them), the LG beam cue,
//   ClientConfirmHit, EndAbility.
// Ray directions on both ends: VeloxShotDirs (pellets / Q3 MG spread from FRandomStream(ShotSeq ^ PlayerId)).

#pragma once

#include "Combat/VeloxHitMath.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxWeaponDefinition.h"

#include "VeloxGameplayAbility_FireHitscan.generated.h"

class UVeloxWeaponDefinition;
struct FGameplayAbilityTargetDataHandle;

namespace VeloxShotDirs
{
	/**
	 * Ray directions of one shot around the unit centre direction Dir: PelletCount > 1 -> Q3 ShotgunPattern; else a
	 * spread angle -> one Q3 Bullet_Fire direction (MG); else Dir. Deterministic for Seed (client tracers = server rays).
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

	/** ADR 0003 section 5: FRandomStream(ShotSeq ^ PlayerId). */
	inline int32 MakeSeed(uint8 ShotSeq, int32 PlayerId) { return static_cast<int32>(ShotSeq) ^ PlayerId; }
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

protected:
	struct FShotInput
	{
		FVector Origin = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		double ViewServerTime = 0.0;
		TWeakObjectPtr<AActor> HintTarget;
		float HintTargetStamp = 0.f;
		uint8 ShotSeq = 0;
	};

	void OnServerTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);

	/** Server (remote shooter after validation, or local shooter directly): steps 2-5 of the review. */
	void ResolveServerShot(const UVeloxWeaponDefinition& Definition, const FShotInput& Shot);

	/** Client: cosmetic rays against rendered proxies + world, local fire and impact cues. */
	void PlayLocalShotCosmetics(const UVeloxWeaponDefinition& Definition, const FShotInput& Shot) const;

	void Reject(uint8 ShotSeq, const TCHAR* Reason);

	FDelegateHandle TargetDataDelegateHandle;
};
