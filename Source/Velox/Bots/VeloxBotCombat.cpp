// Velox - bot combat (ADR 0005 sections 6, 8, 9, 10; task M4-05).

#include "Bots/VeloxBotCombat.h"

#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotCoreTypes.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotPathFollowingComponent.h"
#include "Bots/VeloxBotCost.h"
#include "Bots/VeloxBotSettings.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxBotItemRouting.h"
#include "Bots/VeloxWeaponChoice.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxHealthComponent.h"
#include "Combat/VeloxHealthSet.h"
#include "Combat/VeloxHazardSubsystem.h"
#include "Combat/VeloxHazardVolume.h"
#include "Core/VeloxCollisionChannels.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Movement/VeloxMovementComponent.h"
#include "Weapons/VeloxProjectile.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxBotCombat)

namespace VeloxBotCombatPrivate
{
	TAutoConsoleVariable<int32> CVarBotEdgeAware(TEXT("velox.bots.EdgeAware"), 1,
		TEXT("0 = the ADR 0008 3.6 edge behaviours (edge-aware overlay, hop landing guard) stay off even on maps with a hazard volume (A/B for FtBotEdge). Server."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarBotEdgeGuard(TEXT("velox.bots.EdgeGuard"), 1,
		TEXT("0 = the task M7 H13 void-edge additions stay off (ground edge guard, void back-off of the overlay, airborne hop guard, own-hop void recovery); the rest of the ADR 0008 3.6 behaviours stay (A/B). Server."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarBotPeaceful(TEXT("velox.bots.Peaceful"), 0,
		TEXT("1 = bots perceive no enemies and ignore damage (navigation functional tests; task 05). Server."), ECVF_Default);
}

const UVeloxBotCombatSettings& UVeloxBotCombatSettings::Get()
{
	return *GetDefault<UVeloxBotCombatSettings>();
}

// ------------------------------------------------------------------------------------------------ world-free rules

namespace VeloxBotCombat
{
	EVeloxFireVerdict EvaluateFire(const FVeloxFireInput& In, const FVeloxBotDifficultyParams& Difficulty)
	{
		if (!In.bHasWeapon)
		{
			return EVeloxFireVerdict::NoWeapon;
		}
		if (!In.bWeaponReady)
		{
			return EVeloxFireVerdict::NotReady;
		}
		if (!In.bHasAmmo)
		{
			return EVeloxFireVerdict::NoAmmo;
		}
		// D50: a held trigger stays down FireHoldAfterLostSeconds after the target left sight (TargetCentre = last seen).
		// D51 (2)(a): automatic weapons only; a single-shot weapon's next shot is an aimed one.
		const bool bHeldLost = In.bAutomaticWeapon && !In.bTargetVisible && In.bTriggerHeld && Difficulty.FireHoldAfterLostSeconds > 0.f
			&& In.SecondsSinceTargetSeen <= Difficulty.FireHoldAfterLostSeconds;
		if (!In.bTargetVisible && !bHeldLost)
		{
			return EVeloxFireVerdict::NoTarget;
		}
		const double Range = FVector::Dist(In.Eye, In.TargetCentre);
		if (In.MaxRangeCm > 0.f && (Range < In.MinRangeCm || Range > In.MaxRangeCm))
		{
			return EVeloxFireVerdict::OutOfRange;
		}
		const bool bSplash = In.SplashRadiusCm > 0.f;
		if (bSplash)
		{
			// No self-splash danger: a wall in the face or a target inside the own blast radius.
			const double Safe = In.SplashRadiusCm + In.SelfSplashMarginCm;
			if (Range <= Safe || (In.bAimHit && FVector::Dist(In.Eye, In.AimHitPoint) <= Safe))
			{
				return EVeloxFireVerdict::SelfSplash;
			}
		}
		if (bHeldLost)
		{
			// Firing through the corner just left: no aim test (the aim model turns toward the last seen location).
			return EVeloxFireVerdict::FireHeld;
		}
		const FVector ToDesired = (In.DesiredPoint - In.Eye).GetSafeNormal();
		const FVector Aim = In.AimDir.GetSafeNormal();
		const double CosAngle = FMath::Clamp(FVector::DotProduct(ToDesired, Aim), -1.0, 1.0);
		const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(CosAngle));
		bool bInCone = !ToDesired.IsZero() && AngleDeg <= Difficulty.FireConeDeg;
		// D50/D51: the per-preset gate scale applies to automatic weapons only (0 = cone only); single-shot weapons keep D14's radius.
		const float Radius = In.DirectHitRadiusCm * (In.bAutomaticWeapon ? FMath::Max(Difficulty.DirectHitRadiusScale, 0.f) : 1.f);
		if (bInCone && !bSplash && Radius > 0.f)
		{
			// Q3 BotCheckAttack: the shot would actually hit (the ray passes the desired point within the hit radius).
			const FVector ToPoint = In.DesiredPoint - In.Eye;
			const double Along = FVector::DotProduct(ToPoint, Aim);
			bInCone = Along > 0.0 && (ToPoint - Aim * Along).Size() <= Radius;
		}
		const bool bGroundHit = bSplash && In.bAimHit && FVector::Dist(In.AimHitPoint, In.TargetCentre) <= In.SplashRadiusCm * In.SplashHitFraction;
		if (bInCone || bGroundHit)
		{
			return EVeloxFireVerdict::Fire;
		}
		// D50 trigger hysteresis: a held trigger sweeps across the target inside the release cone, without the gate.
		// D51 (2)(a), (8): automatic weapons only, and FireReleaseConeScale <= 1 means no hysteresis.
		if (In.bAutomaticWeapon && In.bTriggerHeld && Difficulty.FireReleaseConeScale > 1.f && !ToDesired.IsZero() && AngleDeg <= Difficulty.FireConeDeg * Difficulty.FireReleaseConeScale)
		{
			return EVeloxFireVerdict::FireHeld;
		}
		return EVeloxFireVerdict::OffTarget;
	}

	bool ShouldFire(const FVeloxFireInput& In, const FVeloxBotDifficultyParams& Difficulty)
	{
		const EVeloxFireVerdict Verdict = EvaluateFire(In, Difficulty);
		return Verdict == EVeloxFireVerdict::Fire || Verdict == EVeloxFireVerdict::FireHeld;
	}

	bool InFov(const FVector& ViewDir, const FVector& Eye, const FVector& Target, float FovDeg)
	{
		if (FovDeg >= 360.f)
		{
			return true;
		}
		const FVector To = (Target - Eye).GetSafeNormal();
		const FVector View = ViewDir.GetSafeNormal();
		if (To.IsZero() || View.IsZero())
		{
			return true;
		}
		const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(To, View), -1.0, 1.0)));
		return Angle <= FovDeg * 0.5;
	}

