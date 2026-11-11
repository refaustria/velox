// Velox - server list parsing and classification (ADR 0007 section 2, task M6-05). World-free and header-only so the unit
// tests and the browser share it. The producer is infra/master (servers.json, schema 1); the sender side is
// VeloxServerStatus.h. Parse is tolerant: unknown fields are ignored, a malformed entry is skipped and listed in Error
// without failing the document; a wrong schema or a missing "servers" array fails the document.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/** One row of servers.json (heartbeat fields plus what veloxmaster adds). */
struct FVeloxServerEntry
{
	FString Id;
	FString Name;
	int32 Port = 0;
	FString Map;
	FString MapDisplayName;
	FString Mode;
	FString Phase;
	int32 TimeLeftSeconds = 0;
	int32 Players = 0;
	int32 MaxPlayers = 0;
	int32 Bots = 0;
	int32 Spectators = 0;
	FString Version;
	uint32 NetVersion = 0;
	bool bPassword = false;
	/** "ip:port" as ConnectToServer takes it. */
	FString Address;
	FString Host;
	/** Age of the veloxmaster entry when the file was written (0 when absent). */
	double UpdatedSecondsAgo = 0.0;
};

enum class EVeloxServerCompat : uint8
{
	Ok,
	/** Another ProjectVersion. */
	VersionMismatch,
	/** Another FNetworkVersion hash: the engine refuses the connection. */
	NetVersionMismatch,
	/** No heartbeat for StaleAfterSeconds: probably gone. */
	Stale,
	/** players >= max_players: connectable as a spectator. */
	Full,
};

namespace VeloxServerList
{
	inline constexpr int32 SupportedSchema = 1;
	inline constexpr double DefaultStaleAfterSeconds = 120.0;

	namespace Private
	{
		inline FString GetString(const FJsonObject& Object, const TCHAR* Field)
		{
			FString Value;
			Object.TryGetStringField(Field, Value);
			return Value;
		}

		inline int32 GetInt(const FJsonObject& Object, const TCHAR* Field)
		{
			double Value = 0.0;
			Object.TryGetNumberField(Field, Value);
			return static_cast<int32>(FMath::Clamp(Value, 0.0, 1.0e9));
		}

		/** net_version arrives as a decimal string (what we send) or an integer. */
		inline bool GetNetVersion(const FJsonObject& Object, uint32& Out)
		{
			const TSharedPtr<FJsonValue> Value = Object.TryGetField(TEXT("net_version"));
			if (!Value.IsValid())
			{
				return false;
			}
			if (Value->Type == EJson::Number)
			{
				const double Number = Value->AsNumber();
				if (Number < 0.0 || Number > 4294967295.0)
				{
					return false;
				}
				Out = static_cast<uint32>(Number);
				return true;
			}
			if (Value->Type == EJson::String)
			{
				const FString Text = Value->AsString();
				if (Text.IsEmpty() || Text.Len() > 10)
				{
					return false;
				}
				for (const TCHAR C : Text)
				{
					if (!FChar::IsDigit(C))
					{
						return false;
					}
				}
				const uint64 Wide = FCString::Strtoui64(*Text, nullptr, 10);
				if (Wide > 0xFFFFFFFFull)
				{
					return false;
				}
				Out = static_cast<uint32>(Wide);
				return true;
			}
			return false;
		}
	}

	/**
	 * Parses a servers.json document into Out (replaced). False with Error when the document itself is unusable (not
	 * JSON, schema != 1, no "servers" array); Out is then left untouched. True otherwise; Error then lists the skipped
	 * malformed entries ("entry 2: no address; entry 5: ..."), empty when none were skipped.
	 */
	inline bool Parse(const FString& Json, TArray<FVeloxServerEntry>& Out, FString& Error)
	{
		Error.Reset();
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			Error = TEXT("The server list is not valid JSON.");
			return false;
		}
		double Schema = 0.0;
		if (!Root->TryGetNumberField(TEXT("schema"), Schema) || FMath::RoundToInt(Schema) != SupportedSchema)
		{
			Error = FString::Printf(TEXT("Unsupported server list schema (need %d)."), SupportedSchema);
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Servers = nullptr;
		if (!Root->TryGetArrayField(TEXT("servers"), Servers) || !Servers)
		{
			Error = TEXT("The server list has no \"servers\" array.");
			return false;
		}

		TArray<FVeloxServerEntry> Parsed;
		TArray<FString> Skipped;
		for (int32 Index = 0; Index < Servers->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
			if (!(*Servers)[Index].IsValid() || !(*Servers)[Index]->TryGetObject(ObjectPtr) || !ObjectPtr || !ObjectPtr->IsValid())
			{
				Skipped.Add(FString::Printf(TEXT("entry %d: not an object"), Index));
				continue;
			}
			const FJsonObject& Object = **ObjectPtr;
			FVeloxServerEntry Entry;
			Entry.Address = Private::GetString(Object, TEXT("address"));
			if (Entry.Address.IsEmpty())
			{
				Skipped.Add(FString::Printf(TEXT("entry %d: no address"), Index));
				continue;
			}
			if (!Private::GetNetVersion(Object, Entry.NetVersion))
			{
				Skipped.Add(FString::Printf(TEXT("entry %d: bad net_version"), Index));
				continue;
			}
			Entry.Id = Private::GetString(Object, TEXT("id"));
			Entry.Name = Private::GetString(Object, TEXT("name"));
			Entry.Port = Private::GetInt(Object, TEXT("port"));
			Entry.Map = Private::GetString(Object, TEXT("map"));
			Entry.MapDisplayName = Private::GetString(Object, TEXT("map_display_name"));
			if (Entry.MapDisplayName.IsEmpty())
			{
				Entry.MapDisplayName = Entry.Map;
			}
			Entry.Mode = Private::GetString(Object, TEXT("mode"));
			Entry.Phase = Private::GetString(Object, TEXT("phase"));
			Entry.TimeLeftSeconds = Private::GetInt(Object, TEXT("time_left_s"));
			Entry.Players = Private::GetInt(Object, TEXT("players"));
			Entry.MaxPlayers = Private::GetInt(Object, TEXT("max_players"));
			Entry.Bots = Private::GetInt(Object, TEXT("bots"));
			Entry.Spectators = Private::GetInt(Object, TEXT("spectators"));
			Entry.Version = Private::GetString(Object, TEXT("version"));
			Object.TryGetBoolField(TEXT("password"), Entry.bPassword);
			Entry.Host = Private::GetString(Object, TEXT("host"));
			double Ago = 0.0;
			Entry.UpdatedSecondsAgo = Object.TryGetNumberField(TEXT("updated_s_ago"), Ago) ? FMath::Max(Ago, 0.0) : 0.0;
			if (Entry.Name.IsEmpty())
			{
				Entry.Name = Entry.Address;
			}
			Parsed.Add(MoveTemp(Entry));
		}
		Out = MoveTemp(Parsed);
		Error = FString::Join(Skipped, TEXT("; "));
		return true;
	}

