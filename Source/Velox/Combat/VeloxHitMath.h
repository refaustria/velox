// Velox - world-free hit geometry (ADR 0003 sections 2, 5 and 6): hit shapes, ray-capsule intersection, Q3 spread
// patterns. No UWorld, no components: used by UVeloxHitboxComponent (poses), the lag compensation subsystem (server
// rewind traces, task 03), the client's cosmetic shot trace and FVeloxDamageMath (splash distance), and unit-tested in
// Velox.Combat.HitMath.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * One hit volume: a capsule given by its centre, its axis (unit), radius and half height (cm, half height includes the
 * hemispherical cap, as UCapsuleComponent). M2: exactly one vertical capsule per pawn, the collision capsule (crouch
 * shrinks it). M5: per-bone shapes through the same type (BoneId, Axis). DamageScale stays 1: no locational damage (Q3).
 */
struct FVeloxHitShape
{
	FVector Centre = FVector::ZeroVector;
	FVector Axis = FVector::UpVector;
	float Radius = 0.f;
	float HalfHeight = 0.f;
	int32 BoneId = INDEX_NONE;
	float DamageScale = 1.f;

	FVeloxHitShape() = default;
	FVeloxHitShape(const FVector& InCentre, float InRadius, float InHalfHeight)
		: Centre(InCentre), Radius(InRadius), HalfHeight(InHalfHeight)
	{
	}

	/** Half length of the inner segment (centre to cap-sphere centre); 0 for a sphere. */
	double GetSegmentHalfLength() const { return FMath::Max(static_cast<double>(HalfHeight) - static_cast<double>(Radius), 0.0); }

	FVector GetSegmentStart() const { return Centre - Axis * GetSegmentHalfLength(); }
	FVector GetSegmentEnd() const { return Centre + Axis * GetSegmentHalfLength(); }
};

/** All hit shapes of one pawn at one instant. One inline shape in M2 (the lag-comp ring buffer copies these at 60 Hz). */
struct FVeloxHitboxPose
{
	TArray<FVeloxHitShape, TInlineAllocator<1>> Shapes;
};

struct FVeloxHitMath
{
	/**
	 * First intersection of the ray Origin + t * Dir (Dir unit length, t in [0, MaxDist]) with the capsule A-B of
	 * Radius. Origin inside the capsule counts as a hit at t = 0. The capsule is the union of the finite cylinder and
	 * the two end spheres, so the entry distance is the smallest entry over the three (the flat cylinder ends lie inside
	 * the spheres).
	 */
	static bool RayCapsule(const FVector& Origin, const FVector& Dir, double MaxDist, const FVector& A, const FVector& B, double Radius, double& OutDist)
	{
		const double R2 = Radius * Radius;
		const FVector AB = B - A;
		const double ABLenSq = AB.SizeSquared();

		// Origin inside: distance to the segment <= radius.
		{
			const double S = ABLenSq > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(FVector::DotProduct(Origin - A, AB) / ABLenSq, 0.0, 1.0) : 0.0;
			if (FVector::DistSquared(Origin, A + AB * S) <= R2)
			{
				OutDist = 0.0;
				return true;
			}
		}

		double Best = TNumericLimits<double>::Max();
		double T;
		if (RaySphere(Origin, Dir, A, R2, T))
		{
			Best = FMath::Min(Best, T);
		}
		if (ABLenSq > UE_DOUBLE_SMALL_NUMBER)
		{
			if (RaySphere(Origin, Dir, B, R2, T))
			{
				Best = FMath::Min(Best, T);
			}

			// Infinite cylinder around AB, restricted to the segment range.
			const double ABLen = FMath::Sqrt(ABLenSq);
			const FVector Axis = AB / ABLen;
			const FVector AO = Origin - A;
			const FVector DPerp = Dir - Axis * FVector::DotProduct(Dir, Axis);
			const FVector OPerp = AO - Axis * FVector::DotProduct(AO, Axis);
			const double QA = DPerp.SizeSquared();
			if (QA > UE_DOUBLE_SMALL_NUMBER) // parallel rays only meet the caps
			{
				const double QB = FVector::DotProduct(DPerp, OPerp);
				const double QC = OPerp.SizeSquared() - R2;
				const double Disc = QB * QB - QA * QC;
				if (Disc >= 0.0)
				{
					T = (-QB - FMath::Sqrt(Disc)) / QA;
					const double Along = FVector::DotProduct(AO + Dir * T, Axis);
					if (T >= 0.0 && Along >= 0.0 && Along <= ABLen)
					{
						Best = FMath::Min(Best, T);
					}
				}
			}
		}

		if (Best <= MaxDist)
		{
			OutDist = Best;
			return true;
		}
		return false;
	}

