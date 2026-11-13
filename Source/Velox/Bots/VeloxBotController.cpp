// Velox - bot controller (ADR 0005 sections 1, 2, 5, 12).

#include "Bots/VeloxBotController.h"

#include "Bots/VeloxBotCost.h"

#include "Bots/VeloxBotDifficulty.h"
#include "Bots/VeloxBotItemRouting.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotSettings.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxNavFilters.h"
#include "Bots/VeloxNavLink.h"
#include "Character/VeloxCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHazardSubsystem.h"
#include "Combat/VeloxHazardVolume.h"
#include "Components/BoxComponent.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "Movement/VeloxMovementComponent.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxBotController)

namespace VeloxBotControllerPrivate
{
	/**
	 * ADR 0005 D44: the nav point nearest to Point (2-D, within HorizontalExtentCm) inside the reachable window
	 * [FeetZ - DownCm, FeetZ + StepHeight]. Detour ranks projection candidates by 2-D distance and uses the vertical extent
	 * only as a filter, so a surface overhead inside a symmetric box shadows every acceptable point further away; the window
	 * is therefore a shifted box whose top is the step height. The explicit Z guard covers the query-extent widening and
	 * Detour's epsilon.
	 */
	bool ProjectReachable(const UNavigationSystemV1& NavSys, const ANavigationData& NavData, const FSharedConstNavQueryFilter& Filter, const FVector& Point, double FeetZ,
		float StepHeight, float HorizontalExtentCm, float DownCm, FNavLocation& Out)
	{
		const float HalfZ = (StepHeight + DownCm) * 0.5f;
		const FVector Centre(Point.X, Point.Y, FeetZ + StepHeight - HalfZ);
		return NavSys.ProjectPointToNavigation(Centre, Out, FVector(HorizontalExtentCm, HorizontalExtentCm, HalfZ), &NavData, Filter) &&
			Out.Location.Z <= FeetZ + StepHeight + KINDA_SMALL_NUMBER;
	}

	TAutoConsoleVariable<int32> CVarBotFreeze(TEXT("velox.bots.Freeze"), 0,
		TEXT("1 = every bot releases all keys and stands (ADR 0005 section 14). Server."), ECVF_Default);

	TAutoConsoleVariable<int32> CVarBotDebug(TEXT("velox.bots.Debug"), 0,
		TEXT("1 = one `VeloxBot name= goal= ...` line per bot per second (ADR 0005 section 14). Server."), ECVF_Default);

	/** Pitch limit of the bot view (degrees), the player's camera manager range. */
	constexpr float MaxViewPitchDeg = 89.f;

	const UVeloxWeaponDefinition* CurrentWeapon(const AVeloxPlayerState* PS)
	{
		const UVeloxWeaponInventoryComponent* Inventory = PS ? PS->GetWeaponInventory() : nullptr;
		return Inventory ? Inventory->GetCurrentDefinition() : nullptr;
	}

