// Velox - local player. Set via LocalPlayerClassName in Config/DefaultEngine.ini.

#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"

#include "VeloxLocalPlayer.generated.h"

/**
 * Sends the configured player name at login. UPendingNetGame builds the login URL from ULocalPlayer::GetNickname() and
 * overrides the `?Name=` of the travel URL with it, so the connect URL's name never reaches the server; the game-specific
 * login options come after the nickname, and FURL::AddOption replaces a key that is already there, so `Name=` from here wins
 * (M3-13 F5; found by M3-10).
 */
UCLASS()
class VELOX_API UVeloxLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	/** `Name=<UVeloxGameUserSettings::PlayerName, sanitised>`, empty when the name is empty. */
	virtual FString GetGameLoginOptions() const override;
};
