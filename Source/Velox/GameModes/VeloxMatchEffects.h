// Velox - match-state gameplay effect and tags (ADR 0004 section 5). Owned by task 05 (lead decision 2026-10-29); task 02
// consumes Velox.State.MatchFrozen in the damage execution and the fire abilities' ActivationBlockedTags.

#pragma once

#include "GameplayEffect.h"
#include "NativeGameplayTags.h"

#include "VeloxMatchEffects.generated.h"

namespace VeloxMatchTags
{
	/** Intermission freeze: granted by UVeloxGE_MatchFrozen to every ASC at PostMatch, removed by the level reset. */
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_MatchFrozen);
	/** UVeloxGameModeDefinition::ModeTag values. */
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_FFA);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_Duel);
}

/** Infinite; grants Velox.State.MatchFrozen. Applied by AVeloxGameMode at PostMatch, removed explicitly by the reset. */
UCLASS()
class VELOX_API UVeloxGE_MatchFrozen : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVeloxGE_MatchFrozen();
};