	/** Inventory index of the owned weapon with WeaponTag, INDEX_NONE if not owned. */
	int32 OwnedWeaponIndex(const AVeloxPlayerState* PS, const FGameplayTag& WeaponTag)
	{
		const UVeloxWeaponInventoryComponent* Inventory = PS ? PS->GetWeaponInventory() : nullptr;
		if (!Inventory)
		{
			return INDEX_NONE;
		}
		for (int32 i = 0; i < Inventory->NumWeapons(); ++i)
		{
			const UVeloxWeaponDefinition* Definition = Inventory->GetDefinition(i);
			if (Definition && Definition->WeaponTag == WeaponTag && Inventory->OwnsIndex(i))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	const TCHAR* FilterLabel(const TSubclassOf<UNavigationQueryFilter>& Filter)
	{
		if (Filter == UVeloxNavFilter_Full::StaticClass())
		{
			return TEXT("Full");
		}
		if (Filter == UVeloxNavFilter_Hop::StaticClass())
		{
			return TEXT("Hop");
		}
		return TEXT("Walk");
	}

	const TCHAR* ManeuverLabel(EVeloxManeuverType Type)
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

// ---------------------------------------------------------------------------------------------------------- rules

namespace VeloxBotRules
{
	int32 BotSeed(int32 MasterSeed, int32 BotIndex, int32 LifeCount)
	{
		return static_cast<int32>(HashCombine(HashCombine(GetTypeHash(MasterSeed), GetTypeHash(BotIndex)), GetTypeHash(LifeCount)));
	}

	FVector2D MoveKeys(const FVector2D& WishDirWorld, float WishScale, float ViewYawDeg, bool bSnapKeys, float SnapThreshold)
	{
		const FVector2D Wish = WishDirWorld.GetSafeNormal();
		if (Wish.IsZero() || WishScale <= 0.f)
		{
			return FVector2D::ZeroVector;
		}
		const double Yaw = FMath::DegreesToRadians(static_cast<double>(ViewYawDeg));
		const FVector2D Forward(FMath::Cos(Yaw), FMath::Sin(Yaw));
		const FVector2D Right(-FMath::Sin(Yaw), FMath::Cos(Yaw));
		double F = FVector2D::DotProduct(Wish, Forward);
		double R = FVector2D::DotProduct(Wish, Right);
		if (bSnapKeys)
		{
			if (FMath::Abs(F) < FMath::Abs(R))
			{
				F = FMath::Abs(F) < SnapThreshold ? 0.0 : F;
			}
			else
			{
				R = FMath::Abs(R) < SnapThreshold ? 0.0 : R;
			}
		}
		return FVector2D(F, R) * WishScale;
	}

	FRotator TurnToward(const FRotator& Current, const FRotator& Desired, float MaxDegPerSec, float Dt)
	{
		const float MaxStep = FMath::Max(0.f, MaxDegPerSec * Dt);
		const float DeltaYaw = FMath::Clamp(FRotator::NormalizeAxis(Desired.Yaw - Current.Yaw), -MaxStep, MaxStep);
		const float DeltaPitch = FMath::Clamp(FRotator::NormalizeAxis(Desired.Pitch - Current.Pitch), -MaxStep, MaxStep);
		FRotator Result(FRotator::NormalizeAxis(Current.Pitch + DeltaPitch), FRotator::NormalizeAxis(Current.Yaw + DeltaYaw), 0.f);
		Result.Pitch = FMath::Clamp(Result.Pitch, -VeloxBotControllerPrivate::MaxViewPitchDeg, VeloxBotControllerPrivate::MaxViewPitchDeg);
		return Result;
	}

	bool UpdateStuck(double& SlowSince, bool bTracking, float Speed2D, float SpeedThreshold, double Now, float StuckSeconds)
	{
		if (!bTracking || Speed2D >= SpeedThreshold)
		{
			SlowSince = -1.0;
			return false;
		}
		if (SlowSince < 0.0)
		{
			SlowSince = Now;
			return false;
		}
		if (Now - SlowSince >= StuckSeconds)
		{
			SlowSince = Now;
			return true;
		}
		return false;
	}

	bool UpdateStuckProgress(double& Since, float& StartDist, FVector2D& WindowTarget, float& PausedSeconds, bool bTracking, bool bPaused,
		const FVector2D& Target, float Dist2D, float ProgressCm, double Now, float Dt, float StuckSeconds, float MaxPauseSeconds,
		bool bOnGround, bool bFlight)
	{
		if (!bTracking)
		{
			Since = -1.0;
			PausedSeconds = 0.f;
			return false;
		}
		// A target that moved by more than this (cm, 2-D) is a new one (numeric equality of path points, not a tunable).
		constexpr double TargetToleranceCm = 1.0;
		if (Since < 0.0 || FVector2D::DistSquared(Target, WindowTarget) > FMath::Square(TargetToleranceCm))
		{
			Since = Now;
			StartDist = Dist2D;
			WindowTarget = Target;
			PausedSeconds = 0.f;
			return false;
		}
		// Task 06b: a flight (pad, drop, fall, knockback) is not the follower's time: pause without the D27 cap.
		if (bFlight)
		{
			Since = FMath::Min(Since + static_cast<double>(FMath::Max(0.f, Dt)), Now);
			StartDist = Dist2D;
			return false;
		}
		// D27: the pause is bounded per window; past the cap the window runs with the overlay active.
		if (bPaused && PausedSeconds < MaxPauseSeconds)
		{
			const float Step = FMath::Max(0.f, Dt);
			Since = FMath::Min(Since + static_cast<double>(Step), Now);
			StartDist = Dist2D;
			PausedSeconds += Step;
			return false;
		}
		const bool bProgress = Dist2D <= StartDist - ProgressCm;
		// Task 06b: the strike waits for the ground (a hop lands within its airtime; a flight is paused above).
		const bool bExpired = !bProgress && bOnGround && Now - Since >= StuckSeconds;
		if (bProgress || bExpired)
		{
			Since = Now;
			StartDist = Dist2D;
			PausedSeconds = 0.f;
		}
		return bExpired;
	}

	bool IsFightWatched(EVeloxBotGoal Goal, bool bMoveActive, bool bEnemyVisible, bool bOnGround, bool bOnLink)
	{
		return Goal == EVeloxBotGoal::Fight && !bMoveActive && bEnemyVisible && bOnGround && !bOnLink;
	}

	bool IsFlight(bool bOnGround, double AirborneSince, double Now, float HopAirSeconds)
	{
		return !bOnGround && AirborneSince >= 0.0 && Now - AirborneSince > HopAirSeconds;
	}

	float HopAirSeconds(float JumpVelocity, float GravityZ, float SlackSeconds)
	{
		const float Gravity = FMath::Abs(GravityZ);
		return (Gravity > UE_KINDA_SMALL_NUMBER ? 2.f * JumpVelocity / Gravity : 0.f) + SlackSeconds;
	}

	bool UpdateFightStandoff(FVeloxStandoffWindow& Window, bool bWatched, const FVector2D& Pos, double Now, float RadiusCm, float Seconds, float SampleSeconds)
	{
		if (!bWatched)
		{
			Window.Reset();
			return false;
		}
		// Sample times tolerate float accumulation of the caller's world time.
		constexpr double TimeEpsilon = 1e-6;
		if (Window.Times.Num() == 0 || Now - Window.Times.Last() + TimeEpsilon >= SampleSeconds)
		{
			Window.Times.Add(Now);
			Window.Positions.Add(Pos);
		}
		// Keep the latest sample at or before the window start and everything after it.
		const double WindowStart = Now - Seconds;
		int32 Drop = 0;
		while (Drop + 1 < Window.Times.Num() && Window.Times[Drop + 1] <= WindowStart + TimeEpsilon)
		{
			++Drop;
		}
		if (Drop > 0)
		{
			Window.Times.RemoveAt(0, Drop);
			Window.Positions.RemoveAt(0, Drop);
		}
		if (Window.Times[0] > WindowStart + TimeEpsilon)
		{
			return false;
		}
		const double RadiusSq = FMath::Square(static_cast<double>(RadiusCm));
		for (const FVector2D& P : Window.Positions)
		{
			if (FVector2D::DistSquared(P, Pos) > RadiusSq)
			{
				return false;
			}
		}
		Window.Reset();
		return true;
	}

	bool IsAtGoal(const FVector& Feet, const FVector& Target, float Radius2D, float MaxDzCm)
	{
		return FVector::DistSquared2D(Feet, Target) <= FMath::Square(static_cast<double>(Radius2D)) && FMath::Abs(Feet.Z - Target.Z) <= MaxDzCm;
	}

	bool WallTangent(const FVector& ImpactNormal, FVector2D& OutTangent)
	{
		const FVector2D N2(ImpactNormal.X, ImpactNormal.Y);
		if (N2.Size() < 0.5)
		{
			return false;
		}
		OutTangent = FVector2D(-N2.Y, N2.X).GetSafeNormal();
		return true;
	}

	bool IsForeignFlight(bool bJumpHeldAtTakeoff, float KnockbackGraceSeconds, bool bIsFlight)
	{
		return !bJumpHeldAtTakeoff || KnockbackGraceSeconds > 0.f || bIsFlight;
	}

	float FlightSecondsToLevel(float VelocityZ, float HeightAboveLevelCm, float Gravity)
	{
		if (Gravity <= UE_KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		// z(t) = Vz t - g t^2 / 2 = -h  ->  t = (Vz + sqrt(Vz^2 + 2 g h)) / g (the later, descending root).
		const float Disc = VelocityZ * VelocityZ + 2.f * Gravity * HeightAboveLevelCm;
		if (Disc < 0.f)
		{
			return 0.f;
		}
		return FMath::Max(0.f, (VelocityZ + FMath::Sqrt(Disc)) / Gravity);
	}

	FVector2D AirRecoveryWish(const FVector2D& Landing, const FVector2D& Target, float OnCourseCm)
	{
		const FVector2D Delta = Target - Landing;
		if (Delta.SizeSquared() <= FMath::Square(static_cast<double>(OnCourseCm)))
		{
			return FVector2D::ZeroVector;
		}
		return Delta.GetSafeNormal();
	}

	FVector2D HazardBoxExit(const FVector2D& Feet, const FVector2D& Center, const FVector2D& AxisX, float HalfX, float HalfY, float OutCm)
	{
		const FVector2D X = AxisX.GetSafeNormal().IsNearlyZero() ? FVector2D(1.0, 0.0) : AxisX.GetSafeNormal();
		const FVector2D Y(-X.Y, X.X);
		const FVector2D Rel = Feet - Center;
		const double LX = FVector2D::DotProduct(Rel, X);
		const double LY = FVector2D::DotProduct(Rel, Y);
		// Distance to each side from inside; the smallest names the side to leave through.
		const double ToPosX = HalfX - LX;
		const double ToNegX = HalfX + LX;
		const double ToPosY = HalfY - LY;
		const double ToNegY = HalfY + LY;
		const double Best = FMath::Min(FMath::Min(ToPosX, ToNegX), FMath::Min(ToPosY, ToNegY));
		double OutX = LX;
		double OutY = LY;
		if (Best == ToPosX)
		{
			OutX = HalfX + OutCm;
		}
		else if (Best == ToNegX)
		{
			OutX = -HalfX - OutCm;
		}
		else if (Best == ToPosY)
		{
			OutY = HalfY + OutCm;
		}
		else
		{
			OutY = -HalfY - OutCm;
		}
		return Center + X * OutX + Y * OutY;
	}

	bool UpdateOffMesh(double& OffMeshSince, bool bOnMesh, bool bRecoveryMoveActive, double RecoveryMoveUntil, double Now, float GraceSeconds)
	{
		if (bOnMesh)
		{
			OffMeshSince = -1.0;
			return false;
		}
		if (OffMeshSince < 0.0)
		{
			OffMeshSince = Now;
		}
		if (bRecoveryMoveActive && Now < RecoveryMoveUntil)
		{
			return false;
		}
		return Now - OffMeshSince >= GraceSeconds;
	}

	FString NextBotName(const TArray<FString>& Names, const TArray<FString>& Taken, const FString& Requested)
	{
		auto IsTaken = [&Taken](const FString& Name)
		{
			return Taken.ContainsByPredicate([&Name](const FString& Other) { return Other.Equals(Name, ESearchCase::IgnoreCase); });
		};
		const FString Base = !Requested.IsEmpty() ? Requested : FString();
		if (!Base.IsEmpty())
		{
			if (!IsTaken(Base))
			{
				return Base;
			}
		}
		else
		{
			for (const FString& Name : Names)
			{
				if (!Name.IsEmpty() && !IsTaken(Name))
				{
					return Name;
				}
			}
		}
		// Every name (or the requested one) is taken: the first name with the lowest free suffix.
		const FString Stem = !Base.IsEmpty() ? Base : (Names.Num() > 0 && !Names[0].IsEmpty() ? Names[0] : FString(TEXT("Bot")));
		if (Base.IsEmpty() && Names.Num() == 0 && !IsTaken(Stem))
		{
			return Stem;
		}
		for (int32 Suffix = 2;; ++Suffix)
		{
			const FString Candidate = FString::Printf(TEXT("%s%d"), *Stem, Suffix);
			if (!IsTaken(Candidate))
			{
				return Candidate;
			}
		}
	}

	int32 UrlBotCount(int32 Requested, int32 MaxActive, bool bFill)
	{
		if (Requested <= 0)
		{
			return 0;
		}
		if (MaxActive <= 0)
		{
			return Requested;
		}
		return FMath::Clamp(Requested, 0, FMath::Max(0, bFill ? MaxActive : MaxActive - 1));
	}
}

// ------------------------------------------------------------------------------------------------------ lifecycle

AVeloxBotController::AVeloxBotController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVeloxBotPathFollowingComponent>(TEXT("PathFollowingComponent")))
{
	// Section 1: a PlayerState (ASC, inventory, scoreboard, colour); the control rotation is the bot's own.
	bWantsPlayerState = true;
	bSetControlRotationFromPawnOrientation = false;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

UVeloxBotPathFollowingComponent* AVeloxBotController::GetBotPathFollowing() const
{
	return Cast<UVeloxBotPathFollowingComponent>(GetPathFollowingComponent());
}

AVeloxCharacter* AVeloxBotController::GetBotCharacter() const
{
	return Cast<AVeloxCharacter>(GetPawn());
}

double AVeloxBotController::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

FString AVeloxBotController::GetBotName() const
{
	return PlayerState ? PlayerState->GetPlayerName() : FString(TEXT("?"));
}

void AVeloxBotController::SetDifficulty(const UVeloxBotDifficulty* InDifficulty)
{
	Difficulty = InDifficulty;
	Params = Difficulty ? Difficulty->BuildParams() : FVeloxBotDifficultyParams();
}

bool AVeloxBotController::IsFrozen() const
{
	return bFrozen || VeloxBotControllerPrivate::CVarBotFreeze.GetValueOnGameThread() != 0;
}

void AVeloxBotController::BeginPlay()
{
	Super::BeginPlay();
	if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld()))
	{
		Bots->RegisterBot(this);
	}
	if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(GetWorld()))
	{
		PickupTakenHandle = Registry->OnPickupTaken.AddUObject(this, &ThisClass::OnPickupTaken);
	}
}

void AVeloxBotController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(GetWorld()))
	{
		Registry->OnPickupTaken.Remove(PickupTakenHandle);
	}
	if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld()))
	{
		Bots->UnregisterBot(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AVeloxBotController::ResetLifeState()
{
	// A forced goal (velox.bots.Goal, tests) survives the respawn; the brain's own goal, path and memory do not.
	const FVeloxBotGoal Forced = State.Goal;
	const bool bKeepForced = State.bForcedGoal;
	State = FVeloxBotState();
	if (bKeepForced)
	{
		State.Goal = Forced;
		State.bForcedGoal = true;
	}
	State.PossessTime = Now();
}

void AVeloxBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (!GetPawn())
	{
		return;
	}
	++LifeCount;
	if (!Difficulty)
	{
		Difficulty = UVeloxBotSettings::Get().DefaultDifficulty.LoadSynchronous();
	}
	Params = Difficulty ? Difficulty->BuildParams() : FVeloxBotDifficultyParams();

	const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld());
	const int32 MasterSeed = Bots ? Bots->GetMasterSeed() : 0;
	const int32 Seed = VeloxBotRules::BotSeed(MasterSeed, BotIndex, LifeCount);
	Stream.Initialize(Seed);
	ResetLifeState();

	// Aim starts at the spawn yaw, level.
	State.View = FRotator(0.f, GetPawn()->GetActorRotation().Yaw, 0.f);
	SetControlRotation(State.View);
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBot possess name=%s index=%d life=%d seed=%d difficulty=%s pawn=%s at=%s"), *GetBotName(), BotIndex, LifeCount, Seed,
		*GetNameSafe(Difficulty), *GetNameSafe(GetPawn()), *GetPawn()->GetActorLocation().ToCompactString());
}

void AVeloxBotController::ReleaseAllInput()
{
	AVeloxCharacter* Character = GetBotCharacter();
	if (Character && State.bJumpApplied)
	{
		Character->SetJumpInput(false);
	}
	if (Character && State.bCrouchApplied)
	{
		Character->UnCrouch();
	}
	const AVeloxPlayerState* PS = GetPlayerState<AVeloxPlayerState>();
	UVeloxAbilitySystemComponent* ASC = PS ? PS->GetVeloxAbilitySystemComponent() : nullptr;
	if (ASC && State.bFireApplied)
	{
		ASC->AbilityInputTagReleased(VeloxGameplayTags::InputTag_Fire);
	}
	State.bJumpApplied = false;
	State.bCrouchApplied = false;
	State.bFireApplied = false;
}

void AVeloxBotController::OnUnPossess()
{
	// Death (B8: next-tick UnPossess) or kick: stop the path and drop every key while the pawn is still ours.
	ReleaseAllInput();
	if (UPathFollowingComponent* Follow = GetPathFollowingComponent(); Follow && Follow->GetStatus() != EPathFollowingStatus::Idle)
	{
		StopMovement();
	}
	State.bMoveActive = false;
	Super::OnUnPossess();
}

// ------------------------------------------------------------------------------------------------------ goals

void AVeloxBotController::SetGoal(const FVeloxBotGoal& NewGoal)
{
	State.Goal = NewGoal;
	State.Goal.SetTime = Now();
	State.bGoalArrived = false;
	State.FailedMoves = 0;
	State.StuckStrikes = 0;
	State.NextMoveTime = 0.0;
	State.FightStandoff.Reset();
}

void AVeloxBotController::SetForcedItemGoal(AVeloxPickup* Item)
{
	FVeloxBotGoal Goal;
	Goal.Type = Item ? EVeloxBotGoal::Item : EVeloxBotGoal::None;
	Goal.Item = Item;
	SetGoal(Goal);
	State.bForcedGoal = Item != nullptr;
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotGoal bot=%s forced=item item=%s at=%s"), *GetBotName(), *GetNameSafe(Item), Item ? *Item->GetActorLocation().ToCompactString() : TEXT("-"));
}

