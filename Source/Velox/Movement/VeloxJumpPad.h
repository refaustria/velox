// Velox - predicted jump pad (ADR 0002 section 4, amendment A4).
//
// Map-placed, not replicated. The trigger overlaps on the server and on the owning client during the same move; both
// call UVeloxMovementComponent::AddKnockback with the same launch velocity (same actor data, same gravity from the
// character's movement settings), so the launch is predicted and needs no RPC. Override launches are idempotent when a
// replay re-fires the overlap.

#pragma once

#include "GameFramework/Actor.h"

#include "VeloxJumpPad.generated.h"

class UArrowComponent;
class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;
struct FHitResult;

UENUM(BlueprintType)
enum class EVeloxJumpPadMode : uint8
{
	/** Fly to Target: Q3 AimAtTarget (apex at the target) above the pad, minimum-speed arc level with or below it. */
	AimAtTarget,
	/** Launch with LaunchVelocity (world space). */
	FixedVelocity,
};

/** Result of the launch validation shared by map check, runtime warning and tests. */
UENUM()
enum class EVeloxJumpPadLaunchCheck : uint8
{
	Ok,
	/** Zero launch velocity: no target, target directly below the pad, no gravity, or FixedVelocity zero. */
	Unsolvable,
	/** Launch speed above MaxLaunchSpeed (usually an AimAtTarget target barely above the pad). */
	TooFast,
};

UCLASS(ClassGroup = (Velox), HideCategories = (Input, Rendering, Replication, HLOD, Physics, Networking))
class VELOX_API AVeloxJumpPad : public AActor
{
	GENERATED_BODY()

public:
	AVeloxJumpPad(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Launch velocity under GravityZ (cm/s^2, negative is down) from the trigger centre (Q3). Zero if unsolvable. */
	UFUNCTION(BlueprintPure, Category = "Velox|JumpPad")
	FVector ComputeLaunchVelocity(float GravityZ) const;

	/**
	 * Where the launch ends for nav-link generation (M4), with the same trigger-centre approximation: AimAtTarget returns
	 * the target location; FixedVelocity the point where the arc comes back down to the launch height (the trigger
	 * centre if it never does). Bots trace down from it.
	 */
	UFUNCTION(BlueprintPure, Category = "Velox|JumpPad")
	FVector GetLandingPoint(float GravityZ) const;

	/** Launch point: the trigger centre. */
	UFUNCTION(BlueprintPure, Category = "Velox|JumpPad")
	FVector GetLaunchOrigin() const;

	/** World-free launch math (tests): the velocity for Mode from From, using TargetLocation or FixedVelocity. */
	static FVector ComputeLaunchVelocityFor(EVeloxJumpPadMode Mode, const FVector& From, const FVector& TargetLocation, const FVector& FixedVelocity, float GravityZ);

	/** World-free validation (tests): zero is unsolvable, faster than InMaxLaunchSpeed is too fast. */
	static EVeloxJumpPadLaunchCheck CheckLaunchVelocity(const FVector& Launch, float InMaxLaunchSpeed);

	EVeloxJumpPadMode GetMode() const { return Mode; }
	AActor* GetTarget() const { return Target; }
	bool OverridesVelocity() const { return bOverrideVelocity; }

#if WITH_EDITOR
	//~AActor interface
	virtual void CheckForErrors() override;
	//~End of AActor interface
#endif

protected:
	//~AActor interface
	virtual void PostInitializeComponents() override;
	//~End of AActor interface

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Validation message for a launch, empty when it is fine. */
	FString DescribeLaunchProblem(const FVector& Launch, float GravityZ) const;

	/** Overlaps pawns only. Its centre is the launch point. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|JumpPad")
	TObjectPtr<UBoxComponent> Trigger;

	/** Visible pad (no collision). Mesh and material are set per placement. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|JumpPad")
	TObjectPtr<UStaticMeshComponent> PadMesh;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UArrowComponent> Arrow;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|JumpPad")
	EVeloxJumpPadMode Mode = EVeloxJumpPadMode::AimAtTarget;

	/** AimAtTarget: where to fly (any actor, usually an ATargetPoint). Place it at least 1 m above the pad or level/below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|JumpPad", Meta = (EditCondition = "Mode == EVeloxJumpPadMode::AimAtTarget"))
	TObjectPtr<AActor> Target;

	/** FixedVelocity: world-space launch velocity (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|JumpPad", Meta = (EditCondition = "Mode == EVeloxJumpPadMode::FixedVelocity", Units = "cm/s"))
	FVector LaunchVelocity = FVector::ZeroVector;

	/** Replace the pawn's velocity (Q3 trigger_push) instead of adding to it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|JumpPad")
	bool bOverrideVelocity = true;

	/** Validation bound: map check and a one-time runtime warning when the launch speed exceeds it (ADR 0002 section 4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|JumpPad", Meta = (ClampMin = "0", Units = "cm/s"))
	float MaxLaunchSpeed = 4000.f;

private:
	/** The runtime warning is logged once per pad. */
	bool bWarnedInvalidLaunch = false;
};
