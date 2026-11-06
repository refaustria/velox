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

#endif // WITH_DEV_AUTOMATION_TESTS
