// Velox - gameplay cue manager.

#include "Combat/VeloxGameplayCueManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameplayCueManager)

bool UVeloxGameplayCueManager::ShouldAsyncLoadRuntimeObjectLibraries() const
{
	return !IsRunningDedicatedServer() && Super::ShouldAsyncLoadRuntimeObjectLibraries();
}
