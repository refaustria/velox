// Velox - bot path follower (ADR 0005 sections 4 and 5).

#include "Bots/VeloxBotPathFollowingComponent.h"

#include "AI/Navigation/NavigationTypes.h"
#include "Bots/VeloxBotCombat.h"
#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotItemRouting.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotSettings.h"
#include "Bots/VeloxBotSteering.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxNavLink.h"
#include "Character/VeloxCharacter.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Movement/VeloxMovementComponent.h"
#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationSystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxBotPathFollowingComponent)

namespace VeloxBotFollow
{
	EVeloxManeuverType ManeuverTypeFor(EVeloxNavLinkType LinkType)
	{
		switch (LinkType)
		{
		case EVeloxNavLinkType::JumpPad: return EVeloxManeuverType::JumpPad;
		case EVeloxNavLinkType::Drop: return EVeloxManeuverType::Drop;
		case EVeloxNavLinkType::Jump: return EVeloxManeuverType::Jump;
		case EVeloxNavLinkType::JumpFast: return EVeloxManeuverType::JumpFast;
		case EVeloxNavLinkType::RocketJump: return EVeloxManeuverType::RocketJump;
		}
		return EVeloxManeuverType::Drop;
	}

	float CornerAngleDeg(const FVector& In, const FVector& Out)
	{
		const FVector2D A = FVector2D(In).GetSafeNormal();
		const FVector2D B = FVector2D(Out).GetSafeNormal();
		if (A.IsZero() || B.IsZero())
		{
			return 0.f;
		}
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector2D::DotProduct(A, B)), -1.f, 1.f)));
	}

	float HopHeadroomCm(float JumpVelocity, float GravityZ, float MarginCm)
	{
		const float G = FMath::Abs(GravityZ);
		const float Margin = FMath::Max(0.f, MarginCm);
		if (G < KINDA_SMALL_NUMBER)
		{
			return Margin;
		}
		return FMath::Square(JumpVelocity) / (2.f * G) + Margin;
	}

	bool IsSegmentBlocked(bool bHit, float HitDistCm, float RemainingCm, float ClearanceCm)
	{
		return bHit && HitDistCm < RemainingCm - FMath::Max(0.f, ClearanceCm);
	}
}

namespace VeloxBotFollowPrivate
{
	/** A knockback arrived: the movement component's knockback grace rose since the previous step. */
	constexpr float GraceRiseEpsilon = 1e-4f;

	/**
	 * Hop headroom sweep (D18): the capsule starts lifted by this much off the floor (cm; a geometric tolerance of the
	 * query, not a tunable). Full radius: a capsule partly under a slab edge (the L_ItemTest ramp strip) must see it.
	 */
	constexpr float HeadroomSkinCm = 2.f;

	/** A start-penetrating headroom hit whose depenetration normal points down by more than this is a ceiling. */
	constexpr float CeilingNormalZ = 0.5f;

	/**
	 * D35 segment check: the projected feet are moved this far toward the centre of their poly before the nav raycast
	 * (cm; a geometric tolerance of the query, not a tunable). The projection of a bot in the eroded band lands on the
	 * mesh boundary, often on a corner vertex, and a string-pulled segment from there runs along a boundary edge; Detour's
	 * segment/poly clip rejects such a collinear ray at t = 0 (dtIntersectSegmentPoly2D, parallel edge with n < 0 from
	 * rounding), which would read as a blocked segment on every corner the path hugs. From a start strictly inside the
	 * poly the ray is no longer collinear; a ray into a wall still hits within a few cm.
	 */
	constexpr float SegmentRayInsetCm = 2.f;

	const TCHAR* ManeuverName(EVeloxManeuverType Type)
	{
		switch (Type)
		{
		case EVeloxManeuverType::JumpPad: return TEXT("JumpPad");
		case EVeloxManeuverType::Drop: return TEXT("Drop");
		case EVeloxManeuverType::Jump: return TEXT("Jump");
		case EVeloxManeuverType::JumpFast: return TEXT("JumpFast");
		case EVeloxManeuverType::RocketJump: return TEXT("RocketJump");
		}
		return TEXT("?");
	}
}

UVeloxBotPathFollowingComponent::UVeloxBotPathFollowingComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Driven by AVeloxBotController::Tick (StepFollow) so the follow step runs inside the controller's phases.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// The controller's own stuck rule (section 5) replaces the engine's block detection, which would abort a bot that
	// waits on a link (rocket-jump switch) or stands at an unavailable item.
	bUseBlockDetection = false;
}

AVeloxBotController* UVeloxBotPathFollowingComponent::GetBotController() const
{
	return Cast<AVeloxBotController>(GetOwner());
}

AVeloxCharacter* UVeloxBotPathFollowingComponent::GetBotCharacter() const
{
	const AVeloxBotController* Bot = GetBotController();
	return Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
}

UVeloxMovementComponent* UVeloxBotPathFollowingComponent::GetBotMovement() const
{
	const AVeloxCharacter* Character = GetBotCharacter();
	return Character ? Character->GetVeloxMovementComponent() : nullptr;
}

void UVeloxBotPathFollowingComponent::StepFollow(float DeltaTime)
{
	NavOutput = FVeloxBotNavOutput();
	// Task 06c: airborne-since for the flight test at BeginManeuver (before the follow step, which may begin a link).
	if (const UVeloxMovementComponent* Movement = GetBotMovement())
	{
		const UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if (Movement->IsOnGround())
		{
			AirborneSince = -1.0;
			LastGroundFeetZ = Movement->GetActorFeetLocation().Z;
			NextAirHopCheckTime = -1.0;
		}
		else
		{
			if (AirborneSince < 0.0)
			{
				AirborneSince = Now;
			}
			// H8 (review F4): the hop landing verdict is cached per grounded stretch only.
			NextHopLandingCheckTime = -1.0;
		}
	}
	else
	{
		AirborneSince = -1.0;
	}
	// The engine's TickComponent body (Super::TickComponent itself does nothing for this class).
	if (GetStatus() == EPathFollowingStatus::Moving)
	{
		UpdatePathSegment();
	}
	if (GetStatus() == EPathFollowingStatus::Moving)
	{
		FollowPathSegment(DeltaTime);
	}
	if (const UVeloxMovementComponent* Movement = GetBotMovement())
	{
		LastKnockbackGrace = Movement->GetKnockbackGrace();
	}
}

void UVeloxBotPathFollowingComponent::ClearLink()
{
	Maneuver = FVeloxBotManeuver();
	CurrentLink.Reset();
	bLinkPending = false;
}

void UVeloxBotPathFollowingComponent::Reset()
{
	Super::Reset();
	ClearLink();
	NextHopHeadroomCheckTime = -1.0;
	bHopHeadroomClear = true;
	bSegmentBlocked = false;
	bGapLinkAhead = false;
	bShortGapAhead = false;
}

bool UVeloxBotPathFollowingComponent::IsLedgeAhead(const FVector& Feet, const FVector2D& Axis, float StepCm) const
{
	const AVeloxCharacter* Character = GetBotCharacter();
	UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return false;
	}
	const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
	const FVector Probe = Feet + FVector(Axis, 0.0) * (FMath::Max(StepCm, 0.f) + Settings.TakeoffLedgeProbeCm);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(VeloxBotTakeoffLedge), false, Character);
	return !World->LineTraceTestByObjectType(Probe + FVector(0.0, 0.0, 20.0), Probe - FVector(0.0, 0.0, Settings.TakeoffLedgeDepthCm),
		FCollisionObjectQueryParams(ECC_WorldStatic), Query);
}

