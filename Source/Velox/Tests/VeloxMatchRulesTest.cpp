// Velox - match phase rules (ADR 0004 sections 5 and 10, task 05).
//   Velox.GameMode.MatchRules        world-free transition table: frag limit, time limit with a leader, tie -> Overtime
//                                    (Duel) / draw (FFA), sudden death, timed overtime repeat, countdown requires ready in
//                                    Duel and not in FFA, AI counts as ready, countdown aborts below MinPlayers, forfeit,
//                                    intermission end / all ready, empty server, phase names.
//   Velox.GameMode.ModeDefinitions   DA_GameMode_FFA / DA_GameMode_Duel carry the ADR 0004 defaults and map to the rules.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxMatchEffects.h"
#include "GameModes/VeloxMatchRules.h"

namespace VeloxMatchRulesTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FVeloxMatchRulesConfig FFA()
	{
		FVeloxMatchRulesConfig Rules;
		Rules.FragLimit = 20;
		Rules.TimeLimitSeconds = 600;
		Rules.MinPlayersToStart = 2;
		Rules.bRequireReady = false;
		Rules.WarmupCountdownSeconds = 20.f;
		Rules.OvertimeRule = EVeloxOvertimeRule::None;
		Rules.PostMatchSeconds = 15.f;
		Rules.bLeaverForfeits = false;
		return Rules;
	}

	FVeloxMatchRulesConfig Duel()
	{
		FVeloxMatchRulesConfig Rules = FFA();
		Rules.FragLimit = 0;
		Rules.bRequireReady = true;
		Rules.OvertimeRule = EVeloxOvertimeRule::SuddenDeath;
		Rules.bLeaverForfeits = true;
		return Rules;
	}

	FVeloxMatchPlayer Player(int32 Id, int32 Score, bool bReady = false, bool bHuman = true)
	{
		FVeloxMatchPlayer P;
		P.Id = Id;
		P.Score = Score;
		P.bReady = bReady;
		P.bHuman = bHuman;
		return P;
	}

	FVeloxMatchInput Input(const FVeloxMatchRulesConfig& Rules, EVeloxMatchPhase Phase, double Now, double PhaseEnd, TArray<FVeloxMatchPlayer> Players)
	{
		FVeloxMatchInput In;
		In.Rules = Rules;
		In.Phase = Phase;
		In.Now = Now;
		In.PhaseEndTime = PhaseEnd;
		In.Players = MoveTemp(Players);
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMatchRulesTest, "Velox.GameMode.MatchRules", VeloxMatchRulesTest::Flags)

