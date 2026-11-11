// Velox - client side of the server directory (ADR 0007 section 2, task M6-05). Fetches servers.json (the static file
// veloxmaster publishes, UVeloxOnlineSettings::ServerListUrl), parses it with the world-free VeloxServerList, keeps the
// sorted rows and tells listeners. Client only: never created on a dedicated server or on a headless host (-ServerId).

#pragma once

#include "Containers/Ticker.h"
#include "Net/VeloxServerList.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "VeloxServerDirectorySubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVeloxServersUpdatedSignature);
DECLARE_MULTICAST_DELEGATE(FVeloxServersUpdatedNative);

UCLASS()
class VELOX_API UVeloxServerDirectorySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	//~End of USubsystem interface

	/**
	 * Rule behind ShouldCreateSubsystem, world-free for tests: false on a dedicated server, in a commandlet and on a
	 * directory server (a process started with -ServerId=, i.e. the headless listen host). TODO: use
	 * VeloxServerKind::IsServerLike once a world exists at subsystem creation (it needs one).
	 */
	static bool IsClientProcess(bool bDedicatedServer, bool bCommandlet, bool bHasServerId);

	/**
	 * Fetches the list (GET, Accept: application/json, timeout UVeloxUISettings::ServerListTimeoutSeconds). Idempotent
	 * while a request is in flight. UrlOverride replaces UVeloxOnlineSettings::ServerListUrl (tests, DumpServers).
	 * Returns whether a request was started.
	 */
	UFUNCTION(BlueprintCallable, Category = "Velox|Online")
	bool Refresh(const FString& UrlOverride = FString());

	/** Starts / stops the periodic Refresh while a browser is open (UVeloxOnlineSettings::ServerListRefreshSeconds). */
	void StartAutoRefresh();
	void StopAutoRefresh();

	/** Sorted rows of the last successful fetch (compatible first). Retained across failed fetches. */
	const TArray<FVeloxServerEntry>& GetServers() const { return Servers; }

	/** Classification of Entry against this build's ProjectVersion and network version. */
	EVeloxServerCompat Classify(const FVeloxServerEntry& Entry) const;

	/** Text of the last failure ("" after a success). */
	UFUNCTION(BlueprintPure, Category = "Velox|Online")
	const FString& GetLastError() const { return LastError; }

	/** Non-fatal note of the last successful parse (skipped entries); "" when none. */
	const FString& GetLastParseNote() const { return LastParseNote; }

	UFUNCTION(BlueprintPure, Category = "Velox|Online")
	bool IsRefreshing() const { return bInFlight; }

	/** True once a fetch has completed (successfully or not). */
	bool HasCompletedFetch() const { return bCompletedFetch; }

	/** Feeds a response through the same path as the HTTP completion (tests). */
	void ApplyResponse(bool bConnected, int32 ResponseCode, const FString& Body);

	/** One log line per row plus a summary (`VeloxServers ...`), for DumpServers and live checks. */
	void LogServers(const TCHAR* Context) const;

	/** Broadcast after every completed fetch, successful or not (LastError tells). */
	UPROPERTY(BlueprintAssignable, Category = "Velox|Online")
	FVeloxServersUpdatedSignature OnServersUpdated;

	/** The same, for C++ listeners (lambdas, one-shot waits). */
	FVeloxServersUpdatedNative OnServersUpdatedNative;

private:
	bool TickAutoRefresh(float DeltaTime);

	TArray<FVeloxServerEntry> Servers;
	FString LastError;
	FString LastParseNote;
	FString RequestUrl;
	bool bInFlight = false;
	bool bCompletedFetch = false;
	FTSTicker::FDelegateHandle AutoRefreshHandle;
};
