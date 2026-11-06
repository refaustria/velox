// Velox - online tests (ADR 0007 section 2, task M6-04).
//
//   Velox.Online.HeartbeatJson   world-free: the heartbeat body for a fixed snapshot (parsed and compared field by field, the
//                                shape infra/master accepts), escaping, clamping, time-left arithmetic, id sanitising.
//   Velox.Online.MapRegistry     DefaultGame.ini [/Script/Velox.VeloxMapRegistry]: L_DM1 is "Praesidium" for FFA and Duel and
//                                public, the test maps are not, an unknown map falls back to its short name.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/VeloxMapRegistry.h"
#include "Core/VeloxOnlineSettings.h"
#include "Dom/JsonObject.h"
#include "GameModes/VeloxMatchEffects.h"
#include "Net/VeloxServerHeartbeatSubsystem.h"
#include "Net/VeloxServerStatus.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace VeloxOnlineTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FVeloxServerSnapshot MakeSnapshot()
	{
		FVeloxServerSnapshot S;
		S.Id = TEXT("ffa");
		S.Name = TEXT("Velox FFA | games.inceptum.at");
		S.Port = 7777;
		S.Map = TEXT("L_DM1");
		S.MapDisplayName = TEXT("Praesidium");
		S.Mode = TEXT("FFA");
		S.Phase = TEXT("Live");
		S.TimeLeftSeconds = 412;
		S.Players = 3;
		S.MaxPlayers = 16;
		S.Bots = 3;
		S.Spectators = 1;
		S.Version = TEXT("0.6.0");
		S.NetVersion = 2891746305u;
		S.bPassword = false;
		return S;
	}

	TSharedPtr<FJsonObject> Parse(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxHeartbeatJsonTest, "Velox.Online.HeartbeatJson", VeloxOnlineTest::Flags)