bool UVeloxBotPathFollowingComponent::HopOverrunsGapTakeoff(const FVector& Feet, const FVector& Velocity) const
{
	const UVeloxMovementComponent* Movement = GetBotMovement();
	if (!bGapLinkAhead || !Movement || !Movement->IsOnGround())
	{
		return false;
	}
	const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
	const float Air = VeloxBotRules::HopAirSeconds(Movement->GetSimParams().JumpVelocity, Movement->GetGravityZ(), Settings.HopLandingSlackSeconds);
	const double FeetPast = FVector2D::DotProduct(FVector2D(Feet) - FVector2D(GapLinkStart), GapLinkAxis);
	const double Along = FVector2D::DotProduct(FVector2D(Velocity), GapLinkAxis);
	if (FeetPast + Along * Air <= Settings.TakeoffMaxPastStartCm)
	{
		// Lands (even with the slack's strafe gain) on the take-off side, with ground ticks left for the take-off.
		return false;
	}
	// A hop that clears the whole link by itself (the plain air time, no slack: the short estimate) is the gap jump; the
	// pending traversal ends Ok on the end's surface. Anything between lands in the gap or on its edge.
	const float PlainAir = VeloxBotRules::HopAirSeconds(Movement->GetSimParams().JumpVelocity, Movement->GetGravityZ(), 0.f);
	return FeetPast + Along * PlainAir < GapLinkLength;
}

float UVeloxBotPathFollowingComponent::ProbeLandBeyondEnd(const FVector& Start, const FVector& End) const
{
	const FVector2D Axis = FVector2D(End - Start).GetSafeNormal();
	UWorld* World = GetWorld();
	const float Probe = UVeloxBotSettings::Get().TakeoffLandProbeCm;
	if (Axis.IsZero() || !World || Probe <= 0.f)
	{
		return -1.f;
	}
	// NavigationRaycast is true when the ray leaves the mesh (the island's eroded edge, a wall): Hit is the exit point.
	FVector Hit;
	AVeloxBotController* Bot = GetBotController();
	const FVector Far = End + FVector(Axis, 0.0) * Probe;
	if (!UNavigationSystemV1::NavigationRaycast(World, End, Far, Hit, Bot ? Bot->GetNavFilter() : TSubclassOf<UNavigationQueryFilter>(), Bot))
	{
		return -1.f;
	}
	return static_cast<float>(FMath::Max(0.0, FVector2D::DotProduct(FVector2D(Hit - End), Axis)));
}

bool UVeloxBotPathFollowingComponent::ShortGapCap(const FVector& Start, const FVector& End, float& OutMax, float& OutNeed) const
{
	OutMax = 0.f;
	OutNeed = 0.f;
	const UVeloxMovementComponent* Moves = GetBotMovement();
	if (!Moves)
	{
		return false;
	}
	const float Beyond = ProbeLandBeyondEnd(Start, End);
	const float Air = VeloxBotManeuverRules::JumpAirSeconds(Moves->GetSimParams().JumpVelocity, Moves->GetGravityZ(), static_cast<float>(Start.Z - End.Z));
	const UVeloxBotSettings& Tune = UVeloxBotSettings::Get();
	const float Length = static_cast<float>(FVector::Dist2D(Start, End));
	OutMax = VeloxBotManeuverRules::GapTakeoffMaxSpeed(Length, Tune.TakeoffMaxPastStartCm, Beyond, Tune.TakeoffLandFarInsetCm, Air);
	OutNeed = VeloxBotManeuverRules::GapTakeoffSpeed(Length, Tune.TakeoffMaxPastStartCm, Tune.TakeoffLandInsetCm, Air);
	return OutMax > 0.f && OutMax > OutNeed;
}

bool UVeloxBotPathFollowingComponent::InShortGapBrakeZone(const FVector& Feet) const
{
	return bShortGapAhead && bGapLinkAhead && FVector::Dist2D(Feet, ShortGapStart) <= UVeloxBotSettings::Get().TakeoffBrakeLeadCm;
}

bool UVeloxBotPathFollowingComponent::HopIntoShortGap(const FVector& Feet, const FVector& Velocity) const
{
	const UVeloxMovementComponent* Movement = GetBotMovement();
	if (!bShortGapAhead || !Movement)
	{
		return false;
	}
	const float Lead = UVeloxBotSettings::Get().TakeoffBrakeLeadCm;
	if (FVector::Dist2D(Feet, ShortGapStart) <= Lead)
	{
		return true;
	}
	const float Air = VeloxBotRules::HopAirSeconds(Movement->GetSimParams().JumpVelocity, Movement->GetGravityZ(), 0.f);
	return FVector2D::Distance(FVector2D(Feet) + FVector2D(Velocity) * Air, FVector2D(ShortGapStart)) <= Lead;
}

bool UVeloxBotPathFollowingComponent::ShortGapGroundWish(const FVector2D& Velocity, const FVector2D& Axis, float NeedAlong, float MaxAlong, FVector2D& OutWish)
{
	const float Along = static_cast<float>(FVector2D::DotProduct(Velocity, Axis));
	const FVector2D Desired = Axis * FMath::Clamp(Along, NeedAlong, FMath::Max(NeedAlong, MaxAlong));
	const FVector2D Error = Desired - Velocity;
	if (Error.Size() <= UVeloxBotSettings::Get().PadArriveDeadbandCmS)
	{
		return false;
	}
	OutWish = Error.GetSafeNormal();
	return true;
}

FVector2D UVeloxBotPathFollowingComponent::ShortGapAirWish(const FVector& Feet, const FVector& Velocity) const
{
	const UVeloxMovementComponent* Movement = GetBotMovement();
	const FVector2D Axis = FVector2D(LinkEnd - LinkStart).GetSafeNormal();
	const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
	if (!Movement || Axis.IsZero())
	{
		return FVector2D::ZeroVector;
	}
	const FVeloxBallisticArrive Arrive = VeloxBotRocketJump::ArriveWish(Feet, Velocity, LinkEnd, Movement->GetGravityZ(), Settings.PadArriveDeadbandCmS);
	if (Arrive.TimeToTarget <= UE_KINDA_SMALL_NUMBER)
	{
		return Arrive.WishDir;
	}
	const double T = Arrive.TimeToTarget;
	const FVector2D Vel(Velocity);
	const double ToEndAlong = FVector2D::DotProduct(FVector2D(LinkEnd - Feet), Axis);
	const double MinAlong = (ToEndAlong - Maneuver.Params.TakeoffLandInsetCm) / T;
	const double MaxAlong = FMath::Max(MinAlong, (ToEndAlong + Maneuver.Params.LandBeyondEndCm - Maneuver.Params.TakeoffLandFarInsetCm) / T);
	const double Along = FVector2D::DotProduct(Vel, Axis);
	const FVector2D NeededAcross = Arrive.NeededVelocity - Axis * FVector2D::DotProduct(Arrive.NeededVelocity, Axis);
	const FVector2D Desired = Axis * FMath::Clamp(Along, MinAlong, MaxAlong) + NeededAcross;
	const FVector2D Error = Desired - Vel;
	return Error.Size() > Settings.PadArriveDeadbandCmS ? Error.GetSafeNormal() : FVector2D::ZeroVector;
}

bool UVeloxBotPathFollowingComponent::HasHopHeadroom(double Now)
{
	if (Now < NextHopHeadroomCheckTime)
	{
		return bHopHeadroomClear;
	}
	NextHopHeadroomCheckTime = Now + HopHeadroomCheckSeconds;
	const AVeloxCharacter* Character = GetBotCharacter();
	const UVeloxMovementComponent* Movement = GetBotMovement();
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	UWorld* World = GetWorld();
	if (!Character || !Movement || !Capsule || !World)
	{
		bHopHeadroomClear = true;
		return true;
	}
	using namespace VeloxBotFollowPrivate;
	const float Rise = VeloxBotFollow::HopHeadroomCm(Movement->GetSimParams().JumpVelocity, Movement->GetGravityZ(), HopHeadroomMarginCm);
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = FMath::Max(Radius, Capsule->GetScaledCapsuleHalfHeight());
	const FVector Start = Character->GetActorLocation() + FVector(0.0, 0.0, HeadroomSkinCm);
	const FVector End = Start + FVector(0.0, 0.0, FMath::Max(0.f, Rise - HeadroomSkinCm));
	FCollisionQueryParams Query(SCENE_QUERY_STAT(VeloxBotHopHeadroom), false, Character);
	FHitResult Hit;
	const bool bHit = World->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeCapsule(Radius, HalfHeight), Query);
	// A start overlap is geometry the pawn already touches: the floor or a wall (depenetration up / sideways) is not a
	// block, a ceiling it is jammed against (depenetration down: the capsule fills the clearance) is.
	const bool bClear = !bHit || (Hit.bStartPenetrating && Hit.Normal.Z > -VeloxBotFollowPrivate::CeilingNormalZ);
	if (bClear != bHopHeadroomClear)
	{
		const AVeloxBotController* Bot = GetBotController();
		UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotHeadroom bot=%s clear=%d at=%s rise=%.0f hit=%s"), Bot ? *Bot->GetBotName() : TEXT("-"), bClear ? 1 : 0,
			*Character->GetActorLocation().ToCompactString(), Rise, bClear ? TEXT("-") : *GetNameSafe(Hit.GetActor()));
	}
	bHopHeadroomClear = bClear;
	return bClear;
}

