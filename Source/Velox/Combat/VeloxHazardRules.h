// Velox - hazard kill credit rules (ADR 0008 section 3.2). World-free: the single implementation of "does this damage
// event write the last-attacker record" and "is the record still credited", used by UVeloxHealthComponent (and, from
// H2 on, by AVeloxHazardVolume through UVeloxHealthComponent::GetCreditedAttacker). Unit-tested in
// Tests/VeloxHazardRulesTest.cpp.

#pragma once

#include "CoreMinimal.h"
#include "Core/VeloxGameplayTags.h"
#include "GameplayTagContainer.h"

namespace VeloxHazardRules
{
	/** Hazard (Velox.Damage.Hazard and its children) or world damage (Velox.Damage.Environment): never an attacker source. */
	inline bool IsWorldOrHazardDamage(const FGameplayTagContainer& DamageTags)
	{
		return DamageTags.HasTag(VeloxGameplayTags::Damage_Hazard) || DamageTags.HasTag(VeloxGameplayTags::Damage_Environment);
	}

	/**
	 * ADR 0008 3.2: a damage event writes the last-attacker record when its instigator resolves to a player state other
	 * than the victim's own, it carried health loss, armor loss or knockback, and it is neither hazard nor world damage.
	 * bSelfTagged (Velox.Damage.Self on the spec) is treated as self damage even if the instigator lookup disagreed.
	 * Self damage therefore neither writes nor clears the record; callers never clear it on a rejected event.
	 */
	inline bool ShouldRecordAttacker(bool bInstigatorIsOtherPlayer, bool bSelfTagged, float HealthLoss, float ArmorLoss, bool bHasKnockback, bool bWorldOrHazard)
	{
		if (!bInstigatorIsOtherPlayer || bSelfTagged || bWorldOrHazard)
		{
			return false;
		}
		return HealthLoss > 0.f || ArmorLoss > 0.f || bHasKnockback;
	}

	/** Convenience overload that classifies the tags itself. */
	inline bool ShouldRecordAttacker(bool bInstigatorIsOtherPlayer, const FGameplayTagContainer& DamageTags, float HealthLoss, float ArmorLoss, bool bHasKnockback)
	{
		return ShouldRecordAttacker(bInstigatorIsOtherPlayer, DamageTags.HasTag(VeloxGameplayTags::Damage_Self), HealthLoss, ArmorLoss, bHasKnockback,
			IsWorldOrHazardDamage(DamageTags));
	}

	/**
	 * ADR 0008 3.2: the record credits its attacker while it is strictly younger than WindowSeconds
	 * (Now - RecordTime < WindowSeconds; at exactly the window edge it has expired) and the attacker still exists.
	 * A record from the future (clock reset, Now < RecordTime) counts as age 0. A non-positive window credits nobody.
	 */
	inline bool IsCredited(bool bHasRecord, bool bAttackerValid, double RecordTime, double Now, float WindowSeconds)
	{
		if (!bHasRecord || !bAttackerValid || WindowSeconds <= 0.f)
		{
			return false;
		}
		const double Age = FMath::Max(Now - RecordTime, 0.0);
		return Age < static_cast<double>(WindowSeconds);
	}
}
