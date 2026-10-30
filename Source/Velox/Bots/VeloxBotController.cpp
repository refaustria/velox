// Velox - bot controller (ADR 0005 sections 1, 2, 5, 12).

#include "Bots/VeloxBotController.h"

#include "Bots/VeloxBotDifficulty.h"
#include "Bots/VeloxBotItemRouting.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotSettings.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxNavFilters.h"
#include "Bots/VeloxNavLink.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
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
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxBotController)

namespace VeloxBotControllerPrivate
{
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
		if (const APawn* Enemy = State.Goal.Enemy.Get())
		{
			OutTarget = Enemy->GetActorLocation();
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

void AVeloxBotController::Perceive(float /*DeltaSeconds*/)
{
	// Task 05: FOV + LOS perception, damage hearing, enemy memory (section 6).
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

void AVeloxBotController::UpdateCombat(float /*DeltaSeconds*/)
{
	// Task 05: strafe / dodge / range-keeping overlay on State.Pending (section 10).
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

void AVeloxBotController::UpdateRocketJumpState()
{
	UVeloxBotPathFollowingComponent* Follow = GetBotPathFollowing();
	if (!Follow)
	{
		return;
	}
	const AVeloxNavLink* Link = Follow->GetCurrentLink();
	const AVeloxPlayerState* PS = GetPlayerState<AVeloxPlayerState>();
	const UVeloxHealthSet* Health = PS ? PS->GetHealthSet() : nullptr;
	const UVeloxAmmoSet* Ammo = PS ? PS->GetAmmoSet() : nullptr;
	const UVeloxWeaponInventoryComponent* Inventory = PS ? PS->GetWeaponInventory() : nullptr;
	const bool bOwns = VeloxBotControllerPrivate::OwnedWeaponIndex(PS, VeloxGameplayTags::Weapon_RocketLauncher) != INDEX_NONE;
	const bool bEligible = Params.bRocketJumps && bOwns && Ammo && Ammo->GetRockets() >= 1.f && Health && Link && Health->GetHealth() >= Link->GetMinHealthForRocketJump();
	const UVeloxWeaponDefinition* Current = VeloxBotControllerPrivate::CurrentWeapon(PS);
	const bool bSwitchDone = Current && Current->WeaponTag == VeloxGameplayTags::Weapon_RocketLauncher && Inventory && Inventory->GetPendingIndex() == INDEX_NONE &&
		Now() >= Inventory->GetSwitchEndTime();
	Follow->SetRocketJumpState(bEligible, bSwitchDone);
}

void AVeloxBotController::StopMove()
{
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
		if (FVector::Dist2D(Movement->GetActorFeetLocation(), Target) <= Acceptance + GoalMoveToleranceCm && FVector::Dist(Target, State.MoveTarget) <= GoalMoveToleranceCm)
		{
			return;
		}
		State.bGoalArrived = false;
	}
	const TSubclassOf<UNavigationQueryFilter> Filter = GetNavFilter();
	const bool bNeedPath = !State.bMoveActive || Follow->GetStatus() == EPathFollowingStatus::Idle || FVector::Dist(Target, State.MoveTarget) > GoalMoveToleranceCm ||
		Filter != State.MoveFilter;
	if (!bNeedPath || T < State.NextMoveTime || !Movement->IsOnGround())
	{
		// Path queries start from the ground (an airborne start may be far above the mesh).
		return;
	}
	State.NextMoveTime = T + RepathIntervalSeconds;

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
		State.bGoalArrived = true;
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

void AVeloxBotController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);
	if (!State.bMoveActive || RequestID != State.MoveRequestId)
	{
		// An older request replaced by a newer one.
		return;
	}
	State.bMoveActive = false;
	const UPathFollowingComponent* Follow = GetPathFollowingComponent();
	const bool bReachedGoal = Result.IsSuccess() && Follow && Follow->DidMoveReachGoal();
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
		// Missed or aborted on a live path: drop it and re-query from where the bot ends up (on the ground).
		StopMove();
		State.NextMoveTime = T;
	}
}

void AVeloxBotController::Navigate(float DeltaSeconds)
{
	UVeloxBotPathFollowingComponent* Follow = GetBotPathFollowing();
	const AVeloxCharacter* Character = GetBotCharacter();
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	State.Pending = FVeloxBotInput();
	State.Pending.View = State.View;
	if (!Follow || !Movement)
	{
		State.Nav = FVeloxBotNavOutput();
		return;
	}
	UpdateRocketJumpState();
	Follow->StepFollow(DeltaSeconds);
	State.Nav = Follow->GetNavOutput();
	State.Pending.WishDirWorld = State.Nav.WishDir;
	State.Pending.WishScale = State.Nav.WishScale;
	State.Pending.bJumpHeld = State.Nav.bJumpHeld;

	// Stuck (section 5): a path is being followed (not on a link, not arrived) and the bot barely moves.
	const double T = Now();
	const bool bTracking = State.bMoveActive && !State.bGoalArrived && !Follow->IsOnLink() && Follow->GetStatus() == EPathFollowingStatus::Moving;
	if (VeloxBotRules::UpdateStuck(State.SlowSince, bTracking, Movement->GetSpeed2D(), StuckSpeedCmS, T, UVeloxBotSettings::Get().StuckSeconds))
	{
		++State.StuckStrikes;
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotStuck bot=%s at=%s goal=%s strikes=%d"), *GetBotName(), *Character->GetActorLocation().ToCompactString(), LexToString(State.Goal.Type),
			State.StuckStrikes);
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
}

void AVeloxBotController::Act(float DeltaSeconds)
{
	// Idle aim (task 05 replaces it with the aim model): look along the path, level, at the preset's turn rate.
	FRotator Desired = State.View;
	if (!State.Nav.PathDir.IsNearlyZero())
	{
		Desired = FRotator(0.f, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(State.Nav.PathDir.Y, State.Nav.PathDir.X))), 0.f);
	}
	const AVeloxCharacter* Character = GetBotCharacter();
	if (State.Nav.bFaceEnd && Character)
	{
		const FVector ToEnd = State.Nav.FaceTarget - Character->GetActorLocation();
		Desired.Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToEnd.Y, ToEnd.X)));
		Desired.Pitch = 0.f;
	}
	State.Pending.View = VeloxBotRules::TurnToward(State.View, Desired, Params.MaxTurnRateDegPerSec, DeltaSeconds);
	if (State.Nav.bForcePitch)
	{
		// The rocket-jump tick: pitch set in one tick (Q3 BotRocketJump), yaw already facing the end.
		State.Pending.View.Pitch = State.Nav.PitchDeg;
	}
	State.Pending.bFireHeld = State.Nav.bFire;
	if (State.Nav.bWantRocketLauncher)
	{
		const AVeloxPlayerState* PS = GetPlayerState<AVeloxPlayerState>();
		const UVeloxWeaponInventoryComponent* Inventory = PS ? PS->GetWeaponInventory() : nullptr;
		const int32 Index = VeloxBotControllerPrivate::OwnedWeaponIndex(PS, VeloxGameplayTags::Weapon_RocketLauncher);
		const UVeloxWeaponDefinition* Launcher = Inventory ? Inventory->GetDefinition(Index) : nullptr;
		if (Launcher && Inventory->GetCurrentIndex() != Index && Inventory->GetPendingIndex() != Index)
		{
			State.Pending.SelectSlot = Launcher->Slot;
		}
	}
}