bool UVeloxBotPathFollowingComponent::HasHopLanding()
{
	const AVeloxBotController* Bot = GetBotController();
	const UVeloxMovementComponent* Movement = GetBotMovement();
	if (!Bot || !Movement || !VeloxBotCombatGlue::IsEdgeAware(GetWorld()))
	{
		NextHopLandingCheckTime = -1.0;
		return true;
	}
	if (!Movement->IsOnGround())
	{
		NextHopLandingCheckTime = -1.0;
		return IsOnLink() || !VeloxBotCombatGlue::IsEdgeGuardOn(GetWorld()) || HasAirHopLanding();
	}
	const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
	const double Now = GetWorld()->GetTimeSeconds();
	const float JumpVelocity = Movement->GetSimParams().JumpVelocity;
	const float Gravity = FMath::Max(FMath::Abs(Movement->GetGravityZ()), 1.f);
	const float AirSeconds = VeloxBotRules::HopAirSeconds(JumpVelocity, Movement->GetGravityZ(), Settings.HopLandingSlackSeconds);
	const FVector2D Velocity2D(Movement->Velocity);
	const FVector Landing = VeloxBotCombat::HopLandingPoint(Movement->GetActorFeetLocation(), Velocity2D, AirSeconds);
	// The cached verdict holds only while the predicted landing stays near the one it was computed for (a strafe flip or
	// a turn moves the landing by metres within one cache period).
	if (NextHopLandingCheckTime >= 0.0 && Now < NextHopLandingCheckTime &&
		FVector::DistSquared2D(Landing, CachedHopLanding) <= FMath::Square(Settings.HopLandingRecheckCm))
	{
		HopLandingBlockedTicks += bHopLandingClear ? 0 : 1;
		return bHopLandingClear;
	}
	NextHopLandingCheckTime = Now + Settings.EdgeProbeCheckSeconds;
	CachedHopLanding = Landing;
	++HopLandingChecks;
	// H8 (review F5): the ground under the landing within the slope window, not at the take-off height (ramps, chutes).
	bHopLandingClear = VeloxBotCombatGlue::HopLandsSafely(*Bot, Landing, JumpVelocity * JumpVelocity / (2.f * Gravity),
		static_cast<float>(Velocity2D.Size() * AirSeconds), Movement->GetWalkableFloorZ());
	if (bHopLandingClear)
	{
		return true;
	}
	++HopLandingBlockedTicks;
	UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotHopLanding bot=%s refused at=%s landing=%s"), *Bot->GetBotName(), *Movement->GetActorFeetLocation().ToCompactString(),
		*Landing.ToCompactString());
	return false;
}

bool UVeloxBotPathFollowingComponent::HasAirHopLanding()
{
	// Task M7 H13: a held jump re-jumps in the landing frame itself (bAutoHop), so a bunny-hop chain never shows the bot a
	// grounded tick and the take-off guard above ran only for its first hop. Judge the re-jump from the air: touchdown at
	// the last ground level, then one more hop with the same horizontal speed.
	const AVeloxBotController* Bot = GetBotController();
	const UVeloxMovementComponent* Movement = GetBotMovement();
	const UWorld* World = GetWorld();
	if (!Bot || !Movement || !World || Movement->Velocity.Z > 0.0)
	{
		return true;
	}
	const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
	const float Gravity = FMath::Max(FMath::Abs(Movement->GetGravityZ()), 1.f);
	const FVector Feet = Movement->GetActorFeetLocation();
	const float Touchdown = VeloxBotRules::FlightSecondsToLevel(static_cast<float>(Movement->Velocity.Z), static_cast<float>(Feet.Z - LastGroundFeetZ), Gravity);
	if (Touchdown > Settings.AirHopGuardSeconds)
	{
		return true;
	}
	const float JumpVelocity = Movement->GetSimParams().JumpVelocity;
	const float AirSeconds = VeloxBotRules::HopAirSeconds(JumpVelocity, Movement->GetGravityZ(), Settings.HopLandingSlackSeconds);
	const FVector2D Velocity2D(Movement->Velocity);
	const FVector TouchdownAt(FVector2D(Feet) + Velocity2D * Touchdown, LastGroundFeetZ);
	const FVector Landing = VeloxBotCombat::HopLandingPoint(TouchdownAt, Velocity2D, AirSeconds);
	const double Now = World->GetTimeSeconds();
	if (NextAirHopCheckTime >= 0.0 && Now < NextAirHopCheckTime && FVector::DistSquared2D(Landing, CachedAirHopLanding) <= FMath::Square(Settings.HopLandingRecheckCm))
	{
		AirHopReleasedTicks += bAirHopClear ? 0 : 1;
		return bAirHopClear;
	}
	NextAirHopCheckTime = Now + Settings.EdgeProbeCheckSeconds;
	CachedAirHopLanding = Landing;
	++AirHopChecks;
	bAirHopClear = VeloxBotCombatGlue::HopLandsSafely(*Bot, Landing, JumpVelocity * JumpVelocity / (2.f * Gravity), static_cast<float>(Velocity2D.Size() * AirSeconds),
		Movement->GetWalkableFloorZ());
	AirHopReleasedTicks += bAirHopClear ? 0 : 1;
	return bAirHopClear;
}

bool UVeloxBotPathFollowingComponent::IsNearLinkAhead(const FVector& Feet, float RadiusCm) const
{
	return bLinkAhead && FVector::DistSquared2D(Feet, LinkAheadStart) <= FMath::Square(static_cast<double>(RadiusCm));
}

bool UVeloxBotPathFollowingComponent::UpdateSegmentBlocked(const FVector& Feet, const FVector& Target, double Now)
{
	if (Now < NextSegmentBlockCheckTime)
	{
		return bSegmentBlocked;
	}
	const FVeloxBotCostScope Cost(GetWorld(), EVeloxBotCost::Segment);
	const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
	NextSegmentBlockCheckTime = Now + Settings.SegmentBlockCheckSeconds;
	UWorld* World = GetWorld();
	AVeloxBotController* Bot = GetBotController();
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Bot || !NavSys)
	{
		return bSegmentBlocked;
	}
	// D35: the feet onto the mesh; no projection (a hop apex, off the mesh) skips the check. D41 (task 04i): with the shared
	// on-mesh extent, so this check and the controller's off-mesh test agree on "on the mesh" in the same tick.
	const ANavigationData* NavData = NavSys->GetNavDataForProps(Bot->GetNavAgentPropertiesRef(), Feet);
	FNavLocation Projected;
	// D44: with the bot's own filter, like the raycast below and the controller's off-mesh test.
	if (!NavData || !NavSys->ProjectPointToNavigation(Feet, Projected, UVeloxBotItemRouter::GetOnMeshExtent(), NavData,
			UNavigationQueryFilter::GetQueryFilter(*NavData, Bot, Bot->GetNavFilter())))
	{
		return bSegmentBlocked;
	}
	FVector Start = Projected.Location;
	FVector PolyCenter;
	const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavData);
	if (Recast && Recast->GetPolyCenter(Projected.NodeRef, PolyCenter))
	{
		const FVector Inset = (PolyCenter - Start).GetClampedToMaxSize(VeloxBotFollowPrivate::SegmentRayInsetCm);
		Start += Inset;
	}
	// True when blocked (also without a start poly; Detour reports t = 0 for a start outside its poly).
	FVector HitLocation;
	const bool bHit = UNavigationSystemV1::NavigationRaycast(World, Start, Target, HitLocation, Bot->GetNavFilter(), Bot);
	const float Remaining = static_cast<float>(FVector::Dist(Start, Target));
	const float HitDist = bHit ? static_cast<float>(FVector::Dist(Start, HitLocation)) : Remaining;
	const bool bBlocked = VeloxBotFollow::IsSegmentBlocked(bHit, HitDist, Remaining, Settings.SegmentBlockClearanceCm);
	if (bBlocked)
	{
		LastSegmentBlock.From = Start;
		LastSegmentBlock.Target = Target;
		LastSegmentBlock.Hit = HitLocation;
		LastSegmentBlock.HitDistCm = HitDist;
		LastSegmentBlock.RemainingCm = Remaining;
	}
	if (bBlocked != bSegmentBlocked)
	{
		UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotSegment bot=%s blocked=%d at=%s target=%s hit=%s hit_dist=%.0f remaining=%.0f"), *Bot->GetBotName(), bBlocked ? 1 : 0,
			*Feet.ToCompactString(), *Target.ToCompactString(), bHit ? *HitLocation.ToCompactString() : TEXT("-"), HitDist, Remaining);
	}
	bSegmentBlocked = bBlocked;
	return bBlocked;
}

