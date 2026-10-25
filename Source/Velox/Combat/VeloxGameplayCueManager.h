// Velox - gameplay cue manager (ADR 0001 section 2, ADR 0003 section 5). M2 cues are handled natively on the pawn; the
// dedicated server never loads cue notify libraries (its cues are suppressed anyway).

#pragma once

#include "GameplayCueManager.h"

#include "VeloxGameplayCueManager.generated.h"

UCLASS()
class VELOX_API UVeloxGameplayCueManager : public UGameplayCueManager
{
	GENERATED_BODY()

public:
	//~UGameplayCueManager interface
	virtual bool ShouldAsyncLoadRuntimeObjectLibraries() const override;
	//~End of UGameplayCueManager interface
};
