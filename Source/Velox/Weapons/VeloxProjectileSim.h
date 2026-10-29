// Velox - world-free projectile math (ADR 0003 section 7): constant-velocity flight (Q3 rockets and plasma fly
// straight, no gravity), sphere-vs-capsule pawn sweeps, Q3 G_RadiusDamage per victim with the ADR's three-point line of
// sight, the owner's self-knockback prediction and the server's forward-simulation window. Shared by the server
// projectile, the owning client's fake and the move-timed self splash; unit-tested in Velox.Combat.ProjectileSim.
// The world enters only through callbacks (world sweep, line of sight).

#pragma once

#include "CoreMinimal.h"
#include "Combat/VeloxDamageMath.h"
#include "Combat/VeloxHitMath.h"

class AActor;

/** Flight state of one projectile. */
struct FVeloxProjectileState
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	/** Seconds flown. */
	double Age = 0.0;
};

/** One pawn the projectile can hit directly: its hit shapes at the time of the step (server: current, fake: rendered). */
struct FVeloxProjectileTarget
{
	AActor* Actor = nullptr;
	const FVeloxHitboxPose* Pose = nullptr;
};

/** Where and why a step ended the flight. */
struct FVeloxProjectileImpact
{
	bool bImpact = false;
	/** World geometry (or the lifetime ran out when neither this nor Pawn is set). */
	bool bHitWorld = false;
	bool bLifetimeExpired = false;
	/** Direct victim (pawn hits only). */
	AActor* Pawn = nullptr;
	/** Sphere centre at the impact: the explosion origin. */
	FVector Location = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
};

/** One victim of a radius explosion (Q3 G_RadiusDamage, ADR 0003 B3). */
struct FVeloxSplashHit
{
	AActor* Actor = nullptr;
	/** Q3 points: (int)(SplashDamage * (1 - Dist / Radius)); 0 at the edge still hits (1 damage, no knockback). */
	int32 Points = 0;
	/** Distance from the origin to the shape surface (0 inside). */
	float Dist = 0.f;
	/** RadiusKnockbackDir(origin, shape centre, ZOffset), unit. */
	FVector Dir = FVector::ZeroVector;
};

/**
 * World sphere sweep of the projectile from Start to End (radius is the caller's): true on a blocking hit, OutDist =
 * distance from Start to the sphere centre at the hit, OutNormal = impact normal.
 */
using FVeloxProjectileWorldSweep = TFunctionRef<bool(const FVector& Start, const FVector& End, double& OutDist, FVector& OutNormal)>;

/** World line-of-sight test from an explosion origin to a point (true = nothing blocks). */
using FVeloxProjectileLineOfSight = TFunctionRef<bool(const FVector& From, const FVector& To)>;

struct FVeloxProjectileSim
{
	/** Constant velocity: where a projectile launched at Start is after Elapsed seconds (dead reckoning). */
	static FVector PositionAt(const FVector& Start, const FVector& Velocity, double Elapsed)
	{
		return Start + Velocity * Elapsed;
	}

	/**
	 * Server forward-simulation window at spawn (ADR 0003 section 7): half the shooter's ping, at most MaxSeconds.
	 * Zero for a shooter on the server's own machine (listen host, bots): pass PingMs = 0.
	 */
	static float ForwardSimSeconds(float PingMs, float MaxSeconds)
	{
		return FMath::Clamp(PingMs / 2000.f, 0.f, FMath::Max(MaxSeconds, 0.f));
	}

	/**
	 * Sphere of Radius moving from Start along unit Dir for up to MaxDist against Targets (sphere vs capsule = ray vs the
	 * capsule grown by Radius). IgnoreActor (the owner: Q3 missiles never hit their owner directly) is skipped. Nearest
	 * hit wins; false if none closer than MaxDist.
	 */
	static bool SweepTargets(const FVector& Start, const FVector& Dir, double MaxDist, float Radius, TConstArrayView<FVeloxProjectileTarget> Targets,
		const AActor* IgnoreActor, double& OutDist, AActor*& OutActor)
	{
		OutActor = nullptr;
		double Best = MaxDist;
		for (const FVeloxProjectileTarget& Target : Targets)
		{
			if (!Target.Actor || Target.Actor == IgnoreActor || !Target.Pose)
			{
				continue;
			}
			for (const FVeloxHitShape& Shape : Target.Pose->Shapes)
			{
				double Dist;
				if (FVeloxHitMath::RayCapsule(Start, Dir, Best, Shape.GetSegmentStart(), Shape.GetSegmentEnd(), static_cast<double>(Shape.Radius) + Radius, Dist) && Dist < Best)
				{
					Best = Dist;
					OutActor = Target.Actor;
				}
			}
		}
		OutDist = Best;
		return OutActor != nullptr;
	}

