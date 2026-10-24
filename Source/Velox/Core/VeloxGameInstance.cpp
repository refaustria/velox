// Velox - game instance.

#include "Core/VeloxGameInstance.h"

#include "Core/VeloxLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameInstance)

UVeloxGameInstance::UVeloxGameInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UVeloxGameInstance::Init()
{
	Super::Init();

	UE_LOG(LogVelox, Log, TEXT("UVeloxGameInstance initialised."));
}
