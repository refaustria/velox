// Velox - asset manager.

#include "Core/VeloxAssetManager.h"

#include "AbilitySystemGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAssetManager)

void UVeloxAssetManager::StartInitialLoading()
{
	// Performs the primary asset scan; must run before anything below.
	Super::StartInitialLoading();

	// Initialise GAS global data (tag maps, cue manager, target data script structs) once at startup.
	UAbilitySystemGlobals::Get().InitGlobalData();
}
