// Velox - game state. Match timer and item timers arrive in M3.

#pragma once

#include "GameFramework/GameState.h"
#include "GameModes/VeloxMatchRules.h"
#include "GameplayTagContainer.h"

#include "VeloxGameState.generated.h"

class AVeloxPlayerState;
class UVeloxCombatSettings;
class UVeloxGameModeDefinition;

/** One frag (server). Killer == Victim for suicides and world kills. */
struct FVeloxKillEvent
{
	TWeakObjectPtr<AVeloxPlayerState> Victim;
	TWeakObjectPtr<AVeloxPlayerState> Killer;
	/** Spec asset tags of the killing damage (Velox.Damage.<Weapon>, Velox.Damage.Self, ...). */
	FGameplayTagContainer DamageTags;
	/** Server world time of the death. */
	double Time = 0.0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnVeloxKill, const FVeloxKillEvent& /*Event*/);

/** Match phase changed (server: in AVeloxGameMode::SetPhase; clients: OnRep_Phase). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnVeloxMatchPhaseChanged, EVeloxMatchPhase /*OldPhase*/, EVeloxMatchPhase /*NewPhase*/);

/** Everything the server sets on a phase change (AVeloxGameState::SetMatchPhaseState). Times are server world times. */
struct FVeloxMatchPhaseState
{
	EVeloxMatchPhase Phase = EVeloxMatchPhase::Warmup;
	double PhaseStartServerTime = 0.0;
	/** Countdown end / time-limit end / overtime end / intermission end; 0 = open-ended. */
	double PhaseEndServerTime = 0.0;
	double LiveStartServerTime = 0.0;
	int32 OvertimeCount = 0;
	APlayerState* Winner = nullptr;
};

UCLASS(Config = Game)
class VELOX_API AVeloxGameState : public AGameState
{
	GENERATED_BODY()

public:
	AVeloxGameState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Server-local in M2 (ADR 0003 section 8); M3 replicates the frag feed and the scoreboard. */
	FOnVeloxKill OnKill;

	// ---- Match phase (ADR 0004 section 5; task 05). Clients compute every clock from GetServerWorldTimeSeconds(). ----
public:
	//~UObject interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~End of UObject interface

	/** Fires on both sides; on clients only from the replicated Phase. */
	FOnVeloxMatchPhaseChanged OnPhaseChanged;

	EVeloxMatchPhase GetPhase() const { return Phase; }
	double GetPhaseStartServerTime() const { return PhaseStartServerTime; }
	/** 0 = open-ended (Warmup, sudden death, no time limit). */
	double GetPhaseEndServerTime() const { return PhaseEndServerTime; }
	double GetLiveStartServerTime() const { return LiveStartServerTime; }
	int32 GetFragLimit() const { return FragLimit; }
	int32 GetTimeLimitSeconds() const { return TimeLimitSeconds; }
	int32 GetOvertimeCount() const { return OvertimeCount; }
	/** PostMatch winner; null = draw (or before the first match end). */
	APlayerState* GetWinnerPlayerState() const { return WinnerPlayerState; }
	/** Null when the game mode runs without a definition (match rules off: free play, M1/M2 test maps). */
	const UVeloxGameModeDefinition* GetGameModeDefinition() const { return GameModeDefinition; }
	const UVeloxCombatSettings* GetCombatSettingsOverride() const { return CombatSettingsOverride; }

	/** Server (AVeloxGameMode::InitGameState): mode definition and the effective limits (URL overrides applied). */
	void InitMatchRules(const UVeloxGameModeDefinition* InDefinition, int32 InFragLimit, int32 InTimeLimitSeconds);
	/** Server (AVeloxGameMode::SetPhase): replicates the new phase state and broadcasts OnPhaseChanged locally. */
	void SetMatchPhaseState(const FVeloxMatchPhaseState& State);

private:
	UFUNCTION()
	void OnRep_Phase(EVeloxMatchPhase OldPhase);

	UPROPERTY(ReplicatedUsing = OnRep_Phase)
	EVeloxMatchPhase Phase = EVeloxMatchPhase::Warmup;

	UPROPERTY(Replicated)
	double PhaseStartServerTime = 0.0;

	UPROPERTY(Replicated)
	double PhaseEndServerTime = 0.0;

	UPROPERTY(Replicated)
	double LiveStartServerTime = 0.0;

	UPROPERTY(Replicated)
	int32 FragLimit = 0;

	UPROPERTY(Replicated)
	int32 TimeLimitSeconds = 0;

	UPROPERTY(Replicated)
	int32 OvertimeCount = 0;

	UPROPERTY(Replicated)
	TObjectPtr<APlayerState> WinnerPlayerState;

	UPROPERTY(Replicated)
	TObjectPtr<const UVeloxGameModeDefinition> GameModeDefinition;

	UPROPERTY(Replicated)
	TObjectPtr<const UVeloxCombatSettings> CombatSettingsOverride;
};
