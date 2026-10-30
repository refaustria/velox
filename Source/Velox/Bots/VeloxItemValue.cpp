// Velox - world-free item value for bot routing (ADR 0005 section 7).

#include "Bots/VeloxItemValue.h"

#include "Bots/VeloxBotTypes.h"

namespace VeloxItemValuePrivate
{
	// ADR 0005 section 7 constants of the need formula (Q3 fuzzy-weight shape, not difficulty tunables).
	constexpr float UnusableAmmoNeedScale = 0.1f;
	constexpr float OwnedWeaponAmmoNeedScale = 0.3f;

	/** (Cap - Have) / Scale clamped to 0..1; 0 without a scale. */
	float Deficit(int32 Have, int32 Cap, int32 Scale)
	{
		return Scale > 0 ? FMath::Clamp(static_cast<float>(Cap - Have) / static_cast<float>(Scale), 0.f, 1.f) : 0.f;
	}

	float Deficit(int32 Have, int32 Cap)
	{
		return Deficit(Have, Cap, Cap);
	}
}

namespace VeloxItemValue
{
	float Need(const FVeloxItemScoringInput& In)
	{
		switch (In.Type)
		{
		case EVeloxItemType::Health:
		{
			// ADR 0005 D21: a hard-cap item grants up to MaxHealth effective points to anyone at or below MaxHealth, so its
			// need is the deficit to the hard cap over MaxHealth (125 -> 0.75, <= 100 -> 1), not over the hard cap.
			return In.HealthCap == EVeloxHealthCap::HardCap ? VeloxItemValuePrivate::Deficit(In.Health, In.HealthHardCap, In.MaxHealth)
				: VeloxItemValuePrivate::Deficit(In.Health, In.MaxHealth);
		}
		case EVeloxItemType::Armor:
			return VeloxItemValuePrivate::Deficit(In.Armor, In.MaxArmor);
		case EVeloxItemType::Ammo:
		{
			const float AmmoNeed = VeloxItemValuePrivate::Deficit(In.Ammo, In.AmmoMax);
			return In.bOwnsWeaponUsingAmmo ? AmmoNeed : AmmoNeed * VeloxItemValuePrivate::UnusableAmmoNeedScale;
		}
		case EVeloxItemType::Weapon:
			// D22: an unowned weapon matters less the better the arsenal the bot already has.
			return In.bOwnsWeapon ? VeloxItemValuePrivate::OwnedWeaponAmmoNeedScale * VeloxItemValuePrivate::Deficit(In.Ammo, In.AmmoMax)
				: FMath::Lerp(1.f, FMath::Clamp(In.UnownedWeaponNeedFloor, 0.f, 1.f), FMath::Clamp(In.OwnedWeaponStrength, 0.f, 1.f));
		case EVeloxItemType::Powerup:
			return 1.f;
		default:
			return 0.f;
		}
	}

	float Score(const FVeloxItemScoringInput& In, const FVeloxItemScoringParams& Params)
	{
		const float Value = In.BotBaseValue * Need(In);
		if (Value <= 0.f)
		{
			return 0.f;
		}
		const float Travel = FMath::Max(In.TravelSeconds, 0.f);
		const float Wait = In.bAvailable ? 0.f : FMath::Max(0.f, In.TimeUntilRespawn - Travel);
		if (Wait > Params.MaxWaitSeconds * Params.ItemTimingSkill)
		{
			return 0.f;
		}
		const float Denominator = Travel + Wait + Params.TravelOffsetSeconds;
		return Denominator > UE_SMALL_NUMBER ? Value / Denominator : 0.f;
	}

	void Rank(TConstArrayView<FVeloxItemScoringInput> Candidates, const FVeloxItemScoringParams& Params, TArray<FVeloxRankedItem>& OutRanking)
	{
		OutRanking.Reset(Candidates.Num());
		for (const FVeloxItemScoringInput& Candidate : Candidates)
		{
			OutRanking.Add({Candidate.CandidateId, Score(Candidate, Params)});
		}
		OutRanking.Sort([](const FVeloxRankedItem& A, const FVeloxRankedItem& B)
		{
			return A.Score != B.Score ? A.Score > B.Score : A.CandidateId < B.CandidateId;
		});
	}

	FVeloxItemScoringParams MakeParams(const FVeloxBotDifficultyParams& Difficulty)
	{
		FVeloxItemScoringParams P;
		P.ItemTimingSkill = Difficulty.ItemTimingSkill;
		P.MaxWaitSeconds = Difficulty.MaxWaitSeconds;
		return P;
	}
}
