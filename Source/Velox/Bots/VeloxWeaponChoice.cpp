// Velox - world-free bot weapon choice (ADR 0005 section 9).

#include "Bots/VeloxWeaponChoice.h"

namespace VeloxWeaponChoicePrivate
{
	// ADR 0005 section 9: the band falls linearly from 1 to this value at MinRange / MaxRange.
	constexpr float BandEdgeScore = 0.3f;

	float Ramp(float From, float To, float X)
	{
		// From = band edge (score BandEdgeScore), To = best edge (score 1).
		const float Span = To - From;
		if (FMath::Abs(Span) <= UE_SMALL_NUMBER)
		{
			return 1.f;
		}
		const float Alpha = FMath::Clamp((X - From) / Span, 0.f, 1.f);
		return FMath::Lerp(BandEdgeScore, 1.f, Alpha);
	}

	const FVeloxWeaponChoiceOption* FindOption(const FVeloxWeaponChoiceInput& In, const FGameplayTag& Weapon)
	{
		return In.Weapons.FindByPredicate([&Weapon](const FVeloxWeaponChoiceOption& O) { return O.Weapon == Weapon; });
	}
}

namespace VeloxWeaponChoice
{
	float Band(const FVeloxBotWeaponPreference& Pref, float Range)
	{
		if (Range < Pref.MinRange || Range > Pref.MaxRange)
		{
			return 0.f;
		}
		if (Range < Pref.BestMin)
		{
			return VeloxWeaponChoicePrivate::Ramp(Pref.MinRange, Pref.BestMin, Range);
		}
		if (Range > Pref.BestMax)
		{
			return VeloxWeaponChoicePrivate::Ramp(Pref.MaxRange, Pref.BestMax, Range);
		}
		return 1.f;
	}

	float ScoreOption(const FVeloxWeaponChoiceOption& Option, const FVeloxBotWeaponPreference& Pref, float Range, float SplashSafetyMarginCm)
	{
		if (!Option.bOwned || Option.Ammo < Option.AmmoPerShot)
		{
			return 0.f;
		}
		if (Option.SplashRadiusCm > 0.f && Range < Option.SplashRadiusCm + SplashSafetyMarginCm)
		{
			return 0.f;
		}
		return FMath::Max(Pref.Weight, 0.f) * Band(Pref, Range);
	}

	int32 Pick(const FVeloxWeaponChoiceInput& In, TConstArrayView<FVeloxBotWeaponPreference> Preferences)
	{
		const float Range = In.bHasTarget ? In.RangeCm : In.PreferredRangeCm;
		int32 Best = INDEX_NONE;
		float BestScore = 0.f;
		for (int32 Index = 0; Index < Preferences.Num(); ++Index)
		{
			const FVeloxBotWeaponPreference& Pref = Preferences[Index];
			const FVeloxWeaponChoiceOption* Option = VeloxWeaponChoicePrivate::FindOption(In, Pref.Weapon);
			if (!Option)
			{
				continue;
			}
			float Score = ScoreOption(*Option, Pref, Range, In.SplashSafetyMarginCm);
			if (In.CurrentWeapon.IsValid() && Pref.Weapon == In.CurrentWeapon)
			{
				Score *= In.SwitchHysteresis;
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Index;
			}
		}
		return Best;
	}
}
