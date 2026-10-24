// Velox - movement tunables (ADR 0001 section 1). Values are filled in M1 via DA_MovementSettings_Default.

#pragma once

#include "Engine/DataAsset.h"

#include "VeloxMovementSettings.generated.h"

/**
 * All movement tunables for UVeloxMovementComponent / FVeloxMoveSim. No movement constant lives in code.
 * Defaults are neutral (zero, or identity for UnitScale); the real values come from the data asset.
 */
UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Movement Settings"))
class VELOX_API UVeloxMovementSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Maximum wish speed on the ground (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0"))
	float MaxGroundSpeed = 0.0f;

	/** Ground acceleration factor (Q3 pm_accelerate, 1/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0"))
	float GroundAcceleration = 0.0f;

	/** Ground friction factor (Q3 pm_friction, 1/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0"))
	float GroundFriction = 0.0f;

	/** Air acceleration factor (Q3 pm_airaccelerate, 1/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Air", Meta = (ClampMin = "0.0"))
	float AirAcceleration = 0.0f;

	/** Wish speed cap applied to air acceleration (cm/s). Strafe jumping depends on this being small. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Air", Meta = (ClampMin = "0.0"))
	float AirMaxWishSpeed = 0.0f;

	/** Vertical take-off velocity of a jump (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Jump", Meta = (ClampMin = "0.0"))
	float JumpVelocity = 0.0f;

	/** Pawn gravity (cm/s^2, negative is down). Used instead of the project gravity from M1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Gravity")
	float GravityZ = 0.0f;

	/** Q3 units to Unreal centimetres. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Units", Meta = (ClampMin = "0.0"))
	float UnitScale = 1.0f;

	/** Surfaces steeper than this (degrees) and approached fast enough are slid along instead of walked on. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ramps", Meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float RampSlideMaxAngle = 0.0f;

	/** Maximum step-up height (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0"))
	float StepHeight = 0.0f;
};
