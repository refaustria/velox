// Velox - bot path follower (ADR 0005 sections 4 and 5). The engine's UPathFollowingComponent keeps the path
// bookkeeping (MoveTo, segments, reach tests, OnPathFinished); this subclass replaces the movement half:
// FollowPathSegment never calls RequestPathMove / RequestDirectMove, it runs VeloxBotSteering::Steer (strafe-jumping,
// bunny hopping) or, on a nav-link segment, the FVeloxBotManeuver of the matched AVeloxNavLink, and writes the result
// into the owning AVeloxBotController's pending input. It never touches the movement component.
//
// Tick order: the component does not tick by itself. AVeloxBotController::Tick calls StepFollow in its Navigate phase,
// so path update -> steering -> aim -> ApplyInput happen in one controller tick, and the engine's controller -> pawn
// movement prerequisite (AController::AddPawnTickDependency) makes the pawn consume that input in the same frame.

#pragma once

#include "Bots/VeloxBotManeuver.h"
#include "Navigation/PathFollowingComponent.h"

#include "VeloxBotPathFollowingComponent.generated.h"

class AVeloxBotController;
class AVeloxCharacter;
class AVeloxNavLink;
class UVeloxMovementComponent;
enum class EVeloxNavLinkType : uint8;

/** What the follower asks the controller to do this tick (merged into FVeloxBotInput by the controller). */
struct FVeloxBotNavOutput
{
	/** Unit XY wish direction, or zero (no move / arrived). */
	FVector2D WishDir = FVector2D::ZeroVector;
	float WishScale = 0.f;
	bool bJumpHeld = false;
	/** Horizontal direction toward the current path target (idle aim); zero without a path. */
	FVector2D PathDir = FVector2D::ZeroVector;
	/** Maneuver view requests (rocket jump: face End, force pitch, fire, rocket launcher). */
	bool bFaceEnd = false;
	FVector FaceTarget = FVector::ZeroVector;
	bool bForcePitch = false;
	float PitchDeg = 0.f;
	bool bFire = false;
	bool bWantRocketLauncher = false;
	/** A link traversal is running (or waiting for the ground to begin). */
	bool bOnLink = false;
};

/** A finished (or aborted) link traversal, reported to the controller (log line, delegate, re-query). */
struct FVeloxBotLinkReport
{
	EVeloxManeuverType Type = EVeloxManeuverType::Drop;
	EVeloxManeuverResult Result = EVeloxManeuverResult::None;
	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	double Seconds = 0.0;
	FString LinkName;
};

namespace VeloxBotFollow
{
	/** The maneuver kind of a link type (EVeloxManeuverType mirrors EVeloxNavLinkType in the same order, task 02). */
	VELOX_API EVeloxManeuverType ManeuverTypeFor(EVeloxNavLinkType LinkType);

	/** Turn at a path corner, degrees in [0, 180]: angle between the XY directions In and Out (0 if either is zero). */
	VELOX_API float CornerAngleDeg(const FVector& In, const FVector& Out);
}

UCLASS(ClassGroup = (Velox), Config = Game)
class VELOX_API UVeloxBotPathFollowingComponent : public UPathFollowingComponent
{
	GENERATED_BODY()

public:
	UVeloxBotPathFollowingComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** One follow step (the engine's TickComponent body: UpdatePathSegment, then FollowPathSegment). Navigate phase. */
	void StepFollow(float DeltaTime);

	/** This tick's steering result (zero wish when idle). Valid after StepFollow. */
	const FVeloxBotNavOutput& GetNavOutput() const { return NavOutput; }

	/** A traversal is active or pending (the controller defers re-queries and stuck checks of a few phases). */
	bool IsOnLink() const { return bLinkPending || Maneuver.IsActive(); }
	const FVeloxBotManeuver& GetManeuver() const { return Maneuver; }
	/** Link of the current traversal (null when none). */
	const AVeloxNavLink* GetCurrentLink() const { return CurrentLink.Get(); }

	/**
	 * The segment into a link start ends this close to it (cm, 2-D): the traversal begins before the pawn reaches the
	 * point (a pad trigger launches the pawn before it can pass the pad centre, which is the engine's reach test).
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "cm"))
	float LinkEntryRadiusCm = 100.f;

	/** An impulse (pad launch, knockback) this close to the next link start begins the traversal at once (cm, 2-D). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "cm"))
	float LinkEntryImpulseRadiusCm = 250.f;

	/** A partial path is finished at its last point within the acceptance radius plus this (2-D), and this vertically (cm). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "cm"))
	float PartialEndSlackCm = 40.f;

	//~UPathFollowingComponent interface
	using Super::OnPathFinished;
	/** Ends any traversal (aborted), then Super (Reset, observers -> AAIController::OnMoveCompleted). */
	virtual void OnPathFinished(const FPathFollowingResult& Result) override;
	//~End of UPathFollowingComponent interface

protected:
	//~UPathFollowingComponent interface
	/** Super, then detects a nav-link segment (FNavMeshNodeFlags::IsNavLink on the start point -> FindLinkByStart). */
	virtual void SetMoveSegment(int32 SegmentStartIndex) override;
	/** Engine bookkeeping, except while a link traversal runs: the maneuver decides when the link segment ends. */
	virtual void UpdatePathSegment() override;
	/** Does not call Super (no RequestPathMove): steering or the maneuver into NavOutput. */
	virtual void FollowPathSegment(float DeltaTime) override;
	virtual void Reset() override;
	//~End of UPathFollowingComponent interface

private:
	AVeloxBotController* GetBotController() const;
	AVeloxCharacter* GetBotCharacter() const;
	UVeloxMovementComponent* GetBotMovement() const;

	void ClearLink();
	/** Starts the pending traversal (pawn on the ground, or launched by an impulse). */
	void BeginManeuver(double Now);
	void FinishManeuver(EVeloxManeuverResult Result, double Now);
	/** Steering toward Target with the bot's preset (shared by plain segments and maneuver wishes). */
	FVeloxBotNavOutput SteerToward(const FVector& Target, float SegmentRemainingCm, float NextCornerDeg, bool bFinalSegment, bool bAllowHop, bool bAllowStrafe) const;
	/** One maneuver tick (or the pending wait) -> NavOutput. Returns false when the traversal finished this tick. */
	bool TickManeuver(float DeltaTime, double Now);

	FVeloxBotNavOutput NavOutput;

	FVeloxBotManeuver Maneuver;
	TWeakObjectPtr<AVeloxNavLink> CurrentLink;
	/** Link segment start / end (path points: on the mesh, already reversed for BothWays links). */
	FVector LinkStart = FVector::ZeroVector;
	FVector LinkEnd = FVector::ZeroVector;
	/** Direction of the segment that led into the link (JumpFast back-up), XY unit. */
	FVector2D LinkIncomingDir = FVector2D::ZeroVector;
	FVeloxManeuverParams PendingParams;
	/** The link segment started while the pawn was airborne (a bunny hop): wait for the ground or an impulse. */
	bool bLinkPending = false;
	double LinkSegmentStartTime = 0.0;
	/** An impulse arrived this step (the movement component's knockback grace rose since the previous step). */
	bool ImpulseThisStep() const;
	/** The landing point is on the nav surface of LinkEnd (a nav raycast between them stays on the mesh). */
	bool IsOnEndSurface(const FVector& Feet) const;

	/** Knockback grace of the previous step (an impulse = the grace rising from 0). */
	float LastKnockbackGrace = 0.f;
};