void AVeloxBotController::SetForcedPointGoal(const FVector& Point)
{
	FVeloxBotGoal Goal;
	Goal.Type = EVeloxBotGoal::Roam;
	Goal.Point = Point;
	SetGoal(Goal);
	State.bForcedGoal = true;
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotGoal bot=%s forced=point at=%s"), *GetBotName(), *Point.ToCompactString());
}

void AVeloxBotController::ClearForcedGoal()
{
	State.bForcedGoal = false;
	SetGoal(FVeloxBotGoal());
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotGoal bot=%s forced=none"), *GetBotName());
}

void AVeloxBotController::OnPickupTaken(AVeloxPickup* Pickup, APlayerState* TakenBy)
{
	if (State.Goal.Type != EVeloxBotGoal::Item || State.Goal.Item.Get() != Pickup)
	{
		return;
	}
	const bool bByMe = TakenBy && TakenBy == PlayerState;
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotGoal bot=%s item=%s taken_by=%s %s after=%.2f s"), *GetBotName(), *GetNameSafe(Pickup), TakenBy ? *TakenBy->GetPlayerName() : TEXT("-"),
		bByMe ? TEXT("reached") : TEXT("lost"), Now() - State.Goal.SetTime);
	if (bByMe || !State.bForcedGoal)
	{
		// Reached (or, for the brain's goals, gone): choose again. A forced goal taken by someone else is kept (wait there).
		State.bForcedGoal = false;
		SetGoal(FVeloxBotGoal());
	}
}

bool AVeloxBotController::GetGoalTarget(FVector& OutTarget, float& OutAcceptance) const
{
	switch (State.Goal.Type)
	{
	case EVeloxBotGoal::Item:
		if (const AVeloxPickup* Item = State.Goal.Item.Get())
		{
			OutTarget = Item->GetActorLocation();
			OutAcceptance = ItemAcceptanceRadiusCm;
			return true;
		}
		return false;
	case EVeloxBotGoal::Fight:
		// D12 (task 05): no path while the enemy is visible (the combat overlay, bounded by the leash, is the whole wish);
		// remembered but unseen -> chase its last seen location, re-queried only when that moved ChaseMoveToleranceCm.
		if (const APawn* Enemy = State.Goal.Enemy.Get())
		{
			const APlayerState* EnemyPS = Enemy->GetPlayerState();
			const FVeloxBotEnemyInfo* Info = EnemyPS ? Combat.FindEnemy(EnemyPS->GetPlayerId()) : nullptr;
			if (!Info || Info->bVisible)
			{
				return false;
			}
			const bool bKeep = State.bMoveActive && FVector::Dist(Info->LastSeenLocation, State.MoveTarget) <= ChaseMoveToleranceCm;
			OutTarget = bKeep ? State.MoveTarget : Info->LastSeenLocation;
			OutAcceptance = PointAcceptanceRadiusCm;
			return true;
		}
		return false;
	case EVeloxBotGoal::Roam:
		OutTarget = State.Goal.Point;
		OutAcceptance = PointAcceptanceRadiusCm;
		return true;
	default:
		return false;
	}
}

bool AVeloxBotController::StartRoam()
{
	const AVeloxCharacter* Character = GetBotCharacter();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld());
	ANavigationData* NavData = Bots ? Bots->GetNavData() : nullptr;
	if (!Character || !NavSys || !NavData)
	{
		return false;
	}
	// Candidates from the bot's own stream (deterministic per seed), projected onto the mesh; the path may be partial.
	const float Radius = UVeloxBotSettings::Get().RoamRadiusCm;
	const FVector Origin = Character->GetActorLocation();
	const FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(*NavData, this, GetNavFilter());
	for (int32 Attempt = 0; Attempt < RoamPointAttempts; ++Attempt)
	{
		const float Angle = Stream.FRandRange(0.f, 2.f * UE_PI);
		const float Dist = Radius * FMath::Sqrt(Stream.FRand());
		const FVector Candidate = Origin + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.0);
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Candidate, Projected, FVector(Radius * 0.1f, Radius * 0.1f, RoamProjectHalfHeightCm), NavData, Filter))
		{
			FVeloxBotGoal Goal;
			Goal.Type = EVeloxBotGoal::Roam;
			Goal.Point = Projected.Location;
			SetGoal(Goal);
			UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotGoal bot=%s roam to=%s attempt=%d"), *GetBotName(), *Goal.Point.ToCompactString(), Attempt);
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------------------------------------------ phases

void AVeloxBotController::Perceive(float DeltaSeconds)
{
	// Section 6 (task 05): FOV + LOS perception at the preset's interval, damage hearing, enemy memory, projectile threats.
	VeloxBotCombatGlue::Perceive(*this, Combat, Stream, DeltaSeconds);
}

void AVeloxBotController::UpdateGoal(float /*DeltaSeconds*/)
{
	// Task 06: item routing (ADR 0005 sections 2, 7) through UVeloxBotItemRouter; roam when it finds nothing.
	const double T = Now();
	UVeloxBotItemRouter* Router = UVeloxBotItemRouter::Get(GetWorld());
	if (!Router)
	{
		if (State.Goal.Type == EVeloxBotGoal::None && T >= State.NextRoamTime)
		{
			State.NextRoamTime = T + RepathIntervalSeconds;
			StartRoam();
		}
		return;
	}
	FVeloxBotGoal NewGoal;
	switch (Router->UpdateBot(*this, Params, State.Goal, State.bGoalArrived, Stream, NewGoal))
	{
	case EVeloxRouteUpdate::NewGoal:
		SetGoal(NewGoal);
		break;
	case EVeloxRouteUpdate::Roam:
		// The current roam point stays until reached; a lost goal (None) or an item / camp goal gets a new point.
		if (State.Goal.Type != EVeloxBotGoal::Roam || State.Goal.Item.IsValid())
		{
			if (!StartRoam())
			{
				SetGoal(FVeloxBotGoal());
			}
		}
		break;
	default:
		break;
	}
}

void AVeloxBotController::UpdateCombat(float DeltaSeconds)
{
	// Section 10 (task 05): strafe / dodge / range-keeping overlay on State.Pending (never on a link segment).
	VeloxBotCombatGlue::Overlay(*this, Combat, Stream, State.Nav, State.Pending, DeltaSeconds);
}

void AVeloxBotController::Decide(float DeltaSeconds)
{
	if (State.bForcedGoal)
	{
		if (State.Goal.Type == EVeloxBotGoal::Item && !State.Goal.Item.IsValid())
		{
			ClearForcedGoal();
		}
		return;
	}
	UpdateGoal(DeltaSeconds);
}

TSubclassOf<UNavigationQueryFilter> AVeloxBotController::GetNavFilter() const
{
	const double T = Now();
	if (T < State.WalkFilterUntil)
	{
		return UVeloxNavFilter_Walk::StaticClass();
	}
	const TSubclassOf<UNavigationQueryFilter> Base = Params.bStrafeJump ? UVeloxNavFilter_Hop::StaticClass() : UVeloxNavFilter_Walk::StaticClass();
	if (!Params.bRocketJumps || T < State.HopFilterUntil)
	{
		return Base;
	}
	// Full only while the bot could rocket-jump every rocket-jump link (section 4): launcher, a rocket, enough health.
	const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld());
	int32 MinHealth = INDEX_NONE;
	if (Bots)
	{
		for (const AVeloxNavLink* Link : Bots->GetNavLinks())
		{
			if (Link->GetLinkType() == EVeloxNavLinkType::RocketJump && Link->IsLinkEnabled())
			{
				MinHealth = FMath::Max(MinHealth, Link->GetMinHealthForRocketJump());
			}
		}
	}
	const AVeloxPlayerState* PS = GetPlayerState<AVeloxPlayerState>();
	const UVeloxHealthSet* Health = PS ? PS->GetHealthSet() : nullptr;
	const UVeloxAmmoSet* Ammo = PS ? PS->GetAmmoSet() : nullptr;
	const bool bCan = MinHealth != INDEX_NONE && Health && Ammo && Ammo->GetRockets() >= 1.f && Health->GetHealth() >= MinHealth &&
		VeloxBotControllerPrivate::OwnedWeaponIndex(PS, VeloxGameplayTags::Weapon_RocketLauncher) != INDEX_NONE;
	return bCan ? TSubclassOf<UNavigationQueryFilter>(UVeloxNavFilter_Full::StaticClass()) : Base;
}

bool AVeloxBotController::EvaluateRocketJump(const AVeloxNavLink& Link, bool& bOutSwitchDone) const
{
	const AVeloxPlayerState* PS = GetPlayerState<AVeloxPlayerState>();
	const UVeloxHealthSet* Health = PS ? PS->GetHealthSet() : nullptr;
	const UVeloxAmmoSet* Ammo = PS ? PS->GetAmmoSet() : nullptr;
	const UVeloxWeaponInventoryComponent* Inventory = PS ? PS->GetWeaponInventory() : nullptr;
	const bool bOwns = VeloxBotControllerPrivate::OwnedWeaponIndex(PS, VeloxGameplayTags::Weapon_RocketLauncher) != INDEX_NONE;
	const UVeloxWeaponDefinition* Current = VeloxBotControllerPrivate::CurrentWeapon(PS);
	bOutSwitchDone = bOwns && Current && Current->WeaponTag == VeloxGameplayTags::Weapon_RocketLauncher && Inventory && Inventory->GetPendingIndex() == INDEX_NONE &&
		Now() >= Inventory->GetSwitchEndTime();
	return Params.bRocketJumps && bOwns && Ammo && Ammo->GetRockets() >= 1.f && Health && Health->GetHealth() >= Link.GetMinHealthForRocketJump();
}

void AVeloxBotController::RequestRepath(const TCHAR* Reason)
{
	if (!State.bRepathRequested)
	{
		State.RepathRequestedAt = Now();
	}
	State.bRepathRequested = true;
	State.RepathReason = Reason;
}

void AVeloxBotController::StopMove()
{
	// A dropped path is re-queried anyway; a pending D35 request has nothing left to replace.
	State.bRepathRequested = false;
	State.RepathRequestedAt = -1.0;
	if (UPathFollowingComponent* Follow = GetPathFollowingComponent(); Follow && Follow->GetStatus() != EPathFollowingStatus::Idle)
	{
		StopMovement();
	}
	State.bMoveActive = false;
}

