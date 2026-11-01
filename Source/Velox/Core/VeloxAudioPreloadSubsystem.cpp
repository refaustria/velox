// Velox - owner of the preloaded audio assets (task M6-01b).

#include "Core/VeloxAudioPreloadSubsystem.h"

#include "Core/VeloxAudioSettings.h"
#include "Engine/Engine.h"
#include "UObject/SoftObjectPath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAudioPreloadSubsystem)

UVeloxAudioPreloadSubsystem* UVeloxAudioPreloadSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UVeloxAudioPreloadSubsystem>() : nullptr;
}

void UVeloxAudioPreloadSubsystem::PreloadOnce(const UVeloxAudioSettings& Settings)
{
	if (bPreloaded)
	{
		return;
	}
	bPreloaded = true;
	TArray<FSoftObjectPath> Paths;
	Settings.GetPreloadPaths(Paths);
	for (const FSoftObjectPath& Path : Paths)
	{
		if (UObject* Object = Path.TryLoad())
		{
			LoadedAssets.Add(Object);
		}
		else
		{
			UE_LOG(LogVeloxAudio, Warning, TEXT("VeloxAudio asset %s could not be loaded"), *Path.ToString());
		}
	}
	UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio preloaded %d assets (engine subsystem)"), LoadedAssets.Num());
}

void UVeloxAudioPreloadSubsystem::Deinitialize()
{
	LoadedAssets.Reset();
	bPreloaded = false;
	Super::Deinitialize();
}
