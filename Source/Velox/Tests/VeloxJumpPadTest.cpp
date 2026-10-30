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

// ADR 0005 section 4: VeloxMoveSim::BallisticPoint, the arc AVeloxJumpPad::ComputeLandingPoint traces for bot nav links.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementBallisticPointTest, "Velox.Movement.BallisticPoint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxMovementBallisticPointTest::RunTest(const FString& Parameters)
{
	using namespace VeloxJumpPadTest;

	const float GravityZ = GetDefault<UVeloxMovementSettings>()->BuildSimParams().GravityZ;
	const double G = -static_cast<double>(GravityZ);
	TestTrue(TEXT("default gravity points down"), G > 0.0);

	const FVector Origin(-1234.5, 678.0, 90.0);
	const FVector V0(600.0, -350.0, 1200.0);

	// t = 0 is the origin; the formula matches the analytic flight position at arbitrary times.
	TestTrue(TEXT("t=0 is the origin"), VeloxMoveSim::BallisticPoint(Origin, V0, GravityZ, 0.f).Equals(Origin, 1e-6));
	for (const double T : { 0.1, 0.37, 1.0, 1.9 })
	{
		TestTrue(FString::Printf(TEXT("t=%.2f matches Origin + V0 t + g t^2 / 2"), T),
			VeloxMoveSim::BallisticPoint(Origin, V0, GravityZ, static_cast<float>(T)).Equals(FlightPosition(Origin, V0, G, T), 1e-3));
	}

	// Apex at t = Vz / |g|: height Vz^2 / 2|g| above the origin, the highest point, and the arc is symmetric about it.
	const double ApexT = V0.Z / G;
	const FVector Apex = VeloxMoveSim::BallisticPoint(Origin, V0, GravityZ, static_cast<float>(ApexT));
	TestTrue(TEXT("apex height is Vz^2 / 2g"), FMath::IsNearlyEqual(Apex.Z - Origin.Z, V0.Z * V0.Z / (2.0 * G), 1e-2));
	for (const double Dt : { 1.0 / 60.0, 0.1, 0.25 })
	{
		const FVector Before = VeloxMoveSim::BallisticPoint(Origin, V0, GravityZ, static_cast<float>(ApexT - Dt));
		const FVector After = VeloxMoveSim::BallisticPoint(Origin, V0, GravityZ, static_cast<float>(ApexT + Dt));
		TestTrue(FString::Printf(TEXT("symmetric heights at apex +- %.3f s"), Dt), FMath::IsNearlyEqual(Before.Z, After.Z, 1e-2));
		TestTrue(FString::Printf(TEXT("apex is higher than apex +- %.3f s"), Dt), Apex.Z > Before.Z && Apex.Z > After.Z);
		TestTrue(FString::Printf(TEXT("horizontal motion is uniform around the apex (+- %.3f s)"), Dt),
			FVector::Dist2D(Before, Apex) > 0.0 && FMath::IsNearlyEqual(FVector::Dist2D(Before, Apex), FVector::Dist2D(Apex, After), 1e-2));
	}

	// Symmetric landing: back at the launch height at t = 2 Vz / |g|, 2 Vz Vxy / |g| away horizontally.
	const double LandT = 2.0 * V0.Z / G;
	const FVector Land = VeloxMoveSim::BallisticPoint(Origin, V0, GravityZ, static_cast<float>(LandT));
	TestTrue(TEXT("lands at the launch height after 2 Vz / g"), FMath::IsNearlyEqual(Land.Z, Origin.Z, 1e-2));
	TestTrue(TEXT("landing distance is Vxy * 2 Vz / g"), FMath::IsNearlyEqual(FVector::Dist2D(Land, Origin), V0.Size2D() * LandT, 1e-2));

	// Consistency with the pad launch: pad A of L_MovementTest (AimAtTarget) reaches its target at the apex.
	const FVector Pad(500.0, -1500.0, 70.0);
	const FVector Target(1100.0, -1500.0, 420.0);
	const FVector Launch = VeloxMoveSim::JumpPadLaunchVelocity(Pad, Target, GravityZ);
	TestTrue(TEXT("pad A: the arc's apex is the AimAtTarget target (1 cm)"),
		VeloxMoveSim::BallisticPoint(Pad, Launch, GravityZ, static_cast<float>(Launch.Z / G)).Equals(Target, LandingToleranceCm));
	// Descending branch from the apex to the platform top (capsule bottom 30 cm above it): ~x 1276, on the 1100..1500 platform.
	const double DescendT = DescendingTimeAtHeight(Launch, G, (320.0 + 70.0) - Pad.Z);
	const FVector PadALanding = VeloxMoveSim::BallisticPoint(Pad, Launch, GravityZ, static_cast<float>(DescendT));
	TestTrue(FString::Printf(TEXT("pad A: descending branch crosses the platform height on the platform (x=%.0f)"), PadALanding.X),
		PadALanding.X > 1100.0 && PadALanding.X < 1500.0 && DescendT > Launch.Z / G);

	// No gravity: a straight line.
	TestTrue(TEXT("zero gravity is a straight line"), VeloxMoveSim::BallisticPoint(Origin, V0, 0.f, 2.f).Equals(Origin + V0 * 2.0, 1e-6));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
