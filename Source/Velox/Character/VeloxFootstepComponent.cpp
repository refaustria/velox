// Velox - footsteps, jump, landing, pain and death sounds.

#include "Character/VeloxFootstepComponent.h"

#include "Character/VeloxCharacter.h"
#include "Combat/VeloxHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/VeloxAudioSettings.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxFootstepComponent)

UVeloxFootstepComponent::UVeloxFootstepComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(false);
}

AVeloxCharacter* UVeloxFootstepComponent::GetCharacter() const
{
	return Cast<AVeloxCharacter>(GetOwner());
}

void UVeloxFootstepComponent::BeginPlay()
{
	Super::BeginPlay();

	const UWorld* World = GetWorld();
	if (!UVeloxAudioSettings::CanPlayAudio(World))
	{
		// The server (and commandlets) never play audio and never load an asset (ADR 0006 section 1).
		UVeloxAudioSettings::EnsureLoaded(World);
		SetComponentTickEnabled(false);
		return;
	}
	UVeloxAudioSettings::EnsureLoaded(World);
	if (UVeloxHealthComponent* Health = UVeloxHealthComponent::FindHealthComponent(GetOwner()))
	{
		Health->OnHealthChanged.AddDynamic(this, &ThisClass::OnHealthChanged);
		Health->OnDeathStarted.AddDynamic(this, &ThisClass::OnDeathStarted);
		bBoundHealth = true;
	}
}

void UVeloxFootstepComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundHealth)
	{
		if (UVeloxHealthComponent* Health = UVeloxHealthComponent::FindHealthComponent(GetOwner()))
		{
			Health->OnHealthChanged.RemoveDynamic(this, &ThisClass::OnHealthChanged);
			Health->OnDeathStarted.RemoveDynamic(this, &ThisClass::OnDeathStarted);
		}
		bBoundHealth = false;
	}
	Super::EndPlay(EndPlayReason);
}

FVector UVeloxFootstepComponent::GetFootLocation(bool bRight) const
{
	const AVeloxCharacter* Character = GetCharacter();
	if (!Character)
	{
		return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	}
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	const FName Socket = bRight ? Settings.FootSocketRight : Settings.FootSocketLeft;
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (Mesh && Mesh->DoesSocketExist(Socket))
	{
		return Mesh->GetSocketLocation(Socket);
	}
	const float HalfHeight = Character->GetSimpleCollisionHalfHeight();
	const FVector Side = Character->GetActorRightVector() * (bRight ? Settings.FootFallbackSideCm : -Settings.FootFallbackSideCm);
	return Character->GetActorLocation() - FVector(0.f, 0.f, HalfHeight) + Side;
}

void UVeloxFootstepComponent::PlayStep(bool bRight)
{
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	if (Settings.FootstepSounds.IsEmpty())
	{
		return;
	}
	// Variation: advance in turn, the left and right feet start on different entries.
	const int32 Index = (StepVariation++ * 2 + (bRight ? 1 : 0)) % Settings.FootstepSounds.Num();
	++NumSteps;
	UVeloxAudioSettings::PlayAt(GetWorld(), Settings.FootstepSounds[Index], GetFootLocation(bRight), EVeloxAudioAttenuation::Footstep,
		EVeloxAudioConcurrency::Footsteps, bRight ? TEXT("footstep_r") : TEXT("footstep_l"));
}

void UVeloxFootstepComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AVeloxCharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || Character->IsCorpse())
	{
		return;
	}
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	const FVector Location = Character->GetActorLocation();
	const float TravelCm = bHasLastLocation ? static_cast<float>(FVector::Dist2D(Location, LastLocation)) : 0.f;
	LastLocation = Location;
	bHasLastLocation = true;

	const bool bGrounded = Movement->IsMovingOnGround();
	const float Speed = static_cast<float>(Movement->Velocity.Size2D());

	// Simulated proxies run no jump logic: a take-off from the ground with a jump-sized upward speed is the jump.
	if (Character->GetLocalRole() == ROLE_SimulatedProxy && bWasGrounded && !bGrounded && Movement->IsFalling()
		&& Movement->Velocity.Z >= Settings.ProxyJumpMinVz && Movement->Velocity.Z <= Settings.ProxyJumpMaxVz)
	{
		UVeloxAudioSettings::PlayAt(GetWorld(), Settings.JumpSound, Character->GetActorLocation(), EVeloxAudioAttenuation::Player, EVeloxAudioConcurrency::None, TEXT("jump_proxy"));
	}
	if (Movement->IsFalling())
	{
		LastFallingVz = static_cast<float>(Movement->Velocity.Z);
	}
	bWasGrounded = bGrounded;

	bool bFirstRight = false;
	const int32 Steps = VeloxFootstepMath::Advance(StepState, TravelCm, VeloxFootstepMath::IsAudible(bGrounded, Character->bIsCrouched, Speed, Settings.FootstepMinSpeed),
		Settings.FootstepDistanceCm, bFirstRight);
	for (int32 i = 0; i < Steps; ++i)
	{
		PlayStep(bFirstRight != ((i & 1) != 0));
	}
}