void UVeloxBotPathFollowingComponent::SetMoveSegment(int32 SegmentStartIndex)
{
	// D35: a new segment (or a new path) starts unblocked; the next check decides.
	bSegmentBlocked = false;
	bGapLinkAhead = false;
	bShortGapAhead = false;
	bLinkAhead = false;
	if (IsOnLink())
	{
		// A repath replaced the segment under a running traversal.
		FVeloxBotLinkReport Report;
		Report.Type = Maneuver.IsActive() ? Maneuver.Type : PendingParams.Type;
		Report.From = LinkStart;
		Report.To = LinkEnd;
		Report.LinkName = GetNameSafe(CurrentLink.Get());
		ClearLink();
		if (AVeloxBotController* Bot = GetBotController())
		{
			Bot->NotifyLinkFinished(Report);
		}
	}

	Super::SetMoveSegment(SegmentStartIndex);

	const FNavigationPath* PathInstance = Path.Get();
	if (!PathInstance || MoveSegmentStartIndex != SegmentStartIndex)
	{
		return;
	}
	const TArray<FNavPathPoint>& Points = PathInstance->GetPathPoints();
	if (!Points.IsValidIndex(MoveSegmentStartIndex) || !Points.IsValidIndex(MoveSegmentEndIndex))
	{
		return;
	}
	const FNavPathPoint& StartPoint = Points[MoveSegmentStartIndex];
	const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld());
	// Task M7 H11: a plain segment into a JUMP / JUMP_FAST link start (the hop overrun guard in FollowPathSegment).
	bGapLinkAhead = false;
	bShortGapAhead = false;
	GapLinkMaxAlongCmS = 0.f;
	GapLinkNeedAlongCmS = 0.f;
	const FNavPathPoint& EndPoint = Points[MoveSegmentEndIndex];
	if (Bots && !FNavMeshNodeFlags(StartPoint.Flags).IsNavLink() && FNavMeshNodeFlags(EndPoint.Flags).IsNavLink() && Points.IsValidIndex(MoveSegmentEndIndex + 1))
	{
		const FVeloxNavLinkMatch Ahead = Bots->FindLinkByStart(EndPoint.Location, TNumericLimits<float>::Max());
		if (Ahead.Link && Ahead.Distance2D <= Ahead.Link->GetSnapRadius())
		{
			bLinkAhead = true;
			LinkAheadStart = EndPoint.Location;
			const EVeloxManeuverType AheadType = VeloxBotFollow::ManeuverTypeFor(Ahead.Link->GetLinkType());
			GapLinkAxis = FVector2D(Points[MoveSegmentEndIndex + 1].Location - EndPoint.Location).GetSafeNormal();
			GapLinkStart = EndPoint.Location;
			GapLinkLength = FVector::Dist2D(Points[MoveSegmentEndIndex + 1].Location, EndPoint.Location);
			bGapLinkAhead = (AheadType == EVeloxManeuverType::Jump || AheadType == EVeloxManeuverType::JumpFast) && !GapLinkAxis.IsZero();
			// Task M7 H14: a short landing zone ahead caps the take-off speed (the same numbers the traversal uses).
			if (bGapLinkAhead && ShortGapCap(EndPoint.Location, Points[MoveSegmentEndIndex + 1].Location, GapLinkMaxAlongCmS, GapLinkNeedAlongCmS))
			{
				bShortGapAhead = true;
				ShortGapStart = EndPoint.Location;
			}
		}
	}
	else if (Bots && !FNavMeshNodeFlags(EndPoint.Flags).IsNavLink() && Points.IsValidIndex(MoveSegmentEndIndex + 2) &&
		FNavMeshNodeFlags(Points[MoveSegmentEndIndex + 1].Flags).IsNavLink())
	{
		// Task M7 H14: the segment before the plain one into a short-landing gap link (Fornax Isle_In: bots hopped at the
		// corner before the take-off segment and landed on the take-off with the hop's 850-1000 cm/s and its direction).
		const FVector NextStart = Points[MoveSegmentEndIndex + 1].Location;
		const FVeloxNavLinkMatch Next = Bots->FindLinkByStart(NextStart, TNumericLimits<float>::Max());
		const EVeloxManeuverType NextType = Next.Link ? VeloxBotFollow::ManeuverTypeFor(Next.Link->GetLinkType()) : EVeloxManeuverType::Drop;
		float Max = 0.f;
		float Need = 0.f;
		if (Next.Link && Next.Distance2D <= Next.Link->GetSnapRadius() && (NextType == EVeloxManeuverType::Jump || NextType == EVeloxManeuverType::JumpFast) &&
			ShortGapCap(NextStart, Points[MoveSegmentEndIndex + 2].Location, Max, Need))
		{
			bShortGapAhead = true;
			ShortGapStart = NextStart;
		}
	}
	if (!FNavMeshNodeFlags(StartPoint.Flags).IsNavLink())
	{
		return;
	}
	// Recast inserts the link ends as path points: the start point matches the link's authored start (or, BothWays, its end).
	const FVeloxNavLinkMatch Match = Bots ? Bots->FindLinkByStart(StartPoint.Location, TNumericLimits<float>::Max()) : FVeloxNavLinkMatch();
	if (!Match.Link || Match.Distance2D > Match.Link->GetSnapRadius())
	{
		UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotLink bot=%s unmatched link segment at %s (nearest %s %.1f cm): followed as a plain segment"),
			*GetNameSafe(GetOwner()), *StartPoint.Location.ToCompactString(), *GetNameSafe(Match.Link), Match.Distance2D);
		return;
	}

	const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
	PendingParams = FVeloxManeuverParams();
	PendingParams.Type = VeloxBotFollow::ManeuverTypeFor(Match.Link->GetLinkType());
	PendingParams.EndAcceptanceCm = Settings.LinkEndAcceptanceCm;
	PendingParams.TakeoffLeadCm = Settings.TakeoffLeadCm;
	PendingParams.TakeoffMaxPastStartCm = Settings.TakeoffMaxPastStartCm;
	PendingParams.StartAcceptanceCm = Settings.LinkStartAcceptanceCm;
	PendingParams.RequiredSpeedFraction = Settings.JumpFastSpeedFraction;
	PendingParams.RunUpCm = Settings.JumpFastRunUpCm;
	PendingParams.MaxRetries = Settings.JumpFastRetries;
	PendingParams.ImpulseTimeoutSeconds = Settings.RocketJumpImpulseTimeoutSeconds;
	PendingParams.TimeoutSeconds = Settings.LinkTimeoutSeconds;
	PendingParams.BelowStartAbortCm = Settings.LinkBelowStartCm;
	PendingParams.RequiredSpeedCmS = Match.Link->GetRequiredSpeed();
	PendingParams.RunUpSeconds = Match.Link->GetRunUpSeconds();
	PendingParams.RocketJumpPitchDeg = Match.Link->GetRocketJumpPitch();

	CurrentLink = Match.Link;
	LinkStart = StartPoint.Location;
	LinkEnd = Points[MoveSegmentEndIndex].Location;
	const FVector Incoming = MoveSegmentStartIndex > 0 ? LinkStart - Points[MoveSegmentStartIndex - 1].Location : LinkEnd - LinkStart;
	LinkIncomingDir = FVector2D(Incoming).GetSafeNormal();
	if (LinkIncomingDir.IsZero())
	{
		LinkIncomingDir = FVector2D(LinkEnd - LinkStart).GetSafeNormal();
	}
	const FVector2D Axis = FVector2D(LinkEnd - LinkStart).GetSafeNormal();
	if ((PendingParams.Type == EVeloxManeuverType::Jump || PendingParams.Type == EVeloxManeuverType::JumpFast) && !Axis.IsZero())
	{
		// Task M7 H11: the take-off speed check and the run-up go along the link (a run-up along the incoming segment
		// re-creates the side approach), as far back as the mesh reaches behind the start (Fornax's 300 cm island).
		LinkIncomingDir = Axis;
		PendingParams.LinkLengthCm = static_cast<float>(FVector::Dist2D(LinkStart, LinkEnd));
		PendingParams.TakeoffLandInsetCm = Settings.TakeoffLandInsetCm;
		PendingParams.TakeoffMaxLateralCmS = Settings.TakeoffMaxLateralCmS;
		PendingParams.LandBeyondEndCm = ProbeLandBeyondEnd(LinkStart, LinkEnd);
		PendingParams.TakeoffLandFarInsetCm = Settings.TakeoffLandFarInsetCm;
		if (const UVeloxMovementComponent* Moves = GetBotMovement())
		{
			PendingParams.GapAirSeconds = VeloxBotManeuverRules::JumpAirSeconds(Moves->GetSimParams().JumpVelocity, Moves->GetGravityZ(),
				static_cast<float>(LinkStart.Z - LinkEnd.Z));
		}
		constexpr float BackMarginCm = 40.f;
		const float Want = PendingParams.RunUpCm + PendingParams.TakeoffLeadCm + BackMarginCm;
		const FVector Back = LinkStart - FVector(Axis, 0.0) * Want;
		FVector Hit;
		AVeloxBotController* Bot = GetBotController();
		const bool bLeaves = UNavigationSystemV1::NavigationRaycast(GetWorld(), LinkStart, Back, Hit, Bot ? Bot->GetNavFilter() : TSubclassOf<UNavigationQueryFilter>(), Bot);
		if (bLeaves)
		{
			const float Free = static_cast<float>(FVector::Dist2D(LinkStart, Hit));
			PendingParams.RunUpCm = FMath::Max(0.f, FMath::Min(PendingParams.RunUpCm, Free - PendingParams.TakeoffLeadCm - BackMarginCm));
		}
	}
	bLinkPending = true;
	const UWorld* World = GetWorld();
	LinkSegmentStartTime = World ? World->GetTimeSeconds() : 0.0;

	// Begin at once on the ground; a bot that reaches the link start mid-hop first lands (or is launched by the pad).
	const UVeloxMovementComponent* Movement = GetBotMovement();
	if (Movement && Movement->IsOnGround())
	{
		BeginManeuver(LinkSegmentStartTime);
	}
}

