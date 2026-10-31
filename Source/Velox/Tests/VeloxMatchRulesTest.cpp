// Velox - match phase rules (ADR 0004 sections 5 and 10, task 05).
//   Velox.GameMode.MatchRules        world-free transition table: frag limit, time limit with a leader, tie -> Overtime
//                                    (Duel) / draw (FFA), sudden death, timed overtime repeat, countdown requires ready in
//                                    Duel and not in FFA, AI counts as ready, countdown aborts below MinPlayers, forfeit,
//                                    intermission end / all ready, empty server, phase names.
//   Velox.GameMode.KillFeed          (task 06) sequence wrap, ordering, damage tag choice; PlayerDied -> Kills/Deaths/Score, feed
//                                    entry with snapshotted names, trimming to KillFeedLength, Reset zeroes.
//   Velox.GameMode.ModeDefinitions   DA_GameMode_FFA / DA_GameMode_Duel carry the ADR 0004 defaults and map to the rules;
//                                    C11: DeathCamera FreeSpectate, Duel ForceRespawnDelay 5 via DA_CombatSettings_Duel (drift guard);
//                                    amendment 3 (ii): ?SpectatorOnly=1 logins get AVeloxPlayerController (CDO).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/VeloxCombatSettings.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/VeloxGameMode.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxKillFeed.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
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

	/** A world with an AVeloxGameMode (match rules off: no definition) and so a GameState. */
	class FKillFeedTestWorld
	{
	public:
		FKillFeedTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxKillFeedTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->AddToRoot();
			Context.OwningGameInstance = GameInstance;
			World->SetGameInstance(GameInstance);
			World->GetWorldSettings()->DefaultGameMode = AVeloxGameMode::StaticClass();
			World->SetGameMode(FURL());
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FKillFeedTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::LevelTransition);
				GEngine->ShutdownWorldNetDriver(World);
				World->DestroyWorld(true);
				World->SetPhysicsScene(nullptr);
				GEngine->DestroyWorldContext(World);
				World->RemoveFromRoot();
				World = nullptr;
			}
			if (GameInstance)
			{
				GameInstance->RemoveFromRoot();
			}
		}

		AVeloxPlayerState* NewPlayer(const TCHAR* Name)
		{
			AVeloxPlayerState* PlayerState = World->SpawnActor<AVeloxPlayerState>();
			if (PlayerState)
			{
				PlayerState->SetPlayerName(Name);
			}
			return PlayerState;
		}

		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;
	};

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

	// C11 amendment 3 (ii): AGameModeBase::SpawnPlayerController uses ReplaySpectatorPlayerControllerClass for ?SpectatorOnly=1.
	{
		const AVeloxGameMode* GameModeCDO = GetDefault<AVeloxGameMode>();
		TestTrue(*FString::Printf(TEXT("spectator controller class = AVeloxPlayerController (%s)"), *GetNameSafe(GameModeCDO->ReplaySpectatorPlayerControllerClass.Get())),
			GameModeCDO->ReplaySpectatorPlayerControllerClass.Get() == AVeloxPlayerController::StaticClass());
		TestTrue(TEXT("player controller class = AVeloxPlayerController"), GameModeCDO->PlayerControllerClass.Get() == AVeloxPlayerController::StaticClass());
	}

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
		// ADR 0004 C11 (q16): free spectate in both modes.
		TestEqual(TEXT("death camera FreeSpectate"), static_cast<int32>(Def->DeathCamera), static_cast<int32>(EVeloxDeathCamera::FreeSpectate));
	}

	// C11 Duel decision (2026-10-31): Duel force-respawns after 5 s through its combat settings override; FFA keeps the
	// default (null override, 20 s).
	TestNull(TEXT("FFA has no combat settings override"), FFA->CombatSettingsOverride.Get());
	TestEqual(TEXT("default ForceRespawnDelay (class defaults)"), UVeloxCombatSettings::GetOrDefault(nullptr).ForceRespawnDelay, 20.f);
	const UVeloxCombatSettings* DefaultSettings = LoadObject<UVeloxCombatSettings>(nullptr, TEXT("/Game/Data/DA_CombatSettings_Default.DA_CombatSettings_Default"));
	const UVeloxCombatSettings* DuelSettings = Duel->CombatSettingsOverride.Get();
	if (!TestNotNull(TEXT("DA_CombatSettings_Default"), DefaultSettings) || !TestNotNull(TEXT("Duel combat settings override (DA_CombatSettings_Duel)"), DuelSettings))
	{
		return false;
	}
	TestEqual(TEXT("FFA (default asset) ForceRespawnDelay"), DefaultSettings->ForceRespawnDelay, 20.f);
	TestEqual(TEXT("Duel override is DA_CombatSettings_Duel"), DuelSettings->GetName(), FString(TEXT("DA_CombatSettings_Duel")));
	TestEqual(TEXT("Duel ForceRespawnDelay"), DuelSettings->ForceRespawnDelay, 5.f);
	// Drift guard: the Duel copy differs from the default asset in ForceRespawnDelay only (create_bootstrap_content.py
	// re-copies everything else on each run).
	const FName ForceRespawnDelayName = GET_MEMBER_NAME_CHECKED(UVeloxCombatSettings, ForceRespawnDelay);
	int32 NumCompared = 0;
	for (TFieldIterator<FProperty> It(UVeloxCombatSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		if (It->GetFName() == ForceRespawnDelayName)
		{
			continue;
		}
		++NumCompared;
		TestTrue(*FString::Printf(TEXT("Duel settings %s equal the default asset"), *It->GetName()), It->Identical_InContainer(DuelSettings, DefaultSettings));
	}
	TestTrue(TEXT("drift guard compared the settings"), NumCompared > 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxKillFeedTest, "Velox.GameMode.KillFeed", VeloxMatchRulesTest::Flags)

bool FVeloxKillFeedTest::RunTest(const FString& Parameters)
{
	// World-free helpers.
	TestTrue(TEXT("SeqLess 1 < 2"), VeloxKillFeed::SeqLess(1, 2));
	TestFalse(TEXT("SeqLess 2 < 1"), VeloxKillFeed::SeqLess(2, 1));
	TestFalse(TEXT("SeqLess equal"), VeloxKillFeed::SeqLess(5, 5));
	TestTrue(TEXT("SeqLess wraps: 65535 < 0"), VeloxKillFeed::SeqLess(65535, 0));
	TestTrue(TEXT("SeqLess wraps: 65530 < 3"), VeloxKillFeed::SeqLess(65530, 3));
	{
		TArray<FVeloxKillFeedEntry> Entries;
		for (const uint16 Seq : {static_cast<uint16>(1), static_cast<uint16>(65534), static_cast<uint16>(0), static_cast<uint16>(65535)})
		{
			Entries.AddDefaulted_GetRef().Seq = Seq;
		}
		const TArray<FVeloxKillFeedEntry> Sorted = VeloxKillFeed::SortedBySeq(Entries);
		TestEqual(TEXT("sorted count"), Sorted.Num(), 4);
		if (Sorted.Num() == 4)
		{
			TestEqual(TEXT("sorted[0] = 65534"), static_cast<int32>(Sorted[0].Seq), 65534);
			TestEqual(TEXT("sorted[1] = 65535"), static_cast<int32>(Sorted[1].Seq), 65535);
			TestEqual(TEXT("sorted[2] = 0"), static_cast<int32>(Sorted[2].Seq), 0);
			TestEqual(TEXT("sorted[3] = 1"), static_cast<int32>(Sorted[3].Seq), 1);
		}
		TestEqual(TEXT("oldest index = the 65534 entry"), VeloxKillFeed::OldestIndex(Entries), 1);
		TestEqual(TEXT("oldest of nothing"), VeloxKillFeed::OldestIndex(TArray<FVeloxKillFeedEntry>()), static_cast<int32>(INDEX_NONE));
	}
	{
		FGameplayTagContainer Tags(VeloxGameplayTags::Damage_Self);
		Tags.AddTag(VeloxGameplayTags::Damage_Rocket);
		TestTrue(TEXT("tag: weapon wins over Self"), VeloxKillFeed::PickDamageTag(Tags) == VeloxGameplayTags::Damage_Rocket.GetTag());
		TestTrue(TEXT("tag: Self alone is kept"), VeloxKillFeed::PickDamageTag(FGameplayTagContainer(VeloxGameplayTags::Damage_Self)) == VeloxGameplayTags::Damage_Self.GetTag());
		TestFalse(TEXT("tag: none"), VeloxKillFeed::PickDamageTag(FGameplayTagContainer()).IsValid());
	}

	// PlayerDied -> counters, score and feed (server path).
	VeloxMatchRulesTest::FKillFeedTestWorld W;
	AVeloxGameMode* GameMode = W.World->GetAuthGameMode<AVeloxGameMode>();
	AVeloxGameState* GameState = GameMode ? GameMode->GetGameState<AVeloxGameState>() : nullptr;
	if (!TestNotNull(TEXT("game mode"), GameMode) || !TestNotNull(TEXT("game state"), GameState))
	{
		return false;
	}
	AVeloxPlayerState* Alice = W.NewPlayer(TEXT("Alice"));
	AVeloxPlayerState* Bob = W.NewPlayer(TEXT("Bob"));
	if (!TestNotNull(TEXT("players"), Alice) || !TestNotNull(TEXT("players"), Bob))
	{
		return false;
	}
	TArray<FVeloxKillFeedEntry> Seen;
	GameState->OnKillFeedEntry.AddLambda([&Seen](const FVeloxKillFeedEntry& Entry) { Seen.Add(Entry); });
	int32 BotEvents = 0;
	GameState->OnKill.AddLambda([&BotEvents](const FVeloxKillEvent&) { ++BotEvents; });

	GameMode->PlayerDied(Bob, Alice, FGameplayTagContainer(VeloxGameplayTags::Damage_Rail));
	TestEqual(TEXT("Alice kills"), Alice->GetKills(), 1);
	TestEqual(TEXT("Alice deaths"), Alice->GetDeaths(), 0);
	TestEqual(TEXT("Bob deaths"), Bob->GetDeaths(), 1);
	TestEqual(TEXT("Alice score"), Alice->GetScore(), 1.f);
	TestEqual(TEXT("OnKill (server-only event) still fires"), BotEvents, 1);
	TestEqual(TEXT("feed delegate fired once on the server"), Seen.Num(), 1);
	if (Seen.Num() == 1)
	{
		TestEqual(TEXT("entry killer"), Seen[0].KillerName, FString(TEXT("Alice")));
		TestEqual(TEXT("entry victim"), Seen[0].VictimName, FString(TEXT("Bob")));
		TestTrue(TEXT("entry tag"), Seen[0].DamageTag == VeloxGameplayTags::Damage_Rail.GetTag());
		TestFalse(TEXT("entry not suicide"), Seen[0].bSuicide);
	}

	// Suicide: a death and -1, no kill; killer name = victim name.
	GameMode->PlayerDied(Bob, nullptr, FGameplayTagContainer(VeloxGameplayTags::Damage_Rocket));
	TestEqual(TEXT("suicide: no kill"), Bob->GetKills(), 0);
	TestEqual(TEXT("suicide: deaths"), Bob->GetDeaths(), 2);
	TestEqual(TEXT("suicide: score -1"), Bob->GetScore(), -1.f);
	if (Seen.Num() == 2)
	{
		TestTrue(TEXT("suicide entry flagged"), Seen[1].bSuicide);
		TestEqual(TEXT("suicide entry killer = victim"), Seen[1].KillerName, FString(TEXT("Bob")));
		TestTrue(TEXT("suicide entry has the weapon tag"), Seen[1].DamageTag == VeloxGameplayTags::Damage_Rocket.GetTag());
		TestEqual(TEXT("seq increases"), static_cast<int32>(Seen[1].Seq), static_cast<int32>(Seen[0].Seq) + 1);
	}

	// Names are snapshotted: the feed outlives the PlayerState.
	Alice->Destroy();
	{
		const TArray<FVeloxKillFeedEntry> Feed = GameState->GetKillFeedOldestFirst();
		TestEqual(TEXT("feed holds both"), Feed.Num(), 2);
		if (Feed.Num() == 2)
		{
			TestEqual(TEXT("name survives the PlayerState"), Feed[0].KillerName, FString(TEXT("Alice")));
		}
	}

	// Trimming to KillFeedLength (default 8): 12 more frags keep the newest 8, oldest first.
	AVeloxPlayerState* Carol = W.NewPlayer(TEXT("Carol"));
	for (int32 Index = 0; Index < 12; ++Index)
	{
		GameMode->PlayerDied(Bob, Carol, FGameplayTagContainer(VeloxGameplayTags::Damage_Plasma));
	}
	{
		const TArray<FVeloxKillFeedEntry> Feed = GameState->GetKillFeedOldestFirst();
		TestEqual(TEXT("feed trimmed to KillFeedLength"), Feed.Num(), GameState->GetKillFeedLength());
		TestEqual(TEXT("KillFeedLength default"), GameState->GetKillFeedLength(), 8);
		bool bOrdered = true;
		for (int32 Index = 1; Index < Feed.Num(); ++Index)
		{
			bOrdered &= Feed[Index].Seq == static_cast<uint16>(Feed[Index - 1].Seq + 1);
		}
		TestTrue(TEXT("trimmed feed is a contiguous run, oldest first"), bOrdered);
		TestTrue(TEXT("newest entry is the last frag"), !Feed.IsEmpty() && Feed.Last().KillerName == TEXT("Carol") && Feed.Last().Seq == 13);
	}
	TestEqual(TEXT("Carol kills"), Carol->GetKills(), 12);
	TestEqual(TEXT("Bob deaths"), Bob->GetDeaths(), 14);

	// Reset zeroes kills, deaths, score (engine) and ready.
	Carol->SetReady(true);
	Carol->Reset();
	TestEqual(TEXT("Reset: kills"), Carol->GetKills(), 0);
	TestEqual(TEXT("Reset: deaths"), Carol->GetDeaths(), 0);
	TestEqual(TEXT("Reset: score"), Carol->GetScore(), 0.f);
	TestFalse(TEXT("Reset: ready"), Carol->IsReady());
	Bob->Reset();
	TestEqual(TEXT("Reset: Bob deaths"), Bob->GetDeaths(), 0);

	GameState->ClearKillFeed();
	TestTrue(TEXT("ClearKillFeed"), GameState->GetKillFeed().Entries.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