bool FVeloxMatchRulesTest::RunTest(const FString& Parameters)
{
	using VeloxMatchRulesTest::Duel;
	using VeloxMatchRulesTest::FFA;
	using VeloxMatchRulesTest::Input;
	using VeloxMatchRulesTest::Player;
	using EP = EVeloxMatchPhase;
	using ER = EVeloxMatchReason;

	auto Expect = [this](const TCHAR* What, const FVeloxMatchVerdict& V, bool bChange, EP Phase = EP::Warmup, ER Reason = ER::None)
	{
		TestEqual(FString::Printf(TEXT("%s: change"), What), V.bChange, bChange);
		if (bChange && V.bChange)
		{
			TestEqual(FString::Printf(TEXT("%s: phase"), What), LexToString(V.NewPhase), LexToString(Phase));
			TestEqual(FString::Printf(TEXT("%s: reason"), What), LexToString(V.Reason), LexToString(Reason));
		}
	};

	// --- Warmup -> Countdown: FFA needs players only; Duel needs every human ready; AI counts as ready.
	{
		const FVeloxMatchVerdict One = VeloxMatchRules::Evaluate(Input(FFA(), EP::Warmup, 10.0, 0.0, {Player(1, 0)}));
		Expect(TEXT("FFA one player stays in warmup"), One, false);

		const FVeloxMatchVerdict Two = VeloxMatchRules::Evaluate(Input(FFA(), EP::Warmup, 10.0, 0.0, {Player(1, 3), Player(2, 0)}));
		Expect(TEXT("FFA two unready players -> countdown"), Two, true, EP::Countdown, ER::PlayersReady);
		TestEqual(TEXT("countdown ends WarmupCountdownSeconds later"), Two.PhaseEndTime, 30.0);
		TestFalse(TEXT("countdown does not reset the level"), Two.bResetLevel);

		Expect(TEXT("Duel two unready players stay in warmup"), VeloxMatchRules::Evaluate(Input(Duel(), EP::Warmup, 10.0, 0.0, {Player(1, 0), Player(2, 0)})), false);
		Expect(TEXT("Duel one ready of two stays in warmup"), VeloxMatchRules::Evaluate(Input(Duel(), EP::Warmup, 10.0, 0.0, {Player(1, 0, true), Player(2, 0)})), false);
		Expect(TEXT("Duel both ready -> countdown"), VeloxMatchRules::Evaluate(Input(Duel(), EP::Warmup, 10.0, 0.0, {Player(1, 0, true), Player(2, 0, true)})), true, EP::Countdown,
			ER::PlayersReady);
		Expect(TEXT("Duel ready human vs unready bot -> countdown"),
			VeloxMatchRules::Evaluate(Input(Duel(), EP::Warmup, 10.0, 0.0, {Player(1, 0, true), Player(2, 0, false, /*bHuman*/ false)})), true, EP::Countdown, ER::PlayersReady);
		Expect(TEXT("Duel ready human alone stays in warmup"), VeloxMatchRules::Evaluate(Input(Duel(), EP::Warmup, 10.0, 0.0, {Player(1, 0, true)})), false);
	}

	// --- Countdown: aborts below MinPlayers or when a Duel player un-readies; ends into Live with a reset.
	{
		Expect(TEXT("countdown aborts below MinPlayers"), VeloxMatchRules::Evaluate(Input(FFA(), EP::Countdown, 15.0, 30.0, {Player(1, 0)})), true, EP::Warmup, ER::StartConditionLost);
		Expect(TEXT("Duel countdown aborts on un-ready"), VeloxMatchRules::Evaluate(Input(Duel(), EP::Countdown, 15.0, 30.0, {Player(1, 0, true), Player(2, 0, false)})), true, EP::Warmup,
			ER::StartConditionLost);
		Expect(TEXT("countdown running"), VeloxMatchRules::Evaluate(Input(FFA(), EP::Countdown, 29.5, 30.0, {Player(1, 0), Player(2, 0)})), false);
		const FVeloxMatchVerdict Done = VeloxMatchRules::Evaluate(Input(FFA(), EP::Countdown, 30.0, 30.0, {Player(1, 4), Player(2, 0)}));
		Expect(TEXT("countdown end -> Live"), Done, true, EP::Live, ER::CountdownDone);
		TestTrue(TEXT("countdown end resets the level"), Done.bResetLevel);
		TestEqual(TEXT("Live ends TimeLimit later"), Done.PhaseEndTime, 630.0);
		FVeloxMatchRulesConfig NoLimit = FFA();
		NoLimit.TimeLimitSeconds = 0;
		TestEqual(TEXT("no time limit = open-ended Live"), VeloxMatchRules::Evaluate(Input(NoLimit, EP::Countdown, 30.0, 30.0, {Player(1, 0), Player(2, 0)})).PhaseEndTime, 0.0);
	}

	// --- Frag limit ends at once (Live and Overtime); below it nothing happens.
	{
		Expect(TEXT("below frag limit"), VeloxMatchRules::Evaluate(Input(FFA(), EP::Live, 100.0, 630.0, {Player(1, 19), Player(2, 5), Player(3, -1)})), false);
		const FVeloxMatchVerdict Hit = VeloxMatchRules::Evaluate(Input(FFA(), EP::Live, 100.0, 630.0, {Player(1, 19), Player(2, 20), Player(3, -1)}));
		Expect(TEXT("frag limit reached"), Hit, true, EP::PostMatch, ER::FragLimit);
		TestEqual(TEXT("frag limit winner"), Hit.WinnerId, 2);
		TestEqual(TEXT("intermission length"), Hit.PhaseEndTime, 115.0);
		TestFalse(TEXT("PostMatch keeps the level (frozen)"), Hit.bResetLevel);
		Expect(TEXT("Duel has no frag limit"), VeloxMatchRules::Evaluate(Input(Duel(), EP::Live, 100.0, 630.0, {Player(1, 50), Player(2, 0)})), false);
	}

	// --- Time limit: unique leader ends; tie -> Draw (FFA) or Overtime (Duel); sudden-death frag ends; timed overtime repeats.
	{
		const FVeloxMatchVerdict Leader = VeloxMatchRules::Evaluate(Input(FFA(), EP::Live, 630.0, 630.0, {Player(1, 7), Player(2, 9), Player(3, 9 - 1)}));
		Expect(TEXT("time limit with a leader"), Leader, true, EP::PostMatch, ER::TimeLimit);
		TestEqual(TEXT("time limit winner"), Leader.WinnerId, 2);
		Expect(TEXT("before the time limit"), VeloxMatchRules::Evaluate(Input(FFA(), EP::Live, 629.9, 630.0, {Player(1, 7), Player(2, 9)})), false);

		const FVeloxMatchVerdict Draw = VeloxMatchRules::Evaluate(Input(FFA(), EP::Live, 630.0, 630.0, {Player(1, 9), Player(2, 9), Player(3, 2)}));
		Expect(TEXT("FFA tie ends as a draw"), Draw, true, EP::PostMatch, ER::Draw);
		TestEqual(TEXT("draw has no winner"), Draw.WinnerId, static_cast<int32>(INDEX_NONE));

		const FVeloxMatchVerdict Overtime = VeloxMatchRules::Evaluate(Input(Duel(), EP::Live, 630.0, 630.0, {Player(1, 4), Player(2, 4)}));
		Expect(TEXT("Duel tie -> Overtime"), Overtime, true, EP::Overtime, ER::Overtime);
		TestEqual(TEXT("sudden death is open-ended"), Overtime.PhaseEndTime, 0.0);

		Expect(TEXT("sudden death, still tied"), VeloxMatchRules::Evaluate(Input(Duel(), EP::Overtime, 700.0, 0.0, {Player(1, 4), Player(2, 4)})), false);
		const FVeloxMatchVerdict Sudden = VeloxMatchRules::Evaluate(Input(Duel(), EP::Overtime, 700.0, 0.0, {Player(1, 4), Player(2, 5)}));
		Expect(TEXT("sudden-death frag ends"), Sudden, true, EP::PostMatch, ER::SuddenDeath);
		TestEqual(TEXT("sudden-death winner"), Sudden.WinnerId, 2);
		const FVeloxMatchVerdict Suicide = VeloxMatchRules::Evaluate(Input(Duel(), EP::Overtime, 700.0, 0.0, {Player(1, 3), Player(2, 4)}));
		TestEqual(TEXT("sudden-death suicide hands the win over"), Suicide.WinnerId, 2);

		FVeloxMatchRulesConfig Timed = Duel();
		Timed.OvertimeRule = EVeloxOvertimeRule::Timed;
		Timed.OvertimeSeconds = 120.f;
		const FVeloxMatchVerdict First = VeloxMatchRules::Evaluate(Input(Timed, EP::Live, 630.0, 630.0, {Player(1, 4), Player(2, 4)}));
		Expect(TEXT("timed overtime"), First, true, EP::Overtime, ER::Overtime);
		TestEqual(TEXT("timed overtime end"), First.PhaseEndTime, 750.0);
		Expect(TEXT("timed overtime, frag inside does not end it"), VeloxMatchRules::Evaluate(Input(Timed, EP::Overtime, 700.0, 750.0, {Player(1, 4), Player(2, 5)})), false);
		const FVeloxMatchVerdict Again = VeloxMatchRules::Evaluate(Input(Timed, EP::Overtime, 750.0, 750.0, {Player(1, 5), Player(2, 5)}));
		Expect(TEXT("timed overtime repeats on a tie"), Again, true, EP::Overtime, ER::Overtime);
		TestEqual(TEXT("next overtime end"), Again.PhaseEndTime, 870.0);
		Expect(TEXT("timed overtime ends with a leader"), VeloxMatchRules::Evaluate(Input(Timed, EP::Overtime, 750.0, 750.0, {Player(1, 6), Player(2, 5)})), true, EP::PostMatch,
			ER::TimeLimit);
	}

	// --- Forfeit (Duel) and leavers in FFA; empty server.
	{
		FVeloxMatchInput Left = Input(Duel(), EP::Live, 200.0, 630.0, {Player(7, 0)});
		Left.bActivePlayerLeft = true;
		const FVeloxMatchVerdict Forfeit = VeloxMatchRules::Evaluate(Left);
		Expect(TEXT("Duel leaver forfeits"), Forfeit, true, EP::PostMatch, ER::Forfeit);
		TestEqual(TEXT("remaining player wins"), Forfeit.WinnerId, 7);
		Left.Phase = EP::Overtime;
		Expect(TEXT("Duel leaver forfeits in overtime"), VeloxMatchRules::Evaluate(Left), true, EP::PostMatch, ER::Forfeit);

		FVeloxMatchInput FFALeft = Input(FFA(), EP::Live, 200.0, 630.0, {Player(7, 0)});
		FFALeft.bActivePlayerLeft = true;
		Expect(TEXT("FFA leaver: match goes on"), VeloxMatchRules::Evaluate(FFALeft), false);

		FVeloxMatchInput Warm = Input(Duel(), EP::Warmup, 200.0, 0.0, {Player(7, 0)});
		Warm.bActivePlayerLeft = true;
		Expect(TEXT("leaving in warmup is no forfeit"), VeloxMatchRules::Evaluate(Warm), false);

		const FVeloxMatchVerdict Empty = VeloxMatchRules::Evaluate(Input(FFA(), EP::Live, 200.0, 630.0, {}));
		Expect(TEXT("empty Live -> Warmup"), Empty, true, EP::Warmup, ER::Empty);
		TestTrue(TEXT("empty server resets"), Empty.bResetLevel);
	}

	// --- PostMatch: intermission timer, all humans ready, bots alone do not skip it.
	{
		Expect(TEXT("intermission running"), VeloxMatchRules::Evaluate(Input(FFA(), EP::PostMatch, 110.0, 115.0, {Player(1, 20), Player(2, 3)})), false);
		const FVeloxMatchVerdict Done = VeloxMatchRules::Evaluate(Input(FFA(), EP::PostMatch, 115.0, 115.0, {Player(1, 20), Player(2, 3)}));
		Expect(TEXT("intermission over -> Warmup"), Done, true, EP::Warmup, ER::IntermissionDone);
		TestTrue(TEXT("restart resets the level"), Done.bResetLevel);
		Expect(TEXT("every human ready ends the intermission"),
			VeloxMatchRules::Evaluate(Input(FFA(), EP::PostMatch, 105.0, 115.0, {Player(1, 20, true), Player(2, 3, false, false)})), true, EP::Warmup, ER::AllReady);
		Expect(TEXT("bots alone do not skip the intermission"),
			VeloxMatchRules::Evaluate(Input(FFA(), EP::PostMatch, 105.0, 115.0, {Player(1, 20, false, false), Player(2, 3, false, false)})), false);
	}

	// --- Helpers and names.
	{
		TestEqual(TEXT("unique leader"), VeloxMatchRules::UniqueLeader({Player(1, -2), Player(2, -1)}), 2);
		TestEqual(TEXT("tie has no leader"), VeloxMatchRules::UniqueLeader({Player(1, 3), Player(2, 3)}), static_cast<int32>(INDEX_NONE));
		TestEqual(TEXT("no players no leader"), VeloxMatchRules::UniqueLeader({}), static_cast<int32>(INDEX_NONE));
		TestEqual(TEXT("top score with negatives"), VeloxMatchRules::TopScore({Player(1, -2), Player(2, -1)}), -1);
		EVeloxMatchPhase Parsed = EP::Warmup;
		TestTrue(TEXT("parse postmatch"), LexTryParseString(Parsed, TEXT("postmatch")) && Parsed == EP::PostMatch);
		TestFalse(TEXT("parse garbage"), LexTryParseString(Parsed, TEXT("Halftime")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxModeDefinitionsTest, "Velox.GameMode.ModeDefinitions", VeloxMatchRulesTest::Flags)

bool FVeloxModeDefinitionsTest::RunTest(const FString& Parameters)
{
	const UVeloxGameModeDefinition* FFA = LoadObject<UVeloxGameModeDefinition>(nullptr, TEXT("/Game/Data/DA_GameMode_FFA.DA_GameMode_FFA"));
	const UVeloxGameModeDefinition* Duel = LoadObject<UVeloxGameModeDefinition>(nullptr, TEXT("/Game/Data/DA_GameMode_Duel.DA_GameMode_Duel"));
	if (!TestNotNull(TEXT("DA_GameMode_FFA (Tools/editor/create_item_content.py)"), FFA) || !TestNotNull(TEXT("DA_GameMode_Duel"), Duel))
	{
		return false;
	}
	// ADR 0004 section 5 defaults (open question 3 pending).
	TestEqual(TEXT("FFA tag"), FFA->ModeTag, VeloxMatchTags::Mode_FFA.GetTag());
	TestEqual(TEXT("FFA frag limit"), FFA->FragLimit, 20);
	TestEqual(TEXT("FFA time limit"), FFA->TimeLimitSeconds, 600);
	TestEqual(TEXT("FFA min players"), FFA->MinPlayersToStart, 2);
	TestEqual(TEXT("FFA max active"), FFA->MaxActivePlayers, 16);
	TestFalse(TEXT("FFA no ready-up"), FFA->bRequireReady);
	TestEqual(TEXT("FFA overtime"), static_cast<int32>(FFA->OvertimeRule), static_cast<int32>(EVeloxOvertimeRule::None));
	TestFalse(TEXT("FFA no forfeit"), FFA->bLeaverForfeits);
	TestTrue(TEXT("FFA joins during a match"), FFA->bAllowJoinDuringMatch);

	TestEqual(TEXT("Duel tag"), Duel->ModeTag, VeloxMatchTags::Mode_Duel.GetTag());
	TestEqual(TEXT("Duel frag limit"), Duel->FragLimit, 0);
	TestEqual(TEXT("Duel time limit"), Duel->TimeLimitSeconds, 600);
	TestEqual(TEXT("Duel max active"), Duel->MaxActivePlayers, 2);
	TestTrue(TEXT("Duel ready-up"), Duel->bRequireReady);
	TestEqual(TEXT("Duel overtime"), static_cast<int32>(Duel->OvertimeRule), static_cast<int32>(EVeloxOvertimeRule::SuddenDeath));
	TestTrue(TEXT("Duel forfeit"), Duel->bLeaverForfeits);
	TestFalse(TEXT("Duel joins only between matches"), Duel->bAllowJoinDuringMatch);

	for (const UVeloxGameModeDefinition* Def : {FFA, Duel})
	{
		TestEqual(TEXT("countdown"), Def->WarmupCountdownSeconds, 20.f);
		TestEqual(TEXT("intermission"), Def->PostMatchSeconds, 15.f);
		TestEqual(TEXT("weapon respawn"), Def->WeaponRespawnSeconds, 5.f);
		TestEqual(TEXT("item respawn scale"), Def->ItemRespawnScale, 1.f);
		TestFalse(TEXT("weapon stay off"), Def->bWeaponStay);
		TestEqual(TEXT("item timers"), static_cast<int32>(Def->ItemTimers), static_cast<int32>(EVeloxItemTimerVisibility::Spectators));
		const FVeloxMatchRulesConfig Rules = Def->MakeRulesConfig();
		TestEqual(TEXT("rules frag limit"), Rules.FragLimit, Def->FragLimit);
		TestEqual(TEXT("rules ready"), Rules.bRequireReady, Def->bRequireReady);
		TestEqual(TEXT("rules forfeit"), Rules.bLeaverForfeits, Def->bLeaverForfeits);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
