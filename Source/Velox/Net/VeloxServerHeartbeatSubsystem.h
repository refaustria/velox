// Velox - server heartbeat (ADR 0007 section 2, task M6-04). A game instance that hosts (listen or dedicated world) reports
// itself to veloxmaster every UVeloxOnlineSettings::HeartbeatIntervalSeconds and, debounced, on every phase change, join and
// leave. A client or standalone instance never posts. Nothing replicates; the events are the engine's FGameModeEvents
// (PostLogin, Logout) and AVeloxGameState::OnPhaseChanged.

#pragma once

#include "Containers/Ticker.h"
#include "Logging/LogMacros.h"
#include "Net/VeloxServerStatus.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "VeloxServerHeartbeatSubsystem.generated.h"

class AGameModeBase;
class AController;
class APlayerController;
class AVeloxGameState;
class UWorld;

VELOX_API DECLARE_LOG_CATEGORY_EXTERN(LogVeloxOnline, Log, All);

UCLASS()
class VELOX_API UVeloxServerHeartbeatSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~End of USubsystem interface

	/** Activation rule: the master switch is on AND a non-empty -ServerId= was given (directory servers only). */
	static bool IsHeartbeatActive(bool bEnabled, const FString& InServerId);

	/** IsHeartbeatActive for this process (command line + UVeloxOnlineSettings::bHeartbeatEnabled). */
	static bool WantsHeartbeat();

	/** A phase change, join or leave happened: one extra heartbeat after UVeloxOnlineSettings::HeartbeatDebounceSeconds. */
	void MarkDirty(const TCHAR* Reason);

	/** Fills Out from World (the GameState must exist). False when the world is not a hosting world or has no GameState. */
	bool BuildSnapshot(const UWorld* World, FVeloxServerSnapshot& Out) const;

	/** Posts now (tests, console). False when nothing could be sent. */
	bool PostNow(const TCHAR* Reason);

	int32 GetNumPosted() const { return NumPosted; }
	bool IsHosting() const { return bHosting; }

private:
	bool Tick(float DeltaTime);
	void ClassifyWorld(const UWorld* World);
	void BindGameState(AVeloxGameState* GameState);
	void UnbindGameState();
	void OnPhaseChanged(EVeloxMatchPhase OldPhase, EVeloxMatchPhase NewPhase);
	void OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void OnLogout(AGameModeBase* GameMode, AController* Exiting);
	void HandleResponse(bool bConnected, int32 Code, const FString& Body, const FString& Reason);

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;
	FDelegateHandle PhaseHandle;
	TWeakObjectPtr<AVeloxGameState> BoundGameState;
	TWeakObjectPtr<const UWorld> ClassifiedWorld;

	bool bHosting = false;
	bool bDirty = false;
	bool bInFlight = false;
	bool bLastFailed = false;
	bool bLoggedBadPort = false;
	int32 NumPosted = 0;
	/** Real-time (FPlatformTime::Seconds) stamps. */
	double NextDueTime = 0.0;
	double DirtySince = 0.0;
	double LastFailureLogTime = -1.0e9;
	double RequestStartTime = 0.0;

	FString ServerId;
	FString ServerName;
	bool bHeadless = false;
};
