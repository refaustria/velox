// Velox - hit geometry tests (ADR 0003 section 9, task 02).
//   Velox.Combat.HitMath        ray-capsule (side, cap, miss, origin inside, parallel, range), pose nearest shape,
//                               splash distance overload, Q3 spread pattern determinism (world-free).
//   Velox.Character.Geometry    class-default geometry, crouch, Weapon-channel response, Linear smoothing (CDO only).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/VeloxCharacter.h"
#include "Combat/VeloxDamageMath.h"
#include "Combat/VeloxHitMath.h"
#include "Combat/VeloxHitboxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/VeloxCollisionChannels.h"
#include "Movement/VeloxMovementComponent.h"

namespace VeloxHitMathTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr double Tol = 1e-3;

	/** Standing Velox capsule (37.5 / 70) centred at C. */
	FVeloxHitShape Pawn(const FVector& C) { return FVeloxHitShape(C, 37.5f, 70.f); }

	bool Ray(const FVector& O, const FVector& D, double MaxDist, const FVeloxHitShape& S, double& Dist)
	{
		return FVeloxHitMath::RayShape(O, D.GetSafeNormal(), MaxDist, S, Dist);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatHitMathTest, "Velox.Combat.HitMath", VeloxHitMathTest::Flags)

bool FVeloxCombatHitMathTest::RunTest(const FString& Parameters)
{
	using namespace VeloxHitMathTest;
	const FVeloxHitShape S = Pawn(FVector(1000, 0, 0));
	double Dist = -1.0;

	// Side: horizontal ray at the centre enters the cylinder at x = 1000 - 37.5.
	TestTrue(TEXT("side hit"), Ray(FVector(0, 0, 0), FVector(1, 0, 0), 20480, S, Dist));
	TestNearlyEqual(TEXT("side dist"), Dist, 962.5, Tol);
	// Side, off-centre in y: chord entry x = 1000 - sqrt(37.5^2 - 30^2) = 977.5.
	TestTrue(TEXT("side off-centre hit"), Ray(FVector(0, 30, 10), FVector(1, 0, 0), 20480, S, Dist));
	TestNearlyEqual(TEXT("side off-centre dist"), Dist, 977.5, Tol);
	// Cap: the segment ends at z = +-32.5; a ray at z = 60 meets the top sphere at dz = 27.5, dx = sqrt(37.5^2 - 27.5^2).
	TestTrue(TEXT("cap hit"), Ray(FVector(0, 0, 60), FVector(1, 0, 0), 20480, S, Dist));
	TestNearlyEqual(TEXT("cap dist"), Dist, 1000.0 - FMath::Sqrt(37.5 * 37.5 - 27.5 * 27.5), Tol);
	// Top of the head from above (parallel to the axis): z = 70 is the surface.
	TestTrue(TEXT("vertical cap hit"), Ray(FVector(1000, 0, 500), FVector(0, 0, -1), 20480, S, Dist));
	TestNearlyEqual(TEXT("vertical cap dist"), Dist, 430.0, Tol);
	// Misses: above the head, beside the body, pointing away, out of range.
	TestFalse(TEXT("miss above"), Ray(FVector(0, 0, 71), FVector(1, 0, 0), 20480, S, Dist));
	TestFalse(TEXT("miss beside"), Ray(FVector(0, 38, 0), FVector(1, 0, 0), 20480, S, Dist));
	TestFalse(TEXT("miss behind"), Ray(FVector(0, 0, 0), FVector(-1, 0, 0), 20480, S, Dist));
	TestFalse(TEXT("miss out of range"), Ray(FVector(0, 0, 0), FVector(1, 0, 0), 962.0, S, Dist));
	TestTrue(TEXT("hit at range"), Ray(FVector(0, 0, 0), FVector(1, 0, 0), 962.5 + Tol, S, Dist));
	// Origin inside: hit at 0 whatever the direction.
	TestTrue(TEXT("inside hit"), Ray(FVector(1010, 5, 50), FVector(-1, 0, 0), 20480, S, Dist));
	TestNearlyEqual(TEXT("inside dist"), Dist, 0.0, Tol);
	// Diagonal down onto the body: from (0, 0, 1000) towards the centre; entry between centre distance and that - 70.
	{
		const FVector O(0, 0, 1000);
		const FVector D = (S.Centre - O).GetSafeNormal();
		TestTrue(TEXT("diagonal hit"), Ray(O, D, 20480, S, Dist));
		const FVector P = O + D * Dist;
		TestNearlyEqual(TEXT("diagonal entry on surface"), FVeloxHitMath::PointShapeDistance(P, S), 0.0, Tol);
		TestTrue(TEXT("diagonal entry before centre"), Dist < FVector::Dist(O, S.Centre) - 37.5 + Tol);
	}
	// Crouched capsule (half height 50, centre 20 cm lower, ground-kept base): a ray at z = 35 hits the standing
	// capsule but passes over the crouched one.
	{
		const FVeloxHitShape Crouched(FVector(1000, 0, -20), 37.5f, 50.f);
		TestFalse(TEXT("crouched miss at standing head"), Ray(FVector(0, 0, 35), FVector(1, 0, 0), 20480, Crouched, Dist));
		TestTrue(TEXT("crouched hit at body"), Ray(FVector(0, 0, -20), FVector(1, 0, 0), 20480, Crouched, Dist));
	}
	// Sphere shape (half height == radius).
	TestTrue(TEXT("sphere hit"), Ray(FVector(0, 0, 0), FVector(1, 0, 0), 20480, FVeloxHitShape(FVector(100, 0, 0), 10.f, 10.f), Dist));
	TestNearlyEqual(TEXT("sphere dist"), Dist, 90.0, Tol);
	// Tilted shape (M5 per-bone): horizontal capsule along Y, hit from above at its end sphere.
	{
		FVeloxHitShape Tilted(FVector(0, 0, 0), 10.f, 50.f);
		Tilted.Axis = FVector(0, 1, 0);
		TestTrue(TEXT("tilted hit"), Ray(FVector(0, 40, 100), FVector(0, 0, -1), 20480, Tilted, Dist));
		TestNearlyEqual(TEXT("tilted dist"), Dist, 90.0, Tol);
		TestFalse(TEXT("tilted miss"), Ray(FVector(0, 51, 100), FVector(0, 0, -1), 20480, Tilted, Dist));
	}

	// Pose: nearest of two shapes wins regardless of order.
	{
		FVeloxHitboxPose Pose;
		Pose.Shapes.Add(Pawn(FVector(2000, 0, 0)));
		Pose.Shapes.Add(Pawn(FVector(1000, 0, 0)));
		int32 Index = INDEX_NONE;
		TestTrue(TEXT("pose hit"), FVeloxHitMath::RayPose(FVector::ZeroVector, FVector(1, 0, 0), 20480, Pose, Dist, Index));
		TestEqual(TEXT("pose nearest index"), Index, 1);
		TestNearlyEqual(TEXT("pose nearest dist"), Dist, 962.5, Tol);
		TestFalse(TEXT("empty pose"), FVeloxHitMath::RayPose(FVector::ZeroVector, FVector(1, 0, 0), 20480, FVeloxHitboxPose(), Dist, Index));
	}

	// Splash distance: the FVeloxHitShape overload equals the vertical-capsule form and the general segment distance.
	{
		const FVector Points[] = { FVector(1000, 0, 200), FVector(1100, 0, 0), FVector(1000, 0, 0), FVector(900, 50, -90) };
		for (const FVector& P : Points)
		{
			const float Expected = FVeloxDamageMath::ClosestCapsuleDistance(P, S.Centre, S.Radius, S.HalfHeight);
			TestNearlyEqual(*FString::Printf(TEXT("shape overload %s"), *P.ToCompactString()), FVeloxDamageMath::ClosestCapsuleDistance(P, S), Expected, 1e-3f);
			TestNearlyEqual(*FString::Printf(TEXT("segment distance %s"), *P.ToCompactString()), static_cast<float>(FVeloxHitMath::PointShapeDistance(P, S)), Expected, 1e-3f);
		}
		TestNearlyEqual(TEXT("splash above head"), FVeloxDamageMath::ClosestCapsuleDistance(FVector(1000, 0, 200), S), 130.f, 1e-3f);
	}

	// Q3 shotgun pattern: same seed -> same pellets; another seed differs; all inside the square spread.
	{
		const FVector Dir = FVector(1, 0.2, -0.1).GetSafeNormal();
		TArray<FVector> A, B, C;
		FVeloxHitMath::PelletDirections(Dir, 11, 4.9f, 1234 ^ 7, A);
		FVeloxHitMath::PelletDirections(Dir, 11, 4.9f, 1234 ^ 7, B);
		FVeloxHitMath::PelletDirections(Dir, 11, 4.9f, 1235 ^ 7, C);
		TestEqual(TEXT("pellet count"), A.Num(), 11);
		bool bSame = A.Num() == B.Num();
		bool bDiffers = false;
		bool bInside = true;
		FVector Right, Up;
		FVeloxHitMath::SpreadBasis(Dir, Right, Up);
		TestNearlyEqual(TEXT("basis orthogonal"), FVector::DotProduct(Right, Dir) + FVector::DotProduct(Up, Dir) + FVector::DotProduct(Right, Up), 0.0, 1e-9);
		const double TanSpread = FMath::Tan(FMath::DegreesToRadians(4.9));
		for (int32 i = 0; i < A.Num(); ++i)
		{
			bSame &= A[i].Equals(B[i], 0.0);
			bDiffers |= !A[i].Equals(C[i], 1e-6);
			const double Fwd = FVector::DotProduct(A[i], Dir);
			bInside &= Fwd > 0.0 && FMath::Abs(FVector::DotProduct(A[i], Right) / Fwd) <= TanSpread + 1e-9 && FMath::Abs(FVector::DotProduct(A[i], Up) / Fwd) <= TanSpread + 1e-9;
			bInside &= FMath::IsNearlyEqual(A[i].Size(), 1.0, 1e-9);
		}
		TestTrue(TEXT("pellets deterministic for a seed"), bSame);
		TestTrue(TEXT("pellets differ for another seed"), bDiffers);
		TestTrue(TEXT("pellets inside the square spread"), bInside);

		// Vertical aim keeps a valid basis.
		TArray<FVector> Down;
		FVeloxHitMath::PelletDirections(FVector(0, 0, -1), 3, 4.9f, 1, Down);
		TestTrue(TEXT("vertical pellets valid"), Down.Num() == 3 && Down[0].IsNormalized() && Down[0].Z < -0.99);

		// MG spread: deterministic per stream, within the cone.
		FRandomStream S1(42), S2(42);
		const FVector M1 = FVeloxHitMath::BulletSpreadDirection(Dir, 1.4f, S1);
		const FVector M2 = FVeloxHitMath::BulletSpreadDirection(Dir, 1.4f, S2);
		TestTrue(TEXT("bullet spread deterministic"), M1.Equals(M2, 0.0));
		TestTrue(TEXT("bullet spread within cone"), FMath::Acos(FVector::DotProduct(M1, Dir)) <= FMath::Atan(FMath::Sqrt(2.0) * FMath::Tan(FMath::DegreesToRadians(1.4))) + 1e-6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCharacterGeometryTest, "Velox.Character.Geometry", VeloxHitMathTest::Flags)

bool FVeloxCharacterGeometryTest::RunTest(const FString& Parameters)
{
	const AVeloxCharacter* CDO = GetDefault<AVeloxCharacter>();
	const UCapsuleComponent* Capsule = CDO->GetCapsuleComponent();
	const UVeloxMovementComponent* Movement = CDO->GetVeloxMovementComponent();
	if (!TestNotNull(TEXT("capsule"), Capsule) || !TestNotNull(TEXT("movement"), Movement))
	{
		return false;
	}
	TestEqual(TEXT("capsule radius"), Capsule->GetUnscaledCapsuleRadius(), 37.5f);
	TestEqual(TEXT("capsule half height"), Capsule->GetUnscaledCapsuleHalfHeight(), 70.f);
	TestEqual(TEXT("eye height"), CDO->BaseEyeHeight, 55.f);
	TestEqual(TEXT("crouched half height"), Movement->GetCrouchedHalfHeight(), 50.f);
	TestEqual(TEXT("crouched eye height"), CDO->CrouchedEyeHeight, 40.f);
	TestTrue(TEXT("can crouch"), Movement->CanEverCrouch());
	TestTrue(TEXT("capsule ignores Weapon"), Capsule->GetCollisionResponseToChannel(Velox_TraceChannel_Weapon) == ECR_Ignore);
	TestTrue(TEXT("capsule still blocks WorldStatic"), Capsule->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
	if (const USkeletalMeshComponent* Mesh = CDO->GetMesh())
	{
		TestTrue(TEXT("mesh ignores Weapon"), Mesh->GetCollisionResponseToChannel(Velox_TraceChannel_Weapon) == ECR_Ignore);
	}
	TestTrue(TEXT("linear smoothing"), Movement->NetworkSmoothingMode == ENetworkSmoothingMode::Linear);
	TestNotNull(TEXT("hitbox component"), CDO->GetHitboxComponent());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
