// Velox - server heartbeat (ADR 0007 section 2, task M6-04).

#include "Net/VeloxServerHeartbeatSubsystem.h"

#include "Core/VeloxMapRegistry.h"
#include "Core/VeloxOnlineSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxGameState.h"
#include "HAL/PlatformTime.h"
#include "Http.h"
#include "Misc/CommandLine.h"
#include "Misc/NetworkVersion.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY(LogVeloxOnline);

namespace
{
	/** Tick period of the scheduler (real seconds); bounds how late a debounced post can be. */
	constexpr float SchedulerPeriod = 0.1f;
}

bool UVeloxServerHeartbeatSubsystem::WantsHeartbeat()
{
	FString Id;
	if (FParse::Value(FCommandLine::Get(), TEXT("ServerId="), Id) && !Id.IsEmpty())
	{
		return true;
	}
	return UVeloxOnlineSettings::Get().bHeartbeatEnabled;
}

bool UVeloxServerHeartbeatSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Whether this instance really hosts is only known once a world runs (ClassifyWorld); here only the cheap exclusions.
	return !IsRunningCommandlet() && WantsHeartbeat() && Super::ShouldCreateSubsystem(Outer);
}

void UVeloxServerHeartbeatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FParse::Value(FCommandLine::Get(), TEXT("ServerId="), ServerId);
	FParse::Value(FCommandLine::Get(), TEXT("ServerName="), ServerName);
	bHeadless = FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));
	PostLoginHandle = FGameModeEvents::OnGameModePostLoginEvent().AddUObject(this, &UVeloxServerHeartbeatSubsystem::OnPostLogin);
	LogoutHandle = FGameModeEvents::OnGameModeLogoutEvent().AddUObject(this, &UVeloxServerHeartbeatSubsystem::OnLogout);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UVeloxServerHeartbeatSubsystem::Tick), SchedulerPeriod);
}

void UVeloxServerHeartbeatSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FGameModeEvents::OnGameModePostLoginEvent().Remove(PostLoginHandle);
	FGameModeEvents::OnGameModeLogoutEvent().Remove(LogoutHandle);
	UnbindGameState();
	Super::Deinitialize();
}

void UVeloxServerHeartbeatSubsystem::MarkDirty(const TCHAR* Reason)
{
	if (!bHosting)
	{
		return;
	}
	if (!bDirty)
	{
		bDirty = true;
		DirtySince = FPlatformTime::Seconds();
		UE_LOG(LogVeloxOnline, Verbose, TEXT("VeloxHeartbeat dirty reason=%s"), Reason);
	}
}

void UVeloxServerHeartbeatSubsystem::OnPhaseChanged(EVeloxMatchPhase OldPhase, EVeloxMatchPhase NewPhase)
{
	MarkDirty(TEXT("phase"));
}

void UVeloxServerHeartbeatSubsystem::OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	MarkDirty(TEXT("join"));
}

void UVeloxServerHeartbeatSubsystem::OnLogout(AGameModeBase* GameMode, AController* Exiting)
{
	MarkDirty(TEXT("leave"));
}

void UVeloxServerHeartbeatSubsystem::BindGameState(AVeloxGameState* GameState)
{
	UnbindGameState();
	if (GameState)
	{
		PhaseHandle = GameState->OnPhaseChanged.AddUObject(this, &UVeloxServerHeartbeatSubsystem::OnPhaseChanged);
		BoundGameState = GameState;
	}
}

void UVeloxServerHeartbeatSubsystem::UnbindGameState()
{
	if (AVeloxGameState* Old = BoundGameState.Get())
	{
		Old->OnPhaseChanged.Remove(PhaseHandle);
	}
	PhaseHandle.Reset();
	BoundGameState.Reset();
}

