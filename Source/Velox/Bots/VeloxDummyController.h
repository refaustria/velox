// Velox - strafing target dummy controller (ADR 0003 section 6, ADR 0002 section 8: bots drive the pawn only via
// SetLocalMoveInput / AddMovementInput / SetJumpHeld).

#pragma once

#include "AIController.h"
#include "Templates/SubclassOf.h"

#include "VeloxDummyController.generated.h"

/**
 * Possesses an AVeloxCharacter and strafes it along world Y between +-StrafeHalfWidth of the possession point, using the
 * same movement component as players. Server-only: AI controllers do not exist on clients, the pawn replicates normally.
 */
class AVeloxCharacter;

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

	/** Yaw the dummy faces on every possession (world -X, towards the first PlayerStart of L_CombatTest). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velox|Dummy", meta = (Units = "deg"))
	float FacingYaw = 180.f;

	/**
	 * Remembers where and as what this dummy respawns (ADR 0003 B28): AVeloxGameMode::RestartPlayer then restarts the
	 * controller at Transform with PawnClass instead of choosing a PlayerStart and the default pawn. Server only.
	 */
	void SetFixedRespawn(const FTransform& Transform, TSubclassOf<AVeloxCharacter> PawnClass);

	bool HasFixedRespawn() const { return bHasFixedRespawn; }
	const FTransform& GetRespawnTransform() const { return RespawnTransform; }
	TSubclassOf<AVeloxCharacter> GetRespawnPawnClass() const { return RespawnPawnClass; }

	/** Permanent spawn protection (no damage, hits still confirm), re-applied on every possession. */
	void SetInvulnerable(bool bInInvulnerable) { bInvulnerable = bInInvulnerable; }
	bool IsInvulnerable() const { return bInvulnerable; }

	/** The strafe centre of the current life (the respawn location when a fixed respawn is set). */
	const FVector& GetStrafeOrigin() const { return StrafeOrigin; }

	/** Seconds between position log lines (Log verbosity, LogVelox); 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velox|Dummy", meta = (ClampMin = "0", Units = "s"))
	float LogInterval = 2.f;

private:
	void LogPosition() const;

	UPROPERTY(Transient)
	FTransform RespawnTransform;

	UPROPERTY(Transient)
	TSubclassOf<AVeloxCharacter> RespawnPawnClass;

	bool bHasFixedRespawn = false;
	bool bInvulnerable = false;

	FVector StrafeOrigin = FVector::ZeroVector;
	/** +1 = world +Y, -1 = world -Y. */
	float StrafeDirection = 1.f;
	FTimerHandle LogTimer;
};
