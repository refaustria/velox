// Velox - game instance. Set via GameInstanceClass in Config/DefaultEngine.ini.

#pragma once

#include "Engine/GameInstance.h"

#include "VeloxGameInstance.generated.h"

UCLASS(Config = Game)
class VELOX_API UVeloxGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UVeloxGameInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UGameInstance interface
	virtual void Init() override;
	/** A new world needs the sound mix again (ADR 0004 section 7). */
	virtual void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld) override;
	//~End of UGameInstance interface

	/** Address plus `?Name=<UVeloxGameUserSettings::PlayerName>`: the URL to ClientTravel to (the menu's connect, task 10). */
	UFUNCTION(BlueprintCallable, Category = "Velox|Settings")
	FString BuildConnectURL(const FString& Address) const;
};