void UVeloxBotPathFollowingComponent::BeginManeuver(double Now)
{
	// Task 06c (review D1): a traversal begun inside a foreign flight (longer than the bot's own hop) may end AirStart;
	// one begun on the ground or in the bot's own hop is an ordinary attempt.
	FVeloxManeuverInput In;
	In.Time = Now;
	const UVeloxMovementComponent* Movement = GetBotMovement();
	const AVeloxBotController* Bot = GetBotController();
	if (Movement && Bot)
	{
		const float HopAir = VeloxBotRules::HopAirSeconds(Movement->GetSimParams().JumpVelocity, Movement->GetGravityZ(), Bot->StuckProgressFlightSlackSeconds);
		In.bBeganInFlight = VeloxBotRules::IsFlight(Movement->IsOnGround(), AirborneSince, Now, HopAir);
	}
	Maneuver.Begin(PendingParams, LinkStart, LinkEnd, In);
	bLinkPending = false;
	UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotLink bot=%s begin type=%s link=%s from=%s to=%s flight=%d"), *GetNameSafe(GetOwner()),
		VeloxBotFollowPrivate::ManeuverName(PendingParams.Type), *GetNameSafe(CurrentLink.Get()), *LinkStart.ToCompactString(), *LinkEnd.ToCompactString(),
		In.bBeganInFlight ? 1 : 0);
}

void UVeloxBotPathFollowingComponent::FinishManeuver(EVeloxManeuverResult Result, double Now)
{
	FVeloxBotLinkReport Report;
	// A traversal that ends while still pending (landed on the end surface, D28 timeout) never began the machine.
	Report.Type = Maneuver.Phase == EVeloxManeuverPhase::Idle ? PendingParams.Type : Maneuver.Type;
	Report.Result = Result;
	Report.From = LinkStart;
	Report.To = LinkEnd;
	Report.Seconds = Now - LinkSegmentStartTime;
	Report.LinkName = GetNameSafe(CurrentLink.Get());
	ClearLink();
	if (Result == EVeloxManeuverResult::Ok)
	{
		// The link segment is done: on to the segment that starts at the link end (if the path goes on).
		const FNavigationPath* PathInstance = Path.Get();
		if (PathInstance && PathInstance->GetPathPoints().IsValidIndex(GetNextPathIndex() + 1))
		{
			SetNextMoveSegment();
		}
	}
	if (AVeloxBotController* Bot = GetBotController())
	{
		Bot->NotifyLinkFinished(Report);
	}
}

bool UVeloxBotPathFollowingComponent::ImpulseThisStep() const
{
	const UVeloxMovementComponent* Movement = GetBotMovement();
	return Movement && Movement->GetKnockbackGrace() > LastKnockbackGrace + VeloxBotFollowPrivate::GraceRiseEpsilon;
}

bool UVeloxBotPathFollowingComponent::IsOnEndSurface(const FVector& Feet) const
{
	FVector Hit;
	AVeloxBotController* Bot = GetBotController();
	// NavigationRaycast is true when the ray leaves the mesh (another level, a gap): then the pawn is not on the end's surface.
	return !UNavigationSystemV1::NavigationRaycast(GetWorld(), Feet, LinkEnd, Hit, Bot ? Bot->GetNavFilter() : TSubclassOf<UNavigationQueryFilter>(), Bot);
}

void UVeloxBotPathFollowingComponent::UpdatePathSegment()
{
	if (IsOnLink() && Path.IsValid() && NavMovementInterface.IsValid())
	{
		// The maneuver ends the link segment (landing near the end), not the engine's reach test, which would advance it
		// mid-flight as soon as the pawn passes the end horizontally.
		return;
	}
	const FNavigationPath* PathInstance = Path.Get();
	const UVeloxMovementComponent* Movement = GetBotMovement();
	if (GetStatus() == EPathFollowingStatus::Moving && PathInstance && PathInstance->IsValid() && Movement)
	{
		const TArray<FNavPathPoint>& Points = PathInstance->GetPathPoints();
		const FVector Feet = Movement->GetActorFeetLocation();
		if (PathInstance->IsPartial() && Points.Num() > 0 && MoveSegmentEndIndex >= Points.Num() - 1)
		{
			// The end of a partial path: the engine's precise reach test only runs near the requested goal, so a partial
			// path would never finish and the bot would stand at its end. Finish it here (the controller then re-plans).
			const FVector& End = Points.Last().Location;
			if (FVector::Dist2D(Feet, End) <= CurrentAcceptanceRadius + PartialEndSlackCm && FMath::Abs(Feet.Z - End.Z) <= PartialEndSlackCm)
			{
				OnPathFinished(EPathFollowingResult::Success, FPathFollowingResultFlags::None);
				return;
			}
		}
		// Enter a link segment early: near its start, or when an impulse (the pad's launch) arrives on the way in.
		if (Points.IsValidIndex(MoveSegmentEndIndex + 1) && FNavMeshNodeFlags(Points[MoveSegmentEndIndex].Flags).IsNavLink())
		{
			const double Dist = FVector::Dist2D(Feet, Points[MoveSegmentEndIndex].Location);
			if (Dist <= LinkEntryRadiusCm || (Dist <= LinkEntryImpulseRadiusCm && ImpulseThisStep()))
			{
				SetNextMoveSegment();
				return;
			}
		}
	}
	Super::UpdatePathSegment();
}