	int32 SelectTarget(TConstArrayView<FVeloxTargetCandidate> Candidates, int32 CurrentId, float Stickiness)
	{
		int32 Nearest = INDEX_NONE;
		int32 Current = INDEX_NONE;
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			const FVeloxTargetCandidate& C = Candidates[i];
			if (!C.bVisible)
			{
				continue;
			}
			if (C.Id == CurrentId && CurrentId != INDEX_NONE)
			{
				Current = i;
			}
			if (Nearest == INDEX_NONE || C.DistanceCm < Candidates[Nearest].DistanceCm)
			{
				Nearest = i;
			}
		}
		if (Current == INDEX_NONE || Nearest == Current)
		{
			return Nearest;
		}
		const float Sticky = FMath::Max(1.f, Stickiness);
		return Candidates[Nearest].DistanceCm < Candidates[Current].DistanceCm / Sticky ? Nearest : Current;
	}

	FVector2D CombatWish(const FVeloxCombatWishInput& In)
	{
		FVector2D Wish = FVector2D::ZeroVector;
		const double Dist = In.ToEnemy.Size();
		const FVector2D Dir = Dist > UE_KINDA_SMALL_NUMBER ? In.ToEnemy / Dist : FVector2D::ZeroVector;
		if (In.bNoLateralOrRetreat)
		{
			// ADR 0008 3.6.1 edge fallback: no strafe and no dodge this tick.
		}
		else if (In.bDodge && !In.DodgeDir.IsNearlyZero())
		{
			Wish += In.DodgeDir.GetSafeNormal();
		}
		else if (!Dir.IsZero())
		{
			// Perp: +1 = left of the enemy direction (counter-clockwise in the XY plane).
			Wish += FVector2D(-Dir.Y, Dir.X) * (In.StrafeSign >= 0.f ? 1.0 : -1.0) * In.StrafeAmplitude;
		}
		if (!Dir.IsZero())
		{
			if (Dist > In.LeashCm)
			{
				Wish += Dir;
			}
			else if (In.BestMaxCm > 0.f && Dist > In.BestMaxCm)
			{
				Wish += Dir * In.RangeWeight;
			}
			else if (In.BestMaxCm > 0.f && Dist < In.BestMinCm && !In.bNoLateralOrRetreat)
			{
				Wish -= Dir * In.RangeWeight;
			}
		}
		if (In.bBlendPath && !In.PathWish.IsNearlyZero())
		{
			Wish += In.PathWish.GetSafeNormal() * In.PathWeight;
		}
		return Wish.GetSafeNormal();
	}

	FVector EdgeProbePoint(const FVector& Feet, const FVector2D& Velocity2D, const FVector2D& Wish, float LookaheadSeconds, float ProbeCm)
	{
		const FVector2D Offset = Velocity2D * FMath::Max(0.f, LookaheadSeconds) + Wish * FMath::Max(0.f, ProbeCm);
		return Feet + FVector(Offset, 0.0);
	}

	EVeloxEdgeChoice ChooseEdgeWish(bool bWishOnNav, bool bMirrorOnNav)
	{
		return bWishOnNav ? EVeloxEdgeChoice::Keep : (bMirrorOnNav ? EVeloxEdgeChoice::Mirror : EVeloxEdgeChoice::Drop);
	}

	FVector HopLandingPoint(const FVector& Feet, const FVector2D& Velocity2D, float AirSeconds)
	{
		return Feet + FVector(Velocity2D * FMath::Max(0.f, AirSeconds), 0.0);
	}

	void HopGroundWindow(double LandingZ, float ApexCm, float Distance2D, float WalkableFloorZ, double& OutTopZ, double& OutBottomZ)
	{
		const double FloorZ = FMath::Clamp(static_cast<double>(WalkableFloorZ), 0.05, 1.0);
		const double TanSlope = FMath::Sqrt(FMath::Max(0.0, 1.0 - FloorZ * FloorZ)) / FloorZ;
		OutTopZ = LandingZ + FMath::Max(0.f, ApexCm);
		// At least 1 cm below the take-off height, so flat ground at exactly LandingZ is inside the window.
		OutBottomZ = LandingZ - FMath::Max(1.0, FMath::Max(0.f, Distance2D) * TanSlope);
	}

	bool IsHeardDamage(const FGameplayTagContainer& DamageTags)
	{
		return !DamageTags.HasTag(VeloxGameplayTags::Damage_Hazard);
	}

	FVeloxProjectileThreat ProjectileThreat(const FVector& Location, const FVector& Velocity, const FVector& Point, float DangerRadiusCm, float HorizonSeconds)
	{
		FVeloxProjectileThreat Out;
		const double SpeedSq = Velocity.SizeSquared();
		if (SpeedSq <= UE_KINDA_SMALL_NUMBER)
		{
			return Out;
		}
		const double T = FMath::Clamp(FVector::DotProduct(Point - Location, Velocity) / SpeedSq, 0.0, static_cast<double>(HorizonSeconds));
		const FVector Closest = Location + Velocity * T;
		Out.TimeToClosest = static_cast<float>(T);
		Out.ClosestDistanceCm = static_cast<float>(FVector::Dist(Closest, Point));
		// Only threats still ahead: a projectile already past the point (T clamped to 0 while moving away) is ignored.
		const bool bAhead = FVector::DotProduct(Point - Location, Velocity) > 0.0;
		Out.bThreat = bAhead && Out.ClosestDistanceCm <= DangerRadiusCm;
		const FVector2D V2 = FVector2D(Velocity).GetSafeNormal();
		FVector2D Away = FVector2D(Point - Closest);
		// Perpendicular to the flight in the XY plane, toward the side the point is on (either side when dead on).
		const FVector2D Perp(-V2.Y, V2.X);
		const double Side = FVector2D::DotProduct(Away, Perp);
		Out.AwayDir = V2.IsZero() ? FVector2D::ZeroVector : Perp * (Side >= 0.0 ? 1.0 : -1.0);
		return Out;
	}

	bool RollChance(float ChancePerSecond, float Dt, FRandomStream& Stream)
	{
		const float P = FMath::Clamp(ChancePerSecond * Dt, 0.f, 1.f);
		return P > 0.f && Stream.FRand() < P;
	}
}

namespace VeloxBotRocketJump
{
	FVeloxBallisticArrive ArriveWish(const FVector& Feet, const FVector& Velocity, const FVector& Target, float GravityZ, float DeadbandCmS)
	{
		FVeloxBallisticArrive Out;
		const FVector2D ToTarget(Target - Feet);
		const double G = -static_cast<double>(GravityZ);
		// z(t) = Feet.Z + Vz t - G/2 t^2 = Target.Z  ->  G/2 t^2 - Vz t + dZ = 0, dZ = Target.Z - Feet.Z; descending root.
		const double DZ = Target.Z - Feet.Z;
		const double Disc = Velocity.Z * Velocity.Z - 2.0 * G * DZ;
		if (G <= UE_KINDA_SMALL_NUMBER || Disc < 0.0)
		{
			Out.WishDir = ToTarget.GetSafeNormal();
			return Out;
		}
		const double T = (Velocity.Z + FMath::Sqrt(Disc)) / G;
		if (T <= UE_KINDA_SMALL_NUMBER)
		{
			Out.WishDir = ToTarget.GetSafeNormal();
			return Out;
		}
		Out.TimeToTarget = static_cast<float>(T);
		Out.NeededVelocity = ToTarget / T;
		const FVector2D Error = Out.NeededVelocity - FVector2D(Velocity);
		Out.WishDir = Error.Size() > DeadbandCmS ? Error.GetSafeNormal() : FVector2D::ZeroVector;
		return Out;
	}
}

// ----------------------------------------------------------------------------------------------------- glue

namespace VeloxBotCombatGlueImpl
{
	AVeloxCharacter* PawnOf(AActor* Actor)
	{
		if (AVeloxCharacter* Character = Cast<AVeloxCharacter>(Actor))
		{
			return Character;
		}
		if (const APlayerState* PS = Cast<APlayerState>(Actor))
		{
			return Cast<AVeloxCharacter>(PS->GetPawn());
		}
		if (const AController* Controller = Cast<AController>(Actor))
		{
			return Cast<AVeloxCharacter>(Controller->GetPawn());
		}
		if (const AActor* Owner = Actor ? Actor->GetOwner() : nullptr)
		{
			return Cast<AVeloxCharacter>(const_cast<AActor*>(Owner));
		}
		return nullptr;
	}

