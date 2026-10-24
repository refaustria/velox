// Velox - strafing target dummy controller (ADR 0003 section 6, ADR 0002 section 8: bots drive the pawn only via
// SetLocalMoveInput / AddMovementInput / SetJumpHeld).

#pragma once

#include "AIController.h"

#include "VeloxDummyController.generated.h"

/**
 * Possesses an AVeloxCharacter and strafes it along world Y between +-StrafeHalfWidth of the possession point, using the
 * same movement component as players. Server-only: AI controllers do not exist on clients, the pawn replicates normally.
 */
UCLASS()
class VELOX_API AVeloxDummyController : public AAIController
{
	GENERATED_BODY()

public:
	AVeloxDummyController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~AController interface
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	//~End of AController interface

	//~AActor interface
	virtual void Tick(float DeltaSeconds) override;
	//~End of AActor interface

	/** Half width of the strafe range around the possession point, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velox|Dummy", meta = (ClampMin = "0", Units = "cm"))
	float StrafeHalfWidth = 300.f;

	/** Fraction of the pawn's max ground speed used while strafing (analog input scale, 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velox|Dummy", meta = (ClampMin = "0", ClampMax = "1"))
	float StrafeSpeedScale = 1.f;

	/** Seconds between position log lines (Log verbosity, LogVelox); 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velox|Dummy", meta = (ClampMin = "0", Units = "s"))
	float LogInterval = 2.f;

private:
	void LogPosition() const;

	FVector StrafeOrigin = FVector::ZeroVector;
	/** +1 = world +Y, -1 = world -Y. */
	float StrafeDirection = 1.f;
	FTimerHandle LogTimer;
};