void UVeloxBotPathFollowingComponent::OnPathFinished(const FPathFollowingResult& Result)
{
	if (IsOnLink())
	{
		FVeloxBotLinkReport Report;
		Report.Type = Maneuver.IsActive() ? Maneuver.Type : PendingParams.Type;
		Report.From = LinkStart;
		Report.To = LinkEnd;
		Report.LinkName = GetNameSafe(CurrentLink.Get());
		const UWorld* World = GetWorld();
		Report.Seconds = (World ? World->GetTimeSeconds() : 0.0) - LinkSegmentStartTime;
		ClearLink();
		Super::OnPathFinished(Result);
		if (AVeloxBotController* Bot = GetBotController())
		{
			// Result None: logged as aborted.
			Bot->NotifyLinkFinished(Report);
		}
		return;
	}
	Super::OnPathFinished(Result);
}

FVeloxBotNavOutput UVeloxBotPathFollowingComponent::SteerToward(const FVector& Target, float SegmentRemainingCm, float NextCornerDeg, bool bFinalSegment, bool bAllowHop,
	bool bAllowStrafe) const
{
	FVeloxBotNavOutput Out;
	const AVeloxBotController* Bot = GetBotController();
	const UVeloxMovementComponent* Movement = GetBotMovement();
	if (!Bot || !Movement)
	{
		return Out;
	}
	const FVeloxMoveParams& Sim = Movement->GetSimParams();
	FVeloxSteerInput In;
	In.Velocity = FVector2D(Movement->Velocity);
	In.bOnGround = Movement->IsOnGround();
	In.ToTarget = FVector2D(Target - Movement->GetActorFeetLocation());
	In.SegmentRemainingCm = SegmentRemainingCm;
	In.NextCornerDeg = NextCornerDeg;
	In.bFinalSegment = bFinalSegment;
	In.WishSpeed = Sim.AirMaxWishSpeed * Movement->GetHasteSpeedScale();
	In.AirAccel = Sim.AirAcceleration;
	In.FixedStep = Sim.FixedStep;

	FVeloxSteerParams SteerParams = VeloxBotSteering::MakeParams(Bot->GetDifficultyParams());
	SteerParams.bBunnyHop &= bAllowHop;
	SteerParams.bStrafeJump &= bAllowStrafe;
	const FVeloxSteerOutput Steer = VeloxBotSteering::Steer(In, SteerParams);
	Out.WishDir = Steer.WishDir;
	Out.WishScale = Steer.WishScale;
	Out.bJumpHeld = Steer.bJumpHeld;
	Out.PathDir = In.ToTarget.GetSafeNormal();
	return Out;
}

