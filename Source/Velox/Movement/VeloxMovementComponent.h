// Velox - custom character movement component (ADR 0001 sections 1 and 3, ADR 0002 section 2).
//
// Quake-style physics through the CMC velocity hooks: the engine keeps PhysWalking/PhysFalling, floor finding,
// stepping, substepping, saved moves and corrections; Velox replaces the velocity integration (CalcVelocity ->
// VeloxMoveSim), the jump, the landing frame (auto-hop) and the ramp/step/slope rules. All tunables come from
// UVeloxMovementSettings; engine fields are written only in SetMovementSettings().

#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/VeloxMoveSim.h"
#include "Movement/VeloxNetworkMoveData.h"

#include "VeloxMovementComponent.generated.h"

class UVeloxMovementSettings;

/**
 * External velocity change waiting to be applied at the start of the next PerformMovement (ADR 0002 section 4).
 * Filled by AddKnockback (M1 task 04); saved/replayed and sent in corrections by task 04.
 */
struct FVeloxPendingKnockback
{
	/** Velocity to add, or to set when bOverride. */
	FVector Velocity = FVector::ZeroVector;
	/** Replace the current velocity instead of adding to it (jump pads). */
	bool bOverride = false;

	/** An override with zero velocity ("stop") is still pending. */
	bool IsPending() const { return bOverride || !Velocity.IsZero(); }
	void Reset() { *this = FVeloxPendingKnockback(); }
};

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UVeloxMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Movement tunables. May be null until pawn data has been applied (then the component behaves like the stock CMC). */
	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	const UVeloxMovementSettings* GetMovementSettings() const;

	/**
	 * Must be called identically on server and owning client (the character does this in PostInitializeComponents).
	 * Rebuilds the cached sim params and is the only place engine movement fields are written.
	 */
	void SetMovementSettings(const UVeloxMovementSettings* InMovementSettings);

	/** Sim params of the current move (rebuilt at the start of every PerformMovement). Zeros without settings. */
	const FVeloxMoveParams& GetSimParams() const { return SimParams; }

	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	bool IsJumpHeld() const { return bWantsJumpHeld; }

	/** Locally controlled input state; replicated to the server through FSavedMove_Velox::FLAG_JumpHeld. */
	void SetJumpHeld(bool bHeld) { bWantsJumpHeld = bHeld; }

	/**
	 * Raw move input of this frame, X = forward, Y = right (player input or bot). Only its presence per axis is used
	 * (CPM side-only / forward-only rules); the wish vector itself always comes from Acceleration.
	 */
	UFUNCTION(BlueprintCallable, Category = "Velox|Movement")
	void SetLocalMoveInput(FVector2D ForwardRight);

	bool HasForwardInput() const { return bHasForwardInput; }
	bool HasSideInput() const { return bHasSideInput; }

	/** Remaining ground-friction-free time after a knockback (s). */
	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	float GetKnockbackGrace() const { return KnockbackGrace; }

	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	float GetSpeed2D() const { return Velocity.Size2D(); }

	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	bool IsOnGround() const { return IsMovingOnGround(); }

	//~UCharacterMovementComponent interface
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual float GetGravityZ() const override;
	virtual bool CanAttemptJump() const override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	//~End of UCharacterMovementComponent interface

	// The single-argument DoJump is deprecated in the engine and forwards to the two-argument one.
	using Super::DoJump;

protected:
	//~UCharacterMovementComponent interface
	virtual void PerformMovement(float DeltaSeconds) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual FVector GetFallingLateralAcceleration(float DeltaTime) override;
	virtual bool ShouldLimitAirControl(float DeltaTime, const FVector& FallAcceleration) const override;
	virtual FVector HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const override;
	virtual bool IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const override;
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;
	virtual bool ShouldCatchAir(const FFindFloorResult& OldFloor, const FFindFloorResult& NewFloor) override;
	virtual void HandleWalkingOffLedge(const FVector& PreviousFloorImpactNormal, const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta) override;
	virtual void StartFalling(int32 Iterations, float remainingTime, float timeTick, const FVector& Delta, const FVector& subLoc) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	//~End of UCharacterMovementComponent interface

	/** True when Velox physics are active (settings assigned). Without settings every hook defers to the engine. */
	bool HasVeloxPhysics() const { return MovementSettings != nullptr; }

	/**
	 * Applies PendingKnockback (velocity, grace, lift-off) at the start of a move. ADR 0002 section 4; the entry point
	 * AddKnockback and the saved-move/correction plumbing are M1 task 04.
	 */
	void ConsumePendingKnockback();

	/**
	 * Vertical speed with which the pawn leaves a ramp with floor normal ImpactNormal at the current horizontal velocity:
	 * the climb rate of the engine's ramp movement (ComputeGroundMovementDelta, bMaintainHorizontalGroundVelocity),
	 * -(V_xy . N) / N.z. Zero for floors flatter than RampExitMinAngle and for descending motion.
	 */
	float ComputeRampExitVelocityZ(const FVector& FloorImpactNormal, const FVector& FloorContactNormal) const;

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Movement")
	TObjectPtr<const UVeloxMovementSettings> MovementSettings;

	/** Jump button is held this move (sampled every move, unlike ACharacter::bPressedJump). */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Movement")
	uint8 bWantsJumpHeld : 1;

	/** Forward/back input present this frame (CPM rules). Sent to the server by task 04 (FLAG_Custom_1). */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Movement")
	uint8 bHasForwardInput : 1;

	/** Side input present this frame (CPM rules). Sent to the server by task 04 (FLAG_Custom_2). */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Movement")
	uint8 bHasSideInput : 1;

	/** Ground-friction-free time left (s); counted down once per PerformMovement. */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Movement")
	float KnockbackGrace = 0.f;

	/** KnockbackGrace at the start of the current move; every CalcVelocity of the move uses it (ADR 0002 A7). */
	float MoveStartKnockbackGrace = 0.f;

	/**
	 * Ramp vertical speed handed from HandleWalkingOffLedge to StartFalling within one call chain of PhysWalking.
	 * Never survives a move (cleared at move start, by StartFalling and on mode changes), so it needs no saved-move slot.
	 */
	float RampExitVelocityZ = 0.f;

	/** Velocity change to apply at the start of the next move. */
	FVeloxPendingKnockback PendingKnockback;

	/** FVeloxMoveParams of the current move: built in SetMovementSettings and at the start of every PerformMovement (A8). */
	FVeloxMoveParams SimParams;

private:
	/** Persistent storage installed via SetNetworkMoveDataContainer in the constructor. */
	FVeloxNetworkMoveDataContainer VeloxNetworkMoveDataContainer;
};