void AVeloxBotController::EnsureMove(double T)
{
	const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld());
	UVeloxBotPathFollowingComponent* Follow = GetBotPathFollowing();
	const AVeloxCharacter* Character = GetBotCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	if (!Bots || !Bots->IsNavReady() || !Follow || !Movement)
	{
		return;
	}
	if (Follow->IsOnLink())
	{
		// Never re-query mid-traversal: the maneuver ends the link segment.
		return;
	}
	bool bOffMeshActive = false;
	{
		const FVeloxBotCostScope Cost(GetWorld(), EVeloxBotCost::OffMesh);
		// ADR 0008 3.6.4: the hazard escape runs ahead of the off-mesh recovery and owns the move while it lasts.
		if (UpdateHazardEscape(T))
		{
			if (State.bMoveActive)
			{
				StopMove();
			}
			State.bRepathRequested = false;
			return;
		}
		bOffMeshActive = UpdateOffMeshRecovery(T);
	}
	if (bOffMeshActive)
	{
		// The recovery owns the move (a direct move); a D35 request against it is dropped.
		State.bRepathRequested = false;
		return;
	}
	FVector Target;
	float Acceptance = 0.f;
	if (!GetGoalTarget(Target, Acceptance))
	{
		if (State.bMoveActive)
		{
			StopMove();
		}
		return;
	}
	if (State.bGoalArrived)
	{
		// Standing at the goal; walk back if pushed away (knockback) or if the goal moved.
		// Task 06b: 3-D (a bot knocked off the item's platform onto the floor below is no longer at it).
		if (VeloxBotRules::IsAtGoal(Movement->GetActorFeetLocation(), Target, Acceptance + GoalMoveToleranceCm, GetArrivalMaxDzCm()) &&
			FVector::Dist(Target, State.MoveTarget) <= GoalMoveToleranceCm)
		{
			return;
		}
		State.bGoalArrived = false;
	}
	const TSubclassOf<UNavigationQueryFilter> Filter = GetNavFilter();
	// D35: a repath request from the follower (segment line-of-walk blocked) is a need like the others.
	const bool bNeedPath = !State.bMoveActive || Follow->GetStatus() == EPathFollowingStatus::Idle || FVector::Dist(Target, State.MoveTarget) > GoalMoveToleranceCm ||
		Filter != State.MoveFilter || State.bRepathRequested;
	if (!bNeedPath || T < State.NextMoveTime || !Movement->IsOnGround())
	{
		// Path queries start from the ground (an airborne start may be far above the mesh).
		return;
	}
	State.NextMoveTime = T + RepathIntervalSeconds;
	if (State.bRepathRequested)
	{
		State.bRepathRequested = false;
		++State.Repaths;
		const FVeloxSegmentBlockInfo& Block = Follow->GetLastSegmentBlock();
		// D44: at= is where the request is issued (a ground tick after RepathIntervalSeconds), from= the ray start where the
		// follower found the block, age= the time in between.
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRepath bot=%s reason=%s at=%s from=%s target=%s hit=%s hit_dist=%.0f remaining=%.0f age=%.2f"), *GetBotName(),
			State.RepathReason ? State.RepathReason : TEXT("-"), *Character->GetActorLocation().ToCompactString(), *Block.From.ToCompactString(), *Block.Target.ToCompactString(),
			*Block.Hit.ToCompactString(), Block.HitDistCm, Block.RemainingCm, State.RepathRequestedAt >= 0.0 ? T - State.RepathRequestedAt : -1.0);
		State.RepathRequestedAt = -1.0;
	}

	FAIMoveRequest Request(Target);
	Request.SetUsePathfinding(true);
	Request.SetAllowPartialPath(true);
	Request.SetProjectGoalLocation(true);
	Request.SetNavigationFilter(Filter);
	Request.SetAcceptanceRadius(Acceptance);
	Request.SetReachTestIncludesAgentRadius(false);
	Request.SetReachTestIncludesGoalRadius(false);
	Request.SetCanStrafe(true);
	const FPathFollowingRequestResult Result = MoveTo(Request);
	NotePathQuery();
	State.MoveTarget = Target;
	State.MoveFilter = Filter;
	switch (Result.Code)
	{
	case EPathFollowingRequestResult::RequestSuccessful:
		State.bMoveActive = true;
		State.MoveRequestId = Result.MoveId;
		State.SlowSince = -1.0;
		UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotPath bot=%s to=%s filter=%s id=%u"), *GetBotName(), *Target.ToCompactString(), VeloxBotControllerPrivate::FilterLabel(Filter), Result.MoveId.GetID());
		break;
	case EPathFollowingRequestResult::AlreadyAtGoal:
		State.bMoveActive = false;
		if (IsAtGoalTarget())
		{
			State.bGoalArrived = true;
			break;
		}
		// Task 06b: the projected goal is reached, the goal itself is on another level (an item above any walkable
		// surface the projection found): counted like a failed query, so an unreachable goal is dropped.
		++State.FailedMoves;
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotPath bot=%s to=%s filter=%s result=not_at_goal failures=%d"), *GetBotName(), *Target.ToCompactString(),
			VeloxBotControllerPrivate::FilterLabel(Filter), State.FailedMoves);
		if (State.FailedMoves >= FailedMovesToRoam && !State.bForcedGoal)
		{
			SetGoal(FVeloxBotGoal());
		}
		break;
	default:
		State.bMoveActive = false;
		++State.FailedMoves;
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotPath bot=%s to=%s filter=%s result=failed failures=%d"), *GetBotName(), *Target.ToCompactString(), VeloxBotControllerPrivate::FilterLabel(Filter),
			State.FailedMoves);
		if (State.FailedMoves >= FailedMovesToRoam && !State.bForcedGoal)
		{
			SetGoal(FVeloxBotGoal());
		}
		break;
	}
}

