// Velox - online tests (ADR 0007 section 2, task M6-04).
//
//   Velox.Online.HeartbeatJson   world-free: the heartbeat body for a fixed snapshot (parsed and compared field by field, the
//                                shape infra/master accepts), escaping, clamping, time-left arithmetic, id sanitising.
//   Velox.Online.ServerListParse / ServerCompat / ConnectPassword   (task M6-05) servers.json parsing, the compatibility rule
//                                and sort, the ?Password= option and the client-only rule of the directory subsystem.
//   Velox.Online.MapRegistry     DefaultGame.ini [/Script/Velox.VeloxMapRegistry]: L_DM1 is "Praesidium" for FFA and Duel and
//                                public, the test maps are not, an unknown map falls back to its short name.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/VeloxMapRegistry.h"
#include "Core/VeloxOnlineSettings.h"
#include "Dom/JsonObject.h"
#include "GameModes/VeloxMatchEffects.h"
#include "Net/VeloxServerDirectorySubsystem.h"
#include "Net/VeloxServerHeartbeatSubsystem.h"
#include "Net/VeloxServerList.h"
#include "Net/VeloxServerStatus.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/VeloxMenuMath.h"
#include "UI/VeloxServerBrowserWidget.h"

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

// ---- Server list (task M6-05) ----

namespace VeloxOnlineTest
{
	// infra/master/tests/golden/servers.json, condensed.
	const TCHAR* const GoldenServers = TEXT("{\"schema\":1,\"generated_at\":\"2026-07-02T00:00:00Z\",\"servers\":["
		"{\"schema\":1,\"id\":\"duel\",\"name\":\"Velox Duel\",\"port\":7778,\"map\":\"L_DM1\",\"map_display_name\":\"Praesidium\",\"mode\":\"duel\",\"phase\":\"warmup\","
		"\"time_left_s\":30,\"players\":1,\"max_players\":2,\"bots\":0,\"spectators\":1,\"version\":\"0.6.0\",\"net_version\":\"2891746305\",\"password\":false,"
		"\"address\":\"77.244.242.34:7778\",\"host\":\"games.inceptum.at\",\"updated_s_ago\":12},"
		"{\"schema\":1,\"id\":\"ffa\",\"name\":\"Velox FFA (Praesidium)\",\"port\":7777,\"map\":\"L_DM1\",\"map_display_name\":\"Praesidium\",\"mode\":\"ffa\",\"phase\":\"match\","
		"\"time_left_s\":412,\"players\":3,\"max_players\":8,\"bots\":3,\"spectators\":0,\"version\":\"0.6.0\",\"net_version\":\"2891746305\",\"password\":true,"
		"\"address\":\"77.244.242.34:7777\",\"host\":\"games.inceptum.at\",\"updated_s_ago\":3}]}");

