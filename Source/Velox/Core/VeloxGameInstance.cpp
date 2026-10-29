// Velox - game instance.

#include "Core/VeloxGameInstance.h"

#include "Core/VeloxLog.h"
#include "UI/VeloxGameUserSettings.h"

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

void UVeloxGameInstance::OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld)
{
	Super::OnWorldChanged(OldWorld, NewWorld);

	if (NewWorld)
	{
		if (UVeloxGameUserSettings* Settings = UVeloxGameUserSettings::Get())
		{
			Settings->ApplyAudio(NewWorld);
		}
	}
}

FString UVeloxGameInstance::BuildConnectURL(const FString& Address) const
{
	return VeloxSettingsMath::AppendNameOption(Address, UVeloxGameUserSettings::GetOrDefault().PlayerName);
}
