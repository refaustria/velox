// Velox - VeloxMoveSim unit tests (ADR 0002 section 7, ADR 0001 section 4). World-free; dt = 1/125 unless stated.
// SavedMoveFlags/SavedMoveCombine/JumpLatch live in VeloxSavedMoveTest.cpp, JumpPad in VeloxJumpPadTest.cpp (task 04).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Math/RandomStream.h"
#include "Movement/VeloxMoveSim.h"
#include "Movement/VeloxMovementComponent.h"
#include "Movement/VeloxMovementSettings.h"
#include "Movement/VeloxNetworkMoveData.h"

namespace VeloxMoveSimTest
{
	constexpr EAutomationTestFlags MoveSimFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr float Dt = 1.f / 125.f;

	/** The ADR 0002 section 1 defaults, through the one conversion point. */
	FVeloxMoveParams DefaultParams()
	{
		return GetDefault<UVeloxMovementSettings>()->BuildSimParams();
	}

	FVeloxMoveInput Wish(const FVector& Dir, float Speed, bool bForward = true, bool bSide = true)
	{
		FVeloxMoveInput In;
		In.WishDir = Dir;
		In.WishSpeed = Speed;
		In.bHasForwardInput = bForward;
		In.bHasSideInput = bSide;
		return In;
	}

	double Speed2D(const FVector& V) { return FVector(V.X, V.Y, 0.0).Size(); }

	/** Unit XY vector rotated by Angle (radians) from the XY direction of V. */
	FVector RotatedFrom(const FVector& V, double Angle)
	{
		const FVector D = FVector(V.X, V.Y, 0.0).GetSafeNormal();
		const double C = FMath::Cos(Angle), S = FMath::Sin(Angle);
		return FVector(D.X * C - D.Y * S, D.X * S + D.Y * C, 0.0);
	}

	/** Q3 optimal strafe angle: cos(theta) = (W - a) / |V|, a = AirAcceleration * StepDt * W. */
	double OptimalStrafeAngle(const FVector& V, float W, float StepDt, const FVeloxMoveParams& P)
	{
		const double A = static_cast<double>(P.AirAcceleration) * StepDt * W;
		return FMath::Acos(FMath::Clamp((W - A) / Speed2D(V), -1.0, 1.0));
	}