	FVeloxServerEntry MakeEntry(const TCHAR* Name, int32 Players, int32 MaxPlayers)
	{
		FVeloxServerEntry E;
		E.Name = Name;
		E.Address = FString::Printf(TEXT("10.0.0.1:%d"), 7000 + Players);
		E.Players = Players;
		E.MaxPlayers = MaxPlayers;
		E.Version = TEXT("0.6.0");
		E.NetVersion = 100u;
		E.UpdatedSecondsAgo = 5.0;
		return E;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxServerListParseTest, "Velox.Online.ServerListParse", VeloxOnlineTest::Flags)

bool FVeloxServerListParseTest::RunTest(const FString& Parameters)
{
	TArray<FVeloxServerEntry> Entries;
	FString Error;

	// Golden: both rows, every field.
	TestTrue(TEXT("golden parses"), VeloxServerList::Parse(VeloxOnlineTest::GoldenServers, Entries, Error));
	TestTrue(TEXT("golden has no skipped entries"), Error.IsEmpty());
	if (TestEqual(TEXT("golden rows"), Entries.Num(), 2))
	{
		const FVeloxServerEntry& Duel = Entries[0];
		TestEqual(TEXT("duel id"), Duel.Id, FString(TEXT("duel")));
		TestEqual(TEXT("duel name"), Duel.Name, FString(TEXT("Velox Duel")));
		TestEqual(TEXT("duel port"), Duel.Port, 7778);
		TestEqual(TEXT("duel map display"), Duel.MapDisplayName, FString(TEXT("Praesidium")));
		TestEqual(TEXT("duel mode"), Duel.Mode, FString(TEXT("duel")));
		TestEqual(TEXT("duel phase"), Duel.Phase, FString(TEXT("warmup")));
		TestEqual(TEXT("duel time left"), Duel.TimeLeftSeconds, 30);
		TestEqual(TEXT("duel players"), Duel.Players, 1);
		TestEqual(TEXT("duel max"), Duel.MaxPlayers, 2);
		TestEqual(TEXT("duel spectators"), Duel.Spectators, 1);
		TestEqual(TEXT("duel version"), Duel.Version, FString(TEXT("0.6.0")));
		TestEqual(TEXT("duel net version (string)"), Duel.NetVersion, 2891746305u);
		TestFalse(TEXT("duel password"), Duel.bPassword);
		TestEqual(TEXT("duel address"), Duel.Address, FString(TEXT("77.244.242.34:7778")));
		TestEqual(TEXT("duel host"), Duel.Host, FString(TEXT("games.inceptum.at")));
		TestEqual(TEXT("duel age"), Duel.UpdatedSecondsAgo, 12.0);
		const FVeloxServerEntry& Ffa = Entries[1];
		TestEqual(TEXT("ffa bots"), Ffa.Bots, 3);
		TestTrue(TEXT("ffa password"), Ffa.bPassword);
		TestEqual(TEXT("ffa address"), Ffa.Address, FString(TEXT("77.244.242.34:7777")));
		TestEqual(TEXT("ffa time left"), Ffa.TimeLeftSeconds, 412);
	}

	// Empty list is a valid, empty result (and replaces the previous rows).
	TestTrue(TEXT("empty list parses"), VeloxServerList::Parse(TEXT("{\"schema\":1,\"generated_at\":\"x\",\"servers\":[]}"), Entries, Error));
	TestEqual(TEXT("empty list rows"), Entries.Num(), 0);
	TestTrue(TEXT("empty list no error"), Error.IsEmpty());

	// Document-level failures leave the previous rows alone.
	Entries.Add(VeloxOnlineTest::MakeEntry(TEXT("keep"), 1, 8));
	TestFalse(TEXT("missing servers array"), VeloxServerList::Parse(TEXT("{\"schema\":1}"), Entries, Error));
	TestFalse(TEXT("missing servers array: error text"), Error.IsEmpty());
	TestFalse(TEXT("wrong schema"), VeloxServerList::Parse(TEXT("{\"schema\":2,\"servers\":[]}"), Entries, Error));
	TestFalse(TEXT("no schema"), VeloxServerList::Parse(TEXT("{\"servers\":[]}"), Entries, Error));
	TestFalse(TEXT("not json"), VeloxServerList::Parse(TEXT("<html>502 Bad Gateway</html>"), Entries, Error));
	TestFalse(TEXT("servers is not an array"), VeloxServerList::Parse(TEXT("{\"schema\":1,\"servers\":{}}"), Entries, Error));
	TestEqual(TEXT("failed parses keep the rows"), Entries.Num(), 1);

	// Malformed entries are skipped and listed; good ones survive; unknown fields and an integer net_version are fine.
	const FString Mixed = TEXT("{\"schema\":1,\"servers\":["
		"{\"address\":\"1.2.3.4:7777\",\"name\":\"ok\",\"net_version\":42,\"future_field\":{\"a\":[1,2]},\"version\":\"1\"},"
		"7,"
		"{\"name\":\"no address\",\"net_version\":\"1\"},"
		"{\"address\":\"1.2.3.5:7777\",\"net_version\":\"abc\"},"
		"{\"address\":\"1.2.3.6:7777\",\"net_version\":\"99999999999\"},"
		"{\"address\":\"1.2.3.7:7777\",\"net_version\":\"7\",\"players\":-4}"
		"]}");
	TestTrue(TEXT("mixed list parses"), VeloxServerList::Parse(Mixed, Entries, Error));
	if (TestEqual(TEXT("mixed list keeps the good rows"), Entries.Num(), 2))
	{
		TestEqual(TEXT("integer net_version"), Entries[0].NetVersion, 42u);
		TestEqual(TEXT("missing name falls back to the address"), Entries[1].Name, FString(TEXT("1.2.3.7:7777")));
		TestEqual(TEXT("negative counts clamp to zero"), Entries[1].Players, 0);
		TestEqual(TEXT("missing display name falls back to map"), Entries[1].MapDisplayName, Entries[1].Map);
	}
	TestTrue(TEXT("skipped entries are listed: not an object"), Error.Contains(TEXT("entry 1")));
	TestTrue(TEXT("skipped entries are listed: no address"), Error.Contains(TEXT("entry 2")));
	TestTrue(TEXT("skipped entries are listed: bad net_version"), Error.Contains(TEXT("entry 3")) && Error.Contains(TEXT("entry 4")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxServerCompatTest, "Velox.Online.ServerCompat", VeloxOnlineTest::Flags)

bool FVeloxServerCompatTest::RunTest(const FString& Parameters)
{
	const FString Version = TEXT("0.6.0");
	constexpr uint32 Net = 100u;
	constexpr double Stale = VeloxServerList::DefaultStaleAfterSeconds;
	TestEqual(TEXT("stale default"), Stale, 120.0);
	const auto Classify = [&](const FVeloxServerEntry& E) { return VeloxServerList::Compatibility(E, Version, Net, Stale); };

	FVeloxServerEntry E = VeloxOnlineTest::MakeEntry(TEXT("a"), 2, 8);
	TestTrue(TEXT("equal version and net -> Ok"), Classify(E) == EVeloxServerCompat::Ok);

	FVeloxServerEntry OtherVersion = E;
	OtherVersion.Version = TEXT("0.5.9");
	TestTrue(TEXT("other version -> VersionMismatch"), Classify(OtherVersion) == EVeloxServerCompat::VersionMismatch);

	FVeloxServerEntry OtherNet = OtherVersion;
	OtherNet.NetVersion = 101u;
	TestTrue(TEXT("other net version takes precedence"), Classify(OtherNet) == EVeloxServerCompat::NetVersionMismatch);
	OtherNet.Version = Version;
	TestTrue(TEXT("other net version, same version -> NetVersionMismatch"), Classify(OtherNet) == EVeloxServerCompat::NetVersionMismatch);

	FVeloxServerEntry Old = E;
	Old.UpdatedSecondsAgo = 120.0;
	TestTrue(TEXT("exactly at the limit is not stale"), Classify(Old) == EVeloxServerCompat::Ok);
	Old.UpdatedSecondsAgo = 120.5;
	TestTrue(TEXT("older than the limit -> Stale"), Classify(Old) == EVeloxServerCompat::Stale);
	Old.Version = TEXT("0.5.9");
	TestTrue(TEXT("mismatch beats stale"), Classify(Old) == EVeloxServerCompat::VersionMismatch);

	FVeloxServerEntry Full = VeloxOnlineTest::MakeEntry(TEXT("full"), 8, 8);
	TestTrue(TEXT("players >= max -> Full"), Classify(Full) == EVeloxServerCompat::Full);
	Full.Players = 9;
	TestTrue(TEXT("players above max -> Full"), Classify(Full) == EVeloxServerCompat::Full);
	FVeloxServerEntry NoMax = VeloxOnlineTest::MakeEntry(TEXT("nomax"), 0, 0);
	TestTrue(TEXT("max_players 0 is not Full"), Classify(NoMax) == EVeloxServerCompat::Ok);
	Full.UpdatedSecondsAgo = 500.0;
	TestTrue(TEXT("stale beats full"), Classify(Full) == EVeloxServerCompat::Stale);

	// Connectable: everything except the version mismatches (Full joins as a spectator, Stale is tried).
	TestTrue(TEXT("Ok connectable"), VeloxServerList::IsConnectable(EVeloxServerCompat::Ok));
	TestTrue(TEXT("Full connectable"), VeloxServerList::IsConnectable(EVeloxServerCompat::Full));
	TestTrue(TEXT("Stale connectable"), VeloxServerList::IsConnectable(EVeloxServerCompat::Stale));
	TestFalse(TEXT("VersionMismatch refused"), VeloxServerList::IsConnectable(EVeloxServerCompat::VersionMismatch));
	TestFalse(TEXT("NetVersionMismatch refused"), VeloxServerList::IsConnectable(EVeloxServerCompat::NetVersionMismatch));

	// Texts: the reasons are non-empty and distinct, Ok has none; the UI settings carry the same defaults.
	TestEqual(TEXT("Ok has no text"), FString(VeloxServerList::CompatText(EVeloxServerCompat::Ok)), FString());
	TSet<FString> Seen;
	for (const EVeloxServerCompat C : { EVeloxServerCompat::VersionMismatch, EVeloxServerCompat::NetVersionMismatch, EVeloxServerCompat::Stale, EVeloxServerCompat::Full })
	{
		const FString Text = VeloxServerList::CompatText(C);
		TestFalse(TEXT("reason text is not empty"), Text.IsEmpty());
		TestFalse(TEXT("reason texts are distinct"), Seen.Contains(Text));
		Seen.Add(Text);
		TestEqual(TEXT("UI setting default matches CompatText"), UVeloxServerBrowserWidget::CompatLabel(C), Text);
	}

	// Sort: compatible (Ok, Full) first, then players desc, then name; incompatible and stale rows last.
	TArray<FVeloxServerEntry> Rows;
	FVeloxServerEntry Mismatch = VeloxOnlineTest::MakeEntry(TEXT("Aardvark mismatch"), 7, 8);
	Mismatch.Version = TEXT("0.5.9");
	FVeloxServerEntry Stale1 = VeloxOnlineTest::MakeEntry(TEXT("Gone"), 6, 8);
	Stale1.UpdatedSecondsAgo = 999.0;
	Rows.Add(Mismatch);
	Rows.Add(VeloxOnlineTest::MakeEntry(TEXT("beta"), 2, 8));
	Rows.Add(Stale1);
	Rows.Add(VeloxOnlineTest::MakeEntry(TEXT("Alpha"), 2, 8));
	Rows.Add(VeloxOnlineTest::MakeEntry(TEXT("Busy"), 5, 8));
	Rows.Add(VeloxOnlineTest::MakeEntry(TEXT("Packed"), 8, 8));
	VeloxServerList::Sort(Rows, Version, Net, Stale);
	TArray<FString> Order;
	for (const FVeloxServerEntry& Row : Rows)
	{
		Order.Add(Row.Name);
	}
	TestEqual(TEXT("sort order"), FString::Join(Order, TEXT(",")), FString(TEXT("Packed,Busy,Alpha,beta,Aardvark mismatch,Gone")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxConnectPasswordTest, "Velox.Online.ConnectPassword", VeloxOnlineTest::Flags)

bool FVeloxConnectPasswordTest::RunTest(const FString& Parameters)
{
	FString Url;
	FString Error;
	TestTrue(TEXT("empty password is fine"), VeloxMenuMath::AppendPasswordOption(TEXT("1.2.3.4:7777?Name=Bo"), FString(), Url, Error));
	TestEqual(TEXT("empty password adds nothing"), Url, FString(TEXT("1.2.3.4:7777?Name=Bo")));
	TestTrue(TEXT("password appended"), VeloxMenuMath::AppendPasswordOption(TEXT("1.2.3.4:7777?Name=Bo"), TEXT("s3cret-x"), Url, Error));
	TestEqual(TEXT("password option"), Url, FString(TEXT("1.2.3.4:7777?Name=Bo?Password=s3cret-x")));
	for (const TCHAR* Bad : { TEXT("a b"), TEXT("a?b"), TEXT("a#b"), TEXT("a\"b"), TEXT("a\\b"), TEXT("a\tb") })
	{
		Error.Reset();
		TestFalse(*FString::Printf(TEXT("refused: %s"), Bad), VeloxMenuMath::AppendPasswordOption(TEXT("x"), Bad, Url, Error));
		TestFalse(TEXT("refusal has a reason"), Error.IsEmpty());
	}

	// The directory exists on clients only.
	TestTrue(TEXT("client process"), UVeloxServerDirectorySubsystem::IsClientProcess(false, false, false));
	TestFalse(TEXT("dedicated server"), UVeloxServerDirectorySubsystem::IsClientProcess(true, false, false));
	TestFalse(TEXT("commandlet"), UVeloxServerDirectorySubsystem::IsClientProcess(false, true, false));
	TestFalse(TEXT("headless host with -ServerId"), UVeloxServerDirectorySubsystem::IsClientProcess(false, false, true));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
