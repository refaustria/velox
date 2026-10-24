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
	//~End of UGameInstance interface
};
