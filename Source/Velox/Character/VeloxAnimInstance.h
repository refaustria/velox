// Velox - third-person anim instance (ADR 0006 section 4.5): C++ decides, the AnimGraph (ABP_Velox) only displays.

#pragma once

#include "Animation/AnimInstance.h"
#include "Character/VeloxAnimState.h"

#include "VeloxAnimInstance.generated.h"

class AVeloxCharacter;

/**
 * Samples an AVeloxCharacter into FVeloxAnimInput: the world-touching half of the anim logic, shared by
 * UVeloxAnimInstance and the sequence-playback driver on the pawn. Simulated proxies use the replicated velocity
 * smoothed with ProxyVelocitySmoothingSeconds (first order, frame-rate independent); other roles the raw velocity.
 */
struct VELOX_API FVeloxAnimSampler
{
	FVeloxAnimInput Sample(const AVeloxCharacter& Character, float DeltaSeconds, const FVeloxAnimTuning& Tuning);

	/** View pitch (-90..90, up positive) of the last Sample. */
	float AimPitch = 0.f;

private:
	FVector SmoothedVelocity = FVector::ZeroVector;
	bool bHasSmoothedVelocity = false;
};

UCLASS(Transient, Blueprintable)
class VELOX_API UVeloxAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Fire cue (UVeloxCombatFeedbackComponent): bFiring is true for the next update only (drives the Fire_Upper slot). */
	void NotifyFired() { bFirePending = true; }

	const FVeloxAnimStateData& GetStateData() const { return State; }

protected:
	//~UAnimInstance interface
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	//~End of UAnimInstance interface

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Velox|Anim")
	float Speed2D = 0.f;

	/** Velocity direction relative to the actor yaw, -180..180 (+90 = moving right). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Velox|Anim")
	float DirectionDeg = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Velox|Anim")
	bool bAirborne = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Velox|Anim")
	bool bCrouching = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Velox|Anim")
	float AimPitch = 0.f;

	/** True for one update after a Fire cue. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Velox|Anim")
	bool bFiring = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Velox|Anim")
	bool bDead = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Velox|Anim")
	EVeloxLegState LegState = EVeloxLegState::Idle;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Velox|Anim")
	EVeloxRunDirection RunDirection = EVeloxRunDirection::Fwd;

	/** Cadence play rate of the run/crouch-walk cycle (1 in other states). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Velox|Anim")
	float PlayRate = 1.f;

private:
	FVeloxAnimStateData State;
	FVeloxAnimSampler Sampler;
	bool bFirePending = false;
};
