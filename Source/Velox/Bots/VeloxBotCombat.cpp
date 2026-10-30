// Velox - bot combat (ADR 0005 sections 6, 8, 9, 10; task M4-05).

#include "Bots/VeloxBotCombat.h"

#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotCoreTypes.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotPathFollowingComponent.h"
#include "Bots/VeloxBotCost.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxWeaponChoice.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxHealthComponent.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxCollisionChannels.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "NavigationSystem.h"
#include "Weapons/VeloxProjectile.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxBotCombat)

namespace VeloxBotCombatPrivate
{
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
		if (!In.bTargetVisible)
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
		const FVector ToDesired = (In.DesiredPoint - In.Eye).GetSafeNormal();
		const FVector Aim = In.AimDir.GetSafeNormal();
		const double CosAngle = FMath::Clamp(FVector::DotProduct(ToDesired, Aim), -1.0, 1.0);
		bool bInCone = !ToDesired.IsZero() && FMath::RadiansToDegrees(FMath::Acos(CosAngle)) <= Difficulty.FireConeDeg;
		if (bInCone && !bSplash && In.DirectHitRadiusCm > 0.f)
		{
			// Q3 BotCheckAttack: the shot would actually hit (the ray passes the desired point within the hit radius).
			const FVector ToPoint = In.DesiredPoint - In.Eye;
			const double Along = FVector::DotProduct(ToPoint, Aim);
			bInCone = Along > 0.0 && (ToPoint - Aim * Along).Size() <= In.DirectHitRadiusCm;
		}
		const bool bGroundHit = bSplash && In.bAimHit && FVector::Dist(In.AimHitPoint, In.TargetCentre) <= In.SplashRadiusCm * In.SplashHitFraction;
		return (bInCone || bGroundHit) ? EVeloxFireVerdict::Fire : EVeloxFireVerdict::OffTarget;
	}

	bool ShouldFire(const FVeloxFireInput& In, const FVeloxBotDifficultyParams& Difficulty)
	{
		return EvaluateFire(In, Difficulty) == EVeloxFireVerdict::Fire;
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
		if (In.bDodge && !In.DodgeDir.IsNearlyZero())
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
			else if (In.BestMaxCm > 0.f && Dist < In.BestMinCm)
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
		const FVector2D Wish = VeloxBotCombat::CombatWish(In);
		if (!Wish.IsZero())
		{
			Pending.WishDirWorld = Wish;
			Pending.WishScale = 1.f;
		}
		// D31: the dodge jump obeys the D18 headroom guard (no hop into the ramp underside); the sweep is cached 0.1 s.
		if (Now < Combat.DodgeJumpUntil)
		{
			UVeloxBotPathFollowingComponent* Follow = Bot.GetBotPathFollowing();
			Pending.bJumpHeld |= !Follow || Follow->HasHopHeadroom(Now);
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
		Fire.TargetCentre = bVisible ? TargetPawn->GetActorLocation() : FVector::ZeroVector;
		Fire.SplashRadiusCm = Current ? Current->SplashRadiusCm : 0.f;
		Fire.SelfSplashMarginCm = Settings.SelfSplashMarginCm;
		Fire.SplashHitFraction = Settings.SplashHitFraction;
		Fire.DirectHitRadiusCm = Settings.DirectHitRadiusCm;
		if (const FVeloxBotWeaponPreference* Pref = VeloxBotCombatGlueImpl::FindPreference(Params, Current))
		{
			Fire.MinRangeCm = Pref->MinRange;
			Fire.MaxRangeCm = Pref->MaxRange;
		}
		if (bVisible && Fire.SplashRadiusCm > 0.f)
		{
			// The aim ray's world hit (wall in the face, ground rockets): one trace while a splash weapon has a target.
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
		Pending.bFireHeld = Combat.LastVerdict == EVeloxFireVerdict::Fire;

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