bool AVeloxBotController::UpdateOffMeshRecovery(double T)
{
	const AVeloxCharacter* Character = GetBotCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	if (!Movement || !Movement->IsOnGround() || T < State.NextOffMeshCheckTime)
	{
		// Airborne (a hop, a fall off the ledge) or between checks: recovery keeps the move it has.
		return State.bOffMeshRecovery;
	}
	State.NextOffMeshCheckTime = T + OffMeshCheckSeconds;
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld());
	ANavigationData* NavData = Bots ? Bots->GetNavData() : nullptr;
	if (!NavSys || !NavData)
	{
		return State.bOffMeshRecovery;
	}
	const FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(*NavData, this, GetNavFilter());
	const FVector Feet = Character->GetNavAgentLocation();
	// ADR 0005 D41: the shared on-mesh extent (UVeloxBotSettings::OnMeshHalfHeightCm, the same as the router's ground
	// projection and the D35 segment check), not the agent's default query extent (100, 100, 200): from the floor the
	// default reaches a thin walkable surface overhead (a ramp, a catwalk) and would call a bot in the void under it "on
	// the mesh". The default extent is still what MoveTo's own start projection uses, so from under such a surface a goal
	// path would start on the surface above: no goal moves are issued while bOffMeshRecovery (UpdateMove returns first).
	FNavLocation OnMesh;
	const bool bOnMesh = NavSys->ProjectPointToNavigation(Feet, OnMesh, UVeloxBotItemRouter::GetOnMeshExtent(), NavData, Filter);
	const double OffSince = State.OffMeshSince;
	const bool bStart = VeloxBotRules::UpdateOffMesh(State.OffMeshSince, bOnMesh, State.bOffMeshRecovery && State.bMoveActive, State.OffMeshMoveUntil, T, OffMeshGraceSeconds);
	if (bOnMesh)
	{
		if (State.bOffMeshRecovery)
		{
			++State.OffMeshRecoveries;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotOffMesh bot=%s recovered at=%s after=%.2f s attempts=%d goal=%s"), *GetBotName(), *Feet.ToCompactString(),
				OffSince >= 0.0 ? T - OffSince : 0.0, State.OffMeshAttempts, LexToString(State.Goal.Type));
			State.bOffMeshRecovery = false;
			State.OffMeshAttempts = 0;
			StopMove();
			State.NextMoveTime = T;
		}
		return false;
	}
	if (!bStart)
	{
		return State.bOffMeshRecovery;
	}

	// Recovery target: the nearest nav point the bot can walk to in a straight line. Rings of candidates around the feet
	// (OffMeshRecoverRingsCm x OffMeshRecoverDirections, the start angle drawn from the bot's stream), each projected onto
	// the mesh; a candidate counts when it is not above step height and a thin static-world sweep at step height to it
	// is clear (a drop on the way is fine, a wall is not; thin because the walk slides along walls, so a narrow strip
	// whose exit is not exactly on a candidate direction still counts). The nearest nav point regardless of walls is the
	// last resort; the previous attempt's target is skipped.
	if (State.OffMeshAttempts >= OffMeshMaxAttempts)
	{
		// D17: terminal escape. No teleport (ADR 0005 section 1): environment damage kills the bot, B28 respawns it.
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotOffMesh bot=%s at=%s attempt=%d result=giveup goal=%s"), *GetBotName(), *Feet.ToCompactString(), State.OffMeshAttempts,
			LexToString(State.Goal.Type));
		State.bOffMeshRecovery = false;
		State.OffMeshAttempts = 0;
		State.OffMeshSince = -1.0;
		StopMove();
		UVeloxAbilitySystemComponent* SelfASC = Character->GetVeloxAbilitySystemComponent();
		const AVeloxPlayerState* SelfPS = GetPlayerState<AVeloxPlayerState>();
		if (SelfASC && GetWorld() && GetWorld()->GetNetMode() != NM_Client)
		{
			FVeloxDamageSpecParams KillParams;
			KillParams.Damage = UVeloxCombatSettings::GetOrDefault(SelfPS ? SelfPS->GetCombatSettings() : nullptr).EnvironmentKillDamage;
			KillParams.DamageTag = VeloxGameplayTags::Damage_Environment;
			KillParams.HitLocation = Character->GetActorLocation();
			KillParams.bNoKnockback = true;
			KillParams.Causer = const_cast<AVeloxCharacter*>(Character);
			const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(SelfASC, KillParams);
			if (Spec.IsValid())
			{
				SelfASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), SelfASC);
			}
		}
		return false;
	}
	++State.OffMeshAttempts;
	State.OffMeshMoveUntil = T + OffMeshRecoverTimeoutSeconds;
	State.bOffMeshRecovery = true;
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 70.f;
	const float StepHeight = Movement->MaxStepHeight;
	const float SweepRadius = FMath::Max(OffMeshSweepRadiusCm, 1.f);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(SweepRadius, FMath::Max(HalfHeight - StepHeight * 0.5f, SweepRadius));
	const float SweepZ = Shape.GetCapsuleHalfHeight() + StepHeight;
	FCollisionQueryParams SweepParams(SCENE_QUERY_STAT(VeloxBotOffMesh), false, Character);
	const FCollisionObjectQueryParams StaticOnly(ECC_WorldStatic);
	const float StartAngle = Stream.FRandRange(0.f, 2.f * UE_PI);
	const int32 NumDirections = FMath::Max(OffMeshRecoverDirections, 0);
	const bool bAvoidLast = State.OffMeshAttempts > 1;
	const FVector SweepStart(Feet.X, Feet.Y, Feet.Z + SweepZ);
	int32 Tested = 0;
	// One candidate test for the ring directions and the D45 tangent probes: the nearest point inside the reachable window
	// (D44), not the previous attempt's target, closer than the ring's best so far, and a clear thin sweep at step height.
	auto TestCandidate = [&](const FVector& Probe, float& BestDist, FNavLocation& Best) -> bool
	{
		FNavLocation Candidate;
		++Tested;
		if (!VeloxBotControllerPrivate::ProjectReachable(*NavSys, *NavData, Filter, Probe, Feet.Z, StepHeight, OffMeshCandidateExtentCm, OffMeshRecoverHalfHeightCm, Candidate))
		{
			return false;
		}
		const float Dist = FVector::Dist2D(Feet, Candidate.Location);
		if (Dist >= BestDist || (bAvoidLast && FVector::Dist2D(Candidate.Location, State.OffMeshLastTarget) < OffMeshCandidateExtentCm))
		{
			return false;
		}
		const FVector SweepEnd(Candidate.Location.X, Candidate.Location.Y, Feet.Z + SweepZ);
		if (GetWorld()->SweepTestByObjectType(SweepStart, SweepEnd, FQuat::Identity, StaticOnly, Shape, SweepParams))
		{
			return false;
		}
		BestDist = Dist;
		Best = Candidate;
		return true;
	};
	FNavLocation Point;
	bool bFound = false;
	for (const float Ring : OffMeshRecoverRingsCm)
	{
		float BestDist = TNumericLimits<float>::Max();
		for (int32 i = 0; i < NumDirections; ++i)
		{
			const float Angle = StartAngle + 2.f * UE_PI * i / NumDirections;
			bFound |= TestCandidate(Feet + FVector(FMath::Cos(Angle) * Ring, FMath::Sin(Angle) * Ring, 0.0), BestDist, Point);
		}
		if (bFound)
		{
			break;
		}
	}
	const TCHAR* Kind = TEXT("walkable");
	if (!bFound)
	{
		Kind = TEXT("nearest");
		const float Far = OffMeshRecoverRingsCm.Num() > 0 ? OffMeshRecoverRingsCm.Last() : 1000.f;
		// D41/D44: a point above step height is not a target (the surface over the bot's head, not a way out): the nearest
		// point inside the reachable window [feet - OffMeshRecoverHalfHeightCm, feet + step]. Nothing there (within Far, 2-D)
		// runs the attempt counter to the D17 terminal escape.
		FNavLocation Nearest;
		if (!VeloxBotControllerPrivate::ProjectReachable(*NavSys, *NavData, Filter, Feet, Feet.Z, StepHeight, Far, OffMeshRecoverHalfHeightCm, Nearest))
		{
			++State.OffMeshNoNavPoints;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotOffMesh bot=%s at=%s attempt=%d tested=%d result=no_nav_point"), *GetBotName(), *Feet.ToCompactString(), State.OffMeshAttempts, Tested);
			StopMove();
			return true;
		}
		// D45: the fallback is swept like the candidates (the nearest window point may lie behind a wall) and skips the
		// previous attempt's target. A blocked fallback probes along the blocking wall's tangent (the same rings, the same
		// candidate test, the two directions only): a wall is what makes a slot, its exits lie along the wall.
		FHitResult Hit;
		const FVector SweepEnd(Nearest.Location.X, Nearest.Location.Y, Feet.Z + SweepZ);
		const bool bBlocked = GetWorld()->SweepSingleByObjectType(Hit, SweepStart, SweepEnd, FQuat::Identity, StaticOnly, Shape, SweepParams);
		const bool bRepeat = bAvoidLast && FVector::Dist2D(Nearest.Location, State.OffMeshLastTarget) < OffMeshCandidateExtentCm;
		FVector2D Tangent;
		if (!bBlocked && !bRepeat)
		{
			Point = Nearest;
			bFound = true;
		}
		else if (bBlocked && VeloxBotRules::WallTangent(Hit.ImpactNormal, Tangent))
		{
			for (const float Ring : OffMeshRecoverRingsCm)
			{
				float BestDist = TNumericLimits<float>::Max();
				for (const float Sign : { 1.f, -1.f })
				{
					bFound |= TestCandidate(Feet + FVector(Tangent * (Sign * Ring), 0.0), BestDist, Point);
				}
				if (bFound)
				{
					break;
				}
			}
			if (bFound)
			{
				Kind = TEXT("tangent");
				++State.OffMeshTangentTargets;
			}
		}
		if (!bFound)
		{
			++State.OffMeshNoClearPoints;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotOffMesh bot=%s at=%s attempt=%d tested=%d nearest=%s blocked=%d result=no_clear_point"), *GetBotName(), *Feet.ToCompactString(),
				State.OffMeshAttempts, Tested, *Nearest.Location.ToCompactString(), bBlocked ? 1 : 0);
			StopMove();
			return true;
		}
	}
	State.OffMeshLastTarget = Point.Location;
	// A straight move through the same follower (abstract nav data: a two-point path); the bot walks or falls off the ledge.
	FAIMoveRequest Request(Point.Location);
	Request.SetUsePathfinding(false);
	Request.SetProjectGoalLocation(false);
	Request.SetAcceptanceRadius(PointAcceptanceRadiusCm);
	Request.SetReachTestIncludesAgentRadius(false);
	Request.SetReachTestIncludesGoalRadius(false);
	Request.SetCanStrafe(true);
	const FPathFollowingRequestResult Result = MoveTo(Request);
	NotePathQuery();
	State.bMoveActive = Result.Code == EPathFollowingRequestResult::RequestSuccessful;
	State.MoveRequestId = Result.MoveId;
	State.MoveTarget = Point.Location;
	State.SlowSince = -1.0;
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotOffMesh bot=%s at=%s to=%s dist=%.0f target=%s tested=%d attempt=%d result=%s goal=%s"), *GetBotName(), *Feet.ToCompactString(),
		*Point.Location.ToCompactString(), FVector::Dist(Feet, Point.Location), Kind, Tested, State.OffMeshAttempts,
		State.bMoveActive ? TEXT("moving") : Result.Code == EPathFollowingRequestResult::AlreadyAtGoal ? TEXT("at_point") : TEXT("failed"), LexToString(State.Goal.Type));
	return true;
}

bool AVeloxBotController::UpdateHazardEscape(double T)
{
	UWorld* World = GetWorld();
	const AVeloxCharacter* Character = GetBotCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	const UVeloxHazardSubsystem* Hazards = World ? UVeloxHazardSubsystem::Get(World) : nullptr;
	if (!Movement || !Hazards || !VeloxBotCombatGlue::IsEdgeAware(World))
	{
		State.bHazardEscape = false;
		return false;
	}
	const FVector Feet = Movement->GetActorFeetLocation();
	// A hair above the capsule bottom: standing on a lava bed puts the bottom exactly on the volume's floor.
	const bool bInLava = Hazards->IsInHazard(Feet + FVector(0.0, 0.0, 1.0), EVeloxHazardType::Lava);
	if (!State.bHazardEscape)
	{
		if (!bInLava)
		{
			return false;
		}
		State.bHazardEscape = true;
		State.HazardEscapeSince = T;
		State.NextHazardEscapeSearchTime = 0.0;
		State.bHazardEscapeHasTarget = false;
		++State.HazardEscapes;
	}
	// Out: on the ground outside the lava (the jump out leaves the thin volume long before it lands on the rim).
	const double Held = T - State.HazardEscapeSince;
	const bool bOut = !bInLava && Movement->IsOnGround();
	if (bOut || Held >= HazardEscapeMaxSeconds)
	{
		State.HazardEscapesOut += bOut ? 1 : 0;
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotHazardEscape bot=%s at=%s result=%s dt=%.2f target=%s escapes=%d"), *GetBotName(), *Feet.ToCompactString(),
			bOut ? TEXT("out") : TEXT("timeout"), Held, State.bHazardEscapeHasTarget ? *State.HazardEscapeTarget.ToCompactString() : TEXT("none"), State.HazardEscapes);
		State.bHazardEscape = false;
		State.bHazardEscapeHasTarget = false;
		// Path from where the escape left the bot.
		State.NextMoveTime = T;
		return false;
	}
	if (T >= State.NextHazardEscapeSearchTime)
	{
		State.NextHazardEscapeSearchTime = T + HazardEscapeSearchSeconds;
		const bool bFirst = !State.bHazardEscapeHasTarget;
		FVector Target;
		if (FindHazardEscapeTarget(Feet, Target))
		{
			State.HazardEscapeTarget = Target;
			State.bHazardEscapeHasTarget = true;
		}
		if (bFirst)
		{
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotHazardEscape bot=%s at=%s result=start target=%s dist=%.0f goal=%s"), *GetBotName(), *Feet.ToCompactString(),
				State.bHazardEscapeHasTarget ? *State.HazardEscapeTarget.ToCompactString() : TEXT("none"),
				State.bHazardEscapeHasTarget ? FVector::Dist2D(Feet, State.HazardEscapeTarget) : -1.0, LexToString(State.Goal.Type));
		}
	}
	return true;
}

bool AVeloxBotController::FindHazardEscapeTarget(const FVector& Feet, FVector& OutTarget) const
{
	UWorld* World = GetWorld();
	const UVeloxHazardSubsystem* Hazards = World ? UVeloxHazardSubsystem::Get(World) : nullptr;
	if (!Hazards)
	{
		return false;
	}
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
	ANavigationData* NavData = Bots ? Bots->GetNavData() : nullptr;
	if (NavSys && NavData)
	{
		const FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(*NavData, this, GetNavFilter());
		const FVector Up(0.0, 0.0, HazardProbeUpCm);
		double BestDist = TNumericLimits<double>::Max();
		auto Test = [&](const FVector& Probe, float Extent)
		{
			FNavLocation Loc;
			if (!VeloxBotControllerPrivate::ProjectReachable(*NavSys, *NavData, Filter, Probe, Feet.Z, HazardEscapeUpCm, Extent, HazardEscapeDownCm, Loc))
			{
				return;
			}
			// A nav point on the lava bed (or in any other volume) is no way out.
			if (Hazards->IsInHazard(Loc.Location + Up))
			{
				return;
			}
			const double Dist = FVector::Dist2D(Feet, Loc.Location);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				OutTarget = Loc.Location;
			}
		};
		// The nearest nav point within the extent first; then four rings of small projections, so a mesh on the bed
		// under the feet (rejected above) does not shadow the rim.
		Test(Feet, HazardEscapeExtentCm);
		const int32 NumDirections = FMath::Max(HazardEscapeDirections, 1);
		constexpr int32 NumRings = 4;
		const float RingStep = HazardEscapeExtentCm / NumRings;
		for (int32 Ring = 1; Ring <= NumRings; ++Ring)
		{
			for (int32 Dir = 0; Dir < NumDirections; ++Dir)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * Dir / NumDirections;
				Test(Feet + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * (RingStep * Ring), RingStep * 0.5f);
			}
		}
		if (BestDist < TNumericLimits<double>::Max())
		{
			return true;
		}
	}
	// No nav point: the nearest side of the volume the feet are in.
	const AVeloxHazardVolume* Volume = Hazards->FindHazardAt(Feet + FVector(0.0, 0.0, 1.0));
	const UBoxComponent* Box = Volume ? Volume->GetBox() : nullptr;
	if (!Box)
	{
		return false;
	}
	const FTransform& Xf = Box->GetComponentTransform();
	const FVector Extent = Box->GetScaledBoxExtent();
	const FVector2D Exit = VeloxBotRules::HazardBoxExit(FVector2D(Feet), FVector2D(Xf.GetLocation()), FVector2D(Xf.GetUnitAxis(EAxis::X)), static_cast<float>(Extent.X),
		static_cast<float>(Extent.Y), HazardEscapeExtentCm * 0.25f);
	OutTarget = FVector(Exit, Feet.Z);
	return true;
}

