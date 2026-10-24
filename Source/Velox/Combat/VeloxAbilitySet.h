// Velox - ability set data asset (Lyra ability-set idea). M0 stub so UVeloxPawnData has its final shape;
// grant lists and GiveToAbilitySystem/TakeFromAbilitySystem are specified in ADR 0001 section 2 and land in M2.

#pragma once

#include "Engine/DataAsset.h"

#include "VeloxAbilitySet.generated.h"

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Ability Set"))
class VELOX_API UVeloxAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()
};
