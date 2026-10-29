// Velox - game state: match phase replication (ADR 0004 section 5; task 05). Kept apart from VeloxGameState.cpp so
// the kill feed (task 06) and the server-time work (08d) merge without overlapping hunks.

#include "Combat/VeloxCombatSettings.h"
#include "GameFramework/PlayerState.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxGameState.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"

void AVeloxGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, Phase, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, PhaseStartServerTime, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, PhaseEndServerTime, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, LiveStartServerTime, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, FragLimit, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, TimeLimitSeconds, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, OvertimeCount, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, WinnerPlayerState, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, KillFeed, Params);

	FDoRepLifetimeParams InitialParams;
	InitialParams.bIsPushBased = true;
	InitialParams.Condition = COND_InitialOnly;
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, GameModeDefinition, InitialParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxGameState, CombatSettingsOverride, InitialParams);
}

void AVeloxGameState::InitMatchRules(const UVeloxGameModeDefinition* InDefinition, int32 InFragLimit, int32 InTimeLimitSeconds)
{
	GameModeDefinition = InDefinition;
	CombatSettingsOverride = InDefinition ? InDefinition->CombatSettingsOverride : nullptr;
	FragLimit = InFragLimit;
	TimeLimitSeconds = InTimeLimitSeconds;
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, GameModeDefinition, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, CombatSettingsOverride, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, FragLimit, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, TimeLimitSeconds, this);
}

void AVeloxGameState::SetMatchPhaseState(const FVeloxMatchPhaseState& State)
{
	const EVeloxMatchPhase OldPhase = Phase;
	Phase = State.Phase;
	PhaseStartServerTime = State.PhaseStartServerTime;
	PhaseEndServerTime = State.PhaseEndServerTime;
	LiveStartServerTime = State.LiveStartServerTime;
	OvertimeCount = State.OvertimeCount;
	WinnerPlayerState = State.Winner;
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, Phase, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, PhaseStartServerTime, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, PhaseEndServerTime, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, LiveStartServerTime, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, OvertimeCount, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, WinnerPlayerState, this);
	// 10 Hz (AInfo default) would add up to 100 ms to every phase change.
	ForceNetUpdate();
	OnPhaseChanged.Broadcast(OldPhase, Phase);
}

void AVeloxGameState::OnRep_Phase(EVeloxMatchPhase OldPhase)
{
	UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch client phase=%s old=%s start=%.3f end=%.3f live_start=%.3f overtime=%d winner=%s server_now=%.3f"), LexToString(Phase), LexToString(OldPhase),
		PhaseStartServerTime, PhaseEndServerTime, LiveStartServerTime, OvertimeCount, *GetNameSafe(WinnerPlayerState), GetServerWorldTimeSeconds());
	OnPhaseChanged.Broadcast(OldPhase, Phase);
}
