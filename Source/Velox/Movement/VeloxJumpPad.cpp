// Velox - predicted jump pad (ADR 0002 section 4, amendment A4).

#include "Movement/VeloxJumpPad.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Core/VeloxCollisionChannels.h"
#include "Core/VeloxLog.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Movement/VeloxMoveSim.h"
#include "Movement/VeloxMovementComponent.h"
#include "Movement/VeloxMovementSettings.h"

#if WITH_EDITOR
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxJumpPad)

#define LOCTEXT_NAMESPACE "VeloxJumpPad"

namespace VeloxJumpPadDefaults
{
	// Default trigger: a 1 m pad, as high as the standing pawn capsule (ADR 0002 section 1, 37.5 x 70 cm), so the
	// trigger centre is close to the capsule centre of a pawn standing on the pad. Edited per placement.
	const FVector TriggerExtent(50.0, 50.0, 70.0);
}

AVeloxJumpPad::AVeloxJumpPad(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->InitBoxExtent(VeloxJumpPadDefaults::TriggerExtent);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCanEverAffectNavigation(false);
	Trigger->SetMobility(EComponentMobility::Static);
	RootComponent = Trigger;

	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(Trigger);
	PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PadMesh->SetGenerateOverlapEvents(false);
	PadMesh->SetCanEverAffectNavigation(false);
	PadMesh->SetMobility(EComponentMobility::Static);

#if WITH_EDITORONLY_DATA
	Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(Trigger);
		Arrow->SetRelativeRotation(FRotator(90.0, 0.0, 0.0));
		Arrow->bIsScreenSizeScaled = true;
	}
#endif
}

void AVeloxJumpPad::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnTriggerBeginOverlap);
}

FVector AVeloxJumpPad::GetLaunchOrigin() const
{
	return Trigger->GetComponentLocation();
}

FVector AVeloxJumpPad::ComputeLaunchVelocityFor(EVeloxJumpPadMode InMode, const FVector& From, const FVector& TargetLocation, const FVector& InFixedVelocity, float GravityZ)
{
	return InMode == EVeloxJumpPadMode::FixedVelocity
		? InFixedVelocity
		: VeloxMoveSim::JumpPadLaunchVelocity(From, TargetLocation, GravityZ);
}

EVeloxJumpPadLaunchCheck AVeloxJumpPad::CheckLaunchVelocity(const FVector& Launch, float InMaxLaunchSpeed)
{
	if (Launch.IsNearlyZero())
	{
		return EVeloxJumpPadLaunchCheck::Unsolvable;
	}
	return Launch.SizeSquared() > FMath::Square(static_cast<double>(InMaxLaunchSpeed))
		? EVeloxJumpPadLaunchCheck::TooFast
		: EVeloxJumpPadLaunchCheck::Ok;
}

FVector AVeloxJumpPad::ComputeLaunchVelocity(float GravityZ) const
{
	if (Mode == EVeloxJumpPadMode::AimAtTarget && !Target)
	{
		return FVector::ZeroVector;
	}
	const FVector TargetLocation = Target ? Target->GetActorLocation() : FVector::ZeroVector;
	return ComputeLaunchVelocityFor(Mode, GetLaunchOrigin(), TargetLocation, LaunchVelocity, GravityZ);
}

FVector AVeloxJumpPad::GetLandingPoint(float GravityZ) const
{
	if (Mode == EVeloxJumpPadMode::AimAtTarget)
	{
		return Target ? Target->GetActorLocation() : GetLaunchOrigin();
	}
	// Ballistic arc back to the launch height: t = 2 Vz / g.
	const double G = -static_cast<double>(GravityZ);
	if (G <= 0.0 || LaunchVelocity.Z <= 0.0)
	{
		return GetLaunchOrigin();
	}
	const double FlightTime = 2.0 * LaunchVelocity.Z / G;
	return GetLaunchOrigin() + FVector(LaunchVelocity.X, LaunchVelocity.Y, 0.0) * FlightTime;
}

FVector AVeloxJumpPad::ComputeLandingPoint(const UWorld* World) const
{
	FVector Landing;
	TryComputeLandingPoint(World, Landing);
	return Landing;
}

