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

	//~AActor interface
	/**
	 * ADR 0003 B31: stamps the replicated server time at send time. The engine refreshes it on a 0.1 s timer, so without
	 * this the client's estimate carries a random 0..100 ms replication age (about 50 ms on average). The engine calls
	 * PreReplication only in the frame the actor is actually sent.
	 */
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
	//~End of AActor interface
};
