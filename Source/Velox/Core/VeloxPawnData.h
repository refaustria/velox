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
	/**
	 * Pawn class to spawn (usually AVeloxCharacter or a Blueprint subclass). Pawn geometry (capsule, crouch, eye heights)
	 * is a class default of this class (ADR 0003 section 6), not pawn data.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Pawn")
	TSubclassOf<APawn> PawnClass;

	/** Movement tunables handed to UVeloxMovementComponent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Movement")
	TObjectPtr<UVeloxMovementSettings> MovementSettings;

	/** Input action to tag mapping used to bind native and ability input. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Input")
	TObjectPtr<UVeloxInputConfig> InputConfig;

	/** Ability sets granted to the owning player's ability system component (M2). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Abilities")
	TArray<TObjectPtr<UVeloxAbilitySet>> AbilitySets;
};