void UVeloxServerHeartbeatSubsystem::ClassifyWorld(const UWorld* World)
{
	// Re-checked whenever the world changes (travel): only a listen or dedicated server world hosts. A client or a standalone
	// instance (menu, tests, PIE client) never posts, whatever the command line says.
	ClassifiedWorld = World;
	const ENetMode Mode = World->GetNetMode();
	const bool bNowHosting = Mode == NM_ListenServer || Mode == NM_DedicatedServer;
	if (!bNowHosting)
	{
		UnbindGameState();
	}
	bHosting = bNowHosting;
	bDirty = false;
	UE_LOG(LogVeloxOnline, Log, TEXT("VeloxHeartbeat %s netmode=%d map=%s"), bNowHosting ? TEXT("hosting") : TEXT("inactive (not a server)"), static_cast<int32>(Mode),
		*UVeloxMapRegistry::ShortNameOfWorld(World));
	if (bNowHosting)
	{
		// A fresh world: report it after the debounce, which also lets bots and the host's own login settle.
		bDirty = true;
		DirtySince = FPlatformTime::Seconds();
		NextDueTime = DirtySince + UVeloxOnlineSettings::Get().HeartbeatIntervalSeconds;
	}
}

bool UVeloxServerHeartbeatSubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !World->HasBegunPlay())
	{
		return true;
	}
	if (ClassifiedWorld.Get() != World)
	{
		ClassifyWorld(World);
	}
	if (!bHosting)
	{
		return true;
	}
	if (AVeloxGameState* GameState = World->GetGameState<AVeloxGameState>(); GameState != BoundGameState.Get())
	{
		BindGameState(GameState);
		if (GameState)
		{
			MarkDirty(TEXT("gamestate"));
		}
	}

	const UVeloxOnlineSettings& Settings = UVeloxOnlineSettings::Get();
	const double Now = FPlatformTime::Seconds();
	const bool bDue = Now >= NextDueTime;
	const bool bDebounced = bDirty && Now - DirtySince >= Settings.HeartbeatDebounceSeconds;
	if ((bDue || bDebounced) && BoundGameState.IsValid())
	{
		PostNow(bDebounced ? TEXT("event") : TEXT("interval"));
	}
	return true;
}

bool UVeloxServerHeartbeatSubsystem::BuildSnapshot(const UWorld* World, FVeloxServerSnapshot& Out) const
{
	const AVeloxGameState* GameState = World ? World->GetGameState<AVeloxGameState>() : nullptr;
	if (!GameState || !World->GetNetDriver())
	{
		return false;
	}
	const UVeloxOnlineSettings& Settings = UVeloxOnlineSettings::Get();
	const UVeloxMapRegistry& Registry = UVeloxMapRegistry::Get();

	int32 Port = 0;
	const FString Address = World->GetNetDriver()->LowLevelGetNetworkNumber();
	int32 Colon = INDEX_NONE;
	if (Address.FindLastChar(TEXT(':'), Colon))
	{
		Port = FCString::Atoi(*Address.Mid(Colon + 1));
	}
	if (Port <= 0)
	{
		Port = World->URL.Port;
	}

	Out = FVeloxServerSnapshot();
	Out.Id = ServerId.IsEmpty() ? FString::Printf(TEXT("port%d"), Port) : ServerId;
	Out.Name = ServerName.IsEmpty() ? Settings.DefaultServerName : ServerName;
	Out.Port = Port;
	Out.Map = UVeloxMapRegistry::ShortNameOfWorld(World);
	Out.MapDisplayName = Registry.DisplayNameFor(Out.Map);
	const UVeloxGameModeDefinition* Definition = GameState->GetGameModeDefinition();
	Out.Mode = Definition && Definition->ModeTag.IsValid() ? VeloxServerStatus::TagLeaf(Definition->ModeTag.ToString()) : FString(TEXT("Free"));
	Out.Phase = LexToString(GameState->GetPhase());
	Out.TimeLeftSeconds = VeloxServerStatus::TimeLeftSeconds(GameState->GetPhase(), GameState->GetServerWorldTimeSeconds(), GameState->GetPhaseEndServerTime(),
		GameState->GetLiveStartServerTime(), GameState->GetTimeLimitSeconds());
	Out.MaxPlayers = Definition ? Definition->MaxActivePlayers : 0;

	for (const APlayerState* PS : GameState->PlayerArray)
	{
		if (!PS)
		{
			continue;
		}
		if (PS->IsABot())
		{
			++Out.Bots;
		}
		else if (PS->IsOnlyASpectator())
		{
			// The headless host's own spectator-only player is not a spectator anyone can see (ADR 0007 section 1).
			const APlayerController* Owner = Cast<APlayerController>(PS->GetOwner());
			if (!(bHeadless && Owner && Owner->IsLocalController()))
			{
				++Out.Spectators;
			}
		}
		else
		{
			++Out.Players;
		}
	}
	Out.Version = FNetworkVersion::GetProjectVersion();
	Out.NetVersion = FNetworkVersion::GetLocalNetworkVersion();
	Out.bPassword = false; // Task 07 sets it.
	return true;
}

