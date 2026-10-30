// Velox - world-free bot goal choice (ADR 0005 section 2).

#include "Bots/VeloxBotDecision.h"

namespace VeloxBotDecisionPrivate
{
	const FVeloxGoalItemCandidate* FindById(const FVeloxBotDecisionInput& In, int32 Id)
	{
		return In.Items.FindByPredicate([Id](const FVeloxGoalItemCandidate& C) { return C.CandidateId == Id; });
	}

	bool IsChoosable(const FVeloxGoalItemCandidate& C)
	{
		return C.bReachable && C.Score > 0.f;
	}

	/** Highest score, ties by CandidateId ascending. */
	const FVeloxGoalItemCandidate* BestItem(const FVeloxBotDecisionInput& In)
	{
		const FVeloxGoalItemCandidate* Best = nullptr;
		for (const FVeloxGoalItemCandidate& C : In.Items)
		{
			if (!IsChoosable(C))
			{
				continue;
			}
			if (!Best || C.Score > Best->Score || (C.Score == Best->Score && C.CandidateId < Best->CandidateId))
			{
				Best = &C;
			}
		}
		return Best;
	}

	/** Best item after the item-switch hysteresis against the current Item goal. */
	const FVeloxGoalItemCandidate* ItemWithHysteresis(const FVeloxBotDecisionInput& In, const FVeloxBotDifficultyParams& Params)
	{
		const FVeloxGoalItemCandidate* Best = BestItem(In);
		if (!Best || In.Current.Type != EVeloxBotGoal::Item)
		{
			return Best;
		}
		const FVeloxGoalItemCandidate* Current = FindById(In, In.Current.ItemId);
		if (!Current || !IsChoosable(*Current) || Current == Best)
		{
			return Best;
		}
		return Best->Score > Current->Score * Params.GoalSwitchHysteresis ? Best : Current;
	}

	FVeloxBotGoalChoice MakeItemGoal(const FVeloxGoalItemCandidate& C, EVeloxBotGoal Type)
	{
		FVeloxBotGoalChoice Goal;
		Goal.Type = Type;
		Goal.ItemId = C.CandidateId;
		Goal.Point = C.Location;
		Goal.Score = C.Score;
		return Goal;
	}

	const FVeloxGoalItemCandidate* DuelCampItem(const FVeloxBotDecisionInput& In)
	{
		const FVeloxGoalItemCandidate* Best = nullptr;
		float BestLead = 0.f;
		for (const FVeloxGoalItemCandidate& C : In.Items)
		{
			if (!C.bMajorItem || C.bAvailable || !C.bReachable)
			{
				continue;
			}
			const float Lead = C.TimeUntilRespawn - C.TravelSeconds;
			if (!Best || Lead < BestLead || (Lead == BestLead && C.CandidateId < Best->CandidateId))
			{
				Best = &C;
				BestLead = Lead;
			}
		}
		return Best;
	}
}

namespace VeloxBotDecision
{
	float FightScore(const FVeloxBotDecisionInput& In)
	{
		const float Strength = In.Fight.FullStrength > 0.f
			? FMath::Clamp(static_cast<float>(In.Health + In.Armor) / In.Fight.FullStrength, 0.f, 1.f)
			: 1.f;
		const float WeaponFactor = FMath::Lerp(In.Fight.WeakWeaponScale, 1.f, FMath::Clamp(In.WeaponStrength, 0.f, 1.f));
		return In.Fight.BaseScore * Strength * WeaponFactor;
	}

	FVeloxBotGoalChoice ChooseGoal(const FVeloxBotDecisionInput& In, const FVeloxBotDifficultyParams& Params, FRandomStream& Stream)
	{
		FVeloxBotGoalChoice Goal;
		if (In.bPostMatch)
		{
			return Goal;
		}

		const FVeloxGoalItemCandidate* Item = VeloxBotDecisionPrivate::ItemWithHysteresis(In, Params);

		if (In.bEnemyVisible)
		{
			const float Fight = FightScore(In);
			const float BiasedFight = Params.FightItemBias * Fight;
			// ADR 0005 D13: the Fight <-> Item switch needs the GoalSwitchHysteresis margin in both directions.
			bool bItemWins = false;
			if (Item)
			{
				switch (In.Current.Type)
				{
				case EVeloxBotGoal::Item:
					bItemWins = !(BiasedFight > Item->Score * Params.GoalSwitchHysteresis);
					break;
				case EVeloxBotGoal::Fight:
					bItemWins = Item->Score > BiasedFight * Params.GoalSwitchHysteresis;
					break;
				default:
					bItemWins = Item->Score > BiasedFight;
					break;
				}
			}
			if (bItemWins)
			{
				return VeloxBotDecisionPrivate::MakeItemGoal(*Item, EVeloxBotGoal::Item);
			}
			Goal.Type = EVeloxBotGoal::Fight;
			Goal.EnemyId = In.EnemyId;
			Goal.Point = In.EnemyLocation;
			Goal.Score = Fight;
			return Goal;
		}

		if (Item)
		{
			return VeloxBotDecisionPrivate::MakeItemGoal(*Item, EVeloxBotGoal::Item);
		}

		if (In.bDuel)
		{
			if (const FVeloxGoalItemCandidate* Camp = VeloxBotDecisionPrivate::DuelCampItem(In))
			{
				return VeloxBotDecisionPrivate::MakeItemGoal(*Camp, EVeloxBotGoal::Roam);
			}
		}

		if (In.Current.Type == EVeloxBotGoal::Roam && In.Current.ItemId == INDEX_NONE)
		{
			return In.Current;
		}

		if (In.RoamPoints.Num() > 0)
		{
			Goal.Type = EVeloxBotGoal::Roam;
			Goal.Point = In.RoamPoints[Stream.RandRange(0, In.RoamPoints.Num() - 1)];
		}
		return Goal;
	}
}
