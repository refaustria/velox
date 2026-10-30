// Velox - bot path follower (ADR 0005 sections 4 and 5).

#include "Bots/VeloxBotPathFollowingComponent.h"

#include "AI/Navigation/NavigationTypes.h"
#include "Bots/VeloxBotCombat.h"
#include "Bots/VeloxBotController.h"
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

bool UVeloxBotPathFollowingComponent::UpdateSegmentBlocked(const FVector& Feet, const FVector& Target, double Now)
{
	if (Now < NextSegmentBlockCheckTime)
	{
		return bSegmentBlocked;
	}
	const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
	NextSegmentBlockCheckTime = Now + Settings.SegmentBlockCheckSeconds;
	UWorld* World = GetWorld();
	AVeloxBotController* Bot = GetBotController();
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Bot || !NavSys)
	{
		return bSegmentBlocked;
	}
	// D35: the feet onto the mesh with the agent's default query extent; no projection (a hop apex) skips the check.
	const ANavigationData* NavData = NavSys->GetNavDataForProps(Bot->GetNavAgentPropertiesRef(), Feet);
	FNavLocation Projected;
	if (!NavData || !NavSys->ProjectPointToNavigation(Feet, Projected, INVALID_NAVEXTENT, NavData))
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
	if (!FNavMeshNodeFlags(StartPoint.Flags).IsNavLink())
	{
		return;
	}
	// Recast inserts the link ends as path points: the start point matches the link's authored start (or, BothWays, its end).
	const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld());
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
	Maneuver.Begin(PendingParams, LinkStart, LinkEnd, Now);
	bLinkPending = false;
	UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotLink bot=%s begin type=%s link=%s from=%s to=%s"), *GetNameSafe(GetOwner()),
		VeloxBotFollowPrivate::ManeuverName(PendingParams.Type), *GetNameSafe(CurrentLink.Get()), *LinkStart.ToCompactString(), *LinkEnd.ToCompactString());
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
	const FVeloxManeuverOutput Out = Maneuver.Tick(In);

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
		}
		NavOutput = Steered;
		NavOutput.bJumpHeld = Out.bJumpHeld;
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
		break;
	}
	NavOutput.WishScale *= Out.WishScale;
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
	NavOutput = SteerToward(Target, Remaining, Corner, bFinal, true, true);
	if (NavOutput.bJumpHeld && !HasHopHeadroom(Now))
	{
		// D18: a low ceiling (the L_ItemTest ramp underside) is inside the jump arc: walk this tick, steering as on the
		// ground (no hop, so the wish is the ground wish, not an air-strafe angle).
		NavOutput = SteerToward(Target, Remaining, Corner, bFinal, false, true);
		++HopHeadroomBlockedTicks;
	}
}
