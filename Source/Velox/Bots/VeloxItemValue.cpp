// Velox - world-free item value for bot routing (ADR 0005 section 7).

#include "Bots/VeloxItemValue.h"

#include "Bots/VeloxBotTypes.h"

namespace VeloxItemValuePrivate
{
	// ADR 0005 section 7 constants of the need formula (Q3 fuzzy-weight shape, not difficulty tunables).
	constexpr float UnusableAmmoNeedScale = 0.1f;
	constexpr float OwnedWeaponAmmoNeedScale = 0.3f;

	float Deficit(int32 Have, int32 Cap)
	{
		return Cap > 0 ? FMath::Clamp(static_cast<float>(Cap - Have) / static_cast<float>(Cap), 0.f, 1.f) : 0.f;
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
			const int32 Cap = In.HealthCap == EVeloxHealthCap::HardCap ? In.HealthHardCap : In.MaxHealth;
			return VeloxItemValuePrivate::Deficit(In.Health, Cap);
		}
		case EVeloxItemType::Armor:
			return VeloxItemValuePrivate::Deficit(In.Armor, In.MaxArmor);
		case EVeloxItemType::Ammo:
		{
			const float AmmoNeed = VeloxItemValuePrivate::Deficit(In.Ammo, In.AmmoMax);
			return In.bOwnsWeaponUsingAmmo ? AmmoNeed : AmmoNeed * VeloxItemValuePrivate::UnusableAmmoNeedScale;
		}
		case EVeloxItemType::Weapon:
			return In.bOwnsWeapon ? VeloxItemValuePrivate::OwnedWeaponAmmoNeedScale * VeloxItemValuePrivate::Deficit(In.Ammo, In.AmmoMax) : 1.f;
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
