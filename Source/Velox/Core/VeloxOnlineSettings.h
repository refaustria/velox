// Velox - online tunables (ADR 0007 section 2, task M6-04): where the server posts its heartbeat, where the client reads the
// server list, and the limits the hardening task (07) and the browser (05) share. Config = Game, DefaultConfig:
// Config/DefaultGame.ini [/Script/Velox.VeloxOnlineSettings]; the command line can override any key
// (-ini:Game:[/Script/Velox.VeloxOnlineSettings]:HeartbeatUrl=...).

#pragma once

#include "Engine/DeveloperSettings.h"

#include "VeloxOnlineSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, Meta = (DisplayName = "Velox Online"))
class VELOX_API UVeloxOnlineSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	//~UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return TEXT("Velox"); }
	//~End of UDeveloperSettings interface

	/** The project's settings (class defaults are valid without a config file, so this never returns null). */
	static const UVeloxOnlineSettings& Get();

	/** Client: the static file veloxmaster publishes (task 05). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Browser")
	FString ServerListUrl = TEXT("https://games.inceptum.at/status/servers.json");

	/** Client: minimum seconds between two automatic server list refreshes (task 05). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Browser", Meta = (ClampMin = "1", Units = "s"))
	float ServerListRefreshSeconds = 10.f;

	/** Server: veloxmaster's heartbeat endpoint (loopback only, ADR 0007 section 2). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Heartbeat")
	FString HeartbeatUrl = TEXT("http://127.0.0.1:8790/api/v1/heartbeat");

	/** Server: a heartbeat at least this often (veloxmaster's TTL is 90 s). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Heartbeat", Meta = (ClampMin = "1", Units = "s"))
	float HeartbeatIntervalSeconds = 30.f;

	/** Server: a phase change, join or leave sends one extra heartbeat this long after the first of them. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Heartbeat", Meta = (ClampMin = "0", Units = "s"))
	float HeartbeatDebounceSeconds = 2.f;

	/** Server: HTTP timeout of one heartbeat. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Heartbeat", Meta = (ClampMin = "1", Units = "s"))
	float HeartbeatTimeoutSeconds = 5.f;

	/** Server: failure warnings are logged at most once per this many seconds. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Heartbeat", Meta = (ClampMin = "0", Units = "s"))
	float HeartbeatFailureLogSeconds = 60.f;

	/** Server master switch: false = never post. True still posts only with -ServerId= on the command line (the systemd unit passes it). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Heartbeat")
	bool bHeartbeatEnabled = true;

	/** Server name when -ServerName= is absent. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Heartbeat")
	FString DefaultServerName = TEXT("Velox");

	/** Characters of a player name the server keeps (task 07). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Limits", Meta = (ClampMin = "1"))
	int32 MaxPlayerNameLength = 24;
};
