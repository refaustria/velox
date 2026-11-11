// Velox - server hardening (ADR 0007 sections 5.1-5.2, M6 task 07).
//   Velox.Net.NameSanitize          world-free VeloxNames::Sanitize / Dedupe: control, format and bidi characters (RTL
//                                   override attack string, isolates, zero-width, BOM, C1), whitespace collapse and trim,
//                                   24-code-point clamp (200-char name, surrogate pairs never split), empty -> "Player",
//                                   " (2)" / " (3)" suffixes within the limit, case-insensitive.
//   Velox.Net.PasswordRule          world-free AVeloxGameMode::IsPasswordAccepted: unset -> anything passes; set -> equal
//                                   passes, missing / other / longer / case-different fails. VeloxNet::ResolveServerPassword:
//                                   cmdline > url > env, an empty value falls through, nothing -> source none.
//   Velox.Net.NoCheatManagerForRemote  AVeloxGameMode (and FFA / Duel) do not override AGameModeBase::AllowCheats (compile-time
//                                   pin); in a game world with an AVeloxGameMode on a listen-server URL and GIsEditor false (a
//                                   deployed server), a remote AVeloxPlayerController gets no CheatManager, while the same
//                                   spawn in a standalone world does (the fixture can see one). RPC gate: ServerExecRPC /
//                                   ServerPause validation refuses the remote controller (counted, OnRpcRefused) and accepts
//                                   a local one; AllowPausing(remote) is false (bPauseable=false, ADR 0007 F1.2);
//                                   velox.net.AllowTestRpcs 0 refuses a test RPC, 1 accepts it.
//   Velox.Net.NameApply             game world: AVeloxGameMode::ChangeName sanitizes a human's name and dedupes it against the
//                                   other PlayerStates; an AI controller's name is untouched.
//   Velox.Net.AnomalyFormat         world-free VeloxAnomaly::FormatLine (ADR 0007 section 5.4, task 09): key order, cumulative
//                                   values with deltas, rejects{reason:n(+d),...} in first-seen order, 2-decimal shares,
//                                   rtt, exceeds= only when non-empty, player= last (spaces kept); Note/SameCounters/HasAnyCounter.
//   Velox.Net.AnomalyRates          world-free VeloxAnomaly::Rates (late_share = late / max(switches,1), reject_share =
//                                   rejects / max(shots + rejects,1), review 0007-09 F2) and Exceeds: nothing with the default
//                                   (0) thresholds even for a blatant cheater, the names for non-zero thresholds, strict ">",
//                                   the min-sample gates (reject_share's on attempts) and an all-rejected client.
// The network kick itself (RPC_ValidateFailed -> connection closed) and PreLogin's rejection are proven by nettest RpcGateRun /
// PasswordRun; a unit row cannot open a connection.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CheatManager.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/VeloxGameMode.h"
#include "GameModes/VeloxGameMode_Duel.h"
#include "GameModes/VeloxGameMode_FFA.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Net/VeloxAnomalyStats.h"
#include "Net/VeloxNames.h"
#include "Net/VeloxNetCheats.h"

#include <type_traits>

// GIsEditor is a variable only in editor builds (CoreGlobals.h defines it as false otherwise, Game targets compile these tests too).
#if WITH_EDITORONLY_DATA || USING_CODE_ANALYSIS
#define VELOX_NET_TEST_NOT_EDITOR() TGuardValue<bool> NotEditor(GIsEditor, false)
#else
#define VELOX_NET_TEST_NOT_EDITOR() do {} while (0)
#endif