	int32 EnemyIdOf(const AVeloxCharacter& Pawn)
	{
		const APlayerState* PS = Pawn.GetPlayerState();
		return PS ? PS->GetPlayerId() : static_cast<int32>(Pawn.GetUniqueID());
	}

	FVeloxBotEnemyInfo& FindOrAddEnemy(FVeloxBotCombatState& Combat, AVeloxCharacter& Pawn)
	{
		const int32 Id = EnemyIdOf(Pawn);
		if (FVeloxBotEnemyInfo* Found = Combat.Enemies.FindByPredicate([Id](const FVeloxBotEnemyInfo& E) { return E.Id == Id; }))
		{
			Found->Pawn = &Pawn;
			return *Found;
		}
		FVeloxBotEnemyInfo& Info = Combat.Enemies.AddDefaulted_GetRef();
		Info.Pawn = &Pawn;
		Info.Id = Id;
		return Info;
	}


	double WorldTime(const UWorld* World)
	{
		return World ? World->GetTimeSeconds() : 0.0;
	}

	const UVeloxWeaponDefinition* HeldDefinition(const UVeloxWeaponInventoryComponent* Inventory)
	{
		if (!Inventory)
		{
			return nullptr;
		}
		const int32 Pending = Inventory->GetPendingIndex();
		return Pending != INDEX_NONE ? Inventory->GetDefinition(Pending) : Inventory->GetCurrentDefinition();
	}

	const FVeloxBotWeaponPreference* FindPreference(const FVeloxBotDifficultyParams& Params, const UVeloxWeaponDefinition* Definition)
	{
		if (!Definition)
		{
			return nullptr;
		}
		return Params.WeaponPreferences.FindByPredicate([Definition](const FVeloxBotWeaponPreference& P) { return P.Weapon == Definition->WeaponTag; });
	}

	/** Current ammo of the definition's ammo type and whether one shot is affordable (weapons without ammo: always). */
	bool ReadAmmo(const UVeloxWeaponInventoryComponent& Inventory, const UAbilitySystemComponent* ASC, const UVeloxWeaponDefinition& Definition, int32& OutAmmo)
	{
		const UVeloxCombatSettings* Settings = Inventory.GetCombatSettings();
		const FGameplayAttribute Attribute = Settings && Definition.AmmoTag.IsValid() ? UVeloxAmmoSet::GetAmmoAttribute(Definition.AmmoTag, *Settings) : FGameplayAttribute();
		if (!Attribute.IsValid() || !ASC || Definition.AmmoPerShot <= 0)
		{
			OutAmmo = TNumericLimits<int32>::Max() / 2;
			return true;
		}
		const float Value = ASC->GetNumericAttribute(Attribute);
		OutAmmo = FMath::FloorToInt32(Value + 0.5f);
		return UVeloxAmmoSet::CanAfford(Value, Definition.AmmoPerShot);
	}

	void HandleDamaged(AVeloxBotController& Bot, FVeloxBotCombatState& Combat, FRandomStream& Stream, const FVeloxDamageEvent& Event)
	{
		if (Event.DamageTags.HasTag(VeloxGameplayTags::Damage_Hazard_Void))
		{
			// Task M7 H13: soak diagnostics, the take-off that ended in the void.
			Bot.NoteVoidDeath();
		}
		if (!VeloxBotCombat::IsHeardDamage(Event.DamageTags))
		{
			// H8 (review F8): a credited lava tick names the attacker as instigator; it gives away no position.
			return;
		}
		AVeloxCharacter* Attacker = PawnOf(Event.Instigator.Get());
		if (!Attacker)
		{
			Attacker = PawnOf(Event.Causer.Get());
		}
		if (!Attacker || Attacker == Bot.GetPawn() || Attacker->IsCorpse() || VeloxBotCombatPrivate::CVarBotPeaceful.GetValueOnGameThread() != 0)
		{
			// Self damage (own rocket) or a world source: nothing to turn to.
			return;
		}
		const double Now = WorldTime(Bot.GetWorld());
		FVeloxBotEnemyInfo& Info = FindOrAddEnemy(Combat, *Attacker);
		Info.HeardTime = Now;
		if (!Info.bVisible)
		{
			Info.LastSeenLocation = Attacker->GetActorLocation();
			Info.LastSeenVelocity = Attacker->GetVelocity();
		}
		if (Combat.HasVisibleTarget())
		{
			return;
		}
		// Arm the reaction once per attacker (a stream of hits does not keep postponing the turn).
		if (Combat.HeardId != Info.Id || Now > Combat.HeardReactTime + UVeloxBotCombatSettings::Get().UnderFireSeconds)
		{
			const FVeloxAimParams& Aim = Combat.AimParams;
			const float Jitter = 1.f + Aim.ReactionJitter * (2.f * Stream.FRand() - 1.f);
			Combat.HeardId = Info.Id;
			Combat.HeardReactTime = Now + Aim.ReactionSeconds * Jitter;
			++Combat.HeardCount;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotHear bot=%s from=%s react_in=%.3f"), *Bot.GetBotName(), *Attacker->GetName(), Combat.HeardReactTime - Now);
		}
	}

	void ResetForLife(AVeloxBotController& Bot, FVeloxBotCombatState& Combat, FRandomStream& Stream, AVeloxCharacter& Self, double Now)
	{
		VeloxBotCombatGlue::Unbind(Combat);
		Combat = FVeloxBotCombatState();
		Combat.LifeCount = Bot.GetLifeCount();
		Combat.BoundPawn = &Self;
		const UVeloxBotCombatSettings& Settings = UVeloxBotCombatSettings::Get();
		const FVeloxBotDifficultyParams& Params = Bot.GetDifficultyParams();
		Combat.AimParams = FVeloxAimModel::MakeParams(Params);
		FVeloxAimModel::Reset(Combat.Aim, Bot.GetBotState().View);
		// Stagger (section 6): BotIndex x one tick inside the interval, so 16 bots spread their updates.
		const float Interval = FMath::Max(Params.PerceptionIntervalSeconds, UE_KINDA_SMALL_NUMBER);
		Combat.NextPerceiveTime = Now + FMath::Fmod(FMath::Max(0, Bot.GetBotIndex()) * Settings.PerceptionStaggerSeconds, Interval);
		Combat.StrafeSign = Stream.FRand() < 0.5f ? -1.f : 1.f;
		if (UVeloxHealthComponent* Health = Self.GetHealthComponent())
		{
			FVeloxBotCombatState* CombatPtr = &Combat;
			AVeloxBotController* BotPtr = &Bot;
			// The stream is the controller's member (same lifetime as Combat; the weak lambda dies with the controller).
			FRandomStream* StreamPtr = &Stream;
			Combat.DamagedHandle = Health->OnDamaged.AddWeakLambda(&Bot, [BotPtr, CombatPtr, StreamPtr](const FVeloxDamageEvent& Event) { HandleDamaged(*BotPtr, *CombatPtr, *StreamPtr, Event); });
		}
	}

	FRotator LookAt(const FVector& Eye, const FVector& Point)
	{
		FRotator R = (Point - Eye).Rotation();
		R.Roll = 0.f;
		return R;
	}
}