bool FVeloxHeartbeatJsonTest::RunTest(const FString& Parameters)
{
	// Golden: the exact body (condensed, in this order). Field order is irrelevant to the receiver; the parsed compare below
	// is the contract, the string compare pins the writer.
	const FString Golden = TEXT("{\"schema\":1,\"id\":\"ffa\",\"name\":\"Velox FFA | games.inceptum.at\",\"port\":7777,\"map\":\"L_DM1\",\"map_display_name\":\"Praesidium\","
		"\"mode\":\"FFA\",\"phase\":\"Live\",\"time_left_s\":412,\"players\":3,\"max_players\":16,\"bots\":3,\"spectators\":1,\"version\":\"0.6.0\","
		"\"net_version\":\"2891746305\",\"password\":false}");
	const FString Body = VeloxServerStatus::BuildHeartbeat(VeloxOnlineTest::MakeSnapshot());
	TestEqual(TEXT("golden body"), Body, Golden);

	const TSharedPtr<FJsonObject> Json = VeloxOnlineTest::Parse(Body);
	if (!TestTrue(TEXT("body parses"), Json.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("schema"), Json->GetIntegerField(TEXT("schema")), 1);
	TestEqual(TEXT("id"), Json->GetStringField(TEXT("id")), FString(TEXT("ffa")));
	TestEqual(TEXT("port"), Json->GetIntegerField(TEXT("port")), 7777);
	TestEqual(TEXT("map_display_name"), Json->GetStringField(TEXT("map_display_name")), FString(TEXT("Praesidium")));
	TestEqual(TEXT("time_left_s"), Json->GetIntegerField(TEXT("time_left_s")), 412);
	TestEqual(TEXT("players"), Json->GetIntegerField(TEXT("players")), 3);
	TestEqual(TEXT("max_players"), Json->GetIntegerField(TEXT("max_players")), 16);
	TestEqual(TEXT("bots"), Json->GetIntegerField(TEXT("bots")), 3);
	TestEqual(TEXT("spectators"), Json->GetIntegerField(TEXT("spectators")), 1);
	TestEqual(TEXT("net_version is the decimal string"), Json->GetStringField(TEXT("net_version")), FString(TEXT("2891746305")));
	TestFalse(TEXT("password"), Json->GetBoolField(TEXT("password")));

	// Escaping: quotes, backslashes, newlines, a non-ASCII character survive a round trip.
	FVeloxServerSnapshot Tricky = VeloxOnlineTest::MakeSnapshot();
	Tricky.Name = TEXT("A \"quoted\" \\ name\nline ä");
	const TSharedPtr<FJsonObject> TrickyJson = VeloxOnlineTest::Parse(VeloxServerStatus::BuildHeartbeat(Tricky));
	if (TestTrue(TEXT("escaped body parses"), TrickyJson.IsValid()))
	{
		TestEqual(TEXT("name round trip"), TrickyJson->GetStringField(TEXT("name")), Tricky.Name);
	}

	// Clamping: negative numbers never go out, ids are sanitised, long strings are cut to the receiver's limits.
	FVeloxServerSnapshot Bad = VeloxOnlineTest::MakeSnapshot();
	Bad.Id = TEXT("My Server/1");
	Bad.Name = FString::ChrN(100, TEXT('n'));
	Bad.TimeLeftSeconds = -5;
	Bad.Players = -1;
	Bad.Spectators = 100000;
	Bad.MapDisplayName = FString();
	const TSharedPtr<FJsonObject> BadJson = VeloxOnlineTest::Parse(VeloxServerStatus::BuildHeartbeat(Bad));
	if (TestTrue(TEXT("clamped body parses"), BadJson.IsValid()))
	{
		TestEqual(TEXT("id sanitised"), BadJson->GetStringField(TEXT("id")), FString(TEXT("my-server-1")));
		TestEqual(TEXT("name cut"), BadJson->GetStringField(TEXT("name")).Len(), VeloxServerStatus::MaxName);
		TestEqual(TEXT("time_left_s never negative"), BadJson->GetIntegerField(TEXT("time_left_s")), 0);
		TestEqual(TEXT("players never negative"), BadJson->GetIntegerField(TEXT("players")), 0);
		TestEqual(TEXT("spectators capped"), BadJson->GetIntegerField(TEXT("spectators")), VeloxServerStatus::MaxCount);
		TestEqual(TEXT("display name falls back to the short name"), BadJson->GetStringField(TEXT("map_display_name")), FString(TEXT("L_DM1")));
	}
	TestEqual(TEXT("empty id"), VeloxServerStatus::SanitizeId(FString()), FString(TEXT("server")));
	TestEqual(TEXT("long id"), VeloxServerStatus::SanitizeId(FString::ChrN(40, TEXT('a'))).Len(), VeloxServerStatus::MaxId);
	// Activation rule: only directory servers (-ServerId) post, and only while the master switch is on.
	TestTrue(TEXT("enabled with id posts"), UVeloxServerHeartbeatSubsystem::IsHeartbeatActive(true, TEXT("test")));
	TestFalse(TEXT("enabled without id is inactive"), UVeloxServerHeartbeatSubsystem::IsHeartbeatActive(true, FString()));
	TestFalse(TEXT("disabled never posts"), UVeloxServerHeartbeatSubsystem::IsHeartbeatActive(false, TEXT("test")));
	TestEqual(TEXT("mode leaf"), VeloxServerStatus::TagLeaf(TEXT("Velox.Mode.Duel")), FString(TEXT("Duel")));

	// Time left (ADR 0004 section 6).
	using VeloxServerStatus::TimeLeftSeconds;
	TestEqual(TEXT("warmup: open-ended"), TimeLeftSeconds(EVeloxMatchPhase::Warmup, 50.0, 0.0, 0.0, 600), 0);
	TestEqual(TEXT("countdown: to phase end, ceil"), TimeLeftSeconds(EVeloxMatchPhase::Countdown, 10.0, 12.2, 0.0, 600), 3);
	TestEqual(TEXT("live: limit minus elapsed"), TimeLeftSeconds(EVeloxMatchPhase::Live, 100.0, 0.0, 40.0, 600), 540);
	TestEqual(TEXT("live without limit"), TimeLeftSeconds(EVeloxMatchPhase::Live, 100.0, 0.0, 40.0, 0), 0);
	TestEqual(TEXT("live past the limit is 0, not negative"), TimeLeftSeconds(EVeloxMatchPhase::Live, 700.0, 0.0, 40.0, 600), 0);
	TestEqual(TEXT("timed overtime"), TimeLeftSeconds(EVeloxMatchPhase::Overtime, 10.0, 70.0, 0.0, 600), 60);
	TestEqual(TEXT("sudden death"), TimeLeftSeconds(EVeloxMatchPhase::Overtime, 10.0, 0.0, 0.0, 600), 0);
	TestEqual(TEXT("postmatch"), TimeLeftSeconds(EVeloxMatchPhase::PostMatch, 10.0, 25.0, 0.0, 600), 15);
	TestEqual(TEXT("postmatch ended"), TimeLeftSeconds(EVeloxMatchPhase::PostMatch, 30.0, 25.0, 0.0, 600), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMapRegistryTest, "Velox.Online.MapRegistry", VeloxOnlineTest::Flags)

bool FVeloxMapRegistryTest::RunTest(const FString& Parameters)
{
	const UVeloxMapRegistry& Registry = UVeloxMapRegistry::Get();

	const FVeloxMapInfo* Dm1 = Registry.FindByShortName(TEXT("L_DM1"));
	if (TestNotNull(TEXT("L_DM1 registered"), Dm1))
	{
		TestEqual(TEXT("L_DM1 display name"), Dm1->DisplayName.ToString(), FString(TEXT("Praesidium")));
		TestTrue(TEXT("L_DM1 public"), Dm1->bPublic);
		TestTrue(TEXT("L_DM1 FFA"), Dm1->Modes.Contains(VeloxMatchTags::Mode_FFA.GetTag()));
		TestTrue(TEXT("L_DM1 Duel"), Dm1->Modes.Contains(VeloxMatchTags::Mode_Duel.GetTag()));
		TestEqual(TEXT("map path names the package"), Dm1->Map.GetLongPackageName(), FString(TEXT("/Game/Maps/DM1/L_DM1")));
	}
	TestEqual(TEXT("display name lookup"), Registry.DisplayNameFor(TEXT("L_DM1")), FString(TEXT("Praesidium")));
	TestEqual(TEXT("lookup by package path and case"), Registry.DisplayNameFor(TEXT("/Game/Maps/DM1/l_dm1")), FString(TEXT("Praesidium")));
	TestEqual(TEXT("lookup of a PIE copy"), Registry.DisplayNameFor(TEXT("/Memory/UEDPIE_0_L_DM1")), FString(TEXT("Praesidium")));

	for (const TCHAR* TestMap : { TEXT("L_DM1_Blockout"), TEXT("L_ItemTest"), TEXT("L_CombatTest"), TEXT("L_MovementTest") })
	{
		const FVeloxMapInfo* Info = Registry.FindByShortName(TestMap);
		if (TestNotNull(*FString::Printf(TEXT("%s registered"), TestMap), Info))
		{
			TestFalse(*FString::Printf(TEXT("%s is not public"), TestMap), Info->bPublic);
		}
	}

	// Unknown map: no entry, the display name is the short name.
	TestNull(TEXT("unknown map has no entry"), Registry.FindByShortName(TEXT("L_Nowhere")));
	TestEqual(TEXT("unknown map falls back to its short name"), Registry.DisplayNameFor(TEXT("L_Nowhere")), FString(TEXT("L_Nowhere")));
	TestNull(TEXT("no world, no entry"), Registry.FindByWorld(nullptr));

	TestEqual(TEXT("short name of a package"), UVeloxMapRegistry::ShortNameOfPackage(TEXT("/Game/Maps/L_ItemTest")), FString(TEXT("L_ItemTest")));
	TestEqual(TEXT("short name strips the PIE prefix"), UVeloxMapRegistry::ShortNameOfPackage(TEXT("/Memory/UEDPIE_12_L_ItemTest")), FString(TEXT("L_ItemTest")));

	// Settings defaults the heartbeat and the browser rely on.
	const UVeloxOnlineSettings& Online = UVeloxOnlineSettings::Get();
	TestEqual(TEXT("heartbeat interval"), Online.HeartbeatIntervalSeconds, 30.f);
	TestEqual(TEXT("heartbeat debounce"), Online.HeartbeatDebounceSeconds, 2.f);
	TestEqual(TEXT("heartbeat timeout"), Online.HeartbeatTimeoutSeconds, 5.f);
	TestEqual(TEXT("max player name"), Online.MaxPlayerNameLength, 24);
	TestEqual(TEXT("default server name"), Online.DefaultServerName, FString(TEXT("Velox")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
