// Velox - player controller. Pushes the default Enhanced Input mapping context for local players.

#pragma once

#include "GameFramework/PlayerController.h"

#include "VeloxPlayerController.generated.h"

class UInputMappingContext;
struct FVeloxShotTiming;

/** Flags of AVeloxPlayerController::ClientConfirmHit. */
namespace EVeloxHitConfirmFlags
{
	inline constexpr uint8 None = 0;
	inline constexpr uint8 Kill = 1 << 0;
}

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

	/**
	 * Server -> shooter: shot ShotSeq hit (ADR 0003 section 5; Q3 server-confirmed hit marker, nothing is predicted).
	 * TotalDamage = health + armor removed, clamped to 255; Flags = EVeloxHitConfirmFlags. Unreliable, 4 bytes.
	 */
	UFUNCTION(Client, Unreliable)
	void ClientConfirmHit(uint8 ShotSeq, uint8 TotalDamage, uint8 Flags);

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
