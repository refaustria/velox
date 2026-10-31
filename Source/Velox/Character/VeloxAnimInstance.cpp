// Velox - third-person anim instance.

#include "Character/VeloxAnimInstance.h"

#include "Character/VeloxCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAnimInstance)

FVeloxAnimInput FVeloxAnimSampler::Sample(const AVeloxCharacter& Character, float DeltaSeconds, const FVeloxAnimTuning& Tuning)
{
	FVector Velocity = Character.GetVelocity();
	if (Character.GetLocalRole() == ROLE_SimulatedProxy && Tuning.ProxyVelocitySmoothingSeconds > 0.f)
	{
		if (!bHasSmoothedVelocity)
		{
			SmoothedVelocity = Velocity;
			bHasSmoothedVelocity = true;
		}
		const float Alpha = 1.f - FMath::Exp(-FMath::Max(DeltaSeconds, 0.f) / Tuning.ProxyVelocitySmoothingSeconds);
		SmoothedVelocity += (Velocity - SmoothedVelocity) * Alpha;
		Velocity = SmoothedVelocity;
	}

	const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
	FVeloxAnimInput Input;
	Input.Speed2D = static_cast<float>(Velocity.Size2D());
	Input.DirectionDeg = VeloxAnimState::ComputeDirectionDeg(Velocity, static_cast<float>(Character.GetActorRotation().Yaw));
	Input.VerticalSpeed = static_cast<float>(Velocity.Z);
	Input.bAirborne = Movement && Movement->IsFalling();
	Input.bCrouching = Character.bIsCrouched;
	Input.bDead = Character.IsCorpse();
	AimPitch = static_cast<float>(FRotator::NormalizeAxis(Character.GetBaseAimRotation().Pitch));
	return Input;
}

void UVeloxAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	State = FVeloxAnimStateData();
	Sampler = FVeloxAnimSampler();
}

void UVeloxAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const AVeloxCharacter* Character = Cast<AVeloxCharacter>(TryGetPawnOwner());
	if (!Character)
	{
		return;
	}
	const UVeloxCharacterCosmetics* Cosmetics = Character->GetCharacterCosmetics();
	const FVeloxAnimTuning Tuning = Cosmetics ? Cosmetics->AnimTuning : FVeloxAnimTuning();
	const FVeloxAnimInput Input = Sampler.Sample(*Character, DeltaSeconds, Tuning);
	VeloxAnimState::Step(Input, State, DeltaSeconds, Tuning);

	Speed2D = Input.Speed2D;
	DirectionDeg = Input.DirectionDeg;
	bAirborne = Input.bAirborne;
	bCrouching = Input.bCrouching;
	bDead = Input.bDead;
	AimPitch = Sampler.AimPitch;
	LegState = State.LegState;
	RunDirection = State.RunDirection;
	PlayRate = State.PlayRate;
	bFiring = bFirePending;
	bFirePending = false;
}