	/**
	 * Rule table (first match wins): net_version differs -> NetVersionMismatch; version differs -> VersionMismatch;
	 * updated_s_ago > StaleAfterSeconds -> Stale; players >= max_players (max_players > 0) -> Full; else Ok.
	 */
	inline EVeloxServerCompat Compatibility(const FVeloxServerEntry& Entry, const FString& LocalVersion, uint32 LocalNetVersion, double StaleAfterSeconds)
	{
		if (Entry.NetVersion != LocalNetVersion)
		{
			return EVeloxServerCompat::NetVersionMismatch;
		}
		if (Entry.Version != LocalVersion)
		{
			return EVeloxServerCompat::VersionMismatch;
		}
		if (Entry.UpdatedSecondsAgo > StaleAfterSeconds)
		{
			return EVeloxServerCompat::Stale;
		}
		if (Entry.MaxPlayers > 0 && Entry.Players >= Entry.MaxPlayers)
		{
			return EVeloxServerCompat::Full;
		}
		return EVeloxServerCompat::Ok;
	}

	/** Default English label of a classification (the browser takes its strings from UVeloxUISettings, which default to these). */
	inline const TCHAR* CompatText(EVeloxServerCompat Compat)
	{
		switch (Compat)
		{
		case EVeloxServerCompat::VersionMismatch:
			return TEXT("other game version");
		case EVeloxServerCompat::NetVersionMismatch:
			return TEXT("incompatible network version");
		case EVeloxServerCompat::Stale:
			return TEXT("not responding");
		case EVeloxServerCompat::Full:
			return TEXT("full - join as spectator");
		default:
			return TEXT("");
		}
	}

	inline const TCHAR* CompatName(EVeloxServerCompat Compat)
	{
		switch (Compat)
		{
		case EVeloxServerCompat::VersionMismatch:
			return TEXT("VersionMismatch");
		case EVeloxServerCompat::NetVersionMismatch:
			return TEXT("NetVersionMismatch");
		case EVeloxServerCompat::Stale:
			return TEXT("Stale");
		case EVeloxServerCompat::Full:
			return TEXT("Full");
		default:
			return TEXT("Ok");
		}
	}

	/** Version mismatches are refused by the engine at connect; Stale and Full are still tried (Full joins as a spectator). */
	inline bool IsConnectable(EVeloxServerCompat Compat)
	{
		return Compat != EVeloxServerCompat::VersionMismatch && Compat != EVeloxServerCompat::NetVersionMismatch;
	}

	/** Compatible rows (Ok, Full) first, then more players first, then name (case-insensitive), then address. */
	inline void Sort(TArray<FVeloxServerEntry>& Entries, const FString& LocalVersion, uint32 LocalNetVersion, double StaleAfterSeconds)
	{
		const auto Usable = [&](const FVeloxServerEntry& Entry)
		{
			const EVeloxServerCompat Compat = Compatibility(Entry, LocalVersion, LocalNetVersion, StaleAfterSeconds);
			return Compat == EVeloxServerCompat::Ok || Compat == EVeloxServerCompat::Full;
		};
		Entries.StableSort([&](const FVeloxServerEntry& A, const FVeloxServerEntry& B)
		{
			const bool bA = Usable(A);
			const bool bB = Usable(B);
			if (bA != bB)
			{
				return bA;
			}
			if (A.Players != B.Players)
			{
				return A.Players > B.Players;
			}
			const int32 NameCompare = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);
			if (NameCompare != 0)
			{
				return NameCompare < 0;
			}
			return A.Address < B.Address;
		});
	}
}
