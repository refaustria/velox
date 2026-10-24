// Velox - pawn data: everything needed to spawn and drive a pawn (Lyra pawn-data idea).

#pragma once

#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"

#include "VeloxPawnData.generated.h"

class APawn;
class UVeloxAbilitySet;
class UVeloxInputConfig;
class UVeloxMovementSettings;

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Pawn Data", ShortTooltip = "Data asset defining a Velox pawn."))
class VELOX_API UVeloxPawnData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Pawn class to spawn (usually AVeloxCharacter or a Blueprint subclass). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Pawn")
	TSubclassOf<APawn> PawnClass;

	/** Movement tunables handed to UVeloxMovementComponent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Movement")
	TObjectPtr<UVeloxMovementSettings> MovementSettings;

	/** Input action to tag mapping used to bind native and ability input. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Input")
	TObjectPtr<UVeloxInputConfig> InputConfig;

	/** Collision capsule radius (cm; Q3 box 30 x 30 u at UnitScale 2.5). Applied by AVeloxCharacter on spawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Geometry", Meta = (ClampMin = "1.0"))
	float CapsuleRadius = 37.5f;

	/** Collision capsule half height (cm; Q3 box height 56 u). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Geometry", Meta = (ClampMin = "1.0"))
	float CapsuleHalfHeight = 70.0f;

	/** Standing eye height above the capsule centre (cm; Q3 viewheight 26 u + origin 24 u = 50 u above the feet). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Geometry")
	float EyeHeight = 55.0f;

	/** Ability sets granted to the owning player's ability system component (M2). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Abilities")
	TArray<TObjectPtr<UVeloxAbilitySet>> AbilitySets;
};