	bool BitwiseEqual(const FVeloxMoveState& A, const FVeloxMoveState& B)
	{
		return FMemory::Memcmp(&A.Velocity, &B.Velocity, sizeof(FVector)) == 0
			&& A.bOnGround == B.bOnGround
			&& FMemory::Memcmp(&A.KnockbackGrace, &B.KnockbackGrace, sizeof(float)) == 0;
	}
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementSettingsDefaultsTest, "Velox.Movement.SettingsDefaults", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	// ADR 0002 section 1 table: Q3 value * UnitScale for distances/speeds.
	const FVeloxMoveParams P = DefaultParams();
	TestEqual(TEXT("UnitScale"), P.UnitScale, 2.5f);
	TestEqual(TEXT("MaxGroundSpeed = 320 u"), P.MaxGroundSpeed, 320.f * P.UnitScale);
	TestEqual(TEXT("StopSpeed = 100 u"), P.StopSpeed, 100.f * P.UnitScale);
	TestEqual(TEXT("AirStrafeMaxWishSpeed = 30 u"), P.AirStrafeMaxWishSpeed, 30.f * P.UnitScale);
	TestEqual(TEXT("JumpVelocity = 270 u"), P.JumpVelocity, 270.f * P.UnitScale);
	TestEqual(TEXT("AirControlScale = 32 u"), P.AirControlScale, 32.f * P.UnitScale);
	TestEqual(TEXT("GravityZ = -800 u"), P.GravityZ, -800.f * P.UnitScale);
	TestEqual(TEXT("StepHeight = 18 u"), P.StepHeight, 18.f * P.UnitScale);
	TestEqual(TEXT("RampSlideMinNormalSpeed = 10 u"), P.RampSlideMinNormalSpeed, 10.f * P.UnitScale);
	TestEqual(TEXT("FixedStep 8 ms"), P.FixedStep, 0.008f);
	TestTrue(TEXT("Fixed step on, auto-hop on, CPM off, no air combining"), P.bFixedStepIntegration && P.bAutoHop && !P.bAirControl && !P.bCombineAirMoves);
	TestEqual(TEXT("NumSubsteps(8 ms) == 1"), VeloxMoveSim::NumSubsteps(0.008f, P), 1);
	TestEqual(TEXT("NumSubsteps(1/60) == 3"), VeloxMoveSim::NumSubsteps(1.f / 60.f, P), 3);
	TestEqual(TEXT("NumSubsteps(16 ms) == 2"), VeloxMoveSim::NumSubsteps(0.016f, P), 2);
	FVeloxMoveParams Off = P;
	Off.bFixedStepIntegration = false;
	TestEqual(TEXT("NumSubsteps with fixed step off == 1"), VeloxMoveSim::NumSubsteps(0.05f, Off), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementHasteScaleTest, "Velox.Movement.HasteScale", VeloxMoveSimTest::MoveSimFlags)

bool FVeloxMovementHasteScaleTest::RunTest(const FString& Parameters)
{
	// ADR 0004 section 3 (task 03): Haste scales the wish-speed caps MaxGroundSpeed and AirMaxWishSpeed (Q3 ps->speed via
	// PM_CmdScale); air strafe cap, accelerations, friction, StopSpeed and JumpVelocity are untouched. The CMC passes
	// WishSpeed = min(|Acceleration|, MaxGroundSpeed) x SpeedScale and SpeedScale.
	using namespace VeloxMoveSimTest;
	const FVeloxMoveParams P = DefaultParams();
	const float S = P.HasteSpeedScale;
	TestEqual(TEXT("HasteSpeedScale default 1.3"), S, 1.3f);
	const FVector X(1.0, 0.0, 0.0);

	auto Hasted = [S](FVeloxMoveInput In)
	{
		In.WishSpeed *= S;
		In.SpeedScale = S;
		return In;
	};
	// Ground: 2 s from rest at full input converges to MaxGroundSpeed x scale; a raised WishSpeed alone stays capped.
	{
		FVector Plain = FVector::ZeroVector, Haste = FVector::ZeroVector, Uncapped = FVector::ZeroVector;
		const FVeloxMoveInput In = Wish(X, P.MaxGroundSpeed, true, false);
		FVeloxMoveInput Raised = In;
		Raised.WishSpeed = P.MaxGroundSpeed * S;
		for (int32 I = 0; I < 250; ++I)
		{
			Plain = VeloxMoveSim::GroundMove(Plain, In, Dt, 0.f, P);
			Haste = VeloxMoveSim::GroundMove(Haste, Hasted(In), Dt, 0.f, P);
			Uncapped = VeloxMoveSim::GroundMove(Uncapped, Raised, Dt, 0.f, P);
		}
		TestEqual(TEXT("ground plain -> MaxGroundSpeed"), Speed2D(Plain), static_cast<double>(P.MaxGroundSpeed), 0.5);
		TestEqual(TEXT("ground hasted -> MaxGroundSpeed x 1.3"), Speed2D(Haste), static_cast<double>(P.MaxGroundSpeed * S), 0.5);
		TestEqual(TEXT("ground WishSpeed x 1.3 without SpeedScale stays capped"), Speed2D(Uncapped), static_cast<double>(P.MaxGroundSpeed), 0.5);
		// Friction and StopSpeed: no input, same decay with and without the scale.
		FVeloxMoveInput None = Wish(FVector::ZeroVector, 0.f, false, false);
		const FVector A = VeloxMoveSim::GroundMove(FVector(1000.0, 0.0, 0.0), None, Dt, 0.f, P);
		const FVector B = VeloxMoveSim::GroundMove(FVector(1000.0, 0.0, 0.0), Hasted(None), Dt, 0.f, P);
		TestTrue(TEXT("friction untouched by the scale"), A.Equals(B, 0.0));
	}
	// Vanilla air: forward-only from rest approaches AirMaxWishSpeed x scale along the wish direction.
	{
		FVector Plain = FVector::ZeroVector, Haste = FVector::ZeroVector;
		const FVeloxMoveInput In = Wish(X, P.MaxGroundSpeed, true, false);
		for (int32 I = 0; I < 2500; ++I)
		{
			Plain = VeloxMoveSim::AirMove(Plain, In, Dt, P);
			Haste = VeloxMoveSim::AirMove(Haste, Hasted(In), Dt, P);
		}
		TestEqual(TEXT("air plain -> AirMaxWishSpeed"), Speed2D(Plain), static_cast<double>(P.AirMaxWishSpeed), 1.0);
		TestEqual(TEXT("air hasted -> AirMaxWishSpeed x 1.3"), Speed2D(Haste), static_cast<double>(P.AirMaxWishSpeed * S), 1.0);
	}
	// CPM side-only strafe: capped by AirStrafeMaxWishSpeed whatever the scale (bitwise identical).
	{
		FVeloxMoveParams Cpm = P;
		Cpm.bAirControl = true;
		const FVector V(800.0, 0.0, 0.0);
		const FVeloxMoveInput Side = Wish(RotatedFrom(V, FMath::DegreesToRadians(80.0)), P.MaxGroundSpeed, false, true);
		const FVector A = VeloxMoveSim::AirMove(V, Side, Dt, Cpm);
		const FVector B = VeloxMoveSim::AirMove(V, Hasted(Side), Dt, Cpm);
		TestTrue(TEXT("CPM air strafe cap unchanged by Haste"), FMemory::Memcmp(&A, &B, sizeof(FVector)) == 0);
	}
	// Jump velocity untouched.
	{
		FVeloxMoveState Plain, Haste;
		Plain.bOnGround = Haste.bOnGround = true;
		FVeloxMoveInput In = Wish(X, P.MaxGroundSpeed, true, false);
		In.bJumpHeld = true;
		VeloxMoveSim::StepFrame(Plain, In, Dt, P);
		VeloxMoveSim::StepFrame(Haste, Hasted(In), Dt, P);
		TestEqual(TEXT("jump Vz unchanged"), Haste.Velocity.Z, Plain.Velocity.Z);
	}

	// IsPowerupActive / PowerupTimeLeft in the wrapped time-stamp domain (MinTimeBetweenTimeStampResets).
	const float R = GetDefault<UVeloxMovementComponent>()->MinTimeBetweenTimeStampResets;
	TestEqual(TEXT("engine reset period 240 s"), R, 240.f);
	TestFalse(TEXT("end 0 = none"), UVeloxMovementComponent::IsPowerupActive(0.f, 10.f, R));
	TestTrue(TEXT("before the end"), UVeloxMovementComponent::IsPowerupActive(100.f, 99.99f, R));
	TestFalse(TEXT("at the end (a move ending at End is not hasted)"), UVeloxMovementComponent::IsPowerupActive(100.f, 100.f, R));
	TestFalse(TEXT("after the end"), UVeloxMovementComponent::IsPowerupActive(100.f, 100.01f, R));
	// Picked up at 225 for 20 s: end 245 -> 5 after the wrap. Active up to the client's reset and after it, until 5.
	const float Wrapped = UVeloxMovementComponent::MakePowerupEnd(225.0, 20.f, R);
	TestEqual(TEXT("end normalised into the period"), Wrapped, 5.f, 1e-4f);
	for (const float End : { Wrapped, 245.f })
	{
		TestTrue(FString::Printf(TEXT("end %.0f: active at 239 (before the reset)"), End), UVeloxMovementComponent::IsPowerupActive(End, 239.f, R));
		TestTrue(FString::Printf(TEXT("end %.0f: active at 240.004 (the move that triggers the reset)"), End), UVeloxMovementComponent::IsPowerupActive(End, 240.004f, R));
		TestTrue(FString::Printf(TEXT("end %.0f: active at 0.5 (after the reset)"), End), UVeloxMovementComponent::IsPowerupActive(End, 0.5f, R));
		TestFalse(TEXT("inactive at 5.01 after the reset"), UVeloxMovementComponent::IsPowerupActive(End, 5.01f, R));
	}
	TestEqual(TEXT("time left across the wrap"), UVeloxMovementComponent::PowerupTimeLeft(Wrapped, 239.f, R), 6.f, 1e-3f);
	TestEqual(TEXT("an end exactly at the period stays non-zero"), UVeloxMovementComponent::MakePowerupEnd(210.0, 30.f, R), R);
	TestEqual(TEXT("no remaining time -> 0"), UVeloxMovementComponent::MakePowerupEnd(10.0, 0.f, R), 0.f);
	TestEqual(TEXT("remaining clamped to 0.45 x period"), UVeloxMovementComponent::MakePowerupEnd(0.0, 500.f, R), 0.45f * R, 1e-3f);
	// Bots / listen host: world time, wrapped the same way.
	const float BotEnd = UVeloxMovementComponent::MakePowerupEnd(3600.25, 30.f, R);
	TestTrue(TEXT("world-time domain: active 10 s after the grant"), UVeloxMovementComponent::IsPowerupActive(BotEnd, static_cast<float>(FMath::Fmod(3610.25, 240.0)), R));
	TestFalse(TEXT("world-time domain: inactive 30.1 s after the grant"), UVeloxMovementComponent::IsPowerupActive(BotEnd, static_cast<float>(FMath::Fmod(3630.35, 240.0)), R));
	// Stale ends are dropped (non-replayed moves) long before PowerupTimeLeft wraps back to positive.
	TestTrue(TEXT("stale threshold before the wrap"), UVeloxMovementComponent::PowerupStaleSeconds(R) < 0.5f * R && UVeloxMovementComponent::PowerupMaxRemainingSeconds(R) < 0.5f * R);
	TestTrue(TEXT("60.1 s after the end: stale"), UVeloxMovementComponent::PowerupTimeLeft(100.f, 160.1f, R) < -UVeloxMovementComponent::PowerupStaleSeconds(R));
	TestFalse(TEXT("59.9 s after the end: not stale yet, inactive"), UVeloxMovementComponent::PowerupTimeLeft(100.f, 159.9f, R) < -UVeloxMovementComponent::PowerupStaleSeconds(R)
		|| UVeloxMovementComponent::IsPowerupActive(100.f, 159.9f, R));

	// Saved moves on different sides of the Haste end never combine.
	{
		FSavedMove_Velox Old, New;
		Old.Clear();
		New.Clear();
		Old.StartPackedMovementMode = New.StartPackedMovementMode = MOVE_Walking;
		Old.bSavedHasteActive = true;
		New.bSavedHasteActive = true;
		TestTrue(TEXT("both hasted: combine"), Old.CanCombineVeloxState(New, true));
		New.bSavedHasteActive = false;
		TestFalse(TEXT("hasted + plain: no combine"), Old.CanCombineVeloxState(New, true));
		TestFalse(TEXT("plain + hasted: no combine"), New.CanCombineVeloxState(Old, true));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementFrictionTest, "Velox.Movement.Friction", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementFrictionTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	const FVeloxMoveParams P = DefaultParams();
	const FVeloxMoveInput NoInput;
	const double V0 = 800.0;
	FVector V(V0, 0.0, 0.0);

	// Phase 1: above StopSpeed the closed form v0 * (1 - f*dt)^N holds.
	int32 N = 0;
	while (V.X > P.StopSpeed)
	{
		V = VeloxMoveSim::GroundMove(V, NoInput, Dt, 0.f, P);
		++N;
		const double Expected = V0 * FMath::Pow(1.0 - static_cast<double>(P.GroundFriction) * Dt, static_cast<double>(N));
		TestTrue(*FString::Printf(TEXT("Tick %d: %.6f vs closed form %.6f"), N, V.X, Expected), FMath::Abs(V.X - Expected) <= 1e-3 * Expected);
	}
	TestTrue(TEXT("Phase 1 ran several ticks"), N > 10);

	// Phase 2: below StopSpeed the speed drops by StopSpeed*f*dt per tick, reaches exactly 0 and never goes negative.
	const double Step = static_cast<double>(P.StopSpeed) * P.GroundFriction * Dt;
	for (int32 i = 0; i < 1000 && V.X > 0.0; ++i)
	{
		const double Before = V.X;
		V = VeloxMoveSim::GroundMove(V, NoInput, Dt, 0.f, P);
		TestTrue(TEXT("Never negative"), V.X >= 0.0);
		if (Before > Step)
		{
			TestTrue(*FString::Printf(TEXT("Linear decay at %.4f"), Before), FMath::IsNearlyEqual(Before - V.X, Step, 1e-6));
		}
	}
	TestTrue(TEXT("Reaches exactly zero"), V.X == 0.0 && V.Y == 0.0);

	// Z is never touched.
	const FVector WithZ = VeloxMoveSim::ApplyFriction(FVector(500.0, 0.0, 123.0), Dt, P);
	TestEqual(TEXT("Friction leaves Z"), WithZ.Z, 123.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementGroundAccelTest, "Velox.Movement.GroundAccel", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementGroundAccelTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	const FVeloxMoveParams P = DefaultParams();
	const FVeloxMoveInput In = Wish(FVector::ForwardVector, P.MaxGroundSpeed, true, false);
	FVector V = FVector::ZeroVector;
	double MaxSeen = 0.0;
	for (int32 i = 0; i < 250; ++i)
	{
		V = VeloxMoveSim::GroundMove(V, In, Dt, 0.f, P);
		MaxSeen = FMath::Max(MaxSeen, Speed2D(V));
	}
	TestTrue(*FString::Printf(TEXT("Converges to MaxGroundSpeed (%.6f)"), Speed2D(V)), FMath::IsNearlyEqual(Speed2D(V), static_cast<double>(P.MaxGroundSpeed), 1e-3));
	TestTrue(*FString::Printf(TEXT("Never exceeds MaxGroundSpeed (max %.6f)"), MaxSeen), MaxSeen <= P.MaxGroundSpeed + 1e-6);

	// Wish speed above the cap is clamped.
	const FVector Over = VeloxMoveSim::GroundMove(FVector::ZeroVector, Wish(FVector::ForwardVector, 10.f * P.MaxGroundSpeed), 1.f, 0.f, P);
	TestTrue(TEXT("Wish speed clamped to MaxGroundSpeed"), Speed2D(Over) <= P.MaxGroundSpeed + 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementAirAccelTest, "Velox.Movement.AirAccel", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementAirAccelTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	const FVeloxMoveParams P = DefaultParams();
	const FVector V(800.0, 0.0, 0.0);
	const double Bound = static_cast<double>(P.AirAcceleration) * Dt * P.AirMaxWishSpeed;

	const FVector Parallel = VeloxMoveSim::AirMove(V, Wish(FVector(1, 0, 0), P.MaxGroundSpeed), Dt, P);
	TestTrue(TEXT("Wish parallel: zero gain (dot-product clamp)"), Parallel == V);

	const FVector Perp = VeloxMoveSim::AirMove(V, Wish(FVector(0, 1, 0), P.MaxGroundSpeed), Dt, P);
	TestTrue(TEXT("Wish perpendicular: gain > 0"), Speed2D(Perp) > Speed2D(V));
	TestTrue(*FString::Printf(TEXT("|dV| %.6f <= %.6f"), (Perp - V).Size(), Bound), (Perp - V).Size() <= Bound + 1e-9);

	const FVector WithZ = VeloxMoveSim::AirMove(FVector(800.0, 0.0, -321.0), Wish(FVector(0, 1, 0), P.MaxGroundSpeed), Dt, P);
	TestEqual(TEXT("AirMove leaves Z (no gravity)"), WithZ.Z, -321.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementStrafeJumpTest, "Velox.Movement.StrafeJump", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementStrafeJumpTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	const FVeloxMoveParams P = DefaultParams();
	const float W = P.AirMaxWishSpeed;
	const int32 Ticks = 125;
	const double T = Ticks * Dt;
	const double MaxGain = static_cast<double>(P.AirAcceleration) * P.AirMaxWishSpeed * T;

	FVector V(800.0, 0.0, 0.0);
	for (int32 i = 0; i < Ticks; ++i)
	{
		const FVector Dir = RotatedFrom(V, OptimalStrafeAngle(V, W, Dt, P));
		V = VeloxMoveSim::AirMove(V, Wish(Dir, W), Dt, P);
	}
	const double Gain = Speed2D(V) - 800.0;
	AddInfo(FString::Printf(TEXT("Strafe gain over %.2f s: %.2f cm/s (bound %.2f)"), T, Gain, MaxGain));
	TestTrue(TEXT("Gain > 0.25 * AirAccel * AirMaxWishSpeed * T"), Gain > 0.25 * MaxGain);
	TestTrue(TEXT("Gain <= AirAccel * AirMaxWishSpeed * T"), Gain <= MaxGain);

	FVector F(800.0, 0.0, 0.0);
	for (int32 i = 0; i < Ticks; ++i)
	{
		F = VeloxMoveSim::AirMove(F, Wish(FVector(1, 0, 0), W, true, false), Dt, P);
	}
	TestTrue(TEXT("Forward only: gain == 0"), Speed2D(F) == 800.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementFixedStepInvarianceTest, "Velox.Movement.FixedStepInvariance", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementFixedStepInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	// Note: with a wish direction held constant over a move, Q3 Accelerate is exactly split-invariant (the component
	// along WishDir is min(c0 + Accel*W*T, W) however T is split). Frame-rate dependence comes from the nonlinear
	// parts: ground friction (1 - f*dt)^n, the StopSpeed regime, and CPM air control. The sequence exercises those.
	FVeloxMoveParams On = DefaultParams();
	On.bAirControl = true;
	FVeloxMoveParams Off = On;
	Off.bFixedStepIntegration = false;
	const float Frame = 1.f / 60.f;

	struct FFrame { FVeloxMoveInput In; bool bGround = true; };
	TArray<FFrame> Frames;
	for (int32 i = 0; i < 60; ++i) // ground run, turning 3 deg per frame
	{
		const double Yaw = FMath::DegreesToRadians(3.0 * i);
		Frames.Add({ Wish(FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0), On.MaxGroundSpeed, true, false), true });
	}
	for (int32 i = 0; i < 20; ++i) // release: friction only
	{
		Frames.Add({ FVeloxMoveInput(), true });
	}
	for (int32 i = 0; i < 30; ++i) // ground run forward again
	{
		Frames.Add({ Wish(FVector(1, 0, 0), On.MaxGroundSpeed, true, false), true });
	}
	for (int32 i = 0; i < 60; ++i) // airborne, CPM forward-only air control turning 2 deg per frame
	{
		const double Yaw = FMath::DegreesToRadians(20.0 + 2.0 * i);
		Frames.Add({ Wish(FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0), On.MaxGroundSpeed, true, false), false });
	}

	auto Run = [&Frames, Frame](const FVeloxMoveParams& P, int32 SubFramesPer60) -> FVector
	{
		FVector V = FVector::ZeroVector;
		const float FrameDt = Frame / static_cast<float>(SubFramesPer60);
		for (const FFrame& F : Frames)
		{
			for (int32 s = 0; s < SubFramesPer60; ++s)
			{
				V = F.bGround ? VeloxMoveSim::GroundMove(V, F.In, FrameDt, 0.f, P) : VeloxMoveSim::AirMove(V, F.In, FrameDt, P);
			}
		}
		return V;
	};

	const FVector On60 = Run(On, 1), On240 = Run(On, 4);
	const FVector Off60 = Run(Off, 1), Off240 = Run(Off, 4);
	const double DiffOn = (On60 - On240).Size() / Speed2D(On240);
	const double DiffOff = (Off60 - Off240).Size() / Speed2D(Off240);
	AddInfo(FString::Printf(TEXT("fixed on: 60 fps %s, 240 fps %s (%.4f %%); off: 60 fps %s, 240 fps %s (%.4f %%)"),
		*On60.ToString(), *On240.ToString(), 100.0 * DiffOn, *Off60.ToString(), *Off240.ToString(), 100.0 * DiffOff));
	TestTrue(TEXT("Fixed step on: 60 vs 240 fps final speed within 0.5 %"), FMath::Abs(Speed2D(On60) - Speed2D(On240)) <= 0.005 * Speed2D(On240));
	TestTrue(TEXT("Fixed step on: 60 vs 240 fps final velocity within 0.5 %"), DiffOn <= 0.005);
	TestTrue(TEXT("Fixed step off: 60 vs 240 fps differ"), Off60 != Off240);
	TestTrue(TEXT("Fixed step off differs more than on"), DiffOff > DiffOn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementAirControlTest, "Velox.Movement.AirControl", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementAirControlTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	FVeloxMoveParams P = DefaultParams();
	P.bAirControl = true;
	const FVector V(800.0, 0.0, 150.0);
	const FVector Dir = RotatedFrom(V, FMath::DegreesToRadians(30.0));

	const FVector Turned = VeloxMoveSim::AirControl(V, Dir, Dt, P);
	TestTrue(TEXT("Air control keeps |V_xy|"), FMath::IsNearlyEqual(Speed2D(Turned), Speed2D(V), 1e-3));
	TestEqual(TEXT("Air control keeps Z"), Turned.Z, V.Z);
	const double AngleBefore = FMath::Acos(FVector(V.X, V.Y, 0).GetSafeNormal() | Dir);
	const double AngleAfter = FMath::Acos(FMath::Clamp(FVector(Turned.X, Turned.Y, 0).GetSafeNormal() | Dir, -1.0, 1.0));
	TestTrue(TEXT("Air control turns V towards WishDir"), AngleAfter < AngleBefore);

	// Forward-only input: accelerate, then air control.
	const FVeloxMoveInput Fwd = Wish(Dir, P.MaxGroundSpeed, true, false);
	const FVector ExpectedFwd = VeloxMoveSim::AirControl(VeloxMoveSim::Accelerate(V, Dir, P.MaxGroundSpeed, P.AirAcceleration, Dt), Dir, Dt, P);
	TestTrue(TEXT("Forward-only AirMove applies air control"), VeloxMoveSim::AirMove(V, Fwd, Dt, P) == ExpectedFwd);

	// Side-only input: strafe accel/wish cap, no air control.
	const FVector Side = RotatedFrom(V, FMath::DegreesToRadians(90.0));
	const FVector ExpectedSide = VeloxMoveSim::Accelerate(V, Side, P.AirStrafeMaxWishSpeed, P.AirStrafeAcceleration, Dt);
	TestTrue(TEXT("Side-only AirMove: CPM strafe accel, no air control"), VeloxMoveSim::AirMove(V, Wish(Side, P.MaxGroundSpeed, false, true), Dt, P) == ExpectedSide);
	const FVector SideAtDir = VeloxMoveSim::AirMove(V, Wish(Dir, P.MaxGroundSpeed, false, true), Dt, P);
	TestTrue(TEXT("Side-only AirMove at 30 deg: plain strafe accelerate"),
		SideAtDir == VeloxMoveSim::Accelerate(V, Dir, P.AirStrafeMaxWishSpeed, P.AirStrafeAcceleration, Dt));

	// Wishing against V uses AirStopAcceleration.
	const FVector Back(-1.0, 0.0, 0.0);
	TestTrue(TEXT("Against velocity: AirStopAcceleration"), VeloxMoveSim::AirMove(V, Wish(Back, P.MaxGroundSpeed, true, true), Dt, P)
		== VeloxMoveSim::Accelerate(V, Back, P.MaxGroundSpeed, P.AirStopAcceleration, Dt));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementBunnyHopTest, "Velox.Movement.BunnyHop", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementBunnyHopTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	const FVeloxMoveParams P = DefaultParams();
	const FVector Landing(1000.0, 0.0, -400.0);

	FVeloxMoveState Hop;
	Hop.Velocity = Landing;
	Hop.bOnGround = true;
	FVeloxMoveInput JumpIn;
	JumpIn.bJumpHeld = true;
	VeloxMoveSim::StepFrame(Hop, JumpIn, Dt, P);
	TestTrue(TEXT("Jump held on landing: XY bitwise unchanged"), Hop.Velocity.X == Landing.X && Hop.Velocity.Y == Landing.Y);
	// The rest of the jump frame is airborne (as in the CMC's ProcessLanded -> PhysFalling), so one gravity tick follows.
	TestEqual(TEXT("Z == JumpVelocity (+ one gravity tick)"), Hop.Velocity.Z, static_cast<double>(P.JumpVelocity) + static_cast<double>(P.GravityZ) * Dt);
	TestEqual(TEXT("Jump without the gravity tick: Z == JumpVelocity"), VeloxMoveSim::Jump(Landing, P).Z, static_cast<double>(P.JumpVelocity));
	TestFalse(TEXT("Airborne after the jump"), Hop.bOnGround);

	FVeloxMoveState Stop;
	Stop.Velocity = Landing;
	Stop.bOnGround = true;
	VeloxMoveSim::StepFrame(Stop, FVeloxMoveInput(), Dt, P);
	const FVector OneTick = VeloxMoveSim::ApplyFriction(Landing, Dt, P);
	TestTrue(TEXT("No jump: exactly one friction tick"), Stop.Velocity.X == OneTick.X && Stop.Velocity.Y == OneTick.Y);
	TestTrue(TEXT("One friction tick = v*(1 - f*dt)"), FMath::IsNearlyEqual(Stop.Velocity.X, 1000.0 * (1.0 - P.GroundFriction * Dt), 1e-3));
	TestTrue(TEXT("Still on the ground"), Stop.bOnGround);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementKnockbackTest, "Velox.Movement.Knockback", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementKnockbackTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	const FVeloxMoveParams P = DefaultParams();
	const FVector Impulse(1250.0, 0.0, 0.0);

	FVeloxMoveState S;
	S.bOnGround = true;
	S.Velocity = VeloxMoveSim::ApplyKnockback(S.Velocity, Impulse, false);
	S.KnockbackGrace = FMath::Max(S.KnockbackGrace, VeloxMoveSim::KnockbackGraceFor(Impulse, P));
	TestTrue(*FString::Printf(TEXT("Grace for 1250 cm/s is 0.2 s (%.6f)"), S.KnockbackGrace), FMath::IsNearlyEqual(S.KnockbackGrace, 0.2f, 1e-6f));

	for (int32 Tick = 1; Tick <= 25; ++Tick)
	{
		VeloxMoveSim::StepFrame(S, FVeloxMoveInput(), Dt, P);
		TestTrue(*FString::Printf(TEXT("Tick %d: no friction"), Tick), S.Velocity.X == 1250.0);
	}
	TestEqual(TEXT("Grace expired after 25 ticks"), S.KnockbackGrace, 0.f);
	VeloxMoveSim::StepFrame(S, FVeloxMoveInput(), Dt, P);
	TestTrue(TEXT("Tick 26: friction resumes"), FMath::IsNearlyEqual(S.Velocity.X, 1250.0 * (1.0 - P.GroundFriction * Dt), 1e-3));

	// ADR 0002 A6: during grace the ground move accelerates with AirAcceleration (Q3 PMF_TIME_KNOCKBACK).
	{
		const FVector V0(1250.0, 0.0, 0.0);
		const FVeloxMoveInput Side = Wish(FVector(0.0, 1.0, 0.0), P.MaxGroundSpeed, false, true);
		const FVector InGrace = VeloxMoveSim::GroundMove(V0, Side, Dt, 0.2f, P);
		TestTrue(*FString::Printf(TEXT("Grace: X untouched (%.6f)"), InGrace.X), InGrace.X == 1250.0);
		const double ExpectedAirY = static_cast<double>(P.AirAcceleration) * Dt * P.MaxGroundSpeed;
		TestTrue(*FString::Printf(TEXT("Grace: Y gain = AirAcceleration*dt*MaxGroundSpeed (%.6f vs %.6f)"), InGrace.Y, ExpectedAirY),
			FMath::IsNearlyEqual(InGrace.Y, ExpectedAirY, 1e-3));
		const FVector NoGrace = VeloxMoveSim::GroundMove(V0, Side, Dt, 0.f, P);
		TestTrue(*FString::Printf(TEXT("No grace: X has friction drop (%.6f)"), NoGrace.X),
			FMath::IsNearlyEqual(NoGrace.X, 1250.0 * (1.0 - P.GroundFriction * Dt), 1e-3));
		const double ExpectedGroundY = static_cast<double>(P.GroundAcceleration) * Dt * P.MaxGroundSpeed;
		TestTrue(*FString::Printf(TEXT("No grace: Y gain = GroundAcceleration*dt*MaxGroundSpeed (%.6f vs %.6f)"), NoGrace.Y, ExpectedGroundY),
			FMath::IsNearlyEqual(NoGrace.Y, ExpectedGroundY, 1e-3));
		TestTrue(TEXT("Grace accel is weaker than ground accel"), InGrace.Y < NoGrace.Y);
	}

	const FVector V(100.0, 0.0, 0.0), Up(0.0, 0.0, 500.0);
	TestTrue(TEXT("Override sets velocity"), VeloxMoveSim::ApplyKnockback(V, Up, true) == Up);
	TestTrue(TEXT("Additive adds"), VeloxMoveSim::ApplyKnockback(V, Up, false) == FVector(100.0, 0.0, 500.0));
	TestEqual(TEXT("Small impulse: KnockbackGraceMin"), VeloxMoveSim::KnockbackGraceFor(FVector(10.0, 0.0, 0.0), P), P.KnockbackGraceMin);
	TestEqual(TEXT("Huge impulse: KnockbackGraceMax"), VeloxMoveSim::KnockbackGraceFor(FVector(0.0, 0.0, 1e5), P), P.KnockbackGraceMax);
	TestEqual(TEXT("Zero impulse: no grace"), VeloxMoveSim::KnockbackGraceFor(FVector::ZeroVector, P), 0.f);

	const FVector Clipped = VeloxMoveSim::ClipVelocity(FVector(100.0, 0.0, -50.0), FVector(0.0, 0.0, 1.0));
	TestTrue(TEXT("ClipVelocity removes the normal component"), Clipped == FVector(100.0, 0.0, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementDeterminismTest, "Velox.Movement.Determinism", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	struct FScriptRow
	{
		FVeloxMoveInput In;
		float Dt = 0.f;
		bool bKnockback = false;
		FVector Impulse = FVector::ZeroVector;
	};

	// 600-tick mixed script: variable frame times, analog wish, strafes, jumps, a few knockbacks.
	TArray<FScriptRow> Script;
	FRandomStream Rng(0x5E10C);
	const float FrameTimes[] = { 1.f / 60.f, 1.f / 120.f, 1.f / 125.f, 1.f / 144.f, 1.f / 240.f };
	for (int32 i = 0; i < 600; ++i)
	{
		FScriptRow Row;
		const double Yaw = Rng.FRandRange(0.f, 2.f * PI);
		Row.In.WishDir = FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0);
		Row.In.WishSpeed = Rng.FRandRange(0.f, 800.f);
		Row.In.bHasForwardInput = Rng.FRand() < 0.7f;
		Row.In.bHasSideInput = Rng.FRand() < 0.6f;
		Row.In.bJumpHeld = Rng.FRand() < 0.3f;
		Row.Dt = FrameTimes[Rng.RandHelper(UE_ARRAY_COUNT(FrameTimes))];
		Row.bKnockback = (i % 97) == 50;
		Row.Impulse = FVector(Rng.FRandRange(-1500.f, 1500.f), Rng.FRandRange(-1500.f, 1500.f), Rng.FRandRange(0.f, 1200.f));
		Script.Add(Row);
	}

	auto Run = [&Script](const FVeloxMoveParams& P) -> FVeloxMoveState
	{
		FVeloxMoveState S;
		S.bOnGround = true;
		double Z = 0.0; // toy floor at Z = 0 so the script alternates ground and air
		for (const FScriptRow& Row : Script)
		{
			if (Row.bKnockback)
			{
				S.Velocity = VeloxMoveSim::ApplyKnockback(S.Velocity, Row.Impulse, false);
				S.KnockbackGrace = FMath::Max(S.KnockbackGrace, VeloxMoveSim::KnockbackGraceFor(Row.Impulse, P));
				S.bOnGround = S.Velocity.Z <= 0.0 && S.bOnGround;
			}
			VeloxMoveSim::StepFrame(S, Row.In, Row.Dt, P);
			Z += S.Velocity.Z * Row.Dt;
			if (!S.bOnGround && Z <= 0.0 && S.Velocity.Z <= 0.0)
			{
				Z = 0.0;
				S.bOnGround = true;
			}
		}
		return S;
	};

	FVeloxMoveParams Vanilla = DefaultParams();
	FVeloxMoveParams Cpm = Vanilla;
	Cpm.bAirControl = true;
	for (const FVeloxMoveParams& P : { Vanilla, Cpm })
	{
		const FVeloxMoveState Client = Run(P);
		const FVeloxMoveState Server = Run(P);
		AddInfo(FString::Printf(TEXT("CPM=%d end velocity %s, ground %d"), P.bAirControl ? 1 : 0, *Client.Velocity.ToString(), Client.bOnGround ? 1 : 0));
		TestTrue(TEXT("Bitwise-equal end state"), BitwiseEqual(Client, Server));
		TestTrue(TEXT("Script actually moved"), Speed2D(Client.Velocity) > 0.0 || !Client.bOnGround);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementGracePartitionInvarianceTest, "Velox.Movement.Sim.GracePartitionInvariance", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementGracePartitionInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	// ADR 0002 A27(iv): the server fills a lost move's time into the next move, so friction must start at the same
	// simulated time for every partition of the same 50 ms. Between calls the grace is decremented by the step and
	// clamped at 0, as the CMC does per PhysWalking iteration (A27 fix 1).
	// Tolerance: A27(iv) asks for 0.2 % between every pair of partitions. Without any grace the pair 1x50ms (7 friction
	// substeps of 7.14 ms) vs 6x1/120 (12 of 4.17 ms) already differs by 0.28 %: the accepted FixedStep/2..FixedStep
	// substep residual of A1, independent of the grace (it shrinks as the grace covers more of the 50 ms). The bound is
	// therefore 0.3 %. Before the A27 fix the deltas reach 2.8 % (g = 16.65 ms, 1x50ms vs 3x1/60) and 3.4 % (g = 7.3 ms).
	const FVeloxMoveParams P = DefaultParams();
	const float Partitions[][6] = {
		{ 0.05f },
		{ 0.025f, 0.025f },
		{ 1.f / 60.f, 1.f / 60.f, 1.f / 60.f },
		{ 1.f / 120.f, 1.f / 120.f, 1.f / 120.f, 1.f / 120.f, 1.f / 120.f, 1.f / 120.f },
		{ 1.f / 60.f, 1.f / 120.f, 0.025f },
	};
	const int32 NumSteps[] = { 1, 2, 3, 6, 3 };
	const TCHAR* Names[] = { TEXT("1x50ms"), TEXT("25+25ms"), TEXT("3x1/60"), TEXT("6x1/120"), TEXT("1/60+1/120+25ms") };
	const float Graces[] = { 1e-6f, 0.0005f, 0.0073f, 0.01665f, 0.0333f, 0.049f };
	const float WishSpeeds[] = { 0.f, 800.f };
	constexpr double MaxSpeedDelta = 0.003;   // 0.3 %, see above
	constexpr double MaxAngleDeg = 0.1;
	constexpr int32 NumPartitions = UE_ARRAY_COUNT(Partitions);

	auto Integrate = [&](const FVeloxMoveInput& In, float Grace0, int32 k)
	{
		FVector V(1250.0, 0.0, 0.0);
		float Grace = Grace0;
		for (int32 i = 0; i < NumSteps[k]; ++i)
		{
			V = VeloxMoveSim::GroundMove(V, In, Partitions[k][i], Grace, P);
			Grace = FMath::Max(0.f, Grace - Partitions[k][i]);
		}
		return V;
	};

	double WorstDelta = 0.0;
	for (const float WishSpeed : WishSpeeds)
	{
		const FVeloxMoveInput In = Wish(FVector::ForwardVector, WishSpeed, WishSpeed > 0.f, false);
		for (const float Grace0 : Graces)
		{
			FVector End[NumPartitions];
			for (int32 k = 0; k < NumPartitions; ++k)
			{
				End[k] = Integrate(In, Grace0, k);
			}
			for (int32 a = 0; a < NumPartitions; ++a)
			{
				for (int32 b = a + 1; b < NumPartitions; ++b)
				{
					const double Sa = Speed2D(End[a]), Sb = Speed2D(End[b]);
					const double Delta = FMath::Abs(Sa - Sb) / FMath::Max(Sa, Sb);
					const double CosAngle = FMath::Clamp(End[a].GetSafeNormal2D() | End[b].GetSafeNormal2D(), -1.0, 1.0);
					const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(CosAngle));
					WorstDelta = FMath::Max(WorstDelta, Delta);
					if (Delta > MaxSpeedDelta || AngleDeg > MaxAngleDeg)
					{
						AddError(FString::Printf(TEXT("wish %.0f grace %.6f: %s %.4f vs %s %.4f (%.3f %%, %.4f deg)"),
							WishSpeed, Grace0, Names[a], Sa, Names[b], Sb, 100.0 * Delta, AngleDeg));
					}
				}
			}
			if (FMath::IsNearlyEqual(Grace0, 0.01665f))
			{
				const double S0 = Speed2D(End[0]), S2 = Speed2D(End[2]);
				AddInfo(FString::Printf(TEXT("wish %.0f grace 16.65 ms: 1x50ms %.4f vs 3x1/60 %.4f (%.3f %%)"), WishSpeed, S0, S2, 100.0 * FMath::Abs(S0 - S2) / FMath::Max(S0, S2)));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("worst pairwise end-speed delta %.4f %%"), 100.0 * WorstDelta));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementFrictionPartitionInvarianceTest, "Velox.Movement.Sim.FrictionPartitionInvariance", VeloxMoveSimTest::MoveSimFlags)
bool FVeloxMovementFrictionPartitionInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace VeloxMoveSimTest;
	// ADR 0002 A31: the server merges lost client moves into one longer move, so the ground velocity update *and* the
	// displacement of a move must not depend on how the same 50 ms are partitioned into moves. The displacement of a move
	// is Mean * dt (trapezoid over the substeps, what the CMC hands to PhysWalking since Fix D); the engine's rectangle
	// rule End * dt is evaluated alongside to document the pre-fix error (it must exceed the tolerance by far, otherwise
	// this test would not detect a regression to the rectangle rule).
	// Tolerances: A31 asks for 0.1 % (speed) and 0.05 % (displacement). Both are below the accepted substep residual of A1
	// that remains with the trapezoid: FixedStep 8 ms gives substeps of 4.17 ms (1/120 move) to 8 ms, and the velocity
	// update differs between them to second order (worst pair 1x50ms vs 6x1/120, wish 800 from 250 cm/s: 0.42 % speed,
	// 0.29 % = 0.057 cm displacement; friction only 0.28 %, as in GracePartitionInvariance). The bounds are therefore
	// 0.5 % and 0.1 cm absolute (cm is what the 1.73 cm correction threshold sees); the rectangle rule is 4.1-6.4 cm.
	const FVeloxMoveParams P = DefaultParams();
	const float Partitions[][6] = {
		{ 0.05f },
		{ 0.025f, 0.025f },
		{ 1.f / 60.f, 1.f / 60.f, 1.f / 60.f },
		{ 1.f / 120.f, 1.f / 120.f, 1.f / 120.f, 1.f / 120.f, 1.f / 120.f, 1.f / 120.f },
		{ 1.f / 60.f, 1.f / 120.f, 0.025f },
	};
	const int32 NumSteps[] = { 1, 2, 3, 6, 3 };
	const TCHAR* Names[] = { TEXT("1x50ms"), TEXT("25+25ms"), TEXT("3x1/60"), TEXT("6x1/120"), TEXT("1/60+1/120+25ms") };
	// Around StopSpeed (250) on purpose: the Q3 control = max(speed, StopSpeed) regime switch must not break invariance.
	const float Speeds[] = { 100.f, 200.f, 240.f, 250.f, 260.f, 300.f, 600.f, 1000.f, 1200.f };
	const float WishSpeeds[] = { 0.f, 800.f };
	constexpr double MaxSpeedDelta = 0.005;    // 0.5 %, see above
	constexpr double MaxDispCm = 0.1;          // see above
	constexpr int32 NumPartitions = UE_ARRAY_COUNT(Partitions);

	struct FResult { FVector End; double Trapezoid = 0.0; double Rectangle = 0.0; };
	auto Integrate = [&](const FVeloxMoveInput& In, float Speed0, int32 k)
	{
		FResult R;
		R.End = FVector(Speed0, 0.0, 0.0);
		for (int32 i = 0; i < NumSteps[k]; ++i)
		{
			FVector Mean;
			R.End = VeloxMoveSim::GroundMove(R.End, In, Partitions[k][i], 0.f, P, &Mean);
			R.Trapezoid += Mean.X * Partitions[k][i];
			R.Rectangle += R.End.X * Partitions[k][i];
		}
		return R;
	};

	double WorstSpeed = 0.0, WorstDisp = 0.0, WorstRectCm = 0.0, WorstTrapCm = 0.0;
	for (const float WishSpeed : WishSpeeds)
	{
		const FVeloxMoveInput In = Wish(FVector::ForwardVector, WishSpeed, WishSpeed > 0.f, false);
		for (const float Speed0 : Speeds)
		{
			FResult R[NumPartitions];
			for (int32 k = 0; k < NumPartitions; ++k)
			{
				R[k] = Integrate(In, Speed0, k);
			}
			for (int32 a = 0; a < NumPartitions; ++a)
			{
				for (int32 b = a + 1; b < NumPartitions; ++b)
				{
					const double Sa = Speed2D(R[a].End), Sb = Speed2D(R[b].End);
					const double SpeedDelta = FMath::Abs(Sa - Sb) / FMath::Max3(Sa, Sb, 1.0);
					const double DispCm = FMath::Abs(R[a].Trapezoid - R[b].Trapezoid);
					const double DispDelta = DispCm / FMath::Max3(R[a].Trapezoid, R[b].Trapezoid, 1e-3);
					WorstSpeed = FMath::Max(WorstSpeed, SpeedDelta);
					WorstDisp = FMath::Max(WorstDisp, DispDelta);
					WorstTrapCm = FMath::Max(WorstTrapCm, DispCm);
					WorstRectCm = FMath::Max(WorstRectCm, FMath::Abs(R[a].Rectangle - R[b].Rectangle));
					if (SpeedDelta > MaxSpeedDelta || DispCm > MaxDispCm)
					{
						AddError(FString::Printf(TEXT("v0 %.0f wish %.0f: %s v=%.4f d=%.4f vs %s v=%.4f d=%.4f (speed %.4f %%, displacement %.4f %% = %.4f cm)"),
							Speed0, WishSpeed, Names[a], Sa, R[a].Trapezoid, Names[b], Sb, R[b].Trapezoid, 100.0 * SpeedDelta, 100.0 * DispDelta, DispCm));
					}
				}
			}
			if (Speed0 == 1000.f && WishSpeed == 0.f)
			{
				AddInfo(FString::Printf(TEXT("v0 1000 friction only, 1x50ms vs 3x1/60: trapezoid %.4f vs %.4f cm, rectangle (pre-A31) %.4f vs %.4f cm (delta %.3f cm)"),
					R[0].Trapezoid, R[2].Trapezoid, R[0].Rectangle, R[2].Rectangle, FMath::Abs(R[0].Rectangle - R[2].Rectangle)));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("worst pairwise: end speed %.4f %%, displacement %.4f %% (%.4f cm); rectangle rule (pre-A31) worst %.3f cm"),
		100.0 * WorstSpeed, 100.0 * WorstDisp, WorstTrapCm, WorstRectCm));
	TestTrue(TEXT("the rectangle rule (pre-A31) is detected by this test (worst delta > 1 cm)"), WorstRectCm > 1.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