bool UVeloxBotPathFollowingComponent::TickManeuver(float DeltaTime, double Now)
{
	const UVeloxMovementComponent* Movement = GetBotMovement();
	if (!Movement)
	{
		return true;
	}
	const FVector Feet = Movement->GetActorFeetLocation();
	const bool bOnGround = Movement->IsOnGround();
	const bool bImpulse = ImpulseThisStep();

	if (bLinkPending)
	{
		if (PendingParams.TimeoutSeconds > 0.f && Now - LinkSegmentStartTime >= PendingParams.TimeoutSeconds)
		{
			// D28 / D37 (d): one budget for the link segment (a flight that never lands, a bot that never reaches the ground).
			FinishManeuver(EVeloxManeuverResult::Timeout, Now);
			return false;
		}
		if (!bOnGround && !bImpulse)
		{
			// Mid-hop (or mid-flight) at the link start: release jump, drift toward the end, decide on landing.
			NavOutput = SteerToward(LinkEnd, static_cast<float>(FVector::Dist2D(Feet, LinkEnd)), 0.f, false, false, false);
			NavOutput.bJumpHeld = false;
			NavOutput.bOnLink = true;
			return true;
		}
		if (bOnGround && PendingParams.Type != EVeloxManeuverType::RocketJump && FVector::Dist2D(Feet, LinkStart) > PendingParams.EndAcceptanceCm &&
			IsOnEndSurface(Feet))
		{
			// The flight already carried the pawn onto the end's surface (the segment started in the air).
			FinishManeuver(EVeloxManeuverResult::Ok, Now);
			return false;
		}
		BeginManeuver(Now);
	}
	else if (Maneuver.IsActive() && Maneuver.Phase != EVeloxManeuverPhase::Launched && Maneuver.Params.TimeoutSeconds > 0.f &&
		Now - LinkSegmentStartTime >= Maneuver.Params.TimeoutSeconds)
	{
		// D37 (d): the pending wait and the traversal share one LinkTimeoutSeconds budget from the segment start
		// (Launched keeps its own ImpulseTimeoutSeconds).
		FinishManeuver(EVeloxManeuverResult::Timeout, Now);
		return false;
	}

	FVeloxManeuverInput In;
	In.bOnGround = bOnGround;
	In.Speed2D = Movement->GetSpeed2D();
	In.DistToStart = static_cast<float>(FVector::Dist2D(Feet, LinkStart));
	In.DistToEnd = static_cast<float>(FVector::Dist(Feet, LinkEnd));
	In.HeightBelowStartCm = static_cast<float>(LinkStart.Z - Feet.Z);
	In.HeightAboveEndCm = static_cast<float>(Feet.Z - LinkEnd.Z);
	if (Maneuver.Type == EVeloxManeuverType::Jump || Maneuver.Type == EVeloxManeuverType::JumpFast)
	{
		// Task M7 H11: the take-off point along the link axis, and the ledge probe once the take-off window is open.
		const FVector2D Axis = FVector2D(LinkEnd - LinkStart).GetSafeNormal();
		In.ProgressCm = static_cast<float>(FVector2D::DotProduct(FVector2D(Feet - LinkStart), Axis));
		In.StepCm = In.Speed2D * DeltaTime;
		In.AlongSpeedCmS = static_cast<float>(FVector2D::DotProduct(FVector2D(Movement->Velocity), Axis));
		In.LateralSpeedCmS = static_cast<float>(FMath::Abs(FVector2D::CrossProduct(Axis, FVector2D(Movement->Velocity))));
		if (bOnGround && Maneuver.Phase == EVeloxManeuverPhase::Approach && !Axis.IsZero() && In.ProgressCm + In.StepCm >= -Maneuver.Params.TakeoffLeadCm)
		{
			In.bLedgeAhead = IsLedgeAhead(Feet, Axis, In.StepCm);
		}
	}
	if (Maneuver.Phase == EVeloxManeuverPhase::Airborne && Maneuver.bSeenAirborne && bOnGround && In.DistToEnd > Maneuver.Params.EndAcceptanceCm && IsOnEndSurface(Feet))
	{
		// Landed on the end's nav surface (the platform), only farther than the acceptance radius from the authored end
		// point: the traversal is done; the next segment walks the rest.
		In.DistToEnd = 0.f;
	}
	In.bImpulseReceived = bImpulse;
	if (Maneuver.Type == EVeloxManeuverType::RocketJump)
	{
		// Review F1 / D11: eligibility of this link, asked now (the link was matched in this step's SetMoveSegment).
		const AVeloxBotController* Bot = GetBotController();
		const AVeloxNavLink* Link = CurrentLink.Get();
		bool bSwitchDone = false;
		In.bEligible = Bot && Link && Bot->EvaluateRocketJump(*Link, bSwitchDone);
		In.bSwitchDone = bSwitchDone;
	}
	In.Time = Now;
	const EVeloxManeuverPhase PhaseBefore = Maneuver.Phase;
	const FVeloxManeuverOutput Out = Maneuver.Tick(In);
	if ((Maneuver.Type == EVeloxManeuverType::Jump || Maneuver.Type == EVeloxManeuverType::JumpFast) && bOnGround &&
		(Out.bJumpHeld || (Maneuver.Phase == EVeloxManeuverPhase::BackUp && PhaseBefore != EVeloxManeuverPhase::BackUp)))
	{
		// Task M7 H11: one line per take-off decision (soak diagnostics for gap links). H14: the short-landing cap (max=0:
		// none), how far the landing zone's nav reaches past End (beyond=-1: not short) and the coasted ticks so far.
		const AVeloxBotController* Bot = GetBotController();
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotTakeoff bot=%s link=%s action=%s past_start=%.0f along=%.0f speed=%.0f need=%.0f max=%.0f beyond=%.0f brake=%d ledge=%d retries=%d run_up=%.0f"),
			Bot ? *Bot->GetBotName() : TEXT("-"), *GetNameSafe(CurrentLink.Get()), Out.bJumpHeld ? TEXT("jump") : TEXT("backup"), In.ProgressCm, In.AlongSpeedCmS, In.Speed2D,
			VeloxBotManeuverRules::GapTakeoffSpeed(Maneuver.Params.LinkLengthCm, In.ProgressCm, Maneuver.Params.TakeoffLandInsetCm, Maneuver.Params.GapAirSeconds),
			VeloxBotManeuverRules::GapTakeoffMaxSpeed(Maneuver.Params.LinkLengthCm, In.ProgressCm, Maneuver.Params.LandBeyondEndCm, Maneuver.Params.TakeoffLandFarInsetCm,
				Maneuver.Params.GapAirSeconds),
			Maneuver.Params.LandBeyondEndCm, TakeoffBrakeTicks, In.bLedgeAhead ? 1 : 0, Maneuver.Retries, Maneuver.Params.RunUpCm);
	}

	if (Out.bDone || Out.bAbort)
	{
		FinishManeuver(Out.bDone ? EVeloxManeuverResult::Ok : Out.Reason, Now);
		return false;
	}
	// Already standing at the end (a short drop walked down, a hop that carried the pawn across): the traversal is done.
	const bool bWalkable = Maneuver.Type != EVeloxManeuverType::RocketJump;
	if (bWalkable && bOnGround && In.DistToEnd <= Maneuver.Params.EndAcceptanceCm && FMath::Abs(Feet.Z - LinkEnd.Z) <= Maneuver.Params.EndAcceptanceCm * 0.5f)
	{
		FinishManeuver(EVeloxManeuverResult::Ok, Now);
		return false;
	}

	FVector Target = LinkEnd;
	switch (Out.WishToward)
	{
	case EVeloxManeuverWish::Start: Target = LinkStart; break;
	case EVeloxManeuverWish::End: Target = LinkEnd; break;
	case EVeloxManeuverWish::AwayFromStart:
		Target = LinkStart - FVector(LinkIncomingDir, 0.0) * (Maneuver.Params.RunUpCm + Maneuver.Params.TakeoffLeadCm);
		break;
	}
	const float Remaining = static_cast<float>(FVector::Dist2D(Feet, Target));
	// Task M7 H14: the short-landing ground correction owns the wish scale (it replaces the maneuver's coast).
	bool bGroundArrive = false;
	switch (Maneuver.Type)
	{
	case EVeloxManeuverType::Jump:
	case EVeloxManeuverType::JumpFast:
	{
		// Strafe toward the far side to keep (or gain) speed; the take-off tick counts as air for the steering.
		FVeloxBotNavOutput Steered = SteerToward(Target, Remaining, 0.f, false, false, true);
		if (Out.bJumpHeld && bOnGround)
		{
			// The jump fires this tick, so the movement is air movement: steer as in the air.
			const AVeloxBotController* Bot = GetBotController();
			FVeloxSteerInput AirIn;
			AirIn.Velocity = FVector2D(Movement->Velocity);
			AirIn.bOnGround = false;
			AirIn.ToTarget = FVector2D(Target - Feet);
			AirIn.SegmentRemainingCm = Remaining;
			AirIn.WishSpeed = Movement->GetSimParams().AirMaxWishSpeed * Movement->GetHasteSpeedScale();
			AirIn.AirAccel = Movement->GetSimParams().AirAcceleration;
			AirIn.FixedStep = Movement->GetSimParams().FixedStep;
			FVeloxSteerParams AirParams = Bot ? VeloxBotSteering::MakeParams(Bot->GetDifficultyParams()) : FVeloxSteerParams();
			AirParams.bBunnyHop = false;
			const FVeloxSteerOutput Air = VeloxBotSteering::Steer(AirIn, AirParams);
			Steered.WishDir = Air.WishDir;
			Steered.WishScale = Air.WishScale;
			if (Maneuver.Params.LandBeyondEndCm >= 0.f)
			{
				// Task M7 H14: a short landing zone: no strafe gain on the take-off tick; brake toward the ballistic velocity
				// that lands on End from the first air tick (the jump's own lift: JumpVelocity).
				const FVector Launch(Movement->Velocity.X, Movement->Velocity.Y, Movement->GetSimParams().JumpVelocity);
				Steered.WishDir = ShortGapAirWish(Feet, Launch);
				Steered.WishScale = Steered.WishDir.IsZero() ? 0.f : 1.f;
			}
		}
		else if (!bOnGround && Maneuver.Phase == EVeloxManeuverPhase::Airborne)
		{
			// Task M7 H11: in the gap flight correct the horizontal velocity toward the ballistic one that lands on the link end
			// (the pad / rocket-jump arrive wish). The strafe wish toward End adds nothing at 800 cm/s and never removed the
			// sideways speed of a side approach: Nemus bots with 780 cm/s along the link and 350-470 across drifted off the
			// 400 cm log. Q3 air acceleration against the velocity brakes up to AirAcceleration x AirMaxWishSpeed.
			const FVeloxBallisticArrive Arrive = VeloxBotRocketJump::ArriveWish(Feet, Movement->Velocity, LinkEnd, Movement->GetGravityZ(),
				UVeloxBotSettings::Get().PadArriveDeadbandCmS);
			// Task M7 H14: a short landing zone: the sideways error first, along only out of the zone (ShortGapAirWish).
			Steered.WishDir = Maneuver.Params.LandBeyondEndCm >= 0.f ? ShortGapAirWish(Feet, Movement->Velocity) : Arrive.WishDir;
			Steered.WishScale = Steered.WishDir.IsZero() ? 0.f : 1.f;
		}
		NavOutput = Steered;
		NavOutput.bJumpHeld = Out.bJumpHeld;
		if (bOnGround && !Out.bJumpHeld && Maneuver.Phase == EVeloxManeuverPhase::Approach && Maneuver.Params.LandBeyondEndCm >= 0.f)
		{
			// Task M7 H14: a short landing zone (Fornax Isle_In): on the ground before the take-off turn the velocity into the
			// take-off velocity, along the link within [need, cap], nothing across (B4/H13 misses: 830 cm/s along plus
			// 300-400 across off a diagonal approach landed on the 300 cm island's side edge, or past its far edge).
			const FVector2D Axis = FVector2D(LinkEnd - LinkStart).GetSafeNormal();
			const float TakeoffAt = FMath::Max(In.ProgressCm, Maneuver.Params.TakeoffMaxPastStartCm);
			const float MaxAlong = VeloxBotManeuverRules::GapTakeoffMaxSpeed(Maneuver.Params.LinkLengthCm, TakeoffAt, Maneuver.Params.LandBeyondEndCm,
				Maneuver.Params.TakeoffLandFarInsetCm, Maneuver.Params.GapAirSeconds);
			const float NeedAlong = VeloxBotManeuverRules::GapTakeoffSpeed(Maneuver.Params.LinkLengthCm, TakeoffAt, Maneuver.Params.TakeoffLandInsetCm,
				Maneuver.Params.GapAirSeconds);
			FVector2D Wish;
			if (MaxAlong > NeedAlong && ShortGapGroundWish(FVector2D(Movement->Velocity), Axis, NeedAlong, MaxAlong, Wish))
			{
				NavOutput.WishDir = Wish;
				NavOutput.WishScale = 1.f;
				bGroundArrive = true;
				++TakeoffBrakeTicks;
			}
		}
		break;
	}
	default:
		// Pads, drops, rocket jumps: straight at the target, no hop and no strafe gain (the landing is what matters).
		NavOutput = SteerToward(Target, Remaining, 0.f, false, false, false);
		NavOutput.bJumpHeld = Out.bJumpHeld;
		if (Maneuver.Type == EVeloxManeuverType::RocketJump && !bOnGround &&
			(Maneuver.Phase == EVeloxManeuverPhase::Launched || Maneuver.Phase == EVeloxManeuverPhase::Airborne))
		{
			// Task 05: the launch carries the run-up speed plus the splash's horizontal push, which overshoots a 4 m
			// platform when the wish simply points at the end (vanilla air accelerate only adds speed below the wish
			// speed). Steer the horizontal velocity toward the ballistic one that lands on the end (air braking).
			const FVeloxBallisticArrive Arrive = VeloxBotRocketJump::ArriveWish(Feet, Movement->Velocity, LinkEnd, Movement->GetGravityZ(),
				UVeloxBotCombatSettings::Get().RocketJumpArriveDeadbandCmS);
			NavOutput.WishDir = Arrive.WishDir;
			NavOutput.WishScale = Arrive.WishDir.IsZero() ? 0.f : 1.f;
		}
		else if (Maneuver.Type == EVeloxManeuverType::Drop && !bOnGround && Maneuver.Phase == EVeloxManeuverPhase::Airborne)
		{
			// Task M7 H13: a drop walked off at 800 cm/s flies ~450 cm past a 320 cm drop's edge; the plain wish at End only
			// adds speed (Orbita: the hub tower's 650 -> 330 drops landed past End, 14 of 17 missed in H12). Brake toward
			// the ballistic velocity that lands on End, as the pad and gap flights do.
			const FVeloxBallisticArrive Arrive = VeloxBotRocketJump::ArriveWish(Feet, Movement->Velocity, LinkEnd, Movement->GetGravityZ(),
				UVeloxBotSettings::Get().PadArriveDeadbandCmS);
			NavOutput.WishDir = Arrive.WishDir;
			NavOutput.WishScale = Arrive.WishDir.IsZero() ? 0.f : 1.f;
		}
		else if (Maneuver.Type == EVeloxManeuverType::JumpPad && !bOnGround && Maneuver.IsPadFlight() && Maneuver.bLaunchImpulseSeen)
		{
			// Task M7 H7: the pad's arc already ends near the link end; a plain wish at End air-accelerates a launch slower than
			// AirMaxWishSpeed past it (Sacrarium PadSouth). Correct the horizontal velocity toward the ballistic one instead.
			const FVeloxBallisticArrive Arrive = VeloxBotRocketJump::ArriveWish(Feet, Movement->Velocity, LinkEnd, Movement->GetGravityZ(),
				UVeloxBotSettings::Get().PadArriveDeadbandCmS);
			NavOutput.WishDir = Arrive.WishDir;
			NavOutput.WishScale = Arrive.WishDir.IsZero() ? 0.f : 1.f;
		}
		break;
	}
	if (!bGroundArrive)
	{
		NavOutput.WishScale *= Out.WishScale;
	}
	NavOutput.bFaceEnd = Out.bFaceEnd;
	NavOutput.FaceTarget = LinkEnd;
	NavOutput.bForcePitch = Out.bForcePitch;
	NavOutput.PitchDeg = Out.PitchDeg;
	NavOutput.bFire = Out.bFire;
	NavOutput.bWantRocketLauncher = Out.bWantRocketLauncher;
	NavOutput.bOnLink = true;
	return true;
}

