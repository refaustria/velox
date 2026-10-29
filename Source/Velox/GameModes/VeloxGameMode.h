// Velox - base game mode. AGameMode (not AGameModeBase) for its match-state machine (ADR 0001 section 1).
// Death bookkeeping, respawn and spawn selection live here (ADR 0003 section 8).

#pragma once

#include "GameFramework/GameMode.h"
#include "GameplayTagContainer.h"
#include "Math/RandomStream.h"

#include "VeloxGameMode.generated.h"

class AVeloxCharacter;
class AVeloxPlayerState;
class UVeloxCombatSettings;

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
	//~End of AGameModeBase interface

protected:
	/** AI controllers (dummies, bots) respawn by themselves MinRespawnDelay after dying. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Respawn")
	bool bAutoRespawnAIControllers = true;

	/** Fixed seed for the spawn pick (tests); 0 = random. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Respawn")
	int32 SpawnRandomSeed = 0;

private:
	struct FPendingRespawn
	{
		double DeathTime = 0.0;
		FTimerHandle Timer;
	};

	/** Living pawn of another controller standing in the capsule volume of Start (a taken spawn point). */
	static bool IsStartOccupied(const FVector& Start, const AVeloxCharacter& Pawn);
	void OnRespawnTimer(TWeakObjectPtr<AController> Controller, bool bForced);

	TMap<TWeakObjectPtr<const AController>, FPendingRespawn> PendingRespawns;
	FRandomStream SpawnRandom;
	bool bSpawnRandomReady = false;
};