namespace VeloxBotCombatGlue
{
	void Unbind(FVeloxBotCombatState& Combat)
	{
		if (AVeloxCharacter* Old = Combat.BoundPawn.Get())
		{
			if (UVeloxHealthComponent* Health = Old->GetHealthComponent())
			{
				Health->OnDamaged.Remove(Combat.DamagedHandle);
			}
		}
		Combat.DamagedHandle.Reset();
		Combat.BoundPawn.Reset();
	}

	void Perceive(AVeloxBotController& Bot, FVeloxBotCombatState& Combat, FRandomStream& Stream, float /*Dt*/)
	{
		AVeloxCharacter* Self = Cast<AVeloxCharacter>(Bot.GetPawn());
		UWorld* World = Bot.GetWorld();
		if (!Self || !World)
		{
			return;
		}
		const double Now = World->GetTimeSeconds();
		if (Combat.LifeCount != Bot.GetLifeCount() || Combat.BoundPawn.Get() != Self)
		{
			VeloxBotCombatGlueImpl::ResetForLife(Bot, Combat, Stream, *Self, Now);
		}
		if (Now + 1e-6 < Combat.NextPerceiveTime)
		{
			return;
		}
		const FVeloxBotDifficultyParams& Params = Bot.GetDifficultyParams();
		const UVeloxBotCombatSettings& Settings = UVeloxBotCombatSettings::Get();
		const float Interval = FMath::Max(Params.PerceptionIntervalSeconds, UE_KINDA_SMALL_NUMBER);
		// Keep the stagger phase: advance by whole intervals.
		while (Combat.NextPerceiveTime <= Now + 1e-6)
		{
			Combat.NextPerceiveTime += Interval;
		}

		const FVector Eye = Self->GetPawnViewLocation();
		const FVector ViewDir = Bot.GetBotState().View.Vector();
		const UVeloxLagCompensationSubsystem* LagComp = UVeloxLagCompensationSubsystem::Get(World);
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(VeloxBotPerception), false, Self);
		int32 Traces = 0;

		for (FVeloxBotEnemyInfo& Info : Combat.Enemies)
		{
			Info.bVisible = false;
		}
		const bool bPeaceful = VeloxBotCombatPrivate::CVarBotPeaceful.GetValueOnGameThread() != 0;
		if (const AGameStateBase* GameState = bPeaceful ? nullptr : World->GetGameState())
		{
			for (const APlayerState* PS : GameState->PlayerArray)
			{
				AVeloxCharacter* Other = PS ? Cast<AVeloxCharacter>(PS->GetPawn()) : nullptr;
				// Candidates = living, hittable pawns (the lag-comp registry), minus self.
				if (!Other || Other == Self || Other->IsCorpse() || (LagComp && !LagComp->IsRegistered(Other)))
				{
					continue;
				}
				const FVector Centre = Other->GetActorLocation();
				const double Dist = FVector::Dist(Eye, Centre);
				FVeloxBotEnemyInfo* Info = Combat.Enemies.FindByPredicate([Id = PS->GetPlayerId()](const FVeloxBotEnemyInfo& E) { return E.Id == Id; });
				if (Info)
				{
					Info->Pawn = Other;
					Info->DistanceCm = static_cast<float>(Dist);
				}
				if (Dist > Params.PerceptionRangeCm || !VeloxBotCombat::InFov(ViewDir, Eye, Centre, Params.PerceptionFovDeg))
				{
					continue;
				}
				// World-only LOS: pawn capsules ignore the Weapon channel, so other players never block sight.
				FCollisionQueryParams Query = TraceParams;
				Query.AddIgnoredActor(Other);
				bool bClear = false;
				{
					const FVeloxBotCostScope Cost(World, EVeloxBotCost::PerceiveTraces);
					++Traces;
					bClear = !World->LineTraceTestByChannel(Eye, Centre, Velox_TraceChannel_Weapon, Query);
					if (!bClear)
					{
						++Traces;
						bClear = !World->LineTraceTestByChannel(Eye, Centre + FVector(0.0, 0.0, Settings.HeadOffsetCm), Velox_TraceChannel_Weapon, Query);
					}
				}
				if (!bClear)
				{
					continue;
				}
				FVeloxBotEnemyInfo& Seen = Info ? *Info : VeloxBotCombatGlueImpl::FindOrAddEnemy(Combat, *Other);
				Seen.bVisible = true;
				Seen.DistanceCm = static_cast<float>(Dist);
				Seen.LastSeenTime = Now;
				Seen.LastSeenLocation = Centre;
				Seen.LastSeenVelocity = Other->GetVelocity();
			}
		}

		// Forget the dead and the long unseen (memory, section 6).
		Combat.Enemies.RemoveAll([&](const FVeloxBotEnemyInfo& E)
		{
			const AVeloxCharacter* P = E.Pawn.Get();
			const double Last = FMath::Max(E.LastSeenTime, E.HeardTime);
			return !P || P->IsCorpse() || (!E.bVisible && Now - Last > Params.EnemyMemorySeconds);
		});

		// Target: the nearest visible, sticky; else the remembered one while it is in memory.
		TArray<FVeloxTargetCandidate, TInlineAllocator<16>> Candidates;
		for (const FVeloxBotEnemyInfo& E : Combat.Enemies)
		{
			Candidates.Add({E.Id, E.bVisible, E.DistanceCm});
		}
		const int32 Pick = VeloxBotCombat::SelectTarget(Candidates, Combat.TargetId, Settings.TargetStickiness);
		if (Pick != INDEX_NONE)
		{
			Combat.TargetId = Candidates[Pick].Id;
			Combat.HeardId = INDEX_NONE;
		}
		else if (!Combat.GetTarget())
		{
			Combat.TargetId = INDEX_NONE;
		}
		if (Combat.HeardId != INDEX_NONE && !Combat.FindEnemy(Combat.HeardId))
		{
			Combat.HeardId = INDEX_NONE;
		}

