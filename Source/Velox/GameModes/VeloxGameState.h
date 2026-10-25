// Velox - game state. Match timer and item timers arrive in M3.

#pragma once

#include "GameFramework/GameState.h"
#include "GameplayTagContainer.h"

#include "VeloxGameState.generated.h"

class AVeloxPlayerState;

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

UCLASS(Config = Game)
class VELOX_API AVeloxGameState : public AGameState
{
	GENERATED_BODY()

public:
	AVeloxGameState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Server-local in M2 (ADR 0003 section 8); M3 replicates the frag feed and the scoreboard. */
	FOnVeloxKill OnKill;
};
