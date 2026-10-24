// Velox - jump pad launch math and validation (ADR 0002 section 7 "JumpPad", amendment A4). World-free.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Movement/VeloxJumpPad.h"
#include "Movement/VeloxMoveSim.h"
#include "Movement/VeloxMovementSettings.h"

namespace VeloxJumpPadTest
{
	constexpr double LandingToleranceCm = 1.0;

	/** Analytic ballistic position at time T. */
	FVector FlightPosition(const FVector& From, const FVector& V, double G, double T)
	{
		return From + V * T + FVector(0.0, 0.0, -0.5 * G * T * T);
	}

	/** Descending-branch time at which the arc reaches height Z (relative to From); negative if never. */
	double DescendingTimeAtHeight(const FVector& V, double G, double DeltaZ)
	{
		const double Disc = V.Z * V.Z - 2.0 * G * DeltaZ;
		return Disc < 0.0 ? -1.0 : (V.Z + FMath::Sqrt(Disc)) / G;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementJumpPadTest, "Velox.Movement.JumpPad",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxMovementJumpPadTest::RunTest(const FString& Parameters)
{
	using namespace VeloxJumpPadTest;

	const float GravityZ = GetDefault<UVeloxMovementSettings>()->BuildSimParams().GravityZ;
	const double G = -static_cast<double>(GravityZ);
	TestTrue(TEXT("default gravity points down"), G > 0.0);

	// The same cases from a non-origin pad and along a diagonal, so no axis is special.
	const FVector Origins[] = { FVector::ZeroVector, FVector(-1234.5, 678.0, 90.0) };
	const FVector Directions[] = { FVector(1.0, 0.0, 0.0), FVector(1.0, 1.0, 0.0).GetSafeNormal() };

	for (const FVector& From : Origins)
	{
		for (const FVector& Dir : Directions)
		{
			const FString Ctx = FString::Printf(TEXT("from %s dir %s: "), *From.ToString(), *Dir.ToString());

			// 1. Above: (800, 320) -> Q3 AimAtTarget, apex at the target.
			{
				const FVector Target = From + Dir * 800.0 + FVector(0.0, 0.0, 320.0);
				const FVector V = VeloxMoveSim::JumpPadLaunchVelocity(From, Target, GravityZ);
				// Time at which the pawn is above the target horizontally.
				const double T = 800.0 / V.Size2D();
				TestTrue(Ctx + TEXT("above: reaches the target within 1 cm"), FlightPosition(From, V, G, T).Equals(Target, LandingToleranceCm));
				TestTrue(Ctx + TEXT("above: Vz == 0 at the target (apex)"), FMath::IsNearlyZero(V.Z - G * T, 1e-3 * V.Z));
			}

			// 2. Vertical: (0, 0, 640) -> straight up, apex at the target.
			{
				const FVector Target = From + FVector(0.0, 0.0, 640.0);
				const FVector V = VeloxMoveSim::JumpPadLaunchVelocity(From, Target, GravityZ);
				TestTrue(Ctx + TEXT("vertical: XY == 0"), V.X == 0.0 && V.Y == 0.0);
				TestTrue(Ctx + TEXT("vertical: Z > 0"), V.Z > 0.0);
				TestTrue(Ctx + TEXT("vertical: apex height 640"), FMath::IsNearlyEqual(V.Z * V.Z / (2.0 * G), 640.0, LandingToleranceCm));
			}

			// 3. Level: (1600, 0) -> minimum-speed 45 degree arc.
			{
				const FVector Target = From + Dir * 1600.0;
				const FVector V = VeloxMoveSim::JumpPadLaunchVelocity(From, Target, GravityZ);
				const double T = DescendingTimeAtHeight(V, G, 0.0);
				TestTrue(Ctx + TEXT("level: lands within 1 cm"), T > 0.0 && FlightPosition(From, V, G, T).Equals(Target, LandingToleranceCm));
				TestTrue(Ctx + TEXT("level: Vxy == Vz (45 degrees)"), FMath::IsNearlyEqual(V.Size2D(), V.Z, 1e-3 * V.Z));
			}

			// 4. Below: (800, -400) -> minimum-speed arc down to the target.
			{
				const FVector Target = From + Dir * 800.0 + FVector(0.0, 0.0, -400.0);
				const FVector V = VeloxMoveSim::JumpPadLaunchVelocity(From, Target, GravityZ);
				const double T = DescendingTimeAtHeight(V, G, -400.0);
				TestTrue(Ctx + TEXT("below: lands within 1 cm"), T > 0.0 && FlightPosition(From, V, G, T).Equals(Target, LandingToleranceCm));
				const double ApexZ = From.Z + V.Z * V.Z / (2.0 * G);
				TestTrue(Ctx + TEXT("below: apex not below the target"), ApexZ >= Target.Z);
			}

			// 5. Unsolvable: directly below the pad, or no gravity.
			{
				TestTrue(Ctx + TEXT("directly below: zero"), VeloxMoveSim::JumpPadLaunchVelocity(From, From + FVector(0.0, 0.0, -100.0), GravityZ).IsZero());
				TestTrue(Ctx + TEXT("no gravity: zero"), VeloxMoveSim::JumpPadLaunchVelocity(From, From + Dir * 800.0 + FVector(0.0, 0.0, 320.0), 0.f).IsZero());
			}
		}
	}

	// Actor-level launch selection and validation (ADR 0002 section 4: MaxLaunchSpeed default 4000 cm/s).
	const float MaxLaunchSpeed = 4000.f;
	const FVector Pad(500.0, -1500.0, 70.0);
	const FVector Fixed(100.0, 200.0, 1500.0);
	TestEqual(TEXT("FixedVelocity returns LaunchVelocity"), AVeloxJumpPad::ComputeLaunchVelocityFor(EVeloxJumpPadMode::FixedVelocity, Pad, Pad + FVector(800.0, 0.0, 320.0), Fixed, GravityZ), Fixed);
	const FVector Aim = AVeloxJumpPad::ComputeLaunchVelocityFor(EVeloxJumpPadMode::AimAtTarget, Pad, Pad + FVector(800.0, 0.0, 320.0), Fixed, GravityZ);
	TestEqual(TEXT("AimAtTarget uses JumpPadLaunchVelocity"), Aim, VeloxMoveSim::JumpPadLaunchVelocity(Pad, Pad + FVector(800.0, 0.0, 320.0), GravityZ));
	TestTrue(TEXT("pad A launch is valid"), AVeloxJumpPad::CheckLaunchVelocity(Aim, MaxLaunchSpeed) == EVeloxJumpPadLaunchCheck::Ok);
	TestTrue(TEXT("zero launch is unsolvable"), AVeloxJumpPad::CheckLaunchVelocity(FVector::ZeroVector, MaxLaunchSpeed) == EVeloxJumpPadLaunchCheck::Unsolvable);
	// Q3 AimAtTarget singularity: a target 10 cm above at 800 cm needs Vxy = 800 / sqrt(2 * 10 / g).
	const FVector NearLevel = AVeloxJumpPad::ComputeLaunchVelocityFor(EVeloxJumpPadMode::AimAtTarget, Pad, Pad + FVector(800.0, 0.0, 10.0), Fixed, GravityZ);
	TestTrue(TEXT("near-level AimAtTarget target is too fast"), AVeloxJumpPad::CheckLaunchVelocity(NearLevel, MaxLaunchSpeed) == EVeloxJumpPadLaunchCheck::TooFast);
	TestTrue(TEXT("exactly MaxLaunchSpeed is valid"), AVeloxJumpPad::CheckLaunchVelocity(FVector(0.0, 0.0, MaxLaunchSpeed), MaxLaunchSpeed) == EVeloxJumpPadLaunchCheck::Ok);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
