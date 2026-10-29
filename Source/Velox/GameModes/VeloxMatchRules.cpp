// Velox - match phase rules (ADR 0004 section 5).

#include "GameModes/VeloxMatchRules.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxMatchRules)

DEFINE_LOG_CATEGORY(LogVeloxMatch);

namespace VeloxMatchRulesPrivate
{
	/** A phase end counts as reached this close before it (timer callbacks may run a hair early in world time). */
	constexpr double PhaseEndToleranceSeconds = 1e-3;

	bool IsDue(double PhaseEndTime, double Now)
	{
		return PhaseEndTime > 0.0 && Now + PhaseEndToleranceSeconds >= PhaseEndTime;
	}

	FVeloxMatchVerdict Change(EVeloxMatchPhase NewPhase, EVeloxMatchReason Reason, double PhaseEndTime, bool bResetLevel = false, int32 WinnerId = INDEX_NONE)
	{
		FVeloxMatchVerdict Verdict;
		Verdict.bChange = true;
		Verdict.NewPhase = NewPhase;
		Verdict.Reason = Reason;
		Verdict.PhaseEndTime = PhaseEndTime;
		Verdict.bResetLevel = bResetLevel;
		Verdict.WinnerId = WinnerId;
		return Verdict;
	}
}

const TCHAR* LexToString(EVeloxMatchPhase Phase)
{
	switch (Phase)
	{
	case EVeloxMatchPhase::Warmup: return TEXT("Warmup");
	case EVeloxMatchPhase::Countdown: return TEXT("Countdown");
	case EVeloxMatchPhase::Live: return TEXT("Live");
	case EVeloxMatchPhase::Overtime: return TEXT("Overtime");
	case EVeloxMatchPhase::PostMatch: return TEXT("PostMatch");
	}
	return TEXT("Unknown");
}

const TCHAR* LexToString(EVeloxMatchReason Reason)
{
	switch (Reason)
	{
	case EVeloxMatchReason::None: return TEXT("none");
	case EVeloxMatchReason::PlayersReady: return TEXT("players_ready");
	case EVeloxMatchReason::StartConditionLost: return TEXT("start_condition_lost");
	case EVeloxMatchReason::CountdownDone: return TEXT("countdown_done");
	case EVeloxMatchReason::FragLimit: return TEXT("frag_limit");
	case EVeloxMatchReason::TimeLimit: return TEXT("time_limit");
	case EVeloxMatchReason::Draw: return TEXT("draw");
	case EVeloxMatchReason::Overtime: return TEXT("overtime");
	case EVeloxMatchReason::SuddenDeath: return TEXT("sudden_death");
	case EVeloxMatchReason::Forfeit: return TEXT("forfeit");
	case EVeloxMatchReason::Empty: return TEXT("empty");
	case EVeloxMatchReason::IntermissionDone: return TEXT("intermission_done");
	case EVeloxMatchReason::AllReady: return TEXT("all_ready");
	case EVeloxMatchReason::Forced: return TEXT("forced");
	}
	return TEXT("unknown");
}

bool LexTryParseString(EVeloxMatchPhase& OutPhase, const TCHAR* Name)
{
	for (const EVeloxMatchPhase Phase : {EVeloxMatchPhase::Warmup, EVeloxMatchPhase::Countdown, EVeloxMatchPhase::Live, EVeloxMatchPhase::Overtime, EVeloxMatchPhase::PostMatch})
	{
		if (FCString::Stricmp(Name, LexToString(Phase)) == 0)
		{
			OutPhase = Phase;
			return true;
		}
	}
	return false;
}

namespace VeloxMatchRules
{
	bool AllHumansReady(const TArray<FVeloxMatchPlayer>& Players)
	{
		for (const FVeloxMatchPlayer& Player : Players)
		{
			if (Player.bHuman && !Player.bReady)
			{
				return false;
			}
		}
		return true;
	}

	int32 NumHumans(const TArray<FVeloxMatchPlayer>& Players)
	{
		int32 Count = 0;
		for (const FVeloxMatchPlayer& Player : Players)
		{
			Count += Player.bHuman ? 1 : 0;
		}
		return Count;
	}

	bool CanStartCountdown(const FVeloxMatchRulesConfig& Rules, const TArray<FVeloxMatchPlayer>& Players)
	{
		return Players.Num() >= FMath::Max(Rules.MinPlayersToStart, 1) && (!Rules.bRequireReady || AllHumansReady(Players));
	}

	int32 UniqueLeader(const TArray<FVeloxMatchPlayer>& Players)
	{
		int32 Leader = INDEX_NONE;
		int32 Best = TNumericLimits<int32>::Lowest();
		bool bTie = false;
		for (const FVeloxMatchPlayer& Player : Players)
		{
			if (Player.Score > Best)
			{
				Best = Player.Score;
				Leader = Player.Id;
				bTie = false;
			}
			else if (Player.Score == Best)
			{
				bTie = true;
			}
		}
		return bTie ? INDEX_NONE : Leader;
	}