		// Projectile dodge (Hard+, section 10): the soonest threatening projectile that is not our own.
		if (Params.bDodgeProjectiles && Combat.ProjectileDodgeAt < 0.0 && Now >= Combat.ProjectileDodgeUntil)
		{
			if (const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World))
			{
				const FVector Point = Self->GetActorLocation();
				float BestTime = TNumericLimits<float>::Max();
				for (const AVeloxProjectile* Projectile : Bots->GetProjectiles())
				{
					const UVeloxWeaponDefinition* Definition = Projectile->GetDefinition();
					if (!Definition || Projectile->IsFake() || Projectile->HasExploded() || Projectile->GetOwner() == Self)
					{
						continue;
					}
					const FVeloxProjectileThreat Threat = VeloxBotCombat::ProjectileThreat(Projectile->GetActorLocation(), Projectile->GetLaunchVelocity(), Point,
						Definition->SplashRadiusCm + Settings.ProjectileDodgeMarginCm, Settings.ProjectileDodgeHorizonSeconds);
					if (Threat.bThreat && Threat.TimeToClosest < BestTime)
					{
						BestTime = Threat.TimeToClosest;
						Combat.ProjectileDodgeDir = Threat.AwayDir;
						Combat.ProjectileDodgeAt = Now + Params.ProjectileDodgeReactionSeconds;
						Combat.ProjectileDodgeName = Projectile->GetFName();
						Combat.ProjectileDodgeTClosest = Threat.TimeToClosest;
						Combat.ProjectileDodgeDist = Threat.ClosestDistanceCm;
					}
				}
			}
		}

		Combat.TracesTotal += Traces;
		if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World))
		{
			Bots->NoteTraces(Traces);
		}
	}

	bool IsEdgeAware(const UWorld* World)
	{
		const UVeloxHazardSubsystem* Hazards = World ? UVeloxHazardSubsystem::Get(World) : nullptr;
		return Hazards && Hazards->HasHazards() && VeloxBotCombatPrivate::CVarBotEdgeAware.GetValueOnGameThread() != 0;
	}

	bool IsEdgeGuardOn(const UWorld* World)
	{
		return IsEdgeAware(World) && VeloxBotCombatPrivate::CVarBotEdgeGuard.GetValueOnGameThread() != 0;
	}

	bool IsEdgeAwareEnabled()
	{
		return VeloxBotCombatPrivate::CVarBotEdgeAware.GetValueOnGameThread() != 0;
	}

	bool ProjectsOntoNav(const AVeloxBotController& Bot, const FVector& Point, const FVector& Extent, bool* bOutHazard)
	{
		if (bOutHazard)
		{
			*bOutHazard = false;
		}
		UWorld* World = Bot.GetWorld();
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		const ANavigationData* NavData = Bots ? Bots->GetNavData() : nullptr;
		if (!NavSys || !NavData)
		{
			// No nav data: nothing to judge against (never blocks a wish).
			return true;
		}
		FNavLocation Projected;
		const bool bProjected = NavSys->ProjectPointToNavigation(Point, Projected, Extent, NavData,
			UNavigationQueryFilter::GetQueryFilter(*NavData, const_cast<AVeloxBotController*>(&Bot), Bot.GetNavFilter()));
		// Task M7 H7: a point over a hazard volume is no safe ground for a wish or a hop, whether or not it projects. The
		// projection may land on the bed's mesh in the volume, or on the rim next to the channel (and an airborne probe is far
		// above the bed), so the static surface straight under the probe is tested too: a bridge over lava is safe, the lava
		// top or its bed is not.
		const UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(World);
		if (!Hazards || !Hazards->HasHazards())
		{
			return bProjected;
		}
		bool bHazard = bProjected && Hazards->IsInHazard(Projected.Location + FVector(0.0, 0.0, Bot.HazardProbeUpCm), EVeloxHazardType::Lava);
		if (!bHazard)
		{
			// Task M7 H8 (review F13): the floor under the point is the first Pawn-blocking surface (overlap-only triggers, the
			// hazard boxes and the void slab do not block; pawns are ignored); it is lava when 1 cm above it lies in a Lava
			// volume (the bed). Void never counts here: void ledges keep the H4 projection result.
			FHitResult Hit;
			const FVector Down = Point - FVector(0.0, 0.0, UVeloxBotSettings::Get().HazardTraceDepthCm);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(VeloxBotHazardProbe), false, Bot.GetPawn());
			FCollisionResponseParams Response;
			Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
			bHazard = World->LineTraceSingleByChannel(Hit, Point, Down, ECC_Pawn, Params, Response) &&
				Hazards->IsInHazard(Hit.ImpactPoint + FVector(0.0, 0.0, 1.0), EVeloxHazardType::Lava);
		}
		if (bOutHazard)
		{
			*bOutHazard = bHazard;
		}
		return bProjected && !bHazard;
	}

	bool FindHopLandingGround(const AVeloxBotController& Bot, const FVector& Landing, float ApexCm, float Distance2D, float WalkableFloorZ, FVector& OutGround)
	{
		UWorld* World = Bot.GetWorld();
		if (!World)
		{
			return false;
		}
		double TopZ = 0.0;
		double BottomZ = 0.0;
		VeloxBotCombat::HopGroundWindow(Landing.Z, ApexCm, Distance2D, WalkableFloorZ, TopZ, BottomZ);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VeloxBotHopGround), false, Bot.GetPawn());
		FCollisionResponseParams Response;
		Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
		if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World))
		{
			Bots->NoteTraces(1);
		}
		if (!World->LineTraceSingleByChannel(Hit, FVector(Landing.X, Landing.Y, TopZ), FVector(Landing.X, Landing.Y, BottomZ), ECC_Pawn, Params, Response) || Hit.bStartPenetrating)
		{
			return false;
		}
		OutGround = Hit.ImpactPoint;
		return true;
	}

	bool HopLandsSafely(const AVeloxBotController& Bot, const FVector& Landing, float ApexCm, float Distance2D, float WalkableFloorZ, bool* bOutHazard)
	{
		if (bOutHazard)
		{
			*bOutHazard = false;
		}
		FVector Ground;
		if (!FindHopLandingGround(Bot, Landing, ApexCm, Distance2D, WalkableFloorZ, Ground))
		{
			return false;
		}
		const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
		const FVector Extent(Settings.HopLandingProjectRadiusCm, Settings.HopLandingProjectRadiusCm, Settings.HopLandingProjectHalfHeightCm);
		// 1 cm above the hit: the hazard trace in ProjectsOntoNav must start above the bed to see it.
		return ProjectsOntoNav(Bot, Ground + FVector(0.0, 0.0, 1.0), Extent, bOutHazard);
	}

	bool IsUnsafeGround(const AVeloxBotController& Bot, const FVector& Point)
	{
		const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
		bool bHazard = false;
		if (ProjectsOntoNav(Bot, Point, FVector(Settings.EdgeProbeProjectRadiusCm, Settings.EdgeProbeProjectRadiusCm, Settings.OnMeshHalfHeightCm), &bHazard))
		{
			return false;
		}
		if (bHazard)
		{
			return true;
		}
		UWorld* World = Bot.GetWorld();
		if (!World)
		{
			return false;
		}
		// Off the mesh: a floor within EdgeGuardDropDepthCm under the point (a ledge onto a lower floor, the eroded band of the
		// deck itself) is ground; nothing there is the void.
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VeloxBotEdgeGuard), false, Bot.GetPawn());
		FCollisionResponseParams Response;
		Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
		if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World))
		{
			Bots->NoteTraces(1);
		}
		return !World->LineTraceSingleByChannel(Hit, Point + FVector(0.0, 0.0, 20.0), Point - FVector(0.0, 0.0, Settings.EdgeGuardDropDepthCm), ECC_Pawn, Params, Response);
	}

	void Overlay(const AVeloxBotController& Bot, FVeloxBotCombatState& Combat, FRandomStream& Stream, const FVeloxBotNavOutput& Nav, FVeloxBotInput& Pending, float Dt)
	{
		const AVeloxCharacter* Self = Cast<AVeloxCharacter>(Bot.GetPawn());
		UWorld* World = Bot.GetWorld();
		if (!Self || !World || Nav.bOnLink || Combat.BoundPawn.Get() != Self)
		{
			// Link traversals own the wish (a strafe on a pad or before a rocket jump would miss the link).
			return;
		}
		const double Now = World->GetTimeSeconds();
		const FVeloxBotDifficultyParams& Params = Bot.GetDifficultyParams();
		const UVeloxBotCombatSettings& Settings = UVeloxBotCombatSettings::Get();

		if (Combat.ProjectileDodgeAt >= 0.0 && Now >= Combat.ProjectileDodgeAt)
		{
			Combat.ProjectileDodgeAt = -1.0;
			Combat.ProjectileDodgeUntil = Now + Settings.ProjectileDodgeHoldSeconds;
			Combat.DodgeJumpUntil = Now + Settings.DodgeJumpHoldSeconds;
			++Combat.DodgeCount;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotDodge bot=%s projectile=%s t_closest=%.2f dist=%.0f dir=(%.2f,%.2f)"), *Bot.GetBotName(), *Combat.ProjectileDodgeName.ToString(),
				Combat.ProjectileDodgeTClosest, Combat.ProjectileDodgeDist, Combat.ProjectileDodgeDir.X, Combat.ProjectileDodgeDir.Y);
		}
		const bool bDodging = Now < Combat.ProjectileDodgeUntil;
		const FVeloxBotEnemyInfo* Target = Combat.GetTarget();
		const bool bVisible = Target && Target->bVisible && Target->Pawn.IsValid();
		const FVeloxBotEnemyInfo* Heard = Combat.FindEnemy(Combat.HeardId);
		const bool bUnderFire = !bVisible && Heard && Now >= Combat.HeardReactTime && Now - Heard->HeardTime <= Settings.UnderFireSeconds;
		if (!bVisible && !bUnderFire && !bDodging)
		{
			return;
		}

		const FVector Feet = Self->GetActorLocation();
		FVector EnemyAt = FVector::ZeroVector;
		if (bVisible)
		{
			EnemyAt = Target->Pawn->GetActorLocation();
		}
		else if (bUnderFire)
		{
			EnemyAt = Heard->LastSeenLocation;
		}
		const FVector2D ToEnemy = (bVisible || bUnderFire) ? FVector2D(EnemyAt - Feet) : FVector2D::ZeroVector;

		// Strafe flips at random intervals; each flip probes the new side on the nav mesh (no strafing into pits / walls).
		if (Now >= Combat.NextFlipTime && !ToEnemy.IsNearlyZero())
		{
			Combat.StrafeSign = -Combat.StrafeSign;
			Combat.NextFlipTime = Now + Stream.FRandRange(Params.StrafeFlipSecondsMin, FMath::Max(Params.StrafeFlipSecondsMin, Params.StrafeFlipSecondsMax));
			const FVector2D Dir = ToEnemy.GetSafeNormal();
			const FVector2D Side = FVector2D(-Dir.Y, Dir.X) * Combat.StrafeSign;
			FVector Hit;
			if (UNavigationSystemV1::NavigationRaycast(World, Feet, Feet + FVector(Side * Settings.StrafeProbeCm, 0.0), Hit, Bot.GetNavFilter(), const_cast<AVeloxBotController*>(&Bot)))
			{
				Combat.StrafeSign = -Combat.StrafeSign;
			}
			if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World))
			{
				Bots->NoteNavRaycast();
			}
		}
		if (bVisible && VeloxBotCombat::RollChance(Params.DodgeJumpChancePerSec, Dt, Stream))
		{
			Combat.DodgeJumpUntil = Now + Settings.DodgeJumpHoldSeconds;
		}

		FVeloxCombatWishInput In;
		In.PathWish = Pending.WishDirWorld;
		const EVeloxBotGoal Goal = Bot.GetBotState().Goal.Type;
		// Item goals keep following their path (health need pulls a hurt bot to an item while it shoots); fights without
		// an item path are the overlay alone; under fire without sight the path is kept (section 10: no path change).
		In.bBlendPath = (Goal == EVeloxBotGoal::Item && Bot.GetBotState().bMoveActive) || !bVisible;
		In.PathWeight = Settings.FightPathWeight;
		In.ToEnemy = ToEnemy;
		if (bVisible)
		{
			const AVeloxPlayerState* PS = Bot.GetPlayerState<AVeloxPlayerState>();
			if (const FVeloxBotWeaponPreference* Pref = VeloxBotCombatGlueImpl::FindPreference(Params, VeloxBotCombatGlueImpl::HeldDefinition(PS ? PS->GetWeaponInventory() : nullptr)))
			{
				In.BestMinCm = Pref->BestMin;
				In.BestMaxCm = Pref->BestMax;
			}
		}
		In.LeashCm = Settings.FightLeashCm;
		In.RangeWeight = Settings.RangeKeepWeight;
		In.StrafeSign = Combat.StrafeSign;
		In.StrafeAmplitude = Params.StrafeAmplitude;
		In.bDodge = bDodging;
		In.DodgeDir = Combat.ProjectileDodgeDir;
		FVector2D Wish = VeloxBotCombat::CombatWish(In);
		if (!Wish.IsZero() && IsEdgeAware(World))
		{
			// ADR 0008 3.6.1 edge-aware overlay: probe ahead of the current motion along the wish; cached EdgeProbeCheckSeconds.
			const UVeloxBotSettings& BotSettings = UVeloxBotSettings::Get();
			if (Now >= Combat.NextEdgeProbeTime)
			{
				Combat.NextEdgeProbeTime = Now + BotSettings.EdgeProbeCheckSeconds;
				++Combat.EdgeProbeCount;
				const FVector ProbeFeet = Self->GetVeloxMovementComponent() ? Self->GetVeloxMovementComponent()->GetActorFeetLocation() : Feet;
				const FVector2D Velocity2D(Self->GetVelocity());
				auto OnNav = [&](const FVector2D& W, bool* bHazard = nullptr)
				{
					// H8 (review F6, ADR 0008 G2): the agent radius, not GetOnMeshExtent's 100 cm (that accepts ~55 cm past a ledge).
					return ProjectsOntoNav(Bot, VeloxBotCombat::EdgeProbePoint(ProbeFeet, Velocity2D, W, BotSettings.EdgeProbeLookaheadSeconds, BotSettings.EdgeProbeCm),
						FVector(BotSettings.EdgeProbeProjectRadiusCm, BotSettings.EdgeProbeProjectRadiusCm, BotSettings.OnMeshHalfHeightCm), bHazard);
				};
				const bool bWishOnNav = OnNav(Wish);
				FVeloxCombatWishInput Mirrored = In;
				Mirrored.StrafeSign = -In.StrafeSign;
				Mirrored.DodgeDir = -In.DodgeDir;
				const FVector2D MirrorWish = bWishOnNav ? Wish : VeloxBotCombat::CombatWish(Mirrored);
				const EVeloxEdgeChoice Choice = VeloxBotCombat::ChooseEdgeWish(bWishOnNav, !bWishOnNav && !MirrorWish.IsZero() && OnNav(MirrorWish));
				Combat.bEdgeDrop = Choice == EVeloxEdgeChoice::Drop;
				if (Choice == EVeloxEdgeChoice::Mirror)
				{
					// The other side is the safe one: keep it (the next random flip is probed again within EdgeProbeCheckSeconds).
					++Combat.EdgeMirrorCount;
					Combat.StrafeSign = Mirrored.StrafeSign;
					if (bDodging)
					{
						Combat.ProjectileDodgeDir = Mirrored.DodgeDir;
					}
					In = Mirrored;
				}
				else if (Combat.bEdgeDrop)
				{
					++Combat.EdgeDropCount;
				}
				// H7: what the Drop leaves (closing in, the path blend) is probed too; over lava the overlay backs off.
				Combat.bEdgeHazardBack = false;
				Combat.bEdgeNoWish = false;
				if (Combat.bEdgeDrop)
				{
					FVeloxCombatWishInput Dropped = In;
					Dropped.bNoLateralOrRetreat = true;
					const FVector2D Remaining = VeloxBotCombat::CombatWish(Dropped);
					// H13: and over the void (Orbita: closing in on an enemy across the gap walked the bot off its deck).
					bool bHazard = false;
					const bool bUnsafe = !Remaining.IsZero() && (IsEdgeGuardOn(World)
						? IsUnsafeGround(Bot, VeloxBotCombat::EdgeProbePoint(ProbeFeet, Velocity2D, Remaining, BotSettings.EdgeProbeLookaheadSeconds, BotSettings.EdgeProbeCm))
						: !OnNav(Remaining, &bHazard) && bHazard);
					if (bUnsafe)
					{
						// H8 (review F7): the reversal is probed as well; on a small island it can point at lava or a ledge
						// too, and then the overlay writes nothing (the path wish stays).
						// Task M7 H13: with an item path blended in, the reversal undid the path's progress too (B5b: 6 s
						// dwells in Horreum's corner, the enemy across the gap): the path wish stays instead, and the ground
						// edge guard keeps the momentum off the edge.
						const bool bKeepPath = IsEdgeGuardOn(World) && In.bBlendPath && !Pending.WishDirWorld.IsZero();
						if (!bKeepPath && OnNav(-Remaining))
						{
							Combat.bEdgeHazardBack = true;
							++Combat.EdgeHazardBackCount;
						}
						else
						{
							Combat.bEdgeNoWish = true;
							++Combat.EdgeNoWishCount;
						}
					}
				}
				if (Choice != EVeloxEdgeChoice::Keep)
				{
					UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotEdge bot=%s choice=%s at=%s vel=%s wish=(%.2f,%.2f)"), *Bot.GetBotName(),
						Choice == EVeloxEdgeChoice::Mirror ? TEXT("mirror") : TEXT("drop"), *ProbeFeet.ToCompactString(), *Velocity2D.ToString(), Wish.X, Wish.Y);
				}
			}
			In.bNoLateralOrRetreat = Combat.bEdgeDrop;
			Wish = VeloxBotCombat::CombatWish(In);
			if (Combat.bEdgeHazardBack)
			{
				Wish = -Wish;
			}
			else if (Combat.bEdgeNoWish)
			{
				Wish = FVector2D::ZeroVector;
			}
		}
		if (!Wish.IsZero())
		{
			Pending.WishDirWorld = Wish;
			Pending.WishScale = 1.f;
		}
		// D31: the dodge jump obeys the D18 headroom guard (no hop into the ramp underside); the sweep is cached 0.1 s.
		if (Now < Combat.DodgeJumpUntil)
		{
			UVeloxBotPathFollowingComponent* Follow = Bot.GetBotPathFollowing();
			// ADR 0008 3.6.2: and the hop landing guard on hazard maps (the dodge jump is a hop).
			Pending.bJumpHeld |= !Follow || (Follow->HasHopHeadroom(Now) && Follow->HasHopLanding());
		}
	}

	void Act(AVeloxBotController& Bot, FVeloxBotCombatState& Combat, FRandomStream& Stream, const FVeloxBotNavOutput& Nav, const FRotator& CurrentView, FVeloxBotInput& Pending, float Dt)
	{
		AVeloxCharacter* Self = Cast<AVeloxCharacter>(Bot.GetPawn());
		UWorld* World = Bot.GetWorld();
		if (!Self || !World)
		{
			return;
		}
		const double Now = World->GetTimeSeconds();
		const FVeloxBotDifficultyParams& Params = Bot.GetDifficultyParams();
		const UVeloxBotCombatSettings& Settings = UVeloxBotCombatSettings::Get();
		const AVeloxPlayerState* PS = Bot.GetPlayerState<AVeloxPlayerState>();
		const UVeloxWeaponInventoryComponent* Inventory = PS ? PS->GetWeaponInventory() : nullptr;
		const UAbilitySystemComponent* ASC = PS ? PS->GetAbilitySystemComponent() : nullptr;

		const UVeloxWeaponDefinition* Held = VeloxBotCombatGlueImpl::HeldDefinition(Inventory);
		const bool bSplash = Held && Held->SplashRadiusCm > 0.f;
		const float ProjectileSpeed = Held && Held->ProjectileClass && Held->ProjectileSpeed > 0.f ? Held->ProjectileSpeed : 0.f;
		const FVector Eye = Self->GetPawnViewLocation();

		// --- Aim (section 8) ---
		const FVeloxBotEnemyInfo* Target = Combat.GetTarget();
		const AVeloxCharacter* TargetPawn = Target ? Target->Pawn.Get() : nullptr;
		const bool bVisible = Target && Target->bVisible && TargetPawn && !TargetPawn->IsCorpse();
		FVeloxAimInput AimIn;
		AimIn.bHasTarget = bVisible;
		AimIn.TargetId = bVisible ? Target->Id : INDEX_NONE;
		AimIn.Eye = Eye;
		AimIn.ProjectileSpeed = Params.bLeadProjectiles ? ProjectileSpeed : 0.f;
		if (bVisible)
		{
			// The current server pose (no rewind, section 8); splash weapons aim at the feet.
			AimIn.TargetCentre = TargetPawn->GetActorLocation() - (bSplash ? FVector(0.0, 0.0, Settings.SplashAimDropCm) : FVector::ZeroVector);
			AimIn.TargetVelocity = TargetPawn->GetVelocity();
		}
		const FVeloxBotEnemyInfo* Heard = Combat.FindEnemy(Combat.HeardId);
		if (Target && !bVisible)
		{
			AimIn.IdleAim = VeloxBotCombatGlueImpl::LookAt(Eye, Target->LastSeenLocation);
		}
		else if (Heard && Now >= Combat.HeardReactTime)
		{
			// Hearing (section 6): turn toward the attacker after the reaction time.
			AimIn.IdleAim = VeloxBotCombatGlueImpl::LookAt(Eye, Heard->LastSeenLocation);
		}
		else if (!Nav.PathDir.IsNearlyZero())
		{
			AimIn.IdleAim = FRotator(0.f, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Nav.PathDir.Y, Nav.PathDir.X))), 0.f);
		}
		else
		{
			AimIn.IdleAim = FRotator(0.f, CurrentView.Yaw, 0.f);
		}
		Combat.Aim.Aim = CurrentView;
		Pending.View = FVeloxAimModel::Update(Combat.Aim, AimIn, Combat.AimParams, Dt, Stream);
		Combat.DesiredPoint = bVisible ? (AimIn.ProjectileSpeed > 0.f ? FVeloxAimModel::LeadPoint(Eye, AimIn.TargetCentre, AimIn.TargetVelocity, AimIn.ProjectileSpeed, Combat.AimParams.LeadScale)
			: AimIn.TargetCentre) : FVector::ZeroVector;

		// --- Weapon choice (section 9) ---
		if (Inventory && Now >= Combat.NextWeaponChoiceTime)
		{
			Combat.NextWeaponChoiceTime = Now + Settings.WeaponChoiceIntervalSeconds;
			FVeloxWeaponChoiceInput Choice;
			Choice.bHasTarget = bVisible;
			Choice.RangeCm = bVisible ? static_cast<float>(FVector::Dist(Eye, TargetPawn->GetActorLocation())) : 0.f;
			Choice.PreferredRangeCm = Params.PreferredRange;
			Choice.CurrentWeapon = Held ? Held->WeaponTag : FGameplayTag();
			Choice.SwitchHysteresis = Settings.SwitchHysteresis;
			Choice.SplashSafetyMarginCm = Settings.WeaponSplashSafetyMarginCm;
			TArray<const UVeloxWeaponDefinition*, TInlineAllocator<16>> Definitions;
			for (int32 i = 0; i < Inventory->NumWeapons(); ++i)
			{
				const UVeloxWeaponDefinition* Definition = Inventory->GetDefinition(i);
				if (!Definition || !Inventory->OwnsIndex(i))
				{
					continue;
				}
				FVeloxWeaponChoiceOption& Option = Choice.Weapons.AddDefaulted_GetRef();
				Option.Weapon = Definition->WeaponTag;
				Option.bOwned = true;
				VeloxBotCombatGlueImpl::ReadAmmo(*Inventory, ASC, *Definition, Option.Ammo);
				Option.AmmoPerShot = Definition->AmmoTag.IsValid() ? Definition->AmmoPerShot : 0;
				Option.SplashRadiusCm = Definition->SplashRadiusCm;
				Definitions.Add(Definition);
			}
			const int32 Row = VeloxWeaponChoice::Pick(Choice, Params.WeaponPreferences);
			if (Params.WeaponPreferences.IsValidIndex(Row) && (!Held || Params.WeaponPreferences[Row].Weapon != Held->WeaponTag))
			{
				const FGameplayTag Want = Params.WeaponPreferences[Row].Weapon;
				for (const UVeloxWeaponDefinition* Definition : Definitions)
				{
					if (Definition->WeaponTag == Want)
					{
						Pending.SelectSlot = Definition->Slot;
						break;
					}
				}
			}
		}

		// --- Fire rule (section 8) ---
		const UVeloxWeaponDefinition* Current = Inventory ? Inventory->GetCurrentDefinition() : nullptr;
		FVeloxFireInput Fire;
		Fire.bHasWeapon = Current != nullptr;
		Fire.bWeaponReady = Inventory && Inventory->GetPendingIndex() == INDEX_NONE && Now >= Inventory->GetSwitchEndTime();
		int32 Ammo = 0;
		Fire.bHasAmmo = Current && Inventory && VeloxBotCombatGlueImpl::ReadAmmo(*Inventory, ASC, *Current, Ammo);
		Fire.bTargetVisible = bVisible;
		Fire.Eye = Eye;
		Fire.AimDir = Pending.View.Vector();
		Fire.DesiredPoint = Combat.DesiredPoint;
		// D50: while the target is out of sight (remembered, not a corpse) the range and the splash tests use the last seen
		// location; a dead or forgotten target gives no hold-after-lost.
		const bool bLostAlive = !bVisible && Target && TargetPawn && !TargetPawn->IsCorpse();
		Fire.TargetCentre = bVisible ? TargetPawn->GetActorLocation() : (bLostAlive ? Target->LastSeenLocation : FVector::ZeroVector);
		Fire.bTriggerHeld = Combat.bRuleTriggerHeld;
		Fire.SecondsSinceTargetSeen = bVisible ? 0.f : (bLostAlive ? static_cast<float>(Now - Target->LastSeenTime) : TNumericLimits<float>::Max());
		Fire.SplashRadiusCm = Current ? Current->SplashRadiusCm : 0.f;
		Fire.SelfSplashMarginCm = Settings.SelfSplashMarginCm;
		Fire.SplashHitFraction = Settings.SplashHitFraction;
		// D51 (2)(a): the unscaled gate; EvaluateFire scales it for automatic weapons (FireInterval <= SprayMaxFireInterval).
		Fire.DirectHitRadiusCm = Settings.DirectHitRadiusCm;
		Fire.bAutomaticWeapon = Current && Current->GetSafeFireInterval() <= Settings.SprayMaxFireInterval;
		if (const FVeloxBotWeaponPreference* Pref = VeloxBotCombatGlueImpl::FindPreference(Params, Current))
		{
			Fire.MinRangeCm = Pref->MinRange;
			Fire.MaxRangeCm = Pref->MaxRange;
		}
		if ((bVisible || (bLostAlive && Combat.bRuleTriggerHeld)) && Fire.SplashRadiusCm > 0.f)
		{
			// The aim ray's world hit (wall in the face, ground rockets): one trace while a splash weapon has a target, and
			// while the trigger is held after the target left sight (D50: self-splash is never relaxed).
			const float Length = static_cast<float>(FVector::Dist(Eye, Fire.TargetCentre)) + Fire.SplashRadiusCm;
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(VeloxBotAimRay), false, Self);
			Fire.bAimHit = World->LineTraceSingleByChannel(Hit, Eye, Eye + Fire.AimDir * Length, Velox_TraceChannel_Weapon, Query);
			Fire.AimHitPoint = Hit.ImpactPoint;
			++Combat.TracesTotal;
			if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World))
			{
				Bots->NoteTraces(1);
			}
		}
		Combat.LastVerdict = VeloxBotCombat::EvaluateFire(Fire, Params);
		Pending.bFireHeld = Combat.LastVerdict == EVeloxFireVerdict::Fire || Combat.LastVerdict == EVeloxFireVerdict::FireHeld;

		// --- Maneuver view requests on top (rocket jump: face the end, the one-tick pitch, the jump's rocket, the launcher) ---
		if (Nav.bWantRocketLauncher)
		{
			// The rocket of the jump must not be spent by the fire rule in the approach / switch / run-up (fire interval).
			Pending.bFireHeld = false;
			Combat.LastVerdict = EVeloxFireVerdict::NotReady;
			Pending.SelectSlot = 0;
			if (Inventory)
			{
				for (int32 i = 0; i < Inventory->NumWeapons(); ++i)
				{
					const UVeloxWeaponDefinition* Definition = Inventory->GetDefinition(i);
					if (Definition && Definition->WeaponTag == VeloxGameplayTags::Weapon_RocketLauncher && Inventory->OwnsIndex(i))
					{
						if (Inventory->GetCurrentIndex() != i && Inventory->GetPendingIndex() != i)
						{
							Pending.SelectSlot = Definition->Slot;
						}
						break;
					}
				}
			}
		}
		// D50: the hysteresis remembers the rule's own trigger (a rocket-jump suppression releases it; maneuver fire does not hold it).
		Combat.bRuleTriggerHeld = Pending.bFireHeld;
		const bool bOverride = Nav.bFaceEnd || Nav.bForcePitch;
		if (Nav.bFaceEnd)
		{
			const FVector ToEnd = Nav.FaceTarget - Self->GetActorLocation();
			const FRotator Desired(0.f, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToEnd.Y, ToEnd.X))), 0.f);
			Pending.View = VeloxBotRules::TurnToward(CurrentView, Desired, Params.MaxTurnRateDegPerSec, Dt);
		}
		if (Nav.bForcePitch)
		{
			Pending.View.Pitch = Nav.PitchDeg;
		}
		if (bOverride)
		{
			// The aim model continues from the view actually applied (no snap back after the maneuver).
			Combat.Aim.Aim = Pending.View;
		}
		if (Nav.bFire)
		{
			Pending.bFireHeld = true;
			Combat.LastVerdict = EVeloxFireVerdict::Fire;
		}
		if (Pending.bFireHeld && !Bot.GetBotState().bFireApplied)
		{
			++Combat.ShotsPressed;
		}
	}

	FString Describe(const FVeloxBotCombatState& Combat)
	{
		const FVeloxBotEnemyInfo* Target = Combat.GetTarget();
		return FString::Printf(TEXT("enemy=%s vis=%d known=%d fire=%s presses=%d traces=%d heard=%d dodges=%d"), Target ? *GetNameSafe(Target->Pawn.Get()) : TEXT("-"), Target && Target->bVisible ? 1 : 0,
			Combat.Enemies.Num(), LexToString(Combat.LastVerdict), Combat.ShotsPressed, Combat.TracesTotal, Combat.HeardCount, Combat.DodgeCount);
	}
}