	/**
	 * One flight step of Dt seconds, clamped to the remaining Lifetime: world sweep and pawn sweep over the segment, the
	 * nearer wins (a pawn exactly at the wall distance loses to the wall). On impact the state stops at the impact point;
	 * when the lifetime runs out without impact the projectile explodes in place (Q3). Returns OutImpact.bImpact.
	 */
	static bool Step(FVeloxProjectileState& State, float Dt, float Lifetime, float Radius, FVeloxProjectileWorldSweep WorldSweep,
		TConstArrayView<FVeloxProjectileTarget> Targets, const AActor* IgnoreActor, FVeloxProjectileImpact& OutImpact)
	{
		OutImpact = FVeloxProjectileImpact();
		const double Remaining = FMath::Max(static_cast<double>(Lifetime) - State.Age, 0.0);
		const double StepTime = FMath::Clamp(static_cast<double>(Dt), 0.0, Remaining);
		const double Speed = State.Velocity.Size();
		const FVector Dir = Speed > UE_DOUBLE_SMALL_NUMBER ? State.Velocity / Speed : FVector::ZeroVector;
		const double StepDist = Speed * StepTime;

		if (StepDist > UE_DOUBLE_SMALL_NUMBER)
		{
			const FVector Start = State.Location;
			const FVector End = Start + Dir * StepDist;
			double WorldDist = StepDist;
			FVector WorldNormal = -Dir;
			const bool bWorld = WorldSweep(Start, End, WorldDist, WorldNormal);
			if (!bWorld)
			{
				WorldDist = StepDist;
			}
			double PawnDist = 0.0;
			AActor* PawnHit = nullptr;
			const bool bPawn = SweepTargets(Start, Dir, WorldDist, Radius, Targets, IgnoreActor, PawnDist, PawnHit) && PawnDist < WorldDist;
			if (bPawn || bWorld)
			{
				const double HitDist = bPawn ? PawnDist : WorldDist;
				State.Location = Start + Dir * HitDist;
				State.Age += Speed > UE_DOUBLE_SMALL_NUMBER ? HitDist / Speed : 0.0;
				OutImpact.bImpact = true;
				OutImpact.bHitWorld = !bPawn;
				OutImpact.Pawn = bPawn ? PawnHit : nullptr;
				OutImpact.Location = State.Location;
				OutImpact.Normal = bPawn ? -Dir : WorldNormal;
				return true;
			}
			State.Location = End;
		}
		State.Age += StepTime;
		if (State.Age >= static_cast<double>(Lifetime) - UE_KINDA_SMALL_NUMBER)
		{
			OutImpact.bImpact = true;
			OutImpact.bLifetimeExpired = true;
			OutImpact.Location = State.Location;
			OutImpact.Normal = -Dir;
			return true;
		}
		return false;
	}

	/**
	 * Where and when (seconds after launch) a projectile from Origin along unit Dir at Speed ends against the world alone
	 * (no pawns): the first world hit within Speed x Lifetime, else the lifetime end. Both the owning client and the
	 * server evaluate this from the same quantized origin and direction, so the owner's self splash agrees to the move.
	 */
	static double WorldFlight(const FVector& Origin, const FVector& Dir, float Speed, float Lifetime, FVeloxProjectileWorldSweep WorldSweep, FVector& OutLocation, FVector& OutNormal, bool& bOutHitWorld)
	{
		const double MaxDist = static_cast<double>(FMath::Max(Speed, 0.f)) * FMath::Max(Lifetime, 0.f);
		double Dist = MaxDist;
		FVector Normal = -Dir;
		bOutHitWorld = MaxDist > 0.0 && WorldSweep(Origin, Origin + Dir * MaxDist, Dist, Normal);
		if (!bOutHitWorld)
		{
			Dist = MaxDist;
			Normal = -Dir;
		}
		OutLocation = Origin + Dir * Dist;
		OutNormal = Normal;
		return Speed > 0.f ? Dist / Speed : 0.0;
	}

