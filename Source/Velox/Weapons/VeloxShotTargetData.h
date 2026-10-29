// Velox - one hitscan/projectile shot as GAS target data (ADR 0003 section 5, review of task 03). Sent by the predicting
// client with CallServerSetReplicatedTargetData (reliable). About 34 bytes. A projectile shot is also carried in its fire
// move (FVeloxMoveShot, ADR 0003 B35 / ADR 0002 A36); hitscan shots stay out of moves (A33).

#pragma once

#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Engine/NetSerialization.h"

#include "VeloxShotTargetData.generated.h"

class AActor;
class UPackageMap;

USTRUCT()
struct VELOX_API FGameplayAbilityTargetData_VeloxShot : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	/** The eye (Q3 muzzle = view origin): Pawn->GetPawnViewLocation() at fire time (B6). */
	UPROPERTY()
	FVector_NetQuantize100 Origin = FVector::ZeroVector;

	/** Controller->GetControlRotation().Vector(): the centre ray; the server derives pellets/spread itself. */
	UPROPERTY()
	FVector_NetQuantizeNormal Dir = FVector::ForwardVector;

	/** Server world time the shooter looked at (FVeloxShotTiming::ViewServerTime). Float: ~1 ms within a match. */
	UPROPERTY()
	float ViewServerTime = 0.f;

	/**
	 * Per-PlayerState shot counter of the shooter's machine (wraps). Names the shot in logs and matches the fake and real
	 * projectile (ADR 0003 section 7). It never seeds anything on the server (B21: the MG spread / SG pattern comes from
	 * UVeloxWeaponInventoryComponent::AllocateServerPatternSeq), so a client gains nothing by choosing it.
	 */
	UPROPERTY()
	uint8 ShotSeq = 0;

	/**
	 * UVeloxMovementComponent::GetClientMoveTimeStamp() at fire time (end of the last move, B6: before the frame's move).
	 * ADR 0003 B36: the server validates Origin against the shooter's eye at this stamp (per-move eye ring); the
	 * projectile ability also schedules the owner's self splash from it (B25). Full precision (~4 bytes).
	 */
	UPROPERTY()
	float FireMoveTimeStamp = 0.f;

	/** Rendered-proxy hint (B14): written only when set (one bit otherwise). */
	UPROPERTY()
	bool bHasHint = false;

	UPROPERTY()
	TWeakObjectPtr<AActor> HintTarget;

	/** The hinted proxy's rendered PawnStamp; float on both ends so the server compares the exact value (B12). */
	UPROPERTY()
	float HintTargetStamp = 0.f;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	//~FGameplayAbilityTargetData interface
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	virtual FString ToString() const override;
	virtual bool HasOrigin() const override { return true; }
	virtual FTransform GetOrigin() const override { return FTransform(Dir.Rotation(), Origin); }
	//~End of FGameplayAbilityTargetData interface
};

template <>
struct TStructOpsTypeTraits<FGameplayAbilityTargetData_VeloxShot> : public TStructOpsTypeTraitsBase2<FGameplayAbilityTargetData_VeloxShot>
{
	enum
	{
		WithNetSerializer = true,
	};
};