void AVeloxBotController::ApplyInput(const FVeloxBotInput& Input, float DeltaSeconds)
{
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
	ASC->ProcessAbilityInput(DeltaSeconds, false);
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
	// AAIController::Tick ends with UpdateControlRotation: re-applies last tick's view (the override), harmless.
	Super::Tick(DeltaSeconds);

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

	Perceive(DeltaSeconds);
	Decide(DeltaSeconds);
	EnsureMove(Now());
	Navigate(DeltaSeconds);
	UpdateCombat(DeltaSeconds);
	Act(DeltaSeconds);
	ApplyInput(State.Pending, DeltaSeconds);

	const double Micros = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles) * 1000.0;
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
	return FString::Printf(TEXT("VeloxBot name=%s index=%d life=%d difficulty=%s goal=%s forced=%d arrived=%d item=%s enemy=%s vis=0 weapon=%s speed=%.0f ground=%d link=%s filter=%s pos=%s path=%d stuck=%d links_ok=%d links_failed=%d tick_us_mean=%.1f tick_us_max=%.1f"),
		*GetBotName(), BotIndex, LifeCount, *GetNameSafe(Difficulty), LexToString(State.Goal.Type), State.bForcedGoal ? 1 : 0, State.bGoalArrived ? 1 : 0,
		Item && Item->GetDefinition() ? *Item->GetDefinition()->GetName() : TEXT("-"), *GetNameSafe(State.Goal.Enemy.Get()), Weapon ? *Weapon->GetName() : TEXT("-"),
		Movement ? Movement->GetSpeed2D() : 0.f, Movement && Movement->IsOnGround() ? 1 : 0, *Link, VeloxBotControllerPrivate::FilterLabel(GetNavFilter()),
		Character ? *Character->GetActorLocation().ToCompactString() : TEXT("-"), State.bMoveActive ? 1 : 0, State.StuckStrikes, State.LinksOk, State.LinksFailed,
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