void UVeloxBotPathFollowingComponent::FollowPathSegment(float DeltaTime)
{
	// No Super: the engine version would RequestPathMove its own direction and never jump (section 5).
	if (!Path.IsValid() || !NavMovementInterface.IsValid())
	{
		return;
	}
	const UVeloxMovementComponent* Movement = GetBotMovement();
	const UWorld* World = GetWorld();
	if (!Movement || !World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (IsOnLink())
	{
		if (TickManeuver(DeltaTime, Now))
		{
			return;
		}
		// Finished this tick: follow the next segment right away (unless the controller dropped the path).
		if (GetStatus() != EPathFollowingStatus::Moving || !Path.IsValid() || IsOnLink())
		{
			return;
		}
	}

	const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
	const FVector Feet = Movement->GetActorFeetLocation();
	const FVector Target = GetCurrentTargetLocation();
	const bool bFinal = MoveSegmentEndIndex >= Points.Num() - 1;
	float Corner = 0.f;
	if (!bFinal && Points.IsValidIndex(MoveSegmentStartIndex) && Points.IsValidIndex(MoveSegmentEndIndex + 1))
	{
		Corner = VeloxBotFollow::CornerAngleDeg(Points[MoveSegmentEndIndex].Location - Points[MoveSegmentStartIndex].Location,
			Points[MoveSegmentEndIndex + 1].Location - Points[MoveSegmentEndIndex].Location);
	}
	const float Remaining = static_cast<float>(FVector::Dist2D(Feet, Target));
	if (UpdateSegmentBlocked(Feet, Target, Now))
	{
		// D35: the straight line to the segment target leaves the nav mesh (the overlay pushed the bot off its
		// corridor): no hop (land, so the ground-only re-query can run; the D18 branch's shape) and ask for a new path
		// from here. Not a stuck strike, no StopMove: the current path is replaced in place.
		NavOutput = SteerToward(Target, Remaining, Corner, bFinal, false, true);
		if (AVeloxBotController* Bot = GetBotController())
		{
			Bot->RequestRepath(TEXT("segment_blocked"));
		}
		return;
	}
	if (InShortGapBrakeZone(Feet))
	{
		// Task M7 H14: a short landing zone ahead (Fornax Isle_In): no hop (one landing at the take-off carried 850-1000 cm/s
		// into the jump, past the 300 cm island) and, on the ground above the take-off speed cap, coast (ground friction).
		NavOutput = SteerToward(Target, Remaining, Corner, bFinal, false, true);
		NavOutput.bJumpHeld = false;
		const float Along = static_cast<float>(FVector2D::DotProduct(FVector2D(Movement->Velocity), GapLinkAxis));
		const double FeetPast = FVector2D::DotProduct(FVector2D(Feet) - FVector2D(GapLinkStart), GapLinkAxis);
		FVector2D Wish;
		if (Movement->IsOnGround() && FeetPast >= -UVeloxBotSettings::Get().TakeoffLeadCm &&
			ShortGapGroundWish(FVector2D(Movement->Velocity), GapLinkAxis, GapLinkNeedAlongCmS, GapLinkMaxAlongCmS, Wish))
		{
			// The last TakeoffLeadCm before the start: line the velocity up with the link already (the take-off follows).
			NavOutput.WishDir = Wish;
			NavOutput.WishScale = 1.f;
			++TakeoffBrakeTicks;
		}
		else if (Movement->IsOnGround() && Along > GapLinkMaxAlongCmS)
		{
			NavOutput.WishScale = 0.f;
			++TakeoffBrakeTicks;
		}
		return;
	}
	NavOutput = SteerToward(Target, Remaining, Corner, bFinal, true, true);
	if (NavOutput.bJumpHeld && HopIntoShortGap(Feet, Movement->Velocity))
	{
		// Task M7 H14: a hop landing near a short-landing take-off (from the corner before its segment, too).
		NavOutput = SteerToward(Target, Remaining, Corner, bFinal, false, true);
		++GapHopRefusedTicks;
		return;
	}
	if (NavOutput.bJumpHeld && HopOverrunsGapTakeoff(Feet, Movement->Velocity))
	{
		// Task M7 H11: the hop would land past the take-off of the gap link ahead: run the rest on the ground (every map).
		NavOutput = SteerToward(Target, Remaining, Corner, bFinal, false, true);
		++GapHopRefusedTicks;
		return;
	}
	// D18 headroom, then (ADR 0008 3.6.2, hazard maps only) the hop landing guard.
	if (NavOutput.bJumpHeld && (!HasHopHeadroom(Now) || !HasHopLanding()))
	{
		// D18: a low ceiling (the L_ItemTest ramp underside) is inside the jump arc, or (3.6.2) the hop would land off the
		// mesh: walk this tick, steering as on the ground (no hop, so the wish is the ground wish, not an air-strafe angle).
		NavOutput = SteerToward(Target, Remaining, Corner, bFinal, false, true);
		if (!bHopHeadroomClear)
		{
			++HopHeadroomBlockedTicks;
		}
	}
}
