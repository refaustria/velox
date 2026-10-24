// Velox - asset manager. Set via AssetManagerClassName in Config/DefaultEngine.ini.

#pragma once

#include "Engine/AssetManager.h"

#include "VeloxAssetManager.generated.h"

UCLASS(Config = Game)
class VELOX_API UVeloxAssetManager : public UAssetManager
{
	GENERATED_BODY()

protected:
	//~UAssetManager interface
	virtual void StartInitialLoading() override;
	//~End of UAssetManager interface
};
