// Velox - match-state gameplay effect and tags.

#include "GameModes/VeloxMatchEffects.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxMatchEffects)

namespace VeloxMatchTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_MatchFrozen, "Velox.State.MatchFrozen", "Intermission: no damage, no knockback, no firing, no movement, no pickups (ADR 0004 section 5).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Mode_FFA, "Velox.Mode.FFA", "Free-for-all game mode definition.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Mode_Duel, "Velox.Mode.Duel", "Duel (1v1) game mode definition.");
}

UVeloxGE_MatchFrozen::UVeloxGE_MatchFrozen()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	// Native GE components must be default subobjects listed in GEComponents (UGameplayEffect::PostInitProperties).
	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GrantMatchFrozen"));
	GEComponents.Add(TargetTags);
	FInheritedTagContainer Granted;
	Granted.AddTag(VeloxMatchTags::State_MatchFrozen);
	TargetTags->SetAndApplyTargetTagChanges(Granted);
}