void AVeloxBotController::ApplyHazardBehaviours(double T)
{
	UWorld* World = GetWorld();
	const AVeloxCharacter* Character = GetBotCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	const UVeloxBotPathFollowingComponent* Follow = GetBotPathFollowing();
	if (!Movement || !Follow || !VeloxBotCombatGlue::IsEdgeAware(World))
	{
		return;
	}
	const bool bOnGround = Movement->IsOnGround();
	const FVector Feet = Movement->GetActorFeetLocation();

	// 3.6.4 hazard escape: face the target, forward and jump. Jump is pressed on the ground and released in the air, so the
	// landing on the rim is a landing (a held key would auto-hop on and the escape would never see the ground).
	if (State.bHazardEscape)
	{
		if (State.bHazardEscapeHasTarget)
		{
			const FVector2D Dir = (FVector2D(State.HazardEscapeTarget) - FVector2D(Feet)).GetSafeNormal();
			if (!Dir.IsNearlyZero())
			{
				State.Pending.View.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
				State.Pending.WishDirWorld = Dir;
				State.Pending.WishScale = 1.f;
			}
		}
		State.Pending.bJumpHeld = bOnGround;
		State.Pending.bCrouch = false;
		return;
	}

	// Foreign-flight latch for 3.6.3 (the bot's own hops are left alone).
	if (bOnGround)
	{
		State.bAirborneSeen = false;
		State.bForeignFlight = false;
		State.bAirRecoverySteered = false;
		State.bAirRecoveryHasTarget = false;
		State.bHazardLandingSteered = false;
		State.bHazardLandingHasTarget = false;
		State.LastGroundFeetZ = Feet.Z;
		return;
	}
	const float HopAir = VeloxBotRules::HopAirSeconds(Movement->GetSimParams().JumpVelocity, Movement->GetGravityZ(), StuckProgressFlightSlackSeconds);
	const bool bIsFlight = VeloxBotRules::IsFlight(false, State.AirborneSince, T, HopAir);
	if (!State.bAirborneSeen)
	{
		State.bAirborneSeen = true;
		// bJumpApplied is the key the last ApplyInput sent: the one that started this airborne stretch, if any.
		State.bForeignFlight = VeloxBotRules::IsForeignFlight(State.bJumpApplied, Movement->GetKnockbackGrace(), bIsFlight);
	}
	else if (!State.bForeignFlight)
	{
		State.bForeignFlight = VeloxBotRules::IsForeignFlight(true, Movement->GetKnockbackGrace(), bIsFlight);
	}

	// 3.6.3 pad discipline: no move input in a JUMP_PAD flight (aim, weapon and fire stay as Act left them).
	if (Follow->IsOnLink())
	{
		if (Follow->GetManeuver().IsPadFlight())
		{
			State.Pending.WishDirWorld = FVector2D::ZeroVector;
			State.Pending.WishScale = 0.f;
			++State.PadDisciplineTicks;
		}
		return;
	}
	if (!State.bForeignFlight)
	{
		UpdateHazardLanding(Feet, Movement->Velocity, Movement->GetGravityZ());
		return;
	}

	// 3.6.3 airborne recovery: the predicted landing at the level of the last recovery point (first: the feet's own
	// level), projected onto nav at or below the feet within AirRecoveryExtentCm; steer from the landing toward it.
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
	ANavigationData* NavData = Bots ? Bots->GetNavData() : nullptr;
	if (!NavSys || !NavData)
	{
		return;
	}
	const float Gravity = FMath::Abs(Movement->GetGravityZ());
	const FVector Velocity = Movement->Velocity;
	const float FeetZ = static_cast<float>(Feet.Z);
	const float LevelZ = State.bAirRecoveryHasTarget ? FMath::Min(static_cast<float>(State.AirRecoveryTarget.Z), FeetZ) : FeetZ;
	const float Seconds = VeloxBotRules::FlightSecondsToLevel(static_cast<float>(Velocity.Z), FeetZ - LevelZ, Gravity);
	const FVector2D Landing = FVector2D(Feet) + FVector2D(Velocity) * Seconds;
	const FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(*NavData, this, GetNavFilter());
	FNavLocation Point;
	if (!VeloxBotControllerPrivate::ProjectReachable(*NavSys, *NavData, Filter, FVector(Landing, Feet.Z), Feet.Z, AirRecoveryUpToleranceCm, AirRecoveryExtentCm, AirRecoveryDownCm, Point))
	{
		// Nothing at or below within reach (over the void, or already below the ledge): the path wish stays.
		State.bAirRecoveryHasTarget = false;
		return;
	}
	// Task M7 H7: a landing over lava projects onto the bed's mesh right under it ("on course"); steer to the nearest nav
	// point outside every hazard instead.
	const UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(World);
	if (Hazards && Hazards->IsInHazard(Point.Location + FVector(0.0, 0.0, HazardProbeUpCm)))
	{
		FVector Safe;
		if (!FindHazardEscapeTarget(Point.Location, Safe))
		{
			State.bAirRecoveryHasTarget = false;
			return;
		}
		Point.Location = Safe;
	}
	State.AirRecoveryTarget = Point.Location;
	State.bAirRecoveryHasTarget = true;
	const FVector2D Wish = VeloxBotRules::AirRecoveryWish(Landing, FVector2D(Point.Location), AirRecoveryOnCourseCm);
	if (Wish.IsZero())
	{
		return;
	}
	State.Pending.WishDirWorld = Wish;
	State.Pending.WishScale = 1.f;
	State.Pending.bJumpHeld = false;
	++State.AirRecoveryTicks;
	if (!State.bAirRecoverySteered)
	{
		State.bAirRecoverySteered = true;
		++State.AirRecoveryFlights;
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotAirRecovery bot=%s at=%s vel=%s landing=%s target=%s flights=%d"), *GetBotName(), *Feet.ToCompactString(),
			*Velocity.ToCompactString(), *Landing.ToString(), *Point.Location.ToCompactString(), State.AirRecoveryFlights);
	}
}

void AVeloxBotController::UpdateHazardLanding(const FVector& Feet, const FVector& Velocity, float GravityZ)
{
	// Task M7 H7: the bot's own hop or dodge jump (the 3.6.3 recovery leaves those alone) whose predicted landing at the
	// take-off level is in lava (the overlay's air wish can bend a hop over a channel corner into it): steer from that landing
	// to the nearest nav point outside every hazard, like the airborne recovery.
	const float Gravity = FMath::Abs(GravityZ);
	const float Drop = FMath::Max(0.f, static_cast<float>(Feet.Z - State.LastGroundFeetZ));
	const float Seconds = VeloxBotRules::FlightSecondsToLevel(static_cast<float>(Velocity.Z), Drop, Gravity);
	const FVector2D Landing = FVector2D(Feet) + FVector2D(Velocity) * Seconds;
	const FVector LandingAt(Landing, State.LastGroundFeetZ);
	const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
	bool bHazard = false;
	if (!State.bHazardLandingHasTarget)
	{
		const FVector Extent(Settings.HopLandingProjectRadiusCm, Settings.HopLandingProjectRadiusCm, Settings.OnMeshHalfHeightCm);
		if (VeloxBotCombatGlue::ProjectsOntoNav(*this, LandingAt, Extent, &bHazard) || !bHazard)
		{
			return;
		}
		FVector Target;
		if (!FindHazardEscapeTarget(LandingAt, Target))
		{
			return;
		}
		State.HazardLandingTarget = Target;
		State.bHazardLandingHasTarget = true;
	}
	const FVector2D Wish = VeloxBotRules::AirRecoveryWish(Landing, FVector2D(State.HazardLandingTarget), AirRecoveryOnCourseCm);
	if (Wish.IsZero())
	{
		return;
	}
	State.Pending.WishDirWorld = Wish;
	State.Pending.WishScale = 1.f;
	State.Pending.bJumpHeld = false;
	++State.HazardLandingTicks;
	if (!State.bHazardLandingSteered)
	{
		State.bHazardLandingSteered = true;
		++State.HazardLandingFlights;
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotHazardLanding bot=%s at=%s vel=%s landing=%s target=%s flights=%d"), *GetBotName(), *Feet.ToCompactString(),
			*Velocity.ToCompactString(), *Landing.ToString(), *State.HazardLandingTarget.ToCompactString(), State.HazardLandingFlights);
	}
}

void AVeloxBotController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);
	if (!State.bMoveActive || RequestID != State.MoveRequestId)
	{
		// An older request replaced by a newer one.
		return;
	}
	State.bMoveActive = false;
	if (State.bOffMeshRecovery)
	{
		// The recovery move ended (nav point reached or aborted): it says nothing about the goal; check the mesh at once.
		State.NextOffMeshCheckTime = 0.0;
		return;
	}
	const UPathFollowingComponent* Follow = GetPathFollowingComponent();
	// Task 06b: the engine's reach test is against the projected path end; arrival is also 3-D against the goal target.
	const bool bReachedGoal = Result.IsSuccess() && Follow && Follow->DidMoveReachGoal() && IsAtGoalTarget();
	if (bReachedGoal)
	{
		State.bGoalArrived = true;
		State.StuckStrikes = 0;
		if (State.Goal.Type == EVeloxBotGoal::Roam && !State.bForcedGoal)
		{
			// Roam point reached: choose again.
			SetGoal(FVeloxBotGoal());
		}
		else
		{
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotGoal bot=%s arrived goal=%s after=%.2f s"), *GetBotName(), LexToString(State.Goal.Type), Now() - State.Goal.SetTime);
		}
		return;
	}
	UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotPath bot=%s finished %s"), *GetBotName(), *Result.ToString());
	if (Result.IsSuccess())
	{
		// The end of a partial path: a roam point is simply replaced; any other goal counts it like a failed query so
		// an unreachable goal is eventually dropped.
		++State.FailedMoves;
		const bool bRoam = State.Goal.Type == EVeloxBotGoal::Roam;
		if (!State.bForcedGoal && (bRoam || State.FailedMoves >= FailedMovesToRoam))
		{
			SetGoal(FVeloxBotGoal());
		}
	}
}

