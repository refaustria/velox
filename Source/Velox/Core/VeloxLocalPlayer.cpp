// Velox - local player (M3-13 F5).

#include "Core/VeloxLocalPlayer.h"

#include "UI/VeloxGameUserSettings.h"
#include "UI/VeloxSettingsMath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxLocalPlayer)

FString UVeloxLocalPlayer::GetGameLoginOptions() const
{
	return VeloxSettingsMath::BuildNameLoginOption(UVeloxGameUserSettings::GetOrDefault().PlayerName);
}
