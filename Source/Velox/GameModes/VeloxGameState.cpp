// Velox - game state.

#include "GameModes/VeloxGameState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameState)

AVeloxGameState::AVeloxGameState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AVeloxGameState::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
	// B31: the engine timer (UpdateServerTimeSeconds) stays as a fallback; this makes the value sent exactly "now".
	if (const UWorld* World = GetWorld())
	{
		ReplicatedWorldTimeSecondsDouble = World->GetTimeSeconds();
	}
}