	int32 TopScore(const TArray<FVeloxMatchPlayer>& Players)
	{
		int32 Best = 0;
		for (int32 Index = 0; Index < Players.Num(); ++Index)
		{
			Best = Index == 0 ? Players[Index].Score : FMath::Max(Best, Players[Index].Score);
		}
		return Best;
	}

	double PhaseEndFor(const FVeloxMatchRulesConfig& Rules, EVeloxMatchPhase NewPhase, double Now)
	{
		switch (NewPhase)
		{
		case EVeloxMatchPhase::Countdown:
			return Now + FMath::Max(Rules.WarmupCountdownSeconds, 0.f);
		case EVeloxMatchPhase::Live:
			return Rules.TimeLimitSeconds > 0 ? Now + Rules.TimeLimitSeconds : 0.0;
		case EVeloxMatchPhase::Overtime:
			return Rules.OvertimeRule == EVeloxOvertimeRule::Timed ? Now + FMath::Max(Rules.OvertimeSeconds, 1.f) : 0.0;
		case EVeloxMatchPhase::PostMatch:
			return Now + FMath::Max(Rules.PostMatchSeconds, 0.f);
		case EVeloxMatchPhase::Warmup:
			break;
		}
		return 0.0;
	}

	FVeloxMatchVerdict Evaluate(const FVeloxMatchInput& Input)
	{
		using namespace VeloxMatchRulesPrivate;
		const FVeloxMatchRulesConfig& Rules = Input.Rules;
		const TArray<FVeloxMatchPlayer>& Players = Input.Players;
		const double Now = Input.Now;

		switch (Input.Phase)
		{
		case EVeloxMatchPhase::Warmup:
			if (CanStartCountdown(Rules, Players))
			{
				return Change(EVeloxMatchPhase::Countdown, EVeloxMatchReason::PlayersReady, PhaseEndFor(Rules, EVeloxMatchPhase::Countdown, Now));
			}
			break;

		case EVeloxMatchPhase::Countdown:
			if (!CanStartCountdown(Rules, Players))
			{
				return Change(EVeloxMatchPhase::Warmup, EVeloxMatchReason::StartConditionLost, 0.0);
			}
			if (IsDue(Input.PhaseEndTime, Now) || Input.PhaseEndTime <= 0.0)
			{
				return Change(EVeloxMatchPhase::Live, EVeloxMatchReason::CountdownDone, PhaseEndFor(Rules, EVeloxMatchPhase::Live, Now), /*bResetLevel*/ true);
			}
			break;

		case EVeloxMatchPhase::Live:
		case EVeloxMatchPhase::Overtime:
		{
			if (Players.IsEmpty())
			{
				return Change(EVeloxMatchPhase::Warmup, EVeloxMatchReason::Empty, 0.0, /*bResetLevel*/ true);
			}
			const double PostMatchEnd = PhaseEndFor(Rules, EVeloxMatchPhase::PostMatch, Now);
			if (Input.bActivePlayerLeft && Rules.bLeaverForfeits && Players.Num() == 1)
			{
				return Change(EVeloxMatchPhase::PostMatch, EVeloxMatchReason::Forfeit, PostMatchEnd, false, Players[0].Id);
			}
			const int32 Leader = UniqueLeader(Players);
			if (Rules.FragLimit > 0 && TopScore(Players) >= Rules.FragLimit)
			{
				return Change(EVeloxMatchPhase::PostMatch, EVeloxMatchReason::FragLimit, PostMatchEnd, false, Leader);
			}
			if (Input.Phase == EVeloxMatchPhase::Overtime && Rules.OvertimeRule == EVeloxOvertimeRule::SuddenDeath && Leader != INDEX_NONE)
			{
				return Change(EVeloxMatchPhase::PostMatch, EVeloxMatchReason::SuddenDeath, PostMatchEnd, false, Leader);
			}
			if (IsDue(Input.PhaseEndTime, Now))
			{
				if (Leader != INDEX_NONE)
				{
					return Change(EVeloxMatchPhase::PostMatch, EVeloxMatchReason::TimeLimit, PostMatchEnd, false, Leader);
				}
				if (Rules.OvertimeRule == EVeloxOvertimeRule::None)
				{
					return Change(EVeloxMatchPhase::PostMatch, EVeloxMatchReason::Draw, PostMatchEnd);
				}
				// Tie: sudden death (open-ended) or one more timed overtime period.
				return Change(EVeloxMatchPhase::Overtime, EVeloxMatchReason::Overtime, PhaseEndFor(Rules, EVeloxMatchPhase::Overtime, Now));
			}
			break;
		}

		case EVeloxMatchPhase::PostMatch:
			if (IsDue(Input.PhaseEndTime, Now) || Input.PhaseEndTime <= 0.0)
			{
				return Change(EVeloxMatchPhase::Warmup, EVeloxMatchReason::IntermissionDone, 0.0, /*bResetLevel*/ true);
			}
			if (NumHumans(Players) > 0 && AllHumansReady(Players))
			{
				return Change(EVeloxMatchPhase::Warmup, EVeloxMatchReason::AllReady, 0.0, /*bResetLevel*/ true);
			}
			break;
		}
		return FVeloxMatchVerdict();
	}
}