void AVeloxBotController::NotifyLinkFinished(const FVeloxBotLinkReport& Report)
{
	const bool bOk = Report.Result == EVeloxManeuverResult::Ok;
	const TCHAR* ResultText = bOk ? TEXT("ok") : Report.Result == EVeloxManeuverResult::Missed ? TEXT("missed") : TEXT("aborted");
	const TCHAR* Reason = Report.Result == EVeloxManeuverResult::None ? TEXT("replaced") : LexToString(Report.Result);
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotLink bot=%s type=%s from=%s to=%s result=%s reason=%s dt=%.2f link=%s"), *GetBotName(), VeloxBotControllerPrivate::ManeuverLabel(Report.Type),
		*Report.From.ToCompactString(), *Report.To.ToCompactString(), ResultText, Reason, Report.Seconds, *Report.LinkName);
	bOk ? ++State.LinksOk : ++State.LinksFailed;
	const double T = Now();
	switch (Report.Result)
	{
	case EVeloxManeuverResult::TooSlow:
		State.WalkFilterUntil = T + LinkFallbackSeconds;
		break;
	case EVeloxManeuverResult::NoImpulse:
	case EVeloxManeuverResult::Ineligible:
		State.HopFilterUntil = T + LinkFallbackSeconds;
		break;
	default:
		break;
	}
	OnLinkFinished.Broadcast(this, Report);
	if (!bOk && Report.Result != EVeloxManeuverResult::None)
	{
		// Missed or aborted on a live path: drop it and re-query from where the bot ends up (on the ground). With the
		// follower off the link, UpdateOffMeshRecovery runs again (the D28 strip case recovers through D17).
		StopMove();
		State.NextMoveTime = T;
	}
	if (Report.Result == EVeloxManeuverResult::Timeout)
	{
		// D28: a timed-out traversal is a stuck strike (three on the same link roam); a below_start is not (the bot fell).
		const AVeloxCharacter* Character = GetBotCharacter();
		const FVector2D To(Report.To);
		AddStuckStrike(TEXT("link_timeout"), To, Character ? static_cast<float>(FVector2D::Distance(To, FVector2D(Character->GetActorLocation()))) : -1.f, -1.f);
	}
}

void AVeloxBotController::Navigate(float DeltaSeconds)
{
	UVeloxBotPathFollowingComponent* Follow = GetBotPathFollowing();
	const AVeloxCharacter* Character = GetBotCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	// D18 progress rule input: the previous tick's applied wish was the follower's (the combat overlay did not replace it).
	const bool bFollowerOwnedWish = State.Pending.WishDirWorld.Equals(State.Nav.WishDir, 1e-3);
	State.Pending = FVeloxBotInput();
	State.Pending.View = State.View;
	if (!Follow || !Movement)
	{
		State.Nav = FVeloxBotNavOutput();
		return;
	}
	Follow->StepFollow(DeltaSeconds);
	State.Nav = Follow->GetNavOutput();
	State.Pending.WishDirWorld = State.Nav.WishDir;
	State.Pending.WishScale = State.Nav.WishScale;
	State.Pending.bJumpHeld = State.Nav.bJumpHeld;

	// Stuck (section 5): a path is being followed (not on a link, not arrived) and the bot barely moves (speed rule), or
	// it moves but the distance to the segment target does not shrink (D18 progress rule: oscillation under a low
	// ceiling, circling). The progress clock stops while the combat overlay owns the wish (strafing, range keeping and
	// dodges move the bot off the path on purpose; with an Item goal the path is only blended in): enforcing it there
	// gave 10-22 strikes per FFA run, nearly all moving away from the target at 300-1100 cm/s. Stopping instead of
	// resetting keeps a bot that scrapes under the ramp slab while an enemy flickers in and out of view accountable.
	const double T = Now();
	const float StuckSeconds = UVeloxBotSettings::Get().StuckSeconds;
	const bool bOnGround = Movement->IsOnGround();
	// Task 06b: airborne longer than the bot's own hop (2 x JumpVelocity / |g| + slack) is a flight: a pad launch, a drop,
	// a fall or knockback. The progress window pauses through it and never expires in the air.
	if (bOnGround)
	{
		State.AirborneSince = -1.0;
	}
	else if (State.AirborneSince < 0.0)
	{
		State.AirborneSince = T;
	}
	const float HopAirSeconds = VeloxBotRules::HopAirSeconds(Movement->GetSimParams().JumpVelocity, Movement->GetGravityZ(), StuckProgressFlightSlackSeconds);
	const bool bFlight = VeloxBotRules::IsFlight(bOnGround, State.AirborneSince, T, HopAirSeconds);
	const bool bTracking = State.bMoveActive && !State.bGoalArrived && !Follow->IsOnLink() && Follow->GetStatus() == EPathFollowingStatus::Moving;
	// D30: a pathless fight (enemy visible, D12: no path) pressed against a wall is watched by the speed rule too.
	const bool bFightWatched = !bTracking && VeloxBotRules::IsFightWatched(State.Goal.Type, State.bMoveActive, IsGoalEnemyVisible(), Movement->IsOnGround(), Follow->IsOnLink());
	const bool bSlow = VeloxBotRules::UpdateStuck(State.SlowSince, bTracking || bFightWatched, Movement->GetSpeed2D(), StuckSpeedCmS, T, StuckSeconds);
	const FVector2D SegmentTarget(Follow->GetCurrentTargetLocation());
	const float SegmentDist = static_cast<float>(FVector2D::Distance(SegmentTarget, FVector2D(Movement->GetActorFeetLocation())));
	const float WindowDist = State.ProgressDist;
	const bool bNoProgress = VeloxBotRules::UpdateStuckProgress(State.ProgressSince, State.ProgressDist, State.ProgressTarget, State.ProgressPausedSeconds, bTracking,
		!bFollowerOwnedWish, SegmentTarget, SegmentDist, StuckProgressCm, T, DeltaSeconds, StuckSeconds,
		StuckProgressMaxPauseSeconds, bOnGround, bFlight);
	if (bSlow && bFightWatched)
	{
		// D30: route away for FightStallCooldownSeconds (the decision does not see the enemy; aim and fire are untouched).
		FVector2D EnemyAt = SegmentTarget;
		if (const APawn* Enemy = State.Goal.Enemy.Get())
		{
			EnemyAt = FVector2D(Enemy->GetActorLocation());
		}
		AddStuckStrike(TEXT("fight_stall"), EnemyAt, static_cast<float>(FVector2D::Distance(EnemyAt, FVector2D(Movement->GetActorFeetLocation()))), -1.f);
		State.FightSuppressedUntil = T + FightStallCooldownSeconds;
		if (UVeloxBotItemRouter* Router = UVeloxBotItemRouter::Get(GetWorld()))
		{
			Router->RequestEvaluation(this);
		}
	}
	else if (bSlow || bNoProgress)
	{
		AddStuckStrike(bSlow ? TEXT("speed") : TEXT("progress"), SegmentTarget, SegmentDist, bNoProgress ? WindowDist : -1.f);
	}

	// Task 06b fight stand-off (beside D30): a Fight goal that keeps the bot inside a FightStandoffRadiusCm circle for
	// FightStandoffSeconds - pathless at a ledge corner with the target below, dodge-hopping in place, or a chase whose
	// last seen point is reached or unreachable - yields like a fight_stall strike, without the strike: the decision does
	// not see the enemy for FightStallCooldownSeconds and routes to an item or a roam point; aim and fire are untouched.
	const bool bStandoffWatched = State.Goal.Type == EVeloxBotGoal::Fight && !State.bForcedGoal && !Follow->IsOnLink() && !IsFightSuppressed(T);
	const FVector Feet = Movement->GetActorFeetLocation();
	const double StandoffSpan = State.FightStandoff.Span(T);
	if (VeloxBotRules::UpdateFightStandoff(State.FightStandoff, bStandoffWatched, FVector2D(Feet), T, FightStandoffRadiusCm, FightStandoffSeconds))
	{
		++State.FightYields;
		const APawn* Enemy = State.Goal.Enemy.Get();
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFightYield bot=%s at=%s enemy=%s vis=%d enemy_dist=%.0f enemy_dz=%.0f held=%.2f path=%d ground=%d yields=%d"), *GetBotName(),
			*Character->GetActorLocation().ToCompactString(), *GetNameSafe(Enemy), IsGoalEnemyVisible() ? 1 : 0,
			Enemy ? FVector::Dist2D(Enemy->GetActorLocation(), Character->GetActorLocation()) : -1.0,
			Enemy ? Enemy->GetActorLocation().Z - Character->GetActorLocation().Z : 0.0, StandoffSpan, State.bMoveActive ? 1 : 0,
			bOnGround ? 1 : 0, State.FightYields);
		State.FightSuppressedUntil = T + FightStallCooldownSeconds;
		if (UVeloxBotItemRouter* Router = UVeloxBotItemRouter::Get(GetWorld()))
		{
			Router->RequestEvaluation(this);
		}
	}
}

float AVeloxBotController::GetArrivalMaxDzCm() const
{
	// Task 06b: the vertical arrival tolerance is the nav agent height (140): floor levels of the Velox maps are >= 207 cm
	// apart, items sit 0..80 cm above their floor.
	const float AgentHeight = GetNavAgentPropertiesRef().AgentHeight;
	if (AgentHeight > 0.f)
	{
		return AgentHeight;
	}
	const APawn* MyPawn = GetPawn();
	return MyPawn ? 2.f * MyPawn->GetSimpleCollisionHalfHeight() : 140.f;
}

bool AVeloxBotController::IsAtGoalTarget() const
{
	FVector Target;
	float Acceptance = 0.f;
	const APawn* MyPawn = GetPawn();
	const AVeloxCharacter* Character = GetBotCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	if (!MyPawn || !Movement || !GetGoalTarget(Target, Acceptance))
	{
		// No goal point (a visible fight, no goal): the engine's reach test stands.
		return true;
	}
	return VeloxBotRules::IsAtGoal(Movement->GetActorFeetLocation(), Target, Acceptance + GoalMoveToleranceCm, GetArrivalMaxDzCm());
}

bool AVeloxBotController::IsGoalEnemyVisible() const
{
	const APawn* Enemy = State.Goal.Type == EVeloxBotGoal::Fight ? State.Goal.Enemy.Get() : nullptr;
	const APlayerState* EnemyPS = Enemy ? Enemy->GetPlayerState() : nullptr;
	const FVeloxBotEnemyInfo* Info = EnemyPS ? Combat.FindEnemy(EnemyPS->GetPlayerId()) : nullptr;
	return Info && Info->bVisible;
}

