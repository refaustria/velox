// Velox - player controller. Pushes the default Enhanced Input mapping context for local players.

#pragma once

#include "GameFramework/PlayerController.h"

#include "VeloxPlayerController.generated.h"

class UInputMappingContext;

UCLASS(Config = Game)
class VELOX_API AVeloxPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AVeloxPlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	//~APlayerController interface
	virtual void SetupInputComponent() override;
	/**
	 * Super runs the input stack and then UpdateRotation; afterwards this frame's ability input goes to the ASC, so a
	 * fire ability reads the control rotation including this frame's mouse delta (ADR 0003 B6).
	 */
	virtual void PlayerTick(float DeltaTime) override;
	//~End of APlayerController interface

	/** Mapping context added for local players. Set in DefaultGame.ini or a Blueprint subclass. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Input")
	TSoftObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Input")
	int32 DefaultMappingContextPriority = 0;
};