bool AVeloxJumpPad::TryComputeLandingPoint(const UWorld* World, FVector& OutLanding) const
{
	const float GravityZ = GetDefault<UVeloxMovementSettings>()->BuildSimParams().GravityZ;
	const FVector FootOffset(0.0, 0.0, Trigger->GetScaledBoxExtent().Z);
	const FVector Foot = GetLaunchOrigin() - FootOffset;
	OutLanding = Foot;
	const FVector Launch = ComputeLaunchVelocity(GravityZ);
	const double G = -static_cast<double>(GravityZ);
	if (!World || Launch.IsNearlyZero() || G <= 0.0 || LandingSampleSeconds <= 0.f)
	{
		return false;
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(VeloxJumpPadLanding), false, this);
	const double ApexTime = FMath::Max(0.0, Launch.Z / G);
	const int32 NumSamples = FMath::Max(1, FMath::CeilToInt32(LandingMaxFlightSeconds / LandingSampleSeconds));
	FVector Prev = VeloxMoveSim::BallisticPoint(Foot, Launch, GravityZ, static_cast<float>(ApexTime));
	for (int32 i = 1; i <= NumSamples; ++i)
	{
		const float T = static_cast<float>(ApexTime + i * static_cast<double>(LandingSampleSeconds));
		const FVector Next = VeloxMoveSim::BallisticPoint(Foot, Launch, GravityZ, T);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Prev, Next, Velox_TraceChannel_Weapon, Query))
		{
			OutLanding = Hit.ImpactPoint;
			return true;
		}
		Prev = Next;
	}
	OutLanding = Prev;
	return false;
}

FString AVeloxJumpPad::DescribeLaunchProblem(const FVector& Launch, float GravityZ) const
{
	switch (CheckLaunchVelocity(Launch, MaxLaunchSpeed))
	{
	case EVeloxJumpPadLaunchCheck::Unsolvable:
		if (Mode == EVeloxJumpPadMode::AimAtTarget && !Target)
		{
			return TEXT("AimAtTarget without a Target: the pad does nothing");
		}
		return FString::Printf(TEXT("launch velocity is zero (target directly below the pad, GravityZ %.0f, or FixedVelocity zero): the pad does nothing"), GravityZ);
	case EVeloxJumpPadLaunchCheck::TooFast:
		return FString::Printf(TEXT("launch speed %.0f cm/s exceeds MaxLaunchSpeed %.0f cm/s (AimAtTarget targets must be >= 1 m above the pad, or use FixedVelocity)"), Launch.Size(), MaxLaunchSpeed);
	default:
		return FString();
	}
}

void AVeloxJumpPad::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const ACharacter* Character = Cast<ACharacter>(OtherActor);
	UVeloxMovementComponent* MoveComp = Character ? Cast<UVeloxMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	if (!MoveComp || OtherComp != Character->GetCapsuleComponent())
	{
		return;
	}
	// Server and owning client predict the launch; simulated proxies get the result through replication.
	if (Character->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}

	// Same gravity on both ends: the character's movement settings.
	const float GravityZ = MoveComp->GetGravityZ();
	const FVector Launch = ComputeLaunchVelocity(GravityZ);
	const EVeloxJumpPadLaunchCheck Check = CheckLaunchVelocity(Launch, MaxLaunchSpeed);
	if (Check != EVeloxJumpPadLaunchCheck::Ok && !bWarnedInvalidLaunch)
	{
		bWarnedInvalidLaunch = true;
		UE_LOG(LogVeloxMovement, Warning, TEXT("%s: %s"), *GetPathName(), *DescribeLaunchProblem(Launch, GravityZ));
	}
	if (Check == EVeloxJumpPadLaunchCheck::Unsolvable)
	{
		return;
	}

	UE_LOG(LogVeloxMovement, Verbose, TEXT("%s: launch %s (%s) %s"), *GetName(), *Launch.ToString(), bOverrideVelocity ? TEXT("set") : TEXT("add"), *GetNameSafe(Character));
	const bool bApplied = MoveComp->AddKnockback(Launch, bOverrideVelocity);
#if !UE_BUILD_SHIPPING
	// One line per overlap (A28(iii)); infra/ci/nettest.sh matches the client's live launch and the server's launch by the
	// move time stamp ts (same move on both ends). src: live | replay | teleport (A19, applied=0) | server.
	UE_LOG(LogVeloxMovement, Log, TEXT("VeloxJumpPad: pad=%s role=%s t=%.3f ts=%.6f src=%s applied=%d launch=%s"), *GetName(),
		Character->GetLocalRole() == ROLE_Authority ? TEXT("authority") : TEXT("autonomous"), GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0,
		MoveComp->GetCurrentMoveTimeStamp(), MoveComp->GetMoveSourceName(), bApplied ? 1 : 0, *Launch.ToCompactString());
#endif
}

#if WITH_EDITOR
void AVeloxJumpPad::CheckForErrors()
{
	Super::CheckForErrors();

	// No character exists in the editor: validate with the class-default movement settings (the shipped asset carries
	// the same gravity). A pawn with different settings is checked again at runtime.
	const float GravityZ = GetDefault<UVeloxMovementSettings>()->BuildSimParams().GravityZ;
	const FString Problem = DescribeLaunchProblem(ComputeLaunchVelocity(GravityZ), GravityZ);
	if (!Problem.IsEmpty())
	{
		FMessageLog("MapCheck").Warning()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_JumpPadLaunch", "Jump pad: {0}"), FText::FromString(Problem))));
	}
}
#endif

#undef LOCTEXT_NAMESPACE