	static bool RayShape(const FVector& Origin, const FVector& Dir, double MaxDist, const FVeloxHitShape& Shape, double& OutDist)
	{
		return RayCapsule(Origin, Dir, MaxDist, Shape.GetSegmentStart(), Shape.GetSegmentEnd(), Shape.Radius, OutDist);
	}

	/** Nearest shape of Pose hit by the ray; OutShapeIndex indexes Pose.Shapes. */
	static bool RayPose(const FVector& Origin, const FVector& Dir, double MaxDist, const FVeloxHitboxPose& Pose, double& OutDist, int32& OutShapeIndex)
	{
		OutShapeIndex = INDEX_NONE;
		double Limit = MaxDist;
		for (int32 Index = 0; Index < Pose.Shapes.Num(); ++Index)
		{
			double Dist;
			if (RayShape(Origin, Dir, Limit, Pose.Shapes[Index], Dist))
			{
				Limit = Dist;
				OutDist = Dist;
				OutShapeIndex = Index;
			}
		}
		return OutShapeIndex != INDEX_NONE;
	}

	/** Shortest distance from Point to the surface of Shape (0 inside). Any axis; FVeloxDamageMath forwards here for splash. */
	static double PointShapeDistance(const FVector& Point, const FVeloxHitShape& Shape)
	{
		const FVector A = Shape.GetSegmentStart();
		const FVector AB = Shape.GetSegmentEnd() - A;
		const double ABLenSq = AB.SizeSquared();
		const double S = ABLenSq > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(FVector::DotProduct(Point - A, AB) / ABLenSq, 0.0, 1.0) : 0.0;
		return FMath::Max(FVector::Dist(Point, A + AB * S) - Shape.Radius, 0.0);
	}

	/** Right/up basis for spread offsets around Dir (unit). Vertical Dir uses world X as the reference. */
	static void SpreadBasis(const FVector& Dir, FVector& OutRight, FVector& OutUp)
	{
		const FVector Ref = FMath::Abs(Dir.Z) < 0.999 ? FVector::UpVector : FVector::ForwardVector;
		OutRight = FVector::CrossProduct(Ref, Dir).GetSafeNormal();
		OutUp = FVector::CrossProduct(Dir, OutRight);
	}

	/**
	 * Q3 ShotgunPattern: Count pellets, offsets uniform in the square [-tan(Spread), tan(Spread)]^2 along right/up
	 * (Q3: crandom() * 700 at 8192 u). Deterministic for Seed (ADR 0003 section 5: ShotSeq ^ PlayerId), so the client's
	 * tracers and the server's pellets agree.
	 */
	static void PelletDirections(const FVector& Dir, int32 Count, float SpreadAngleDeg, int32 Seed, TArray<FVector>& OutDirs)
	{
		FRandomStream Stream(Seed);
		FVector Right, Up;
		SpreadBasis(Dir, Right, Up);
		const double TanSpread = FMath::Tan(FMath::DegreesToRadians(static_cast<double>(SpreadAngleDeg)));
		OutDirs.Reset(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double R = (Stream.FRand() * 2.0 - 1.0) * TanSpread;
			const double U = (Stream.FRand() * 2.0 - 1.0) * TanSpread;
			OutDirs.Add((Dir + Right * R + Up * U).GetSafeNormal());
		}
	}

	/** Q3 Bullet_Fire (MG): random angle, radius crandom() * tan(Spread) per axis. Advances Stream by three draws. */
	static FVector BulletSpreadDirection(const FVector& Dir, float SpreadAngleDeg, FRandomStream& Stream)
	{
		FVector Right, Up;
		SpreadBasis(Dir, Right, Up);
		const double TanSpread = FMath::Tan(FMath::DegreesToRadians(static_cast<double>(SpreadAngleDeg)));
		const double Angle = Stream.FRand() * UE_DOUBLE_TWO_PI;
		const double U = FMath::Sin(Angle) * (Stream.FRand() * 2.0 - 1.0) * TanSpread;
		const double R = FMath::Cos(Angle) * (Stream.FRand() * 2.0 - 1.0) * TanSpread;
		return (Dir + Right * R + Up * U).GetSafeNormal();
	}

private:
	/** Entry distance t >= 0 of the ray into the sphere (Origin outside); false if missed or behind. */
	static bool RaySphere(const FVector& Origin, const FVector& Dir, const FVector& Centre, double RadiusSq, double& OutT)
	{
		const FVector OC = Origin - Centre;
		const double B = FVector::DotProduct(Dir, OC);
		const double C = OC.SizeSquared() - RadiusSq;
		const double Disc = B * B - C;
		if (Disc < 0.0)
		{
			return false;
		}
		const double T = -B - FMath::Sqrt(Disc);
		if (T < 0.0)
		{
			return false;
		}
		OutT = T;
		return true;
	}
};
