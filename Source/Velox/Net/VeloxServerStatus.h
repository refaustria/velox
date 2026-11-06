// Velox - server status wire format (ADR 0007 section 2, task M6-04). World-free and header-only so unit tests and the
// server browser (task 05) share it. The receiver is infra/master (src/schema.rs): schema must be 1, the integer fields must
// be non-negative integers, port must be non-zero, id is 1..32 chars of [a-z0-9_-], name 1..64 chars, other strings <= 128,
// counts <= 1024; net_version may be a string or an integer (we send the decimal string). BuildHeartbeat clamps to those
// rules, so a body it produces is always accepted. Task 05 adds the parsing side (VeloxServerList::Parse) in its own header.

#pragma once

#include "CoreMinimal.h"
#include "GameModes/VeloxMatchRules.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

/** Everything one heartbeat reports. The subsystem fills it from the live world; tests fill it by hand. */
struct FVeloxServerSnapshot
{
	FString Id;
	FString Name;
	int32 Port = 0;
	/** Short map name ("L_DM1"). */
	FString Map;
	/** Registry display name ("Praesidium"); the short name when the registry has none. */
	FString MapDisplayName;
	/** Leaf of the mode tag ("FFA", "Duel"). */
	FString Mode;
	/** LexToString(EVeloxMatchPhase). */
	FString Phase;
	int32 TimeLeftSeconds = 0;
	int32 Players = 0;
	int32 MaxPlayers = 0;
	int32 Bots = 0;
	int32 Spectators = 0;
	/** ProjectVersion ("0.6.0"). */
	FString Version;
	/** FNetworkVersion::GetLocalNetworkVersion(). */
	uint32 NetVersion = 0;
	bool bPassword = false;
};

namespace VeloxServerStatus
{
	inline constexpr int32 Schema = 1;
	// Limits of infra/master/src/schema.rs; BuildHeartbeat enforces them.
	inline constexpr int32 MaxId = 32;
	inline constexpr int32 MaxName = 64;
	inline constexpr int32 MaxString = 128;
	inline constexpr int32 MaxCount = 1024;

	/** [a-z0-9_-] only (anything else becomes '-'), at most MaxId chars; empty -> "server". */
	inline FString SanitizeId(const FString& Id)
	{
		FString Out;
		for (const TCHAR C : Id)
		{
			if (Out.Len() >= MaxId)
			{
				break;
			}
			const TCHAR L = FChar::ToLower(C);
			const bool bOk = (L >= TEXT('a') && L <= TEXT('z')) || (L >= TEXT('0') && L <= TEXT('9')) || L == TEXT('_') || L == TEXT('-');
			Out.AppendChar(bOk ? L : TEXT('-'));
		}
		return Out.IsEmpty() ? FString(TEXT("server")) : Out;
	}

	inline FString Truncate(const FString& Text, int32 MaxChars)
	{
		return Text.Len() > MaxChars ? Text.Left(MaxChars) : Text;
	}

	/** Leaf of a dotted tag name: "Velox.Mode.FFA" -> "FFA". Empty stays empty. */
	inline FString TagLeaf(const FString& TagName)
	{
		int32 Dot = INDEX_NONE;
		return TagName.FindLastChar(TEXT('.'), Dot) ? TagName.Mid(Dot + 1) : TagName;
	}

	/**
	 * Whole seconds left on the match clock (ADR 0004 section 6; the HUD's MatchClockText without the formatting), never
	 * negative, 0 when there is nothing to count down (Warmup, Live without a time limit, sudden death).
	 */
	inline int32 TimeLeftSeconds(EVeloxMatchPhase Phase, double Now, double PhaseEnd, double LiveStart, int32 TimeLimitSeconds)
	{
		double Left = 0.0;
		switch (Phase)
		{
		case EVeloxMatchPhase::Warmup:
			break;
		case EVeloxMatchPhase::Live:
			Left = TimeLimitSeconds > 0 ? TimeLimitSeconds - (Now - LiveStart) : 0.0;
			break;
		default: // Countdown, Overtime (timed), PostMatch: time to PhaseEnd; PhaseEnd 0 = open-ended.
			Left = PhaseEnd > 0.0 ? PhaseEnd - Now : 0.0;
			break;
		}
		return Left > 0.0 ? FMath::Min(FMath::CeilToInt32(Left), 24 * 3600) : 0;
	}

	/** The heartbeat body (condensed JSON, field order as the golden). Strings are escaped by the JSON writer. */
	inline FString BuildHeartbeat(const FVeloxServerSnapshot& Snapshot)
	{
		const auto Count = [](int32 Value) { return static_cast<int64>(FMath::Clamp(Value, 0, MaxCount)); };

		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("schema"), static_cast<int64>(Schema));
		Writer->WriteValue(TEXT("id"), SanitizeId(Snapshot.Id));
		Writer->WriteValue(TEXT("name"), Snapshot.Name.IsEmpty() ? FString(TEXT("Velox")) : Truncate(Snapshot.Name, MaxName));
		Writer->WriteValue(TEXT("port"), static_cast<int64>(FMath::Clamp(Snapshot.Port, 0, 65535)));
		Writer->WriteValue(TEXT("map"), Truncate(Snapshot.Map, MaxString));
		Writer->WriteValue(TEXT("map_display_name"), Truncate(Snapshot.MapDisplayName.IsEmpty() ? Snapshot.Map : Snapshot.MapDisplayName, MaxString));
		Writer->WriteValue(TEXT("mode"), Truncate(Snapshot.Mode, MaxString));
		Writer->WriteValue(TEXT("phase"), Truncate(Snapshot.Phase, MaxString));
		Writer->WriteValue(TEXT("time_left_s"), Count(Snapshot.TimeLeftSeconds));
		Writer->WriteValue(TEXT("players"), Count(Snapshot.Players));
		Writer->WriteValue(TEXT("max_players"), Count(Snapshot.MaxPlayers));
		Writer->WriteValue(TEXT("bots"), Count(Snapshot.Bots));
		Writer->WriteValue(TEXT("spectators"), Count(Snapshot.Spectators));
		Writer->WriteValue(TEXT("version"), Truncate(Snapshot.Version, MaxString));
		Writer->WriteValue(TEXT("net_version"), FString::Printf(TEXT("%u"), Snapshot.NetVersion));
		Writer->WriteValue(TEXT("password"), Snapshot.bPassword);
		Writer->WriteObjectEnd();
		Writer->Close();
		return Out;
	}
}
