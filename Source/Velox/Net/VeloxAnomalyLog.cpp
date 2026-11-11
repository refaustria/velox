// Velox - server anomaly log (ADR 0007 section 5.4, task m6-online/09).

#include "Net/VeloxAnomalyLog.h"

#include "Core/VeloxLog.h"
#include "Core/VeloxServerKind.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAnomalyLog)

namespace VeloxAnomalyLogPrivate
{
	static float IntervalSeconds = 60.f;
	static FAutoConsoleVariableRef CVarInterval(TEXT("velox.net.AnomalyLogIntervalSeconds"), IntervalSeconds,
		TEXT("Server: seconds between two VeloxAnomaly interval lines (ADR 0007 section 5.4; default 60, <= 0 = interval lines off, logout lines stay). Read at world begin play."),
		ECVF_Default);
}

bool UVeloxAnomalyLog::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

void UVeloxAnomalyLog::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LogoutHandle = FGameModeEvents::GameModeLogoutEvent.AddUObject(this, &ThisClass::HandleLogout);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::HandleWorldTearDown);
}

void UVeloxAnomalyLog::Deinitialize()
{
	FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IntervalTimer);
	}
	Super::Deinitialize();
}

void UVeloxAnomalyLog::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// Clients and standalone never count (every NoteAnomaly site is server-side); IsActive() repeats this test per line.
	const ENetMode NetMode = InWorld.GetNetMode();
	if ((NetMode == NM_DedicatedServer || NetMode == NM_ListenServer) && VeloxAnomalyLogPrivate::IntervalSeconds > 0.f)
	{
		InWorld.GetTimerManager().SetTimer(IntervalTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { LogInterval(); }), VeloxAnomalyLogPrivate::IntervalSeconds, true);
	}
}

bool UVeloxAnomalyLog::IsActive() const
{
	// Review 0007-09 F1: the net mode, not VeloxServerKind::IsServerLike (that is for skipping cost). A headless listen
	// host's spectator PlayerController / PlayerState can be gone by OnWorldBeginTearDown, which skipped every at=shutdown
	// line; the counters are server-authority and remote-player only anyway, so the wider gate prints nothing extra.
	const UWorld* World = GetWorld();
	const ENetMode NetMode = World ? World->GetNetMode() : NM_Standalone;
	return NetMode == NM_DedicatedServer || NetMode == NM_ListenServer;
}

void UVeloxAnomalyLog::PrintLine(AVeloxPlayerState& PlayerState, const TCHAR* At)
{
	PlayerState.SampleAnomalyRtt();
	const FVeloxPlayerAnomalyStats& Stats = PlayerState.GetAnomalyStats();
	FVeloxPlayerAnomalyStats& Last = PlayerState.GetAnomalyStatsAtLastLine();
	UE_LOG(LogVeloxNet, Log, TEXT("%s"), *VeloxAnomaly::FormatLine(Stats, AVeloxPlayerState::VeloxPlayerLabel(&PlayerState), PlayerState.GetPlayerId(), Last, At,
		VeloxAnomaly::Exceeds(Stats, Thresholds)));
	Last = Stats;
}

int32 UVeloxAnomalyLog::LogInterval()
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState || !IsActive())
	{
		return 0;
	}
	int32 Lines = 0;
	for (APlayerState* Base : GameState->PlayerArray)
	{
		AVeloxPlayerState* PlayerState = Cast<AVeloxPlayerState>(Base);
		if (PlayerState && !PlayerState->IsAnomalySummaryLogged() && !PlayerState->GetAnomalyStats().SameCounters(PlayerState->GetAnomalyStatsAtLastLine()))
		{
			PrintLine(*PlayerState, TEXT("interval"));
			++Lines;
		}
	}
	return Lines;
}

bool UVeloxAnomalyLog::LogFinal(AVeloxPlayerState& PlayerState, const TCHAR* At)
{
	if (PlayerState.IsAnomalySummaryLogged() || !PlayerState.GetAnomalyStats().HasAnyCounter() || !IsActive())
	{
		return false;
	}
	PrintLine(PlayerState, At);
	PlayerState.SetAnomalySummaryLogged();
	return true;
}

void UVeloxAnomalyLog::HandleLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (GameMode && GameMode->GetWorld() == GetWorld() && Exiting)
	{
		if (AVeloxPlayerState* PlayerState = Exiting->GetPlayerState<AVeloxPlayerState>())
		{
			LogFinal(*PlayerState, TEXT("logout"));
		}
	}
}

void UVeloxAnomalyLog::HandleWorldTearDown(UWorld* InWorld)
{
	if (InWorld != GetWorld())
	{
		return;
	}
	InWorld->GetTimerManager().ClearTimer(IntervalTimer);
	if (const AGameStateBase* GameState = InWorld->GetGameState())
	{
		for (APlayerState* Base : GameState->PlayerArray)
		{
			if (AVeloxPlayerState* PlayerState = Cast<AVeloxPlayerState>(Base))
			{
				LogFinal(*PlayerState, TEXT("shutdown"));
			}
		}
	}
}
