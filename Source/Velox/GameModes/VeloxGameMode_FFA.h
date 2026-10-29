// Velox - free-for-all (ADR 0001 section 1, ADR 0004 section 5). All logic lives in AVeloxGameMode; this class only
// selects DA_GameMode_FFA (DefaultGame.ini) and exists for ?game= and per-map defaults.

#pragma once

#include "GameModes/VeloxGameMode.h"

#include "VeloxGameMode_FFA.generated.h"

UCLASS(Config = Game)
class VELOX_API AVeloxGameMode_FFA : public AVeloxGameMode
{
	GENERATED_BODY()
};