	/** ADR 0003 section 7 line-of-sight points of a shape: centre, top and bottom (the cap-sphere centres, inside it). */
	static void LineOfSightPoints(const FVeloxHitShape& Shape, FVector (&OutPoints)[3])
	{
		OutPoints[0] = Shape.Centre;
		OutPoints[1] = Shape.GetSegmentEnd();
		OutPoints[2] = Shape.GetSegmentStart();
	}

	/**
	 * Q3 G_RadiusDamage for one pawn: true iff the nearest shape is within SplashRadius (strictly) and one of its three
	 * line-of-sight points is visible from Origin. Points may be 0 (B3: still a hit).
	 */
	static bool EvaluateSplash(const FVector& Origin, AActor* Actor, const FVeloxHitboxPose& Pose, int32 SplashDamage, float SplashRadius, float ZOffset,
		FVeloxProjectileLineOfSight LineOfSight, FVeloxSplashHit& OutHit)
	{
		const FVeloxHitShape* Nearest = nullptr;
		float NearestDist = TNumericLimits<float>::Max();
		for (const FVeloxHitShape& Shape : Pose.Shapes)
		{
			const float Dist = FVeloxDamageMath::ClosestCapsuleDistance(Origin, Shape);
			if (Dist < NearestDist)
			{
				NearestDist = Dist;
				Nearest = &Shape;
			}
		}
		if (!Nearest || SplashRadius <= 0.f || NearestDist >= SplashRadius)
		{
			return false;
		}
		FVector Points[3];
		LineOfSightPoints(*Nearest, Points);
		if (!LineOfSight(Origin, Points[0]) && !LineOfSight(Origin, Points[1]) && !LineOfSight(Origin, Points[2]))
		{
			return false;
		}
		OutHit.Actor = Actor;
		OutHit.Dist = NearestDist;
		OutHit.Points = FVeloxDamageMath::RadiusPoints(SplashDamage, NearestDist, SplashRadius);
		OutHit.Dir = FVeloxDamageMath::RadiusKnockbackDir(Origin, Nearest->Centre, ZOffset);
		return true;
	}

	/** Splash victims of an explosion at Origin: every target except DirectVictim (Q3: the direct hit gets no splash). */
	template <typename AllocatorType>
	static void CollectSplash(const FVector& Origin, TConstArrayView<FVeloxProjectileTarget> Targets, const AActor* DirectVictim, int32 SplashDamage, float SplashRadius,
		float ZOffset, FVeloxProjectileLineOfSight LineOfSight, TArray<FVeloxSplashHit, AllocatorType>& OutHits)
	{
		OutHits.Reset();
		for (const FVeloxProjectileTarget& Target : Targets)
		{
			if (!Target.Actor || Target.Actor == DirectVictim || !Target.Pose)
			{
				continue;
			}
			FVeloxSplashHit Hit;
			if (EvaluateSplash(Origin, Target.Actor, *Target.Pose, SplashDamage, SplashRadius, ZOffset, LineOfSight, Hit))
			{
				OutHits.Add(Hit);
			}
		}
	}

	/**
	 * The owner's self-knockback of a splash hit, bit-identical to the server's GE path (UVeloxDamageExecution ->
	 * UVeloxHealthSet -> AddKnockback(Server)): FVeloxDamageMath::Apply(bSelf) speed along the direction normalised the
	 * way MakeDamageSpec and the health set normalise it. Zero when protected or the points give no knockback.
	 */
	static FVector SelfKnockback(const FVeloxSplashHit& Hit, float KnockbackScale, const FVeloxCombatParams& Params, bool bProtected)
	{
		FVeloxDamageInput In;
		In.Damage = Hit.Points;
		In.bSelf = true;
		In.bProtected = bProtected;
		In.KnockbackScale = KnockbackScale;
		In.bNoKnockback = Hit.Dir.IsNearlyZero();
		const FVeloxDamageResult Result = FVeloxDamageMath::Apply(In, Params);
		if (Result.KnockbackSpeed <= 0.f)
		{
			return FVector::ZeroVector;
		}
		// MakeDamageSpec stores Direction.GetSafeNormal() in the context, the health set applies GetSafeNormal() again.
		const FVector ContextDir = Hit.Dir.GetSafeNormal();
		return ContextDir.GetSafeNormal() * Result.KnockbackSpeed;
	}
};
