// Velox - player controller. Pushes the default Enhanced Input mapping context for local players.

#pragma once

#include "GameFramework/PlayerController.h"

#include "VeloxPlayerController.generated.h"

class UInputMappingContext;
struct FVeloxShotTiming;

UCLASS(Config = Game)
class VELOX_API AVeloxPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AVeloxPlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Lag-compensation timing for a shot along Origin/Dir fired now (ADR 0003 section 6, B6: call where the fire
	 * ability samples its aim): ViewServerTime and the rendered-proxy hint. Include Net/VeloxLagCompensationSubsystem.h.
	 */
	FVeloxShotTiming BuildShotTiming(const FVector& Origin, const FVector& Dir, float RangeCm) const;

protected:
	//~APlayerController interface
	virtual void SetupInputComponent() override;
	/** Runs after the pawn's input bindings fired: hands this frame's ability input to the ASC (ADR 0003 section 1). */
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
	//~End of APlayerController interface

	/** Mapping context added for local players. Set in DefaultGame.ini or a Blueprint subclass. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Input")
	TSoftObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Input")
	int32 DefaultMappingContextPriority = 0;
};