void UVeloxFootstepComponent::HandleLanded(float ImpactVz)
{
	const AVeloxCharacter* Character = GetCharacter();
	if (!Character || Character->bClientUpdating || Character->IsCorpse())
	{
		return;
	}
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	// The pawn's own velocity may already be cleared; fall back to the last speed seen while falling.
	const float Vz = FMath::IsNearlyZero(ImpactVz) ? LastFallingVz : ImpactVz;
	const int32 Kind = VeloxFootstepMath::LandingKind(Vz, Settings.LandSoftVz, Settings.LandHardVz);
	if (Kind == 0)
	{
		return;
	}
	UVeloxAudioSettings::PlayAt(GetWorld(), Kind == 2 ? Settings.LandHardSound : Settings.LandSoftSound, GetFootLocation(false), EVeloxAudioAttenuation::Footstep,
		EVeloxAudioConcurrency::Footsteps, Kind == 2 ? TEXT("land_hard") : TEXT("land_soft"));
}

void UVeloxFootstepComponent::HandleJumped()
{
	const AVeloxCharacter* Character = GetCharacter();
	if (!Character || Character->bClientUpdating || Character->IsCorpse())
	{
		return;
	}
	UVeloxAudioSettings::PlayAt(GetWorld(), UVeloxAudioSettings::Get().JumpSound, Character->GetActorLocation(), EVeloxAudioAttenuation::Player, EVeloxAudioConcurrency::None, TEXT("jump"));
}

void UVeloxFootstepComponent::OnHealthChanged(UVeloxHealthComponent* HealthComponent, float OldValue, float NewValue, AActor* Instigator)
{
	const AVeloxCharacter* Character = GetCharacter();
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	if (!Character || Character->IsCorpse() || NewValue <= 0.f || OldValue - NewValue < Settings.PainMinDrop)
	{
		return;
	}
	const int32 Band = VeloxFootstepMath::PainBand(NewValue, Settings.PainHeavyBelow, Settings.PainMediumBelow);
	const TSoftObjectPtr<USoundBase>& Sound = Band == 2 ? Settings.PainHeavySound : (Band == 1 ? Settings.PainMediumSound : Settings.PainLightSound);
	if (Character->IsLocallyControlled())
	{
		UVeloxAudioSettings::Play2D(GetWorld(), Sound, TEXT("pain_own"));
		return;
	}
	UVeloxAudioSettings::PlayAt(GetWorld(), Sound, Character->GetActorLocation(), EVeloxAudioAttenuation::Player, EVeloxAudioConcurrency::None, TEXT("pain"));
}

void UVeloxFootstepComponent::OnDeathStarted(AActor* OwningActor)
{
	HandleCorpse();
}

void UVeloxFootstepComponent::HandleCorpse()
{
	const AVeloxCharacter* Character = GetCharacter();
	if (!Character || bDeathPlayed || !UVeloxAudioSettings::CanPlayAudio(GetWorld()))
	{
		return;
	}
	bDeathPlayed = true;
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	if (Character->IsLocallyControlled())
	{
		UVeloxAudioSettings::Play2D(GetWorld(), Settings.DeathSound, TEXT("death_own"));
		return;
	}
	UVeloxAudioSettings::PlayAt(GetWorld(), Settings.DeathSound, Character->GetActorLocation(), EVeloxAudioAttenuation::Player, EVeloxAudioConcurrency::None, TEXT("death"));
}