bool UVeloxServerHeartbeatSubsystem::PostNow(const TCHAR* Reason)
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const UVeloxOnlineSettings& Settings = UVeloxOnlineSettings::Get();
	const double Now = FPlatformTime::Seconds();
	if (bInFlight && Now - RequestStartTime < Settings.HeartbeatTimeoutSeconds + 1.0)
	{
		return false; // The previous post is still out; bDirty and the due time stay, the next tick retries.
	}
	FVeloxServerSnapshot Snapshot;
	if (!bHosting || !BuildSnapshot(World, Snapshot))
	{
		return false;
	}
	if (Snapshot.Port <= 0)
	{
		if (!bLoggedBadPort)
		{
			bLoggedBadPort = true;
			UE_LOG(LogVeloxOnline, Warning, TEXT("VeloxHeartbeat no listen port yet, not posting"));
		}
		return false;
	}

	const FString Body = VeloxServerStatus::BuildHeartbeat(Snapshot);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Settings.HeartbeatUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetTimeout(Settings.HeartbeatTimeoutSeconds);
	Request->SetContentAsString(Body);
	const FString ReasonText = Reason;
	Request->OnProcessRequestComplete().BindWeakLambda(this, [this, ReasonText](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		HandleResponse(bConnected && Response.IsValid(), Response.IsValid() ? Response->GetResponseCode() : 0, Response.IsValid() ? Response->GetContentAsString() : FString(), ReasonText);
	});
	bInFlight = true;
	RequestStartTime = Now;
	bDirty = false;
	NextDueTime = Now + Settings.HeartbeatIntervalSeconds;
	if (!Request->ProcessRequest())
	{
		bInFlight = false;
		HandleResponse(false, 0, FString(), ReasonText);
		return false;
	}
	UE_LOG(LogVeloxOnline, Verbose, TEXT("VeloxHeartbeat post reason=%s body=%s"), Reason, *Body);
	return true;
}

void UVeloxServerHeartbeatSubsystem::HandleResponse(bool bConnected, int32 Code, const FString& Body, const FString& Reason)
{
	bInFlight = false;
	const UVeloxOnlineSettings& Settings = UVeloxOnlineSettings::Get();
	if (bConnected && Code >= 200 && Code < 300)
	{
		++NumPosted;
		if (bLastFailed || NumPosted == 1)
		{
			UE_LOG(LogVeloxOnline, Log, TEXT("VeloxHeartbeat ok code=%d reason=%s url=%s"), Code, *Reason, *Settings.HeartbeatUrl);
		}
		else
		{
			UE_LOG(LogVeloxOnline, Verbose, TEXT("VeloxHeartbeat ok code=%d reason=%s"), Code, *Reason);
		}
		bLastFailed = false;
		return;
	}
	bLastFailed = true;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastFailureLogTime >= Settings.HeartbeatFailureLogSeconds)
	{
		LastFailureLogTime = Now;
		UE_LOG(LogVeloxOnline, Warning, TEXT("VeloxHeartbeat failed code=%d url=%s"), Code, *Settings.HeartbeatUrl);
	}
}