void AVeloxBotController::AddStuckStrike(const TCHAR* Rule, const FVector2D& Target, float Dist, float WindowDist)
{
	const AVeloxCharacter* Character = GetBotCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	++State.StuckStrikes;
	if (Character && Movement)
	{
		// toward: cosine between the horizontal velocity and the direction to the target (-1 = moving away).
		const FVector2D ToTarget = (Target - FVector2D(Movement->GetActorFeetLocation())).GetSafeNormal();
		const float Toward = static_cast<float>(FVector2D::DotProduct(FVector2D(Movement->Velocity).GetSafeNormal(), ToTarget));
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotStuck bot=%s at=%s goal=%s strikes=%d rule=%s speed=%.0f toward=%.2f dist=%.0f window_dist=%.0f ground=%d"),
			*GetBotName(), *Character->GetActorLocation().ToCompactString(), LexToString(State.Goal.Type), State.StuckStrikes, Rule, Movement->GetSpeed2D(), Toward, Dist,
			WindowDist, Movement->IsOnGround() ? 1 : 0);
	}
	// One strike per window: both clocks restart.
	const double T = Now();
	State.SlowSince = -1.0;
	State.ProgressSince = -1.0;
	StopMove();
	State.NextMoveTime = T;
	if (State.StuckStrikes >= StuckStrikesToRoam)
	{
		State.StuckStrikes = 0;
		if (!State.bForcedGoal)
		{
			StartRoam();
		}
	}
}

void AVeloxBotController::Act(float DeltaSeconds)
{
	// Sections 8 / 9 (task 05): aim model -> View (maneuver view requests win: rocket-jump yaw, pitch, fire, launcher),
	// weapon choice -> SelectSlot, fire rule -> bFireHeld. Idle aim follows the path.
	VeloxBotCombatGlue::Act(*this, Combat, Stream, State.Nav, State.View, State.Pending, DeltaSeconds);
}

void AVeloxBotController::ApplyInput(const FVeloxBotInput& Input, float DeltaSeconds)
{
	State.bShotFiredThisTick = false;
	AVeloxCharacter* Character = GetBotCharacter();
	UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	if (!Character || !Movement)
	{
		return;
	}
	// (1) View: control rotation (pitch included) and the capsule yaw, through the override below.
	State.View = Input.View;
	UpdateControlRotation(DeltaSeconds, true);

	// (2) Wish velocity: the engine scales the input by GetMaxAcceleration exactly as a player's Input_Move.
	const FVector2D Wish = Input.WishDirWorld.GetSafeNormal();
	if (!Wish.IsZero() && Input.WishScale > 0.f)
	{
		Character->AddMovementInput(FVector(Wish, 0.0), FMath::Clamp(Input.WishScale, 0.f, 1.f));
	}
	// (3) Key presence (CPM rules), snapped when air control is on.
	const bool bSnapKeys = Movement->GetSimParams().AirControl > 0.f;
	Movement->SetLocalMoveInput(VeloxBotRules::MoveKeys(Wish, FMath::Clamp(Input.WishScale, 0.f, 1.f), Input.View.Yaw, bSnapKeys, CpmKeySnap));

	// (4) Jump through the character's input handler (edges, like a key).
	if (Input.bJumpHeld != State.bJumpApplied)
	{
		Character->SetJumpInput(Input.bJumpHeld);
		State.bJumpApplied = Input.bJumpHeld;
	}
	// (5) Crouch on change.
	if (Input.bCrouch != State.bCrouchApplied)
	{
		Input.bCrouch ? Character->Crouch() : Character->UnCrouch();
		State.bCrouchApplied = Input.bCrouch;
	}
	// (6) Ability input: weapon slot tap (press + release, UVeloxScriptedInputComponent::SelectWeaponSlot), fire edges,
	// then ProcessAbilityInput after the rotation update and before the pawn moves (B6).
	const AVeloxPlayerState* PS = GetPlayerState<AVeloxPlayerState>();
	UVeloxAbilitySystemComponent* ASC = PS ? PS->GetVeloxAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return;
	}
	if (Input.SelectSlot > 0)
	{
		const FGameplayTag SlotTag = FGameplayTag::RequestGameplayTag(*FString::Printf(TEXT("InputTag.Weapon.Slot%d"), Input.SelectSlot), false);
		if (SlotTag.IsValid())
		{
			ASC->AbilityInputTagPressed(SlotTag);
			ASC->AbilityInputTagReleased(SlotTag);
		}
	}
	if (Input.bFireHeld != State.bFireApplied)
	{
		Input.bFireHeld ? ASC->AbilityInputTagPressed(VeloxGameplayTags::InputTag_Fire) : ASC->AbilityInputTagReleased(VeloxGameplayTags::InputTag_Fire);
		State.bFireApplied = Input.bFireHeld;
	}
	// D14: a fire tick is one on which the fire ability's NextFireTime advanced (a shot was taken), not one with fire held.
	const auto SumNextFireTime = [ASC]()
	{
		double Sum = 0.0;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (const UVeloxGameplayAbility_Fire* Fire = Cast<UVeloxGameplayAbility_Fire>(Spec.GetPrimaryInstance()))
			{
				Sum += Fire->GetNextFireTime();
			}
		}
		return Sum;
	};
	const double NextFireBefore = SumNextFireTime();
	{
		const FVeloxBotCostScope Cost(GetWorld(), EVeloxBotCost::Fire);
		ASC->ProcessAbilityInput(DeltaSeconds, false);
	}
	State.bShotFiredThisTick = SumNextFireTime() != NextFireBefore;
}

void AVeloxBotController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	// No Super (section 1): the engine version would replace the view with the focus point / pawn yaw and zero the pitch.
	APawn* const MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	SetControlRotation(State.View);
	if (bUpdatePawn && !MyPawn->GetActorRotation().Equals(State.View, 1e-3f))
	{
		MyPawn->FaceRotation(State.View, DeltaTime);
	}
}

void AVeloxBotController::Tick(float DeltaSeconds)
{
	const uint64 StartCycles = FPlatformTime::Cycles64();
	const UWorld* const CostWorld = GetWorld();
	{
		// AAIController::Tick ends with UpdateControlRotation: re-applies last tick's view (the override), harmless.
		const FVeloxBotCostScope Cost(CostWorld, EVeloxBotCost::Super);
		Super::Tick(DeltaSeconds);
	}

	AVeloxCharacter* Character = GetBotCharacter();
	if (!Character || Character->IsCorpse() || !HasAuthority())
	{
		return;
	}
	if (IsFrozen())
	{
		FVeloxBotInput Idle;
		Idle.View = State.View;
		ApplyInput(Idle, DeltaSeconds);
		return;
	}

	TickPathQueries = 0;
	{
		SCOPE_CYCLE_COUNTER(STAT_VeloxBots_Think);
		{
			SCOPE_CYCLE_COUNTER(STAT_VeloxBots_Perceive);
			const FVeloxBotCostScope Cost(CostWorld, EVeloxBotCost::Perceive);
			Perceive(DeltaSeconds);
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_VeloxBots_Path);
			{
				const FVeloxBotCostScope Cost(CostWorld, EVeloxBotCost::Decide);
				Decide(DeltaSeconds);
			}
			const FVeloxBotCostScope Cost(CostWorld, EVeloxBotCost::Move);
			EnsureMove(Now());
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_VeloxBots_Steer);
			{
				const FVeloxBotCostScope Cost(CostWorld, EVeloxBotCost::Follow);
				Navigate(DeltaSeconds);
			}
			{
				const FVeloxBotCostScope Cost(CostWorld, EVeloxBotCost::Overlay);
				UpdateCombat(DeltaSeconds);
			}
			const FVeloxBotCostScope Cost(CostWorld, EVeloxBotCost::Act);
			Act(DeltaSeconds);
			// ADR 0008 3.6.3/4: the last word on the wish before it is applied (inert off hazard maps).
			ApplyHazardBehaviours(Now());
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_VeloxBots_Apply);
			const FVeloxBotCostScope Cost(CostWorld, EVeloxBotCost::Apply);
			ApplyInput(State.Pending, DeltaSeconds);
		}
	}

	const double Micros = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles) * 1000.0;
	if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld()))
	{
		// Section 13 / D10: VeloxBotBudget; every FindPathSync of this tick (own MoveTo, router query) is counted.
		Bots->RecordBotTick(*this, Micros, TickPathQueries, State.bShotFiredThisTick);
	}
	State.TickMicrosSum += Micros;
	State.TickMicrosMax = FMath::Max(State.TickMicrosMax, Micros);
	++State.TickCount;
	if (VeloxBotControllerPrivate::CVarBotDebug.GetValueOnGameThread() != 0)
	{
		LogDebugLine(Now());
	}
}

FString AVeloxBotController::DescribeForDebug() const
{
	const AVeloxCharacter* Character = GetBotCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	const UVeloxBotPathFollowingComponent* Follow = GetBotPathFollowing();
	const AVeloxPlayerState* PS = GetPlayerState<AVeloxPlayerState>();
	const UVeloxWeaponDefinition* Weapon = VeloxBotControllerPrivate::CurrentWeapon(PS);
	FString Link = TEXT("none");
	if (Follow && Follow->IsOnLink())
	{
		Link = FString::Printf(TEXT("%s:%s"), VeloxBotControllerPrivate::ManeuverLabel(Follow->GetManeuver().Type), *GetNameSafe(Follow->GetCurrentLink()));
	}
	const AVeloxPickup* Item = State.Goal.Item.Get();
	// ADR 0005 D20: the last evaluation's WeaponStrength (the armed fight share of the soak reads it).
	const UVeloxBotItemRouter* Router = UVeloxBotItemRouter::Get(GetWorld());
	const FVeloxBotRouteRecord* LastEval = Router ? Router->FindLastEvaluation(this) : nullptr;
	return FString::Printf(TEXT("VeloxBot name=%s index=%d life=%d difficulty=%s goal=%s forced=%d arrived=%d item=%s %s weapon=%s wstr=%.2f speed=%.0f ground=%d link=%s filter=%s pos=%s path=%d stuck=%d repaths=%d supp=%d links_ok=%d links_failed=%d tick_us_mean=%.1f tick_us_max=%.1f"),
		*GetBotName(), BotIndex, LifeCount, *GetNameSafe(Difficulty), LexToString(State.Goal.Type), State.bForcedGoal ? 1 : 0, State.bGoalArrived ? 1 : 0,
		Item && Item->GetDefinition() ? *Item->GetDefinition()->GetName() : TEXT("-"), *VeloxBotCombatGlue::Describe(Combat), Weapon ? *Weapon->GetName() : TEXT("-"),
		LastEval ? LastEval->WeaponStrength : -1.f, Movement ? Movement->GetSpeed2D() : 0.f, Movement && Movement->IsOnGround() ? 1 : 0, *Link, VeloxBotControllerPrivate::FilterLabel(GetNavFilter()),
		Character ? *Character->GetActorLocation().ToCompactString() : TEXT("-"), State.bMoveActive ? 1 : 0, State.StuckStrikes, State.Repaths, IsFightSuppressed(Now()) ? 1 : 0, State.LinksOk, State.LinksFailed,
		State.TickCount > 0 ? State.TickMicrosSum / State.TickCount : 0.0, State.TickMicrosMax);
}

void AVeloxBotController::LogDebugLine(double T)
{
	if (T < State.NextDebugTime)
	{
		return;
	}
	State.NextDebugTime = T + DebugIntervalSeconds;
	UE_LOG(LogVeloxBots, Log, TEXT("%s"), *DescribeForDebug());
}
