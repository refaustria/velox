// Velox - one hitscan/projectile shot as GAS target data (ADR 0003 section 5, review of task 03). Sent by the predicting
// client with CallServerSetReplicatedTargetData (reliable), never inside moves (ADR 0002 A33). About 30 bytes.

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

	/** Per-PlayerState shot counter (wraps); names the shot in logs, seeds the pellet pattern with the PlayerId. */
	UPROPERTY()
	uint8 ShotSeq = 0;

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
