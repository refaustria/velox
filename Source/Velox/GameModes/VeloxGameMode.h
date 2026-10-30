// Velox - base game mode. AGameMode (not AGameModeBase) for its match-state machine (ADR 0001 section 1).
// Death bookkeeping, respawn and spawn selection live here (ADR 0003 section 8).

#pragma once

#include "GameFramework/GameMode.h"
#include "GameModes/VeloxMatchRules.h"
#include "GameplayTagContainer.h"
#include "Math/RandomStream.h"

#include "VeloxGameMode.generated.h"

class AVeloxCharacter;
class AVeloxPlayerController;
class AVeloxPlayerState;
class UVeloxCombatSettings;
class UVeloxGameModeDefinition;

UCLASS(Config = Game)
class VELOX_API AVeloxGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AVeloxGameMode(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Server, from AVeloxCharacter::HandleDeath (B8 order, synchronous): scores the frag (Q3: +1 for the killer, -1 for a
	 * suicide or world kill) and broadcasts AVeloxGameState::OnKill. Killer may be null (no instigator).
	 */
	void PlayerDied(AVeloxPlayerState* Victim, AVeloxPlayerState* Killer, const FGameplayTagContainer& DamageTags);

	/**
	 * Server, after the dead pawn was unpossessed: the controller now waits for a respawn. Humans respawn on a request
	 * after MinRespawnDelay or forced after ForceRespawnDelay; AI controllers (dummies, M4 bots) respawn on their own
	 * after MinRespawnDelay when bAutoRespawnAIControllers.
	 */
	void NotifyControllerDied(AController* Controller);

	/** True while Controller has died and has not respawned yet. */
	bool IsAwaitingRespawn(const AController* Controller) const { return PendingRespawns.Contains(Controller); }

	/**
	 * Respawns Controller if it is awaiting a respawn and (bForced or MinRespawnDelay has passed). Returns true if the
	 * player was restarted. Called by AVeloxPlayerController::ServerRequestRespawn and by the timers.
	 */
	bool TryRespawn(AController* Controller, bool bForced);

	/** Hands PlayerState the first unused palette colour (wraps around when all are taken). Idempotent. */
	void AssignPlayerColor(AVeloxPlayerState& PlayerState);

	/**
	 * Server, from UVeloxBotSubsystem::AddBot (ADR 0005 section 12): counts the bot (AGameMode::NumBots), restarts it
	 * through RestartPlayer (the AI path; not during PostMatch, the restart spawns everyone) and asks for a match
	 * evaluation (a join: AI counts as active and is always ready, ADR 0004 C8).
	 */
	void StartBot(AController* Bot);

	/** The combat settings of Controller's PlayerState (class defaults when unavailable); never null. */
	static const UVeloxCombatSettings& GetCombatSettingsFor(const AController* Controller);

	//~AGameModeBase interface
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override;
	/** Controllers with a fixed respawn (test dummies, B28) restart at their transform; everyone else uses ChoosePlayerStart. */
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;
	virtual void Logout(AController* Exiting) override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	/** Super, then the URL's bots (?Bots=N, ?BotsFill=1, ?BotDifficulty=Hard; UVeloxBotSettings::DefaultBotCount without ?Bots=). */
	virtual void StartPlay() override;
	/** Makes a joiner a spectator when every player slot of the mode is taken (ADR 0004 section 5). */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	/** Level reset (match restart): drops every pending respawn. */
	virtual void Reset() override;
	/** Local spectator pawns survive ResetLevel (APawn::Reset would destroy them: they have no controller). */
	virtual bool ShouldReset_Implementation(AActor* ActorToReset) override;
	//~End of AGameModeBase interface

	//~AGameMode interface
	/** Match rules on: the engine match starts as soon as the GameState exists (players spawn and frag in Warmup). */
	virtual bool ReadyToStartMatch_Implementation() override;
	//~End of AGameMode interface

	// ---- Match phase machine (ADR 0004 section 5; task 05) ----

	/** False without a mode definition (base AVeloxGameMode on the M1/M2 test maps): free play, the phase stays Warmup. */
	bool AreMatchRulesEnabled() const { return bMatchRulesEnabled; }
	const UVeloxGameModeDefinition* GetGameModeDefinition() const { return GameModeDefinition; }
	/** The effective rules (definition plus ?FragLimit= / ?TimeLimit= URL overrides). */
	const FVeloxMatchRulesConfig& GetMatchRules() const { return MatchRules; }
	EVeloxMatchPhase GetPhase() const { return Phase; }

	/** Server: ready-up from AVeloxPlayerController::ServerSetReady. Spectators are ignored. */
	void SetPlayerReady(AController* Controller, bool bReady);
	/** Server: a spectator asks for a player slot (ServerRequestJoin). Returns true if it became an active player. */
	bool RequestJoin(APlayerController* Controller);
	/** Server, cheat velox.match.Phase: enters Phase as the rules would (Live and Warmup reset the level). */
	void ForcePhase(EVeloxMatchPhase NewPhase);
	/** Server: evaluate the rules on the next tick (kills, joins, leaves, ready changes call this). */
	void RequestMatchEvaluation();
	/** Active (non-spectator, not leaving) players, AI included. */
	int32 GetNumActivePlayers() const;

protected:
	//~AGameMode interface
	/** First match start: phase Warmup and the rules timer. Also runs when RestartMatch puts the match back to InProgress. */
	virtual void HandleMatchHasStarted() override;
	//~End of AGameMode interface

	/** AI controllers (dummies, bots) respawn by themselves MinRespawnDelay after dying. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Respawn")
	bool bAutoRespawnAIControllers = true;

	/** Fixed seed for the spawn pick (tests); 0 = random. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Respawn")
	int32 SpawnRandomSeed = 0;

	/**
	 * Mode definition (ADR 0004 section 5). Set per subclass in DefaultGame.ini (AVeloxGameMode_FFA / _Duel). Empty on
	 * the base class: match rules off, which keeps the M1/M2 soaks and test maps in free play.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Match")
	TSoftObjectPtr<UVeloxGameModeDefinition> GameModeDefinitionAsset;

	/** Period of the rules timer that catches anything without an event (AI joins, clock edges); ADR 0004: 1 Hz. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Match", meta = (ClampMin = "0.1", Units = "s"))
	float RulesTimerIntervalSeconds = 1.f;

	/** Actor tag of the map's intermission camera (first ACameraActor with it); none = each player keeps their pawn view. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Match")
	FName IntermissionCameraTag = TEXT("Intermission");

private:
	struct FPendingRespawn
	{
		double DeathTime = 0.0;
		FTimerHandle Timer;
	};

	/** Living pawn of another controller standing in the capsule volume of Start (a taken spawn point). */
	static bool IsStartOccupied(const FVector& Start, const AVeloxCharacter& Pawn);
	void OnRespawnTimer(TWeakObjectPtr<AController> Controller, bool bForced);

	/** Builds the rule input from the PlayerArray and applies the verdict. */
	void EvaluateMatch();
	void OnRulesTimer();
	FVeloxMatchInput BuildMatchInput() const;
	bool IsActivePlayer(const APlayerState* PlayerState) const;
	void SetPhase(EVeloxMatchPhase NewPhase, EVeloxMatchReason Reason, double PhaseEndTime, APlayerState* Winner);
	/** ResetLevel + engine match back to InProgress + restart every active controller + SetPhase (Live or Warmup). */
	void RestartMatch(EVeloxMatchPhase NewPhase, EVeloxMatchReason Reason, double PhaseEndTime);
	/** PostMatch: EndMatch, GE_MatchFrozen on every ASC, movement off, intermission view. */
	void EnterPostMatch();
	/** Intermission state and view for one controller (Camera, else its own pawn); local controllers get only the client half. */
	void SendIntermissionTo(AVeloxPlayerController* PC, AActor* Camera);
	void ClearAllPendingRespawns();
	void ApplyMatchRulesToGameState();
	AActor* FindIntermissionCamera() const;
	APlayerState* FindPlayerStateById(int32 PlayerId) const;
	double GetServerTime() const;

	UPROPERTY(Transient)
	TObjectPtr<const UVeloxGameModeDefinition> GameModeDefinition;

	FVeloxMatchRulesConfig MatchRules;
	bool bMatchRulesEnabled = false;
	bool bPhaseMachineStarted = false;
	bool bResettingLevel = false;
	bool bEvaluationPending = false;
	bool bActivePlayerLeft = false;
	EVeloxMatchPhase Phase = EVeloxMatchPhase::Warmup;
	double PhaseEndTime = 0.0;
	double LiveStartTime = 0.0;
	int32 OvertimeCount = 0;
	FTimerHandle RulesTimer;
	FTimerHandle PhaseEndTimer;
	/** PlayerStates of controllers inside Logout (still in the PlayerArray until destroyed). */
	TArray<TWeakObjectPtr<const APlayerState>> LeavingPlayerStates;

	TMap<TWeakObjectPtr<const AController>, FPendingRespawn> PendingRespawns;
	FRandomStream SpawnRandom;
	bool bSpawnRandomReady = false;
};