namespace VeloxNetTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr int32 MaxLen = 24;

	// Compile-time pin (ADR 0007 section 5.1): Velox's game modes use the engine's AllowCheats (no UCheatManager for remote
	// players on a networked server). If any of them overrides it, the member pointer's class changes and this fails.
	using FAllowCheatsPtr = bool (AGameModeBase::*)(APlayerController*);
	static_assert(std::is_same_v<decltype(&AVeloxGameMode::AllowCheats), FAllowCheatsPtr>, "AVeloxGameMode must not override AllowCheats (ADR 0007 section 5.1)");
	static_assert(std::is_same_v<decltype(&AVeloxGameMode_FFA::AllowCheats), FAllowCheatsPtr>, "AVeloxGameMode_FFA must not override AllowCheats");
	static_assert(std::is_same_v<decltype(&AVeloxGameMode_Duel::AllowCheats), FAllowCheatsPtr>, "AVeloxGameMode_Duel must not override AllowCheats");

	FString Cp(uint32 CodePoint)
	{
		FString Out;
		VeloxNames::AppendCodePoint(Out, CodePoint);
		return Out;
	}

	/** Private game world with an AVeloxGameMode (the DeathFlow harness pattern, without the floor). */
	struct FNetTestWorld
	{
		FNetTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxNetTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
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
			GameMode = World->GetAuthGameMode<AVeloxGameMode>();
		}

		~FNetTestWorld()
		{
			World->NextURL.Reset();
			World->EndPlay(EEndPlayReason::LevelTransition);
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
			GameInstance->RemoveFromRoot();
		}

		AVeloxPlayerController* SpawnPC()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<AVeloxPlayerController>(AVeloxPlayerController::StaticClass(), FTransform::Identity, Params);
		}

		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;
		AVeloxGameMode* GameMode = nullptr;
	};

	/** Sets an int cvar for the scope. */
	struct FIntCVarScope
	{
		IConsoleVariable* Var = nullptr;
		int32 Old = 0;
		FIntCVarScope(const TCHAR* Name, int32 Value) : Var(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (Var)
			{
				Old = Var->GetInt();
				Var->Set(Value, ECVF_SetByCode);
			}
		}
		~FIntCVarScope()
		{
			if (Var)
			{
				Var->Set(Old, ECVF_SetByCode);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxNetNameSanitizeTest, "Velox.Net.NameSanitize", VeloxNetTest::Flags)

bool FVeloxNetNameSanitizeTest::RunTest(const FString& Parameters)
{
	using namespace VeloxNetTest;
	using VeloxNames::Sanitize;

	TestEqual(TEXT("plain name unchanged"), Sanitize(TEXT("Anarki"), MaxLen), FString(TEXT("Anarki")));
	TestEqual(TEXT("whitespace collapsed and trimmed"), Sanitize(TEXT("   Ana    rki  "), MaxLen), FString(TEXT("Ana rki")));
	TestEqual(TEXT("tab / newline / CR stripped (C0)"), Sanitize(TEXT("Ana\trki\r\n"), MaxLen), FString(TEXT("Anarki")));
	TestEqual(TEXT("DEL and C1 (U+0085, U+009F) stripped"), Sanitize(FString(TEXT("a")) + Cp(0x7F) + Cp(0x85) + Cp(0x9F) + TEXT("b"), MaxLen), FString(TEXT("ab")));
	// RTL override attack: "Evil" + RLO + "gnp.exe" renders as "Evilexe.png"; the override must go.
	TestEqual(TEXT("RTL override attack string"), Sanitize(FString(TEXT("Evil")) + Cp(0x202E) + TEXT("gnp.exe"), MaxLen), FString(TEXT("Evilgnp.exe")));
	TestEqual(TEXT("all embeddings / overrides U+202A-202E stripped"),
		Sanitize(Cp(0x202A) + TEXT("a") + Cp(0x202B) + TEXT("b") + Cp(0x202C) + TEXT("c") + Cp(0x202D) + TEXT("d") + Cp(0x202E), MaxLen), FString(TEXT("abcd")));
	TestEqual(TEXT("isolates U+2066-2069 stripped"), Sanitize(Cp(0x2066) + TEXT("x") + Cp(0x2067) + Cp(0x2068) + TEXT("y") + Cp(0x2069), MaxLen), FString(TEXT("xy")));
	TestEqual(TEXT("zero-width and marks U+200B-200F stripped"),
		Sanitize(FString(TEXT("a")) + Cp(0x200B) + Cp(0x200C) + Cp(0x200D) + Cp(0x200E) + Cp(0x200F) + TEXT("b"), MaxLen), FString(TEXT("ab")));
	TestEqual(TEXT("BOM, soft hyphen, word joiner, tag characters stripped"),
		Sanitize(Cp(0xFEFF) + TEXT("a") + Cp(0xAD) + Cp(0x2060) + TEXT("b") + Cp(0xE0041), MaxLen), FString(TEXT("ab")));
	TestEqual(TEXT("NBSP / ideographic / en space collapse to one space"), Sanitize(FString(TEXT("a")) + Cp(0xA0) + Cp(0x3000) + Cp(0x2002) + TEXT("b"), MaxLen), FString(TEXT("a b")));
	TestEqual(TEXT("empty -> Player"), Sanitize(TEXT(""), MaxLen), FString(TEXT("Player")));
	TestEqual(TEXT("only spaces -> Player"), Sanitize(TEXT("     "), MaxLen), FString(TEXT("Player")));
	TestEqual(TEXT("only bidi controls -> Player"), Sanitize(Cp(0x202E) + Cp(0x2066) + Cp(0x200B), MaxLen), FString(TEXT("Player")));
	TestEqual(TEXT("non-Latin letters kept"), Sanitize(Cp(0x416) + Cp(0x65E5) + Cp(0x672C), MaxLen), Cp(0x416) + Cp(0x65E5) + Cp(0x672C));

	const FString Long200 = FString::ChrN(200, TEXT('x'));
	const FString Clamped = Sanitize(Long200, MaxLen);
	TestEqual(TEXT("200-char name clamped to 24"), Clamped.Len(), MaxLen);
	TestEqual(TEXT("clamp keeps the prefix"), Clamped, FString::ChrN(MaxLen, TEXT('x')));
	TestEqual(TEXT("clamp then trailing space trimmed"), Sanitize(FString::ChrN(23, TEXT('a')) + TEXT(" bcd"), MaxLen), FString::ChrN(23, TEXT('a')));
	TestEqual(TEXT("bidi characters do not count toward the limit"), Sanitize(FString::ChrN(12, TEXT('q')) + Cp(0x202E) + FString::ChrN(12, TEXT('q')), MaxLen),
		FString::ChrN(24, TEXT('q')));

	FString Emoji;
	for (int32 I = 0; I < 30; ++I)
	{
		VeloxNames::AppendCodePoint(Emoji, 0x1F600);
	}
	const FString EmojiClamped = Sanitize(Emoji, MaxLen);
	TestEqual(TEXT("surrogate pairs: 24 code points kept"), VeloxNames::CodePointLen(EmojiClamped), MaxLen);
	TestEqual(TEXT("surrogate pairs: never split"), EmojiClamped.Len(), (sizeof(TCHAR) == 2 ? 2 : 1) * MaxLen);
	if (sizeof(TCHAR) == 2)
	{
		FString Broken = TEXT("a");
		Broken.AppendChar(static_cast<TCHAR>(0xD83D));   // unpaired high surrogate
		Broken += TEXT("b");
		Broken.AppendChar(static_cast<TCHAR>(0xDE00));   // unpaired low surrogate
		TestEqual(TEXT("unpaired surrogates dropped"), Sanitize(Broken, MaxLen), FString(TEXT("ab")));
	}

	// Dedupe
	using VeloxNames::Dedupe;
	TestEqual(TEXT("dedupe: free name unchanged"), Dedupe(TEXT("Anarki"), { TEXT("Visor"), TEXT("Sarge") }, MaxLen), FString(TEXT("Anarki")));
	TestEqual(TEXT("dedupe: taken -> (2)"), Dedupe(TEXT("Anarki"), { TEXT("Anarki") }, MaxLen), FString(TEXT("Anarki (2)")));
	TestEqual(TEXT("dedupe: case-insensitive, (2) taken -> (3)"), Dedupe(TEXT("Anarki"), { TEXT("anarki"), TEXT("ANARKI (2)") }, MaxLen), FString(TEXT("Anarki (3)")));
	const FString Name24 = FString::ChrN(MaxLen, TEXT('z'));
	const FString Deduped24 = Dedupe(Name24, { Name24 }, MaxLen);
	TestEqual(TEXT("dedupe: 24-char name stays within 24"), Deduped24.Len(), MaxLen);
	TestTrue(TEXT("dedupe: 24-char name gets the suffix"), Deduped24.EndsWith(TEXT(" (2)")));
	TestEqual(TEXT("dedupe: Player -> Player (2)"), Dedupe(Sanitize(TEXT(""), MaxLen), { TEXT("Player") }, MaxLen), FString(TEXT("Player (2)")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxNetPasswordRuleTest, "Velox.Net.PasswordRule", VeloxNetTest::Flags)

bool FVeloxNetPasswordRuleTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("unset: no offer passes"), AVeloxGameMode::IsPasswordAccepted(TEXT(""), TEXT("")));
	TestTrue(TEXT("unset: any offer passes"), AVeloxGameMode::IsPasswordAccepted(TEXT(""), TEXT("whatever")));
	TestTrue(TEXT("set: equal passes"), AVeloxGameMode::IsPasswordAccepted(TEXT("hunter2"), TEXT("hunter2")));
	TestFalse(TEXT("set: missing fails"), AVeloxGameMode::IsPasswordAccepted(TEXT("hunter2"), TEXT("")));
	TestFalse(TEXT("set: other (same length) fails"), AVeloxGameMode::IsPasswordAccepted(TEXT("hunter2"), TEXT("hunter3")));
	TestFalse(TEXT("set: longer fails"), AVeloxGameMode::IsPasswordAccepted(TEXT("hunter2"), TEXT("hunter22")));
	TestFalse(TEXT("set: prefix fails"), AVeloxGameMode::IsPasswordAccepted(TEXT("hunter2"), TEXT("hunter")));
	TestFalse(TEXT("set: case matters"), AVeloxGameMode::IsPasswordAccepted(TEXT("hunter2"), TEXT("Hunter2")));

	// Source selection (ADR 0007 amendment F1.3): cmdline > url > env; an empty value at a level falls through.
	using VeloxNet::ResolveServerPassword;
	const VeloxNet::FServerPasswordResolution All = ResolveServerPassword(TEXT("L_X -log -ServerPassword=fromcmd -nullrhi"), TEXT("?listen?ServerPassword=fromurl"), TEXT("fromenv"));
	TestEqual(TEXT("cmdline wins over url and env"), All.Password, FString(TEXT("fromcmd")));
	TestEqual(TEXT("cmdline source"), FString(All.Source), FString(TEXT("cmdline")));
	const VeloxNet::FServerPasswordResolution Url = ResolveServerPassword(TEXT("L_X -log"), TEXT("?listen?ServerPassword=fromurl"), TEXT("fromenv"));
	TestEqual(TEXT("url wins over env"), Url.Password, FString(TEXT("fromurl")));
	TestEqual(TEXT("url source"), FString(Url.Source), FString(TEXT("url")));
	const VeloxNet::FServerPasswordResolution Env = ResolveServerPassword(TEXT("L_X -log -ServerPassword= -nullrhi"), TEXT("?listen?ServerPassword="), TEXT("fromenv"));
	TestEqual(TEXT("empty cmdline and url values fall through to env"), Env.Password, FString(TEXT("fromenv")));
	TestEqual(TEXT("env source"), FString(Env.Source), FString(TEXT("env")));
	const VeloxNet::FServerPasswordResolution None = ResolveServerPassword(TEXT("L_X -log"), TEXT("?listen"), FString());
	TestTrue(TEXT("nothing configured: empty password"), None.Password.IsEmpty());
	TestEqual(TEXT("nothing configured: source none"), FString(None.Source), FString(TEXT("none")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxNetNoCheatManagerForRemoteTest, "Velox.Net.NoCheatManagerForRemote", VeloxNetTest::Flags)

bool FVeloxNetNoCheatManagerForRemoteTest::RunTest(const FString& Parameters)
{
	using namespace VeloxNetTest;
	FNetTestWorld W;
	if (!TestNotNull(TEXT("AVeloxGameMode is the authority game mode"), W.GameMode))
	{
		return false;
	}

	{
		// A deployed server: networked (listen URL, as the headless host runs) and not the editor.
		VELOX_NET_TEST_NOT_EDITOR();
		W.World->NextURL = TEXT("?listen");
		ON_SCOPE_EXIT { W.World->NextURL.Reset(); };
		TestEqual(TEXT("fixture world reports a listen server"), static_cast<int32>(W.World->GetNetMode()), static_cast<int32>(NM_ListenServer));
		AVeloxPlayerController* Remote = W.SpawnPC();
		if (TestNotNull(TEXT("remote PC spawned"), Remote))
		{
			TestFalse(TEXT("remote PC is not local"), Remote->IsLocalController());
			TestFalse(TEXT("engine AllowCheats: false for a remote PC on a networked server"), W.GameMode->AllowCheats(Remote));
			TestNull(TEXT("remote PC has no CheatManager"), Remote->CheatManager.Get());
			// ADR 0007 amendment F1.2: bPauseable=false, so a networked match cannot be paused by anyone.
			TestFalse(TEXT("AllowPausing: false for a remote PC on a networked server"), W.GameMode->AllowPausing(Remote));

			// RPC gate (section 5.1): the remote sender fails validation, counted and broadcast.
			int32 Broadcasts = 0;
			FName LastRpc;
			Remote->OnRpcRefused.AddLambda([&Broadcasts, &LastRpc](AVeloxPlayerController*, FName Rpc) { ++Broadcasts; LastRpc = Rpc; });
			TestFalse(TEXT("ServerExecRPC refused for a remote PC"), Remote->ServerExecRPC_Validate(TEXT("stat fps")));
			TestEqual(TEXT("refusal counted"), Remote->GetNumRpcRefused(), 1);
			TestEqual(TEXT("refusal broadcast with the RPC name"), LastRpc, FName(TEXT("ServerExecRPC")));
			TestFalse(TEXT("ServerPause refused for a remote PC"), Remote->ServerPause_Validate());
			TestEqual(TEXT("both refusals broadcast"), Broadcasts, 2);
			{
				FIntCVarScope Off(TEXT("velox.net.AllowTestRpcs"), 0);
				TestFalse(TEXT("AllowTestRpcs 0: test RPC refused"), VeloxNet::ValidateTestRpc(Remote, TEXT("ServerKillSelfTest")));
				TestEqual(TEXT("test RPC refusal counted on the owning PC"), Remote->GetNumRpcRefused(), 3);
			}
			{
				FIntCVarScope On(TEXT("velox.net.AllowTestRpcs"), 1);
				TestTrue(TEXT("AllowTestRpcs 1: test RPC accepted"), VeloxNet::ValidateTestRpc(Remote, TEXT("ServerKillSelfTest")));
				TestEqual(TEXT("an accepted test RPC is not counted"), Remote->GetNumRpcRefused(), 3);
			}
		}
		AVeloxPlayerController* Local = W.SpawnPC();
		if (TestNotNull(TEXT("local PC spawned"), Local))
		{
			Local->SetAsLocalPlayerController();
			TestTrue(TEXT("ServerExecRPC accepted for the local (host) controller"), Local->ServerExecRPC_Validate(TEXT("stat fps")));
			TestEqual(TEXT("local controller: nothing counted"), Local->GetNumRpcRefused(), 0);
		}
	}
	{
		// Contrast: the same spawn in a standalone world gets a CheatManager, so the fixture can see one.
		VELOX_NET_TEST_NOT_EDITOR();
		TestEqual(TEXT("fixture world back to standalone"), static_cast<int32>(W.World->GetNetMode()), static_cast<int32>(NM_Standalone));
		AVeloxPlayerController* Standalone = W.SpawnPC();
		if (TestNotNull(TEXT("standalone PC spawned"), Standalone))
		{
			TestTrue(TEXT("engine AllowCheats: true in standalone"), W.GameMode->AllowCheats(Standalone));
#if UE_WITH_CHEAT_MANAGER
			TestNotNull(TEXT("standalone PC has a CheatManager (fixture sanity)"), Standalone->CheatManager.Get());
#endif
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxNetNameApplyTest, "Velox.Net.NameApply", VeloxNetTest::Flags)

bool FVeloxNetNameApplyTest::RunTest(const FString& Parameters)
{
	using namespace VeloxNetTest;
	FNetTestWorld W;
	if (!TestNotNull(TEXT("AVeloxGameMode is the authority game mode"), W.GameMode))
	{
		return false;
	}
	AVeloxPlayerController* First = W.SpawnPC();
	AVeloxPlayerController* Second = W.SpawnPC();
	if (!TestNotNull(TEXT("first PC"), First) || !TestNotNull(TEXT("second PC"), Second) || !TestNotNull(TEXT("first PS"), First->PlayerState.Get())
		|| !TestNotNull(TEXT("second PS"), Second->PlayerState.Get()))
	{
		return false;
	}
	W.GameMode->ChangeName(First, FString(TEXT("  Evil")) + Cp(0x202E) + TEXT("gnp.exe  "), false);
	TestEqual(TEXT("human name sanitized on ChangeName"), First->PlayerState->GetPlayerName(), FString(TEXT("Evilgnp.exe")));
	W.GameMode->ChangeName(Second, TEXT("evilgnp.exe"), false);
	TestEqual(TEXT("second human with the same name deduped"), Second->PlayerState->GetPlayerName(), FString(TEXT("evilgnp.exe (2)")));
	W.GameMode->ChangeName(Second, FString::ChrN(200, TEXT('n')), true);
	TestEqual(TEXT("rename (ServerChangeName path) clamped"), Second->PlayerState->GetPlayerName().Len(), W.GameMode->MaxPlayerNameLength);

	FActorSpawnParameters Params;
	AAIController* AI = W.World->SpawnActor<AAIController>(AAIController::StaticClass(), FTransform::Identity, Params);
	if (TestNotNull(TEXT("AI controller"), AI))
	{
		AI->bWantsPlayerState = true;
		AI->InitPlayerState();
		if (TestNotNull(TEXT("AI PlayerState"), AI->PlayerState.Get()))
		{
			const FString BotName = FString(TEXT("Bot")) + Cp(0x200B) + TEXT("Name");
			W.GameMode->ChangeName(AI, BotName, false);
			TestEqual(TEXT("AI controller name untouched (bots are project data)"), AI->PlayerState->GetPlayerName(), BotName);
		}
	}
	return true;
}

// ---- ADR 0007 section 5.4 (task 09): anomaly counters -------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxNetAnomalyFormatTest, "Velox.Net.AnomalyFormat", VeloxNetTest::Flags)

bool FVeloxNetAnomalyFormatTest::RunTest(const FString& Parameters)
{
	FVeloxPlayerAnomalyStats Empty;
	TestFalse(TEXT("fresh stats have no counter"), Empty.HasAnyCounter());
	FVeloxPlayerAnomalyStats RttOnly;
	RttOnly.RttMs = 250.f;
	TestFalse(TEXT("rtt is not a counter"), RttOnly.HasAnyCounter());
	TestTrue(TEXT("rtt is ignored by SameCounters"), RttOnly.SameCounters(Empty));

	TestEqual(TEXT("all-zero line"), VeloxAnomaly::FormatLine(Empty, TEXT("Player"), 0, Empty, TEXT("logout")),
		FString(TEXT("VeloxAnomaly at=logout pid=0 shots=0(+0) switches=0(+0) rejects{} snaps=0(+0) clamps=0(+0) late_retargets=0(+0) fresh_retargets=0(+0) "
			"orphans=0(+0) rpc_refused=0(+0) discrepancies=0(+0) late_share=0.00 reject_share=0.00 rtt_ms=0 player=Player")));

	FVeloxPlayerAnomalyStats Prev;
	Prev.Note(EVeloxAnomaly::Shot, 8);
	Prev.Note(EVeloxAnomaly::Switch, 2);
	Prev.Note(EVeloxAnomaly::Reject, 1, TEXT("type"));
	FVeloxPlayerAnomalyStats Now = Prev;
	TestTrue(TEXT("a copy has the same counters"), Now.SameCounters(Prev));
	Now.Note(EVeloxAnomaly::Shot, 4);
	Now.Note(EVeloxAnomaly::Switch);
	Now.Note(EVeloxAnomaly::Reject, 2, TEXT("rate"));
	Now.Note(EVeloxAnomaly::Reject, 1, NAME_None);
	Now.Note(EVeloxAnomaly::OriginSnap);
	Now.Note(EVeloxAnomaly::TimeClamp, 2);
	Now.Note(EVeloxAnomaly::LateRetarget);
	Now.Note(EVeloxAnomaly::FreshRetarget);
	Now.Note(EVeloxAnomaly::MoveShotOrphan);
	Now.Note(EVeloxAnomaly::RpcRefused);
	Now.Note(EVeloxAnomaly::Discrepancy, 5);
	Now.RttMs = 209.6f;
	TestFalse(TEXT("changed counters differ"), Now.SameCounters(Prev));
	TestTrue(TEXT("changed stats have counters"), Now.HasAnyCounter());
	TestEqual(TEXT("rejects total"), Now.GetRejects(), 4);
	TestEqual(TEXT("rejects rate"), Now.GetRejects(TEXT("rate")), 2);
	TestEqual(TEXT("NAME_None counts as unknown"), Now.GetRejects(TEXT("unknown")), 1);
	TestEqual(TEXT("no inline overflow for the known reasons"), Now.Rejects.Num(), 3);

	TArray<FName> Exceeded;
	Exceeded.Add(TEXT("snaps"));
	Exceeded.Add(TEXT("discrepancies"));
	TestEqual(TEXT("full line"), VeloxAnomaly::FormatLine(Now, TEXT("Big Bad Wolf"), 7, Prev, TEXT("interval"), Exceeded),
		FString(TEXT("VeloxAnomaly at=interval pid=7 shots=12(+4) switches=3(+1) rejects{type:1(+0),rate:2(+2),unknown:1(+1)} snaps=1(+1) clamps=2(+2) "
			"late_retargets=1(+1) fresh_retargets=1(+1) orphans=1(+1) rpc_refused=1(+1) discrepancies=5(+5) late_share=0.33 reject_share=0.25 rtt_ms=210 "
			"exceeds=snaps,discrepancies player=Big Bad Wolf")));

	// The same reason twice: one entry, the sum (reject-reason SameCounters is order-independent).
	FVeloxPlayerAnomalyStats A, B;
	A.Note(EVeloxAnomaly::Reject, 1, TEXT("rate"));
	A.Note(EVeloxAnomaly::Reject, 1, TEXT("dir"));
	B.Note(EVeloxAnomaly::Reject, 1, TEXT("dir"));
	B.Note(EVeloxAnomaly::Reject, 1, TEXT("rate"));
	TestTrue(TEXT("reject order does not matter for SameCounters"), A.SameCounters(B));
	A.Note(EVeloxAnomaly::Reject, 1, TEXT("rate"));
	TestEqual(TEXT("one entry per reason"), A.Rejects.Num(), 2);
	TestFalse(TEXT("different reject counts differ"), A.SameCounters(B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxNetAnomalyRatesTest, "Velox.Net.AnomalyRates", VeloxNetTest::Flags)

bool FVeloxNetAnomalyRatesTest::RunTest(const FString& Parameters)
{
	FVeloxPlayerAnomalyStats Zero;
	VeloxAnomaly::FRates R = VeloxAnomaly::Rates(Zero);
	TestEqual(TEXT("no switches: late_share 0"), R.LateShare, 0.f);
	TestEqual(TEXT("no shots: reject_share 0"), R.RejectShare, 0.f);

	// late_share's denominator is max(switches, 1): late retargets without a counted switch still give a finite share.
	// reject_share's is attempts (review 0007-09 F2): a client whose every shot is rejected has share 1, never above.
	FVeloxPlayerAnomalyStats NoDenominator;
	NoDenominator.Note(EVeloxAnomaly::Reject, 3, TEXT("rate"));
	NoDenominator.Note(EVeloxAnomaly::LateRetarget, 2);
	R = VeloxAnomaly::Rates(NoDenominator);
	TestEqual(TEXT("late / max(0,1)"), R.LateShare, 2.f);
	TestEqual(TEXT("all rejected: rejects / attempts = 1"), R.RejectShare, 1.f);
	TestEqual(TEXT("attempts = shots + rejects"), NoDenominator.GetShotAttempts(), 3);

	FVeloxPlayerAnomalyStats Cheater;
	Cheater.Note(EVeloxAnomaly::Switch, 40);
	Cheater.Note(EVeloxAnomaly::LateRetarget, 30);
	Cheater.Note(EVeloxAnomaly::Shot, 100);
	Cheater.Note(EVeloxAnomaly::Reject, 50, TEXT("rate"));
	Cheater.Note(EVeloxAnomaly::OriginSnap, 9);
	Cheater.Note(EVeloxAnomaly::TimeClamp, 9);
	Cheater.Note(EVeloxAnomaly::MoveShotOrphan, 9);
	Cheater.Note(EVeloxAnomaly::RpcRefused, 9);
	Cheater.Note(EVeloxAnomaly::Discrepancy, 9);
	R = VeloxAnomaly::Rates(Cheater);
	TestEqual(TEXT("late_share 30/40"), R.LateShare, 0.75f);
	TestEqual(TEXT("reject_share 50/(100+50)"), R.RejectShare, 50.f / 150.f);

	const FVeloxAnomalyThresholds Defaults;
	TestEqual(TEXT("M6 defaults: nothing exceeded (log only, no kick)"), VeloxAnomaly::Exceeds(Cheater, Defaults).Num(), 0);

	FVeloxAnomalyThresholds T;
	T.MaxLateShare = 0.2f;
	T.MaxRejectShare = 0.1f;
	T.MaxOriginSnaps = 8;
	T.MaxTimeClamps = 9;      // equal is not exceeded
	T.MaxMoveShotOrphans = 1;
	T.MaxRpcRefused = 0;      // disabled
	T.MaxDiscrepancies = 5;
	const TArray<FName> Names = VeloxAnomaly::Exceeds(Cheater, T);
	TArray<FName> Expected;
	for (const TCHAR* N : { TEXT("late_share"), TEXT("reject_share"), TEXT("snaps"), TEXT("orphans"), TEXT("discrepancies") })
	{
		Expected.Add(N);
	}
	TestTrue(FString::Printf(TEXT("exceeded names, in order (got %d)"), Names.Num()), Names == Expected);

	// The min-sample gates (ADR 0007 M7 trigger: > 20 % over >= 20 switches).
	// reject_share's gate counts attempts (accepted + rejected, review 0007-09 F2).
	FVeloxPlayerAnomalyStats Few;
	Few.Note(EVeloxAnomaly::Switch, 19);
	Few.Note(EVeloxAnomaly::LateRetarget, 19);
	Few.Note(EVeloxAnomaly::Shot, 9);
	Few.Note(EVeloxAnomaly::Reject, 10, TEXT("rate"));
	TestEqual(TEXT("below the min samples (19 switches, 19 attempts): not exceeded"), VeloxAnomaly::Exceeds(Few, T).Num(), 0);
	Few.Note(EVeloxAnomaly::Switch);
	Few.Note(EVeloxAnomaly::Shot);
	TestEqual(TEXT("at the min samples (20 switches, 20 attempts): both shares exceeded"), VeloxAnomaly::Exceeds(Few, T).Num(), 2);

	// Review 0007-09 F2: a client whose every shot is rejected (no accepted shot at all) reaches the reject_share check.
	FVeloxPlayerAnomalyStats AllRejected;
	AllRejected.Note(EVeloxAnomaly::Reject, 19, TEXT("rate"));
	TestEqual(TEXT("all rejected, 19 attempts: below the min samples"), VeloxAnomaly::Exceeds(AllRejected, T).Num(), 0);
	AllRejected.Note(EVeloxAnomaly::Reject, 1, TEXT("type"));
	const TArray<FName> AllRejectedNames = VeloxAnomaly::Exceeds(AllRejected, T);
	TestTrue(TEXT("all rejected, 20 attempts, 0 accepted: reject_share exceeded"), AllRejectedNames.Num() == 1 && AllRejectedNames[0] == FName(TEXT("reject_share")));
	TestTrue(TEXT("all rejected: the line shows shots=0 and reject_share=1.00"),
		VeloxAnomaly::FormatLine(AllRejected, TEXT("R"), 2, FVeloxPlayerAnomalyStats(), TEXT("logout"), AllRejectedNames).Contains(TEXT("shots=0(+0)"))
		&& VeloxAnomaly::FormatLine(AllRejected, TEXT("R"), 2, FVeloxPlayerAnomalyStats(), TEXT("logout"), AllRejectedNames).Contains(TEXT("reject_share=1.00 rtt_ms=0 exceeds=reject_share player=R")));
	TestEqual(TEXT("all rejected: defaults still exceed nothing"), VeloxAnomaly::Exceeds(AllRejected, Defaults).Num(), 0);

	// An honest player at the ping limit: one late retarget in 30 switches, two rate rejects in 200 shots.
	FVeloxPlayerAnomalyStats Honest;
	Honest.Note(EVeloxAnomaly::Switch, 30);
	Honest.Note(EVeloxAnomaly::LateRetarget, 1);
	Honest.Note(EVeloxAnomaly::Shot, 200);
	Honest.Note(EVeloxAnomaly::Reject, 2, TEXT("rate"));
	TestEqual(TEXT("honest at the ping limit: nothing exceeded with the M7 trigger values"), VeloxAnomaly::Exceeds(Honest, T).Num(), 0);
	TestTrue(TEXT("honest line shows 0.03 / 0.01"), VeloxAnomaly::FormatLine(Honest, TEXT("H"), 1, FVeloxPlayerAnomalyStats(), TEXT("logout")).Contains(TEXT("late_share=0.03 reject_share=0.01")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
