// Velox - bot core unit tests (ADR 0005 section 14, ADR 0001 section 4; task M4-02). World-free.
//   Velox.Bots.StrafeSteering  closed loop Steer -> VeloxMoveSim::StepFrame: strafe gain, air turn, walker cap, corner
//                              rule, arrive slowdown, the strafe optimum.
//   Velox.Bots.AimError        OU error statistics (std, mean, lag-1 autocorrelation) and the reaction window.
//   Velox.Bots.AimLead         lead point vs a constant-velocity rocket flight (FVeloxProjectileSim).
//   Velox.Bots.AimTurnRate     a 180 degree jump takes 180 / MaxTurnRate.
//   Velox.Bots.ItemValue       need / score rows of section 7, deterministic ranking.
//   Velox.Bots.WeaponChoice    section 9 rows.
//   Velox.Bots.Decision        section 2 rows (fight / item / roam, hysteresis, Duel camping).
//   Velox.Bots.Maneuver        rocket-jump sequence, jump-fast refusal and retry, pad traversal.
// Tasks 05 / 06 append their rows in their own sections at the end of this file.

#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bots/VeloxAimModel.h"
#include "Bots/VeloxBotDecision.h"
#include "Bots/VeloxBotManeuver.h"
#include "Bots/VeloxBotSteering.h"
#include "Bots/VeloxBotTypes.h"
#include "Bots/VeloxItemValue.h"
#include "Bots/VeloxWeaponChoice.h"
#include "Core/VeloxGameplayTags.h"
#include "Math/RandomStream.h"
#include "Movement/VeloxMoveSim.h"
#include "Movement/VeloxMovementSettings.h"
#include "Weapons/VeloxProjectileSim.h"

namespace VeloxBotTestsShared
{
	constexpr EAutomationTestFlags BotTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	/** ADR 0005 section 11, Hard column (the fields the cores read). */
	FVeloxBotDifficultyParams HardParams()
	{
		FVeloxBotDifficultyParams D;
		D.ReactionSeconds = 0.18f;
		D.AimErrorDeg = 1.8f;
		D.ErrorCorrelationSeconds = 0.25f;
		D.TrackingSmoothingSeconds = 0.08f;
		D.MaxTurnRateDegPerSec = 720.f;
		D.bLeadProjectiles = true;
		D.LeadScale = 0.9f;
		D.FireConeDeg = 4.f;
		D.bBunnyHop = true;
		D.bStrafeJump = true;
		D.bRocketJumps = true;
		D.EstimatedTravelSpeed = 1000.f;
		D.ItemTimingSkill = 1.f;
		D.MaxWaitSeconds = 8.f;
		D.GoalReevaluateSeconds = 1.f;
		D.GoalSwitchHysteresis = 1.3f;
		D.FightItemBias = 1.2f;
		D.PreferredRange = 1000.f;
		return D;
	}

	/** Easy column: no bunny hop, no strafe jump. */
	FVeloxBotDifficultyParams EasyParams()
	{
		FVeloxBotDifficultyParams D = HardParams();
		D.bBunnyHop = false;
		D.bStrafeJump = false;
		D.bRocketJumps = false;
		return D;
	}

	FVeloxBotWeaponPreference Pref(const FGameplayTag& Tag, float Weight, float MinR, float BestMin, float BestMax, float MaxR)
	{
		FVeloxBotWeaponPreference P;
		P.Weapon = Tag;
		P.Weight = Weight;
		P.MinRange = MinR;
		P.BestMin = BestMin;
		P.BestMax = BestMax;
		P.MaxRange = MaxR;
		return P;
	}

	/** ADR 0005 section 9 default table. */
	TArray<FVeloxBotWeaponPreference> DefaultPreferences()
	{
		return {
			Pref(VeloxGameplayTags::Weapon_Gauntlet, 10.f, 0.f, 0.f, 100.f, 120.f),
			Pref(VeloxGameplayTags::Weapon_MachineGun, 20.f, 0.f, 0.f, 20480.f, 20480.f),
			Pref(VeloxGameplayTags::Weapon_Shotgun, 60.f, 0.f, 0.f, 500.f, 1000.f),
			Pref(VeloxGameplayTags::Weapon_PlasmaGun, 50.f, 150.f, 300.f, 1500.f, 2500.f),
			Pref(VeloxGameplayTags::Weapon_RocketLauncher, 90.f, 150.f, 300.f, 1500.f, 3000.f),
			Pref(VeloxGameplayTags::Weapon_LightningGun, 85.f, 0.f, 200.f, 1500.f, 1900.f),
			Pref(VeloxGameplayTags::Weapon_Railgun, 100.f, 400.f, 1200.f, 20480.f, 20480.f),
		};
	}

	int32 PrefIndex(const TArray<FVeloxBotWeaponPreference>& Prefs, const FGameplayTag& Tag)
	{
		return Prefs.IndexOfByPredicate([&Tag](const FVeloxBotWeaponPreference& P) { return P.Weapon == Tag; });
	}

	FVeloxWeaponChoiceOption Option(const FGameplayTag& Tag, int32 Ammo, float Splash = 0.f, int32 PerShot = 1)
	{
		FVeloxWeaponChoiceOption O;
		O.Weapon = Tag;
		O.bOwned = true;
		O.Ammo = Ammo;
		O.AmmoPerShot = PerShot;
		O.SplashRadiusCm = Splash;
		return O;
	}

	/** RL splash 300 cm, PG 50 cm (ADR 0003 section 4). */
	TArray<FVeloxWeaponChoiceOption> AllWeapons()
	{
		return {
			Option(VeloxGameplayTags::Weapon_Gauntlet, 0, 0.f, 0),
			Option(VeloxGameplayTags::Weapon_MachineGun, 100),
			Option(VeloxGameplayTags::Weapon_Shotgun, 10),
			Option(VeloxGameplayTags::Weapon_PlasmaGun, 50, 50.f),
			Option(VeloxGameplayTags::Weapon_RocketLauncher, 10, 300.f),
			Option(VeloxGameplayTags::Weapon_LightningGun, 100),
			Option(VeloxGameplayTags::Weapon_Railgun, 10),
		};
	}

	/** Closed-loop pawn for the steering tests: StepFrame plus a flat floor at Z = 0. */
	struct FSteerPawn
	{
		FVeloxMoveState Move;
		double Z = 0.0;
		int32 Landings = 0;

		FVeloxSteerOutput Step(const FVector2D& ToTarget, const FVeloxSteerParams& SteerParams, const FVeloxMoveParams& P, float Dt,
			float RemainingCm = 1.0e6f, float CornerDeg = 0.f)
		{
			FVeloxSteerInput In;
			In.Velocity = FVector2D(Move.Velocity.X, Move.Velocity.Y);
			In.bOnGround = Move.bOnGround;
			In.ToTarget = ToTarget;
			In.SegmentRemainingCm = RemainingCm;
			In.NextCornerDeg = CornerDeg;
			In.WishSpeed = P.AirMaxWishSpeed;
			In.AirAccel = P.AirAcceleration;
			In.FixedStep = P.FixedStep;
			const FVeloxSteerOutput Out = VeloxBotSteering::Steer(In, SteerParams);

			FVeloxMoveInput MoveIn;
			MoveIn.WishDir = FVector(Out.WishDir, 0.0);
			MoveIn.WishSpeed = P.MaxGroundSpeed * Out.WishScale;
			MoveIn.bHasForwardInput = true;
			MoveIn.bHasSideInput = true;
			MoveIn.bJumpHeld = Out.bJumpHeld;
			VeloxMoveSim::StepFrame(Move, MoveIn, Dt, P);

			Z += Move.Velocity.Z * Dt;
			if (!Move.bOnGround && Z <= 0.0 && Move.Velocity.Z < 0.0)
			{
				Z = 0.0;
				Move.Velocity.Z = 0.0;
				Move.bOnGround = true;
				++Landings;
			}
			return Out;
		}

		double Speed2D() const { return FVector2D(Move.Velocity.X, Move.Velocity.Y).Size(); }

		float HeadingErrorDeg(const FVector2D& ToTarget) const
		{
			return FMath::Abs(VeloxBotSteering::SignedAngleDeg(FVector2D(Move.Velocity.X, Move.Velocity.Y), ToTarget));
		}
	};

	struct FSeriesStats
	{
		double Mean = 0.0;
		double Std = 0.0;
		double Lag1 = 0.0;
	};

	FSeriesStats ComputeStats(const TArray<double>& X)
	{
		FSeriesStats S;
		const int32 N = X.Num();
		if (N < 2)
		{
			return S;
		}
		for (double V : X)
		{
			S.Mean += V;
		}
		S.Mean /= N;
		double Var = 0.0, Cov = 0.0;
		for (int32 i = 0; i < N; ++i)
		{
			const double D = X[i] - S.Mean;
			Var += D * D;
			if (i + 1 < N)
			{
				Cov += D * (X[i + 1] - S.Mean);
			}
		}
		S.Std = FMath::Sqrt(Var / (N - 1));
		S.Lag1 = Var > 0.0 ? Cov / Var : 0.0;
		return S;
	}

	FVeloxItemScoringInput Mega(int32 Health, float Travel)
	{
		FVeloxItemScoringInput In;
		In.CandidateId = 1;
		In.Type = EVeloxItemType::Health;
		In.Quantity = 100;
		In.BotBaseValue = 100.f;
		In.bMajorItem = true;
		In.HealthCap = EVeloxHealthCap::HardCap;
		In.Health = Health;
		In.MaxHealth = 100;
		In.HealthHardCap = 200;
		In.MaxArmor = 200;
		In.TravelSeconds = Travel;
		return In;
	}

	FVeloxManeuverInput ManeuverIn(double Time, float DistToStart, float DistToEnd, bool bOnGround = true, float Speed = 0.f)
	{
		FVeloxManeuverInput In;
		In.Time = Time;
		In.DistToStart = DistToStart;
		In.DistToEnd = DistToEnd;
		In.bOnGround = bOnGround;
		In.Speed2D = Speed;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsStrafeSteeringTest, "Velox.Bots.StrafeSteering", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsStrafeSteeringTest::RunTest(const FString& Parameters)
{
	using VeloxBotTestsShared::FSteerPawn;
	const FVeloxMoveParams P = GetDefault<UVeloxMovementSettings>()->BuildSimParams();
	const float Dt = 1.f / 125.f;
	const FVeloxSteerParams Hard = VeloxBotSteering::MakeParams(VeloxBotTestsShared::HardParams());
	const FVeloxSteerParams Easy = VeloxBotSteering::MakeParams(VeloxBotTestsShared::EasyParams());

	// The optimum: ~37.5 degrees at 1000 cm/s with the M1 defaults; 0 below W - a.
	{
		const float Theta = VeloxBotSteering::OptimalStrafeAngleDeg(1000.f, P.AirMaxWishSpeed, P.AirAcceleration, P.FixedStep);
		const float Expected = FMath::RadiansToDegrees(FMath::Acos((P.AirMaxWishSpeed - P.AirAcceleration * P.FixedStep * P.AirMaxWishSpeed) / 1000.f));
		TestNearlyEqual(TEXT("OptimalStrafeAngleDeg(1000)"), Theta, Expected, 1e-3f);
		AddInfo(FString::Printf(TEXT("Strafe optimum at 1000 cm/s: %.2f deg"), Theta));
		TestEqual(TEXT("OptimalStrafeAngleDeg below W - a is 0"), VeloxBotSteering::OptimalStrafeAngleDeg(700.f, P.AirMaxWishSpeed, P.AirAcceleration, P.FixedStep), 0.f);
	}

	// Closed loop, Hard: 125 ticks from (800, 0) toward +X.
	{
		FSteerPawn Pawn;
		Pawn.Move.Velocity = FVector(800.0, 0.0, 0.0);
		Pawn.Move.bOnGround = true;
		const FVector2D Target(1.0, 0.0);
		for (int32 i = 0; i < 125; ++i)
		{
			Pawn.Step(Target, Hard, P, Dt);
		}
		const double Speed = Pawn.Speed2D();
		const float Err = Pawn.HeadingErrorDeg(Target);
		AddInfo(FString::Printf(TEXT("Strafe loop: speed %.1f cm/s after 1 s (bound %.1f), heading error %.2f deg, landings %d"), Speed, 1.25 * P.MaxGroundSpeed, Err, Pawn.Landings));
		TestTrue(TEXT("Strafe loop reaches >= 1.25 x MaxGroundSpeed"), Speed >= 1.25 * P.MaxGroundSpeed);
		TestTrue(TEXT("Strafe loop heading error <= 10 deg"), Err <= 10.f);
		TestTrue(TEXT("Strafe loop landed and re-hopped at least once"), Pawn.Landings >= 1);
	}

	// 90 degree goal change at 1000 cm/s (see the task report: vanilla air turn rate bounds this to ~0.3 deg / tick).
	{
		FSteerPawn Pawn;
		Pawn.Move.Velocity = FVector(1000.0, 0.0, 0.0);
		Pawn.Move.bOnGround = true;
		const FVector2D Target(0.0, 1.0);
		double MinSpeed = TNumericLimits<double>::Max();
		float PrevErr = 90.f;
		float ErrAt60 = 90.f;
		bool bMonotone = true;
		int32 TicksTo15 = INDEX_NONE;
		for (int32 i = 0; i < 500 && TicksTo15 == INDEX_NONE; ++i)
		{
			Pawn.Step(Target, Hard, P, Dt);
			MinSpeed = FMath::Min(MinSpeed, Pawn.Speed2D());
			const float Err = Pawn.HeadingErrorDeg(Target);
			bMonotone &= Err <= PrevErr + 1e-3f;
			PrevErr = Err;
			if (i == 59)
			{
				ErrAt60 = Err;
			}
			if (Err <= 15.f)
			{
				TicksTo15 = i + 1;
			}
		}
		AddInfo(FString::Printf(TEXT("Air turn 90 deg at 1000 cm/s: error %.2f deg after 60 ticks, within 15 deg after %d ticks, min speed %.1f"), ErrAt60, TicksTo15, MinSpeed));
		TestTrue(TEXT("Air turn: never below 950 cm/s"), MinSpeed >= 950.0);
		TestTrue(TEXT("Air turn: heading error decreases monotonically"), bMonotone);
		TestTrue(TEXT("Air turn: turned >= 15 deg within 60 ticks"), ErrAt60 <= 75.f);
		TestTrue(TEXT("Air turn: within 15 deg of the goal within 375 ticks"), TicksTo15 != INDEX_NONE && TicksTo15 <= 375);
	}

	// Walker presets never exceed MaxGroundSpeed on a straight run (Easy: no hop; Easy + hop: Medium-like).
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		FVeloxSteerParams Walker = Easy;
		Walker.bBunnyHop = Variant == 1;
		for (const double StartSpeed : {0.0, 800.0})
		{
			FSteerPawn Pawn;
			Pawn.Move.Velocity = FVector(StartSpeed, 0.0, 0.0);
			Pawn.Move.bOnGround = true;
			double MaxSpeed = 0.0;
			for (int32 i = 0; i < 375; ++i)
			{
				Pawn.Step(FVector2D(1.0, 0.0), Walker, P, Dt);
				MaxSpeed = FMath::Max(MaxSpeed, Pawn.Speed2D());
			}
			TestTrue(*FString::Printf(TEXT("Walker (hop %d, start %.0f) max speed %.2f <= MaxGroundSpeed + 1"), Variant, StartSpeed, MaxSpeed), MaxSpeed <= P.MaxGroundSpeed + 1.0);
		}
	}

	// Corner rule and hop distance.
	{
		FVeloxSteerInput In;
		In.Velocity = FVector2D(900.0, 0.0);
		In.bOnGround = true;
		In.ToTarget = FVector2D(1.0, 0.0);
		In.SegmentRemainingCm = 2000.f;
		In.WishSpeed = P.AirMaxWishSpeed;
		In.AirAccel = P.AirAcceleration;
		In.FixedStep = P.FixedStep;
		In.NextCornerDeg = 60.f;
		TestFalse(TEXT("Corner 60 deg -> bJumpHeld false"), VeloxBotSteering::Steer(In, Hard).bJumpHeld);
		In.NextCornerDeg = 20.f;
		TestTrue(TEXT("Corner 20 deg, long segment -> bJumpHeld"), VeloxBotSteering::Steer(In, Hard).bJumpHeld);
		In.SegmentRemainingCm = 300.f;
		TestFalse(TEXT("Short segment -> bJumpHeld false"), VeloxBotSteering::Steer(In, Hard).bJumpHeld);
		In.SegmentRemainingCm = 2000.f;
		TestFalse(TEXT("No bunny hop preset -> bJumpHeld false"), VeloxBotSteering::Steer(In, Easy).bJumpHeld);
	}

	// Arrive slowdown on the final segment.
	{
		FVeloxSteerInput In;
		In.bOnGround = true;
		In.ToTarget = FVector2D(1.0, 0.0);
		In.bFinalSegment = true;
		In.WishSpeed = P.AirMaxWishSpeed;
		In.AirAccel = P.AirAcceleration;
		In.FixedStep = P.FixedStep;
		const float Radius = Hard.ArriveSlowdownCm;
		In.SegmentRemainingCm = 0.f;
		TestNearlyEqual(TEXT("Arrive: WishScale 0 at the target"), VeloxBotSteering::Steer(In, Hard).WishScale, 0.f, 1e-6f);
		In.SegmentRemainingCm = Radius * 0.5f;
		TestNearlyEqual(TEXT("Arrive: WishScale 0.5 at half the radius"), VeloxBotSteering::Steer(In, Hard).WishScale, 0.5f, 1e-5f);
		In.SegmentRemainingCm = Radius * 2.f;
		TestNearlyEqual(TEXT("Arrive: WishScale 1 outside the radius"), VeloxBotSteering::Steer(In, Hard).WishScale, 1.f, 1e-6f);
		In.bFinalSegment = false;
		In.SegmentRemainingCm = 0.f;
		TestNearlyEqual(TEXT("No slowdown on an intermediate corner"), VeloxBotSteering::Steer(In, Hard).WishScale, 1.f, 1e-6f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsAimErrorTest, "Velox.Bots.AimError", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsAimErrorTest::RunTest(const FString& Parameters)
{
	const float Dt = 1.f / 60.f;
	const FVeloxAimParams Params = FVeloxAimModel::MakeParams(VeloxBotTestsShared::HardParams());
	const float Sigma = Params.AimErrorDeg;
	const float Tau = Params.ErrorCorrelationSeconds;

	// Distribution: 20 000 steps, static target.
	{
		FRandomStream Stream(20261030);
		FVeloxAimState State;
		FVeloxAimModel::Reset(State, FRotator::ZeroRotator);
		FVeloxAimInput In;
		In.bHasTarget = true;
		In.TargetId = 7;
		In.Eye = FVector::ZeroVector;
		In.TargetCentre = FVector(2000.0, 0.0, 0.0);
		TArray<double> Yaw, Pitch;
		Yaw.Reserve(20000);
		Pitch.Reserve(20000);
		for (int32 i = 0; i < 20000; ++i)
		{
			FVeloxAimModel::Update(State, In, Params, Dt, Stream);
			Yaw.Add(State.ErrorDeg.X);
			Pitch.Add(State.ErrorDeg.Y);
		}
		const double Rho = FMath::Exp(-static_cast<double>(Dt) / Tau);
		const VeloxBotTestsShared::FSeriesStats SY = VeloxBotTestsShared::ComputeStats(Yaw);
		const VeloxBotTestsShared::FSeriesStats SP = VeloxBotTestsShared::ComputeStats(Pitch);
		AddInfo(FString::Printf(TEXT("AimError sigma %.3f tau %.3f: yaw std %.4f mean %.4f lag1 %.4f | pitch std %.4f mean %.4f lag1 %.4f | expected lag1 %.4f"),
			Sigma, Tau, SY.Std, SY.Mean, SY.Lag1, SP.Std, SP.Mean, SP.Lag1, Rho));
		for (const VeloxBotTestsShared::FSeriesStats* S : {&SY, &SP})
		{
			TestTrue(TEXT("Error std within 10 % of sigma"), FMath::Abs(S->Std - Sigma) <= 0.1 * Sigma);
			TestTrue(TEXT("Error mean within 0.05 sigma"), FMath::Abs(S->Mean) <= 0.05 * Sigma);
			TestTrue(TEXT("Error lag-1 autocorrelation within 0.05 of exp(-Dt/tau)"), FMath::Abs(S->Lag1 - Rho) <= 0.05);
		}
	}

	// Reaction window: 200 target appearances; the aim first moves in a tick overlapping [0.7, 1.3] x ReactionSeconds.
	{
		FRandomStream Stream(4711);
		const float Reaction = Params.ReactionSeconds;
		double MinT = TNumericLimits<double>::Max(), MaxT = 0.0;
		int32 Bad = 0;
		for (int32 Trial = 0; Trial < 200; ++Trial)
		{
			FVeloxAimState State;
			FVeloxAimModel::Reset(State, FRotator::ZeroRotator);
			FVeloxAimInput Idle;
			Idle.IdleAim = FRotator::ZeroRotator;
			for (int32 i = 0; i < 10; ++i)
			{
				FVeloxAimModel::Update(State, Idle, Params, Dt, Stream);
			}
			FVeloxAimInput In = Idle;
			In.bHasTarget = true;
			In.TargetId = Trial;
			In.TargetCentre = FRotator(0.f, 30.f, 0.f).Vector() * 1000.0;
			int32 FirstChange = INDEX_NONE;
			for (int32 i = 0; i < 120 && FirstChange == INDEX_NONE; ++i)
			{
				const FRotator Before = State.Aim;
				FVeloxAimModel::Update(State, In, Params, Dt, Stream);
				if (!State.Aim.Equals(Before, 1e-4f))
				{
					FirstChange = i;
				}
			}
			const double TStart = FirstChange * static_cast<double>(Dt);
			const double TEnd = TStart + Dt;
			MinT = FMath::Min(MinT, TStart);
			MaxT = FMath::Max(MaxT, TStart);
			if (FirstChange == INDEX_NONE || TStart > 1.3 * Reaction + 1e-6 || TEnd < 0.7 * Reaction - 1e-6)
			{
				++Bad;
			}
		}
		AddInfo(FString::Printf(TEXT("Reaction %.3f s: first aim change at tick start %.4f .. %.4f s over 200 trials"), Reaction, MinT, MaxT));
		TestEqual(TEXT("First aim change inside [0.7, 1.3] x ReactionSeconds"), Bad, 0);
		TestTrue(TEXT("Reaction jitter is used (spread > 0.3 x reaction)"), MaxT - MinT > 0.3 * Reaction);

		// A target switch re-arms the reaction; the same target does not.
		FVeloxAimState State;
		FVeloxAimModel::Reset(State, FRotator::ZeroRotator);
		FVeloxAimInput In;
		In.bHasTarget = true;
		In.TargetId = 1;
		In.TargetCentre = FVector(1000.0, 0.0, 0.0);
		FVeloxAimModel::Update(State, In, Params, Dt, Stream);
		TestTrue(TEXT("New target arms the reaction"), State.ReactionLeft > 0.f);
		for (int32 i = 0; i < 60; ++i)
		{
			FVeloxAimModel::Update(State, In, Params, Dt, Stream);
		}
		TestEqual(TEXT("Same target: reaction stays expired"), State.ReactionLeft, 0.f);
		In.TargetId = 2;
		FVeloxAimModel::Update(State, In, Params, Dt, Stream);
		TestTrue(TEXT("Target switch re-arms the reaction"), State.ReactionLeft > 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsAimLeadTest, "Velox.Bots.AimLead", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsAimLeadTest::RunTest(const FString& Parameters)
{
	const FVector Eye = FVector::ZeroVector;
	const FVector Target(1000.0, 0.0, 0.0);
	const FVector TargetVel(0.0, 600.0, 0.0);
	const float RocketSpeed = 2250.f;

	auto ClosestApproach = [&](const FVector& Dir)
	{
		double Best = TNumericLimits<double>::Max();
		for (double T = 0.0; T <= 1.0; T += 1e-4)
		{
			const FVector Rocket = FVeloxProjectileSim::PositionAt(Eye, Dir * RocketSpeed, T);
			const FVector Pawn = FVeloxProjectileSim::PositionAt(Target, TargetVel, T);
			Best = FMath::Min(Best, FVector::Dist(Rocket, Pawn));
		}
		return Best;
	};

	const FVector Lead = FVeloxAimModel::LeadPoint(Eye, Target, TargetVel, RocketSpeed, 1.f);
	const double Miss = ClosestApproach((Lead - Eye).GetSafeNormal());
	const double NoLeadMiss = ClosestApproach((Target - Eye).GetSafeNormal());
	AddInfo(FString::Printf(TEXT("AimLead: closest approach %.2f cm with lead, %.1f cm without"), Miss, NoLeadMiss));
	TestTrue(TEXT("Lead: rocket passes within 10 cm of the target"), Miss <= 10.0);
	TestTrue(TEXT("Without lead the rocket misses by > 100 cm"), NoLeadMiss > 100.0);
	TestEqual(TEXT("Hitscan (speed 0): no lead"), FVeloxAimModel::LeadPoint(Eye, Target, TargetVel, 0.f, 1.f), Target);

	// Through Update: no error, no reaction, no smoothing, unlimited turn -> the aim is the lead direction.
	FVeloxAimParams Params;
	Params.bLeadProjectiles = true;
	Params.LeadScale = 1.f;
	FRandomStream Stream(1);
	FVeloxAimState State;
	FVeloxAimModel::Reset(State, FRotator::ZeroRotator);
	FVeloxAimInput In;
	In.bHasTarget = true;
	In.TargetId = 1;
	In.Eye = Eye;
	In.TargetCentre = Target;
	In.TargetVelocity = TargetVel;
	In.ProjectileSpeed = RocketSpeed;
	const FRotator Aim = FVeloxAimModel::Update(State, In, Params, 1.f / 60.f, Stream);
	TestTrue(TEXT("Update aims at the lead point"), Aim.Equals((Lead - Eye).Rotation(), 0.01f));
	Params.LeadScale = 0.5f;
	FVeloxAimModel::Reset(State, FRotator::ZeroRotator);
	const FRotator Under = FVeloxAimModel::Update(State, In, Params, 1.f / 60.f, Stream);
	TestTrue(TEXT("LeadScale 0.5 under-leads"), Under.Yaw > 0.f && Under.Yaw < Aim.Yaw);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsAimTurnRateTest, "Velox.Bots.AimTurnRate", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsAimTurnRateTest::RunTest(const FString& Parameters)
{
	const float Dt = 1.f / 60.f;
	for (const float Rate : {240.f, 720.f, 1080.f})
	{
		FVeloxAimParams Params;
		Params.MaxTurnRateDegPerSec = Rate;
		FRandomStream Stream(3);
		FVeloxAimState State;
		FVeloxAimModel::Reset(State, FRotator::ZeroRotator);
		FVeloxAimInput In;
		In.bHasTarget = true;
		In.TargetId = 1;
		In.TargetCentre = FVector(1000.0, 0.0, 0.0);
		for (int32 i = 0; i < 5; ++i)
		{
			FVeloxAimModel::Update(State, In, Params, Dt, Stream);
		}
		In.TargetCentre = FVector(-1000.0, 0.0, 0.0);
		int32 Ticks = INDEX_NONE;
		for (int32 i = 0; i < 600 && Ticks == INDEX_NONE; ++i)
		{
			FVeloxAimModel::Update(State, In, Params, Dt, Stream);
			if (FMath::Abs(FRotator::NormalizeAxis(State.Aim.Yaw - 180.f)) < 0.01f)
			{
				Ticks = i + 1;
			}
		}
		const float Expected = 180.f / Rate / Dt;
		AddInfo(FString::Printf(TEXT("Turn rate %.0f deg/s: 180 deg in %d ticks (expected %.2f)"), Rate, Ticks, Expected));
		TestTrue(*FString::Printf(TEXT("180 deg at %.0f deg/s within one tick of 180 / rate"), Rate), Ticks != INDEX_NONE && FMath::Abs(Ticks - Expected) <= 1.f);
	}

	// With the Hard smoothing the clamp still bounds the first ticks of a flick.
	{
		const FVeloxAimParams Params = FVeloxAimModel::MakeParams(VeloxBotTestsShared::HardParams());
		FVeloxAimParams NoError = Params;
		NoError.AimErrorDeg = 0.f;
		NoError.ReactionSeconds = 0.f;
		FRandomStream Stream(3);
		FVeloxAimState State;
		FVeloxAimModel::Reset(State, FRotator::ZeroRotator);
		FVeloxAimInput In;
		In.bHasTarget = true;
		In.TargetId = 1;
		In.TargetCentre = FVector(-1000.0, 1.0, 0.0);
		FVeloxAimModel::Update(State, In, NoError, Dt, Stream);
		TestTrue(TEXT("Smoothed flick: first tick turns at most MaxTurnRate x Dt"), FMath::Abs(State.Aim.Yaw) <= NoError.MaxTurnRateDegPerSec * Dt + 1e-3f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsItemValueTest, "Velox.Bots.ItemValue", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsItemValueTest::RunTest(const FString& Parameters)
{
	using VeloxBotTestsShared::Mega;
	const FVeloxItemScoringParams Hard = VeloxItemValue::MakeParams(VeloxBotTestsShared::HardParams());

	// Mega uses the hard cap: a 100-health bot still wants it (need 0.5, section 7); at 50 health need 0.75.
	{
		const float Need100 = VeloxItemValue::Need(Mega(100, 2.f));
		const float Need50 = VeloxItemValue::Need(Mega(50, 2.f));
		TestNearlyEqual(TEXT("Mega need at 100/100 = 0.5"), Need100, 0.5f, 1e-6f);
		TestNearlyEqual(TEXT("Mega need at 50/100 = 0.75"), Need50, 0.75f, 1e-6f);
		const float S100 = VeloxItemValue::Score(Mega(100, 2.f), Hard);
		const float S50 = VeloxItemValue::Score(Mega(50, 2.f), Hard);
		TestTrue(TEXT("Mega at 100 health scores less than at 50"), S100 < S50);
		TestNearlyEqual(TEXT("Mega score ratio 100 vs 50 health = need ratio 2/3"), S100 / S50, 2.f / 3.f, 1e-5f);
		TestNearlyEqual(TEXT("Mega score = 100 x 0.5 / (2 + 1)"), S100, 50.f / 3.f, 1e-4f);
	}

	// Red armor at 200 armor -> 0.
	{
		FVeloxItemScoringInput RA;
		RA.Type = EVeloxItemType::Armor;
		RA.BotBaseValue = 90.f;
		RA.Armor = 200;
		RA.MaxArmor = 200;
		RA.TravelSeconds = 1.f;
		TestEqual(TEXT("RA at 200 armor scores 0"), VeloxItemValue::Score(RA, Hard), 0.f);
		RA.Armor = 0;
		TestTrue(TEXT("RA at 0 armor scores > 0"), VeloxItemValue::Score(RA, Hard) > 0.f);
	}

	// Timing: unavailable, 30 s left, 5 s travel.
	{
		FVeloxItemScoringInput In = Mega(50, 5.f);
		In.bAvailable = false;
		In.TimeUntilRespawn = 30.f;
		FVeloxItemScoringParams Skill0;
		Skill0.ItemTimingSkill = 0.f;
		Skill0.MaxWaitSeconds = 30.f;
		FVeloxItemScoringParams Skill1 = Skill0;
		Skill1.ItemTimingSkill = 1.f;
		TestEqual(TEXT("Timing skill 0 ignores an unavailable item"), VeloxItemValue::Score(In, Skill0), 0.f);
		const float S1 = VeloxItemValue::Score(In, Skill1);
		TestTrue(TEXT("Timing skill 1, MaxWait 30: > 0"), S1 > 0.f);
		TestNearlyEqual(TEXT("Timing score = 75 / (5 + 25 + 1)"), S1, 75.f / 31.f, 1e-4f);
		In.TimeUntilRespawn = 3.f;
		TestTrue(TEXT("Respawning before arrival: no wait, scored at skill 0"), VeloxItemValue::Score(In, Skill0) > 0.f);
	}

	// Monotone decreasing in travel time.
	{
		float Prev = TNumericLimits<float>::Max();
		bool bMonotone = true;
		for (float Travel = 0.f; Travel <= 20.f; Travel += 0.5f)
		{
			const float S = VeloxItemValue::Score(Mega(50, Travel), Hard);
			bMonotone &= S < Prev && S > 0.f;
			Prev = S;
		}
		TestTrue(TEXT("Score strictly decreasing in travel time"), bMonotone);
		TestTrue(TEXT("Item under the feet is finite"), FMath::IsFinite(VeloxItemValue::Score(Mega(50, 0.f), Hard)));
	}

	// Weapons and ammo.
	{
		FVeloxItemScoringInput W;
		W.Type = EVeloxItemType::Weapon;
		W.BotBaseValue = 60.f;
		W.Ammo = 5;
		W.AmmoMax = 25;
		TestEqual(TEXT("Weapon not owned: need 1"), VeloxItemValue::Need(W), 1.f);
		W.bOwnsWeapon = true;
		TestNearlyEqual(TEXT("Owned weapon: need = 0.3 x ammo need"), VeloxItemValue::Need(W), 0.3f * 0.8f, 1e-6f);
		FVeloxItemScoringInput A = W;
		A.Type = EVeloxItemType::Ammo;
		A.bOwnsWeaponUsingAmmo = true;
		TestNearlyEqual(TEXT("Ammo for an owned weapon: ammo need"), VeloxItemValue::Need(A), 0.8f, 1e-6f);
		A.bOwnsWeaponUsingAmmo = false;
		TestNearlyEqual(TEXT("Ammo without its weapon: x 0.1"), VeloxItemValue::Need(A), 0.08f, 1e-6f);
	}

	// Powerup beats Mega at equal travel (even a Mega at 0 health).
	{
		FVeloxItemScoringInput Quad;
		Quad.CandidateId = 2;
		Quad.Type = EVeloxItemType::Powerup;
		Quad.BotBaseValue = 110.f;
		Quad.TravelSeconds = 3.f;
		TestTrue(TEXT("Powerup beats Mega at equal travel"), VeloxItemValue::Score(Quad, Hard) > VeloxItemValue::Score(Mega(0, 3.f), Hard));
	}

	// Deterministic ranking: score descending, ties by CandidateId, independent of input order.
	{
		TArray<FVeloxItemScoringInput> Items;
		for (int32 i = 0; i < 6; ++i)
		{
			FVeloxItemScoringInput In = Mega(50, static_cast<float>(i % 3));
			In.CandidateId = 10 + i;
			Items.Add(In);
		}
		TArray<FVeloxItemScoringInput> Reversed = Items;
		Algo::Reverse(Reversed);
		TArray<FVeloxRankedItem> A, B;
		VeloxItemValue::Rank(Items, Hard, A);
		VeloxItemValue::Rank(Reversed, Hard, B);
		bool bSame = A.Num() == B.Num();
		for (int32 i = 0; bSame && i < A.Num(); ++i)
		{
			bSame &= A[i].CandidateId == B[i].CandidateId;
		}
		TestTrue(TEXT("Ranking independent of input order"), bSame);
		TestTrue(TEXT("Ranking: ties ordered by CandidateId (10 before 13)"), A.Num() == 6 && A[0].CandidateId == 10 && A[1].CandidateId == 13);
		TestTrue(TEXT("Ranking: best first"), A.Num() == 6 && A[0].Score >= A[2].Score && A[2].Score >= A[5].Score);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsWeaponChoiceTest, "Velox.Bots.WeaponChoice", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsWeaponChoiceTest::RunTest(const FString& Parameters)
{
	using namespace VeloxGameplayTags;
	const TArray<FVeloxBotWeaponPreference> Prefs = VeloxBotTestsShared::DefaultPreferences();
	auto Idx = [&Prefs](const FGameplayTag& Tag) { return VeloxBotTestsShared::PrefIndex(Prefs, Tag); };

	// Bands.
	{
		const FVeloxBotWeaponPreference& RL = Prefs[Idx(Weapon_RocketLauncher)];
		TestEqual(TEXT("Band 1 inside the best range"), VeloxWeaponChoice::Band(RL, 800.f), 1.f);
		TestNearlyEqual(TEXT("Band 0.3 at MaxRange"), VeloxWeaponChoice::Band(RL, 3000.f), 0.3f, 1e-5f);
		TestNearlyEqual(TEXT("Band 0.3 at MinRange"), VeloxWeaponChoice::Band(RL, 150.f), 0.3f, 1e-5f);
		TestEqual(TEXT("Band 0 outside"), VeloxWeaponChoice::Band(RL, 3001.f), 0.f);
	}

	FVeloxWeaponChoiceInput In;
	In.Weapons = VeloxBotTestsShared::AllWeapons();
	In.bHasTarget = true;

	In.RangeCm = 2500.f;
	TestEqual(TEXT("RG at 2500 cm beats RL (all weapons)"), VeloxWeaponChoice::Pick(In, Prefs), Idx(Weapon_Railgun));

	{
		FVeloxWeaponChoiceInput LgRg = In;
		LgRg.Weapons = {VeloxBotTestsShared::Option(Weapon_MachineGun, 100), VeloxBotTestsShared::Option(Weapon_LightningGun, 100), VeloxBotTestsShared::Option(Weapon_Railgun, 10)};
		LgRg.RangeCm = 500.f;
		TestEqual(TEXT("LG at 500 cm with ammo beats RG"), VeloxWeaponChoice::Pick(LgRg, Prefs), Idx(Weapon_LightningGun));
		LgRg.Weapons[1].Ammo = 0;
		TestEqual(TEXT("LG without ammo -> next best (RG)"), VeloxWeaponChoice::Pick(LgRg, Prefs), Idx(Weapon_Railgun));
	}

	{
		FVeloxWeaponChoiceInput Close = In;
		Close.Weapons = {VeloxBotTestsShared::Option(Weapon_MachineGun, 100), VeloxBotTestsShared::Option(Weapon_RocketLauncher, 10, 300.f)};
		Close.RangeCm = 150.f;
		TestEqual(TEXT("RL excluded at 150 cm (score 0)"), VeloxWeaponChoice::ScoreOption(Close.Weapons[1], Prefs[Idx(Weapon_RocketLauncher)], 150.f, Close.SplashSafetyMarginCm), 0.f);
		TestEqual(TEXT("RL excluded at 150 cm -> MG"), VeloxWeaponChoice::Pick(Close, Prefs), Idx(Weapon_MachineGun));
	}

	{
		FVeloxWeaponChoiceInput NoAmmo = In;
		NoAmmo.Weapons.FindByPredicate([](const FVeloxWeaponChoiceOption& O) { return O.Weapon == Weapon_Railgun; })->Ammo = 0;
		TestEqual(TEXT("No RG ammo at 2500 cm -> RL"), VeloxWeaponChoice::Pick(NoAmmo, Prefs), Idx(Weapon_RocketLauncher));
		FVeloxWeaponChoiceInput NotOwned = In;
		NotOwned.Weapons.FindByPredicate([](const FVeloxWeaponChoiceOption& O) { return O.Weapon == Weapon_Railgun; })->bOwned = false;
		TestEqual(TEXT("RG not owned at 2500 cm -> RL"), VeloxWeaponChoice::Pick(NotOwned, Prefs), Idx(Weapon_RocketLauncher));
	}

	// Hysteresis: the current RL keeps a challenger at score ratio 1.2 out, not one at 1.3.
	{
		TArray<FVeloxBotWeaponPreference> Pair = {
			VeloxBotTestsShared::Pref(Weapon_RocketLauncher, 90.f, 150.f, 300.f, 1500.f, 3000.f),
			VeloxBotTestsShared::Pref(Weapon_Railgun, 90.f * 1.2f, 400.f, 400.f, 20480.f, 20480.f),
		};
		FVeloxWeaponChoiceInput H;
		H.Weapons = {VeloxBotTestsShared::Option(Weapon_RocketLauncher, 10, 300.f), VeloxBotTestsShared::Option(Weapon_Railgun, 10)};
		H.bHasTarget = true;
		H.RangeCm = 1000.f;
		TestEqual(TEXT("No current weapon: the 1.2x challenger wins"), VeloxWeaponChoice::Pick(H, Pair), 1);
		H.CurrentWeapon = Weapon_RocketLauncher;
		TestEqual(TEXT("Hysteresis keeps RL at score ratio 1.2"), VeloxWeaponChoice::Pick(H, Pair), 0);
		Pair[1].Weight = 90.f * 1.3f;
		TestEqual(TEXT("Hysteresis yields at score ratio 1.3"), VeloxWeaponChoice::Pick(H, Pair), 1);
	}

	// No target: pre-select for PreferredRange (RangeCm ignored).
	{
		FVeloxWeaponChoiceInput Idle = In;
		Idle.bHasTarget = false;
		Idle.RangeCm = 150.f;
		Idle.PreferredRangeCm = 1000.f;
		TestEqual(TEXT("No target: best weapon for PreferredRange 1000 (RL)"), VeloxWeaponChoice::Pick(Idle, Prefs), Idx(Weapon_RocketLauncher));
	}

	{
		FVeloxWeaponChoiceInput Empty;
		Empty.bHasTarget = true;
		Empty.RangeCm = 500.f;
		TestEqual(TEXT("Nothing owned -> INDEX_NONE"), VeloxWeaponChoice::Pick(Empty, Prefs), static_cast<int32>(INDEX_NONE));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsDecisionTest, "Velox.Bots.Decision", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsDecisionTest::RunTest(const FString& Parameters)
{
	const FVeloxBotDifficultyParams Hard = VeloxBotTestsShared::HardParams();
	const FVeloxItemScoringParams Scoring = VeloxItemValue::MakeParams(Hard);
	FRandomStream Stream(99);

	auto MegaCandidate = [&](int32 Health, float Travel)
	{
		FVeloxGoalItemCandidate C;
		C.CandidateId = 1;
		C.Score = VeloxItemValue::Score(VeloxBotTestsShared::Mega(Health, Travel), Scoring);
		C.Location = FVector(100.0, 200.0, 300.0);
		C.bMajorItem = true;
		C.TravelSeconds = Travel;
		return C;
	};

	FVeloxBotDecisionInput Base;
	Base.EnemyId = 42;
	Base.EnemyLocation = FVector(1000.0, 0.0, 0.0);
	Base.Health = 100;
	Base.Armor = 0;
	Base.WeaponStrength = 1.f;

	{
		FVeloxBotDecisionInput In = Base;
		In.bPostMatch = true;
		In.bEnemyVisible = true;
		In.Items = {MegaCandidate(20, 2.f)};
		TestTrue(TEXT("PostMatch -> None"), VeloxBotDecision::ChooseGoal(In, Hard, Stream).Type == EVeloxBotGoal::None);
	}
	{
		FVeloxBotDecisionInput In = Base;
		In.bEnemyVisible = true;
		In.Items = {MegaCandidate(100, 2.f)};
		const FVeloxBotGoalChoice G = VeloxBotDecision::ChooseGoal(In, Hard, Stream);
		TestTrue(TEXT("Enemy visible + healthy -> Fight"), G.Type == EVeloxBotGoal::Fight);
		TestEqual(TEXT("Fight goal carries the enemy"), G.EnemyId, 42);
	}
	{
		FVeloxBotDecisionInput In = Base;
		In.bEnemyVisible = true;
		In.Health = 20;
		In.Items = {MegaCandidate(20, 2.f)};
		const FVeloxBotGoalChoice G = VeloxBotDecision::ChooseGoal(In, Hard, Stream);
		AddInfo(FString::Printf(TEXT("Hurt bot: Mega score %.2f vs bias x fight %.2f"), In.Items[0].Score, Hard.FightItemBias * VeloxBotDecision::FightScore(In)));
		TestTrue(TEXT("Enemy visible + 20 health + Mega at 2 s -> Item"), G.Type == EVeloxBotGoal::Item);
		TestEqual(TEXT("Item goal is the Mega"), G.ItemId, 1);
	}
	{
		FVeloxBotDecisionInput In = Base;
		In.RoamPoints = {FVector(1.0, 0.0, 0.0), FVector(2.0, 0.0, 0.0), FVector(3.0, 0.0, 0.0)};
		FRandomStream S1(5), S2(5);
		const FVeloxBotGoalChoice G1 = VeloxBotDecision::ChooseGoal(In, Hard, S1);
		const FVeloxBotGoalChoice G2 = VeloxBotDecision::ChooseGoal(In, Hard, S2);
		TestTrue(TEXT("Nothing -> Roam"), G1.Type == EVeloxBotGoal::Roam);
		TestTrue(TEXT("Roam point drawn from RoamPoints"), In.RoamPoints.Contains(G1.Point));
		TestEqual(TEXT("Roam draw deterministic for a seed"), G1.Point, G2.Point);
		In.Current = G1;
		In.RoamPoints = {FVector(9.0, 9.0, 9.0)};
		TestEqual(TEXT("Current Roam point kept"), VeloxBotDecision::ChooseGoal(In, Hard, Stream).Point, G1.Point);
		FVeloxBotDecisionInput Nothing = Base;
		TestTrue(TEXT("No items, no roam points -> None"), VeloxBotDecision::ChooseGoal(Nothing, Hard, Stream).Type == EVeloxBotGoal::None);
	}
	{
		// Item hysteresis (GoalSwitchHysteresis 1.3).
		FVeloxGoalItemCandidate A, B;
		A.CandidateId = 1;
		A.Score = 10.f;
		B.CandidateId = 2;
		B.Score = 12.f;
		FVeloxBotDecisionInput In = Base;
		In.Current.Type = EVeloxBotGoal::Item;
		In.Current.ItemId = 1;
		In.Items = {A, B};
		TestEqual(TEXT("Hysteresis: 12 vs current 10 keeps the current item"), VeloxBotDecision::ChooseGoal(In, Hard, Stream).ItemId, 1);
		In.Items[1].Score = 14.f;
		TestEqual(TEXT("Hysteresis: 14 vs current 10 switches"), VeloxBotDecision::ChooseGoal(In, Hard, Stream).ItemId, 2);
		In.Items[1].Score = 12.f;
		In.Items.RemoveAt(0);
		TestEqual(TEXT("Current item taken (not listed) -> best"), VeloxBotDecision::ChooseGoal(In, Hard, Stream).ItemId, 2);
		In.Items[0].bReachable = false;
		TestTrue(TEXT("Unreachable item never chosen"), VeloxBotDecision::ChooseGoal(In, Hard, Stream).Type == EVeloxBotGoal::None);
	}
	{
		// Duel camping: nothing scores, the major item coming up soonest (relative to travel) is camped.
		FVeloxGoalItemCandidate MegaC, RA;
		MegaC.CandidateId = 1;
		MegaC.bMajorItem = true;
		MegaC.bAvailable = false;
		MegaC.TimeUntilRespawn = 10.f;
		MegaC.TravelSeconds = 3.f;
		MegaC.Location = FVector(1.0, 2.0, 3.0);
		RA = MegaC;
		RA.CandidateId = 2;
		RA.TimeUntilRespawn = 20.f;
		RA.TravelSeconds = 2.f;
		FVeloxBotDecisionInput In = Base;
		In.bDuel = true;
		In.Items = {RA, MegaC};
		In.RoamPoints = {FVector(5.0, 5.0, 5.0)};
		const FVeloxBotGoalChoice G = VeloxBotDecision::ChooseGoal(In, Hard, Stream);
		TestTrue(TEXT("Duel camping -> Roam"), G.Type == EVeloxBotGoal::Roam);
		TestEqual(TEXT("Duel camping targets the Mega (7 s lead vs 18 s)"), G.ItemId, 1);
		TestEqual(TEXT("Duel camping point = item location"), G.Point, MegaC.Location);
		In.bDuel = false;
		TestEqual(TEXT("FFA: no camping, random roam"), VeloxBotDecision::ChooseGoal(In, Hard, Stream).ItemId, static_cast<int32>(INDEX_NONE));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsManeuverTest, "Velox.Bots.Maneuver", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsManeuverTest::RunTest(const FString& Parameters)
{
	using VeloxBotTestsShared::ManeuverIn;
	const double Dt = 1.0 / 60.0;

	FVeloxManeuverParams RJ;
	RJ.Type = EVeloxManeuverType::RocketJump;
	RJ.RunUpSeconds = 0.3f;
	RJ.RocketJumpPitchDeg = 80.f;
	RJ.ImpulseTimeoutSeconds = 0.3f;

	// Rocket jump: approach, wait for the switch, run-up, one launch tick, abort without impulse.
	{
		FVeloxBotManeuver M;
		double T = 0.0;
		M.Begin(RJ, FVector::ZeroVector, FVector(0.0, 0.0, 350.0), T);
		FVeloxManeuverOutput O = M.Tick(ManeuverIn(T, 200.f, 600.f));
		TestTrue(TEXT("RJ approach: wish toward Start"), O.WishToward == EVeloxManeuverWish::Start);
		TestTrue(TEXT("RJ approach: wants the RL"), O.bWantRocketLauncher);
		bool bFiredEarly = false;
		for (int32 i = 0; i < 27; ++i) // 0.45 s switch
		{
			T += Dt;
			O = M.Tick(ManeuverIn(T, 10.f, 600.f));
			bFiredEarly |= O.bFire || O.bJumpHeld;
			TestEqual(TEXT("RJ switch: standing"), O.WishScale, 0.f);
		}
		TestTrue(TEXT("RJ waits for the switch"), M.Phase == EVeloxManeuverPhase::Switch);
		TestFalse(TEXT("RJ: no fire / jump before the switch is done"), bFiredEarly);

		int32 FireTicks = 0;
		int32 RunUpTicks = 0;
		double FireTime = -1.0;
		const double SwitchDone = T + Dt;
		for (int32 i = 0; i < 40 && FireTicks == 0; ++i)
		{
			T += Dt;
			FVeloxManeuverInput In = ManeuverIn(T, 10.f + 20.f * i, 600.f);
			In.bSwitchDone = true;
			O = M.Tick(In);
			if (O.bFire)
			{
				++FireTicks;
				FireTime = T;
				TestTrue(TEXT("RJ launch tick: jump held"), O.bJumpHeld);
				TestTrue(TEXT("RJ launch tick: pitch forced"), O.bForcePitch);
				TestEqual(TEXT("RJ launch tick: pitch -80"), O.PitchDeg, -80.f);
			}
			else
			{
				++RunUpTicks;
				TestTrue(TEXT("RJ run-up: wish toward End"), O.WishToward == EVeloxManeuverWish::End);
				TestFalse(TEXT("RJ run-up: no jump"), O.bJumpHeld);
				TestFalse(TEXT("RJ run-up: no forced pitch"), O.bForcePitch);
			}
		}
		AddInfo(FString::Printf(TEXT("RJ: run-up %d ticks, launch %.4f s after the switch ended"), RunUpTicks, FireTime - SwitchDone));
		TestEqual(TEXT("RJ: exactly one launch tick"), FireTicks, 1);
		TestTrue(TEXT("RJ: launch after RunUpSeconds (+- one tick)"), FMath::Abs(FireTime - SwitchDone - RJ.RunUpSeconds) <= Dt + 1e-6);

		// No impulse: abort after 0.3 s, not before.
		double AbortTime = -1.0;
		int32 LaterFires = 0;
		for (int32 i = 0; i < 40 && AbortTime < 0.0; ++i)
		{
			T += Dt;
			O = M.Tick(ManeuverIn(T, 300.f, 400.f, false));
			LaterFires += O.bFire ? 1 : 0;
			if (O.bAbort)
			{
				AbortTime = T;
				TestTrue(TEXT("RJ abort reason noimpulse"), O.Reason == EVeloxManeuverResult::NoImpulse);
			}
		}
		TestEqual(TEXT("RJ: no second fire"), LaterFires, 0);
		TestTrue(TEXT("RJ: aborts after > 0.3 s without impulse"), AbortTime - FireTime > 0.3 - 1e-6 && AbortTime - FireTime <= 0.3 + Dt + 1e-6);
		TestFalse(TEXT("RJ finished"), M.IsActive());
	}

	// Rocket jump with impulse: done on landing near End, missed far from it.
	for (const float LandDist : {50.f, 400.f})
	{
		FVeloxBotManeuver M;
		double T = 0.0;
		M.Begin(RJ, FVector::ZeroVector, FVector(0.0, 0.0, 350.0), T);
		FVeloxManeuverInput In = ManeuverIn(T, 0.f, 600.f);
		In.bSwitchDone = true;
		FVeloxManeuverOutput O = M.Tick(In);
		for (int32 i = 0; i < 30 && !O.bFire; ++i)
		{
			T += Dt;
			In.Time = T;
			O = M.Tick(In);
		}
		TestTrue(TEXT("RJ (impulse run): fired"), O.bFire);
		T += Dt;
		O = M.Tick(ManeuverIn(T, 100.f, 500.f, false));
		T += Dt;
		FVeloxManeuverInput Kick = ManeuverIn(T, 150.f, 450.f, false);
		Kick.bImpulseReceived = true;
		O = M.Tick(Kick);
		TestFalse(TEXT("RJ impulse: not finished"), O.bDone || O.bAbort);
		for (int32 i = 0; i < 40; ++i)
		{
			T += Dt;
			O = M.Tick(ManeuverIn(T, 300.f, 300.f, false));
			TestTrue(TEXT("RJ airborne: wish toward End"), O.WishToward == EVeloxManeuverWish::End);
		}
		T += Dt;
		O = M.Tick(ManeuverIn(T, 600.f, LandDist, true));
		if (LandDist < RJ.EndAcceptanceCm)
		{
			TestTrue(TEXT("RJ landing near End -> done ok"), O.bDone && O.Reason == EVeloxManeuverResult::Ok);
		}
		else
		{
			TestTrue(TEXT("RJ landing far from End -> abort missed"), O.bAbort && O.Reason == EVeloxManeuverResult::Missed);
		}
	}

	// Rocket jump eligibility lost while waiting.
	{
		FVeloxBotManeuver M;
		M.Begin(RJ, FVector::ZeroVector, FVector::UpVector, 0.0);
		M.Tick(ManeuverIn(0.0, 0.f, 600.f));
		FVeloxManeuverInput In = ManeuverIn(Dt, 0.f, 600.f);
		In.bEligible = false;
		const FVeloxManeuverOutput O = M.Tick(In);
		TestTrue(TEXT("RJ ineligible -> abort"), O.bAbort && O.Reason == EVeloxManeuverResult::Ineligible);
	}

	// Jump fast: refuses below 0.9 x required speed, backs up, retries once, then gives up.
	{
		FVeloxManeuverParams JF;
		JF.Type = EVeloxManeuverType::JumpFast;
		JF.RequiredSpeedCmS = 1100.f;
		FVeloxBotManeuver M;
		double T = 0.0;
		M.Begin(JF, FVector::ZeroVector, FVector(700.0, 0.0, 0.0), T);
		FVeloxManeuverOutput O = M.Tick(ManeuverIn(T, 500.f, 1200.f, true, 900.f));
		TestFalse(TEXT("JumpFast far from Start: no jump yet"), O.bJumpHeld);
		T += Dt;
		O = M.Tick(ManeuverIn(T, 90.f, 790.f, true, 0.85f * 1100.f));
		TestFalse(TEXT("JumpFast below 0.9 x required: no jump"), O.bJumpHeld);
		TestTrue(TEXT("JumpFast too slow: back up"), O.WishToward == EVeloxManeuverWish::AwayFromStart);
		T += Dt;
		O = M.Tick(ManeuverIn(T, 300.f, 1000.f, true, 400.f));
		TestTrue(TEXT("JumpFast backing up"), O.WishToward == EVeloxManeuverWish::AwayFromStart);
		T += Dt;
		O = M.Tick(ManeuverIn(T, 650.f, 1350.f, true, 400.f));
		TestTrue(TEXT("JumpFast run-up done -> approach again"), M.Phase == EVeloxManeuverPhase::Approach);
		T += Dt;
		O = M.Tick(ManeuverIn(T, 90.f, 790.f, true, 900.f));
		TestTrue(TEXT("JumpFast second too-slow approach -> abort tooslow"), O.bAbort && O.Reason == EVeloxManeuverResult::TooSlow);
		TestFalse(TEXT("JumpFast never jumped"), O.bJumpHeld);

		FVeloxBotManeuver Fast;
		T = 0.0;
		Fast.Begin(JF, FVector::ZeroVector, FVector(700.0, 0.0, 0.0), T);
		O = Fast.Tick(ManeuverIn(T, 90.f, 790.f, true, 0.9f * 1100.f + 1.f));
		TestTrue(TEXT("JumpFast at >= 0.9 x required: jump held"), O.bJumpHeld);
		T += Dt;
		O = Fast.Tick(ManeuverIn(T, 50.f, 700.f, false, 1000.f));
		TestTrue(TEXT("JumpFast airborne: keeps the hop"), O.bJumpHeld && O.WishToward == EVeloxManeuverWish::End);
		T += Dt;
		O = Fast.Tick(ManeuverIn(T, 700.f, 60.f, true, 1000.f));
		TestTrue(TEXT("JumpFast landing near End -> ok"), O.bDone);
	}

	// Jump pad: to the pad centre, never jump, done on landing near End.
	{
		FVeloxManeuverParams Pad;
		Pad.Type = EVeloxManeuverType::JumpPad;
		FVeloxBotManeuver M;
		double T = 0.0;
		M.Begin(Pad, FVector::ZeroVector, FVector(0.0, 1200.0, 400.0), T);
		FVeloxManeuverOutput O = M.Tick(ManeuverIn(T, 100.f, 1300.f));
		TestTrue(TEXT("Pad: wish toward the pad centre"), O.WishToward == EVeloxManeuverWish::Start);
		TestFalse(TEXT("Pad: no jump"), O.bJumpHeld);
		T += Dt;
		O = M.Tick(ManeuverIn(T, 20.f, 1250.f));
		TestTrue(TEXT("Pad: on the centre, wish toward End"), O.WishToward == EVeloxManeuverWish::End);
		T += Dt;
		O = M.Tick(ManeuverIn(T, 30.f, 1200.f, false));
		TestTrue(TEXT("Pad: airborne"), M.Phase == EVeloxManeuverPhase::Airborne);
		TestFalse(TEXT("Pad airborne: no jump"), O.bJumpHeld);
		T += Dt;
		O = M.Tick(ManeuverIn(T, 1200.f, 100.f, true));
		TestTrue(TEXT("Pad landing within 150 cm -> ok"), O.bDone);
		O = M.Tick(ManeuverIn(T + Dt, 1200.f, 100.f, true));
		TestTrue(TEXT("Finished machine repeats the final output"), O.bDone && !M.IsActive());
	}

	// Drop: walk toward End, done on landing.
	{
		FVeloxManeuverParams Drop;
		Drop.Type = EVeloxManeuverType::Drop;
		FVeloxBotManeuver M;
		M.Begin(Drop, FVector::ZeroVector, FVector(150.0, 0.0, -350.0), 0.0);
		FVeloxManeuverOutput O = M.Tick(ManeuverIn(0.0, 0.f, 400.f));
		TestTrue(TEXT("Drop: wish toward End"), O.WishToward == EVeloxManeuverWish::End);
		M.Tick(ManeuverIn(Dt, 60.f, 300.f, false));
		O = M.Tick(ManeuverIn(2.0 * Dt, 160.f, 20.f, true));
		TestTrue(TEXT("Drop: landing near End -> ok"), O.bDone);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// ======================================================================================================== task 05
// Velox.Bots.ShouldFire       the section 8 fire rule rows: ammo, visibility, cone, range band, ground rockets,
//                             self-splash safety (wall in the face, target inside the blast), switch in progress.
// Velox.Bots.CombatRules      FOV, sticky target choice, the section 10 overlay (strafe side, range keeping, leash,
//                             path blend, dodge), projectile threat geometry, chance rolls.
// Velox.Bots.RocketJumpAbort  Maneuver rocket-jump rows: eligibility lost on the approach, during the switch and in the
//                             run-up -> Ineligible (the controller logs result=aborted reason=ineligible), no fire.

#if WITH_DEV_AUTOMATION_TESTS

#include "Bots/VeloxBotCombat.h"

namespace VeloxBotTests05
{
	/** A visible target 1000 cm ahead on +X, aimed dead on, ammo, ready. */
	FVeloxFireInput DeadOn()
	{
		FVeloxFireInput In;
		In.bHasWeapon = true;
		In.bWeaponReady = true;
		In.bHasAmmo = true;
		In.bTargetVisible = true;
		In.Eye = FVector(0.0, 0.0, 60.0);
		In.TargetCentre = FVector(1000.0, 0.0, 60.0);
		In.DesiredPoint = In.TargetCentre;
		In.AimDir = FVector(1.0, 0.0, 0.0);
		return In;
	}

	FVector DirFromYawDeg(double YawDeg)
	{
		const double R = FMath::DegreesToRadians(YawDeg);
		return FVector(FMath::Cos(R), FMath::Sin(R), 0.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsShouldFireTest, "Velox.Bots.ShouldFire", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsShouldFireTest::RunTest(const FString& Parameters)
{
	using VeloxBotTests05::DeadOn;
	using VeloxBotTests05::DirFromYawDeg;
	FVeloxBotDifficultyParams Hard = VeloxBotTestsShared::HardParams();
	Hard.FireConeDeg = 4.f;

	TestTrue(TEXT("dead on, ammo, visible -> fire"), VeloxBotCombat::ShouldFire(DeadOn(), Hard));
	{
		FVeloxFireInput In = DeadOn();
		In.bHasAmmo = false;
		TestTrue(TEXT("no ammo -> NoAmmo"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::NoAmmo);
		In = DeadOn();
		In.bTargetVisible = false;
		TestTrue(TEXT("target not visible -> NoTarget"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::NoTarget);
		In = DeadOn();
		In.bWeaponReady = false;
		TestTrue(TEXT("switch in progress -> NotReady"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::NotReady);
		In = DeadOn();
		In.bHasWeapon = false;
		TestTrue(TEXT("no weapon -> NoWeapon"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::NoWeapon);
	}
	// Cone: 3.5 degrees off fires at Hard (4), not at Expert (3); 6 degrees never at Hard.
	{
		FVeloxFireInput In = DeadOn();
		In.AimDir = DirFromYawDeg(3.5);
		TestTrue(TEXT("3.5 deg off: Hard cone 4 fires"), VeloxBotCombat::ShouldFire(In, Hard));
		FVeloxBotDifficultyParams Expert = Hard;
		Expert.FireConeDeg = 3.f;
		TestTrue(TEXT("3.5 deg off: Expert cone 3 -> OffTarget"), VeloxBotCombat::EvaluateFire(In, Expert) == EVeloxFireVerdict::OffTarget);
		In.AimDir = DirFromYawDeg(-6.0);
		TestTrue(TEXT("6 deg off -> OffTarget"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::OffTarget);
	}
	// The cone is measured against the desired (lead) point, not the target centre.
	{
		FVeloxFireInput In = DeadOn();
		In.DesiredPoint = FVector(1000.0, 270.0, 60.0); // lead ~15 degrees to the left
		In.AimDir = (In.DesiredPoint - In.Eye).GetSafeNormal();
		TestTrue(TEXT("aimed at the lead point (15 deg off the centre) -> fire"), VeloxBotCombat::ShouldFire(In, Hard));
		In.AimDir = FVector(1.0, 0.0, 0.0);
		TestTrue(TEXT("aimed at the centre while the lead point is 15 deg off -> OffTarget"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::OffTarget);
	}
	// Direct-hit radius (Q3 BotCheckAttack): 3.5 deg at 1000 cm is 61 cm off -> no shot with a 60 cm radius; 3 deg (52 cm) fires.
	{
		FVeloxFireInput In = DeadOn();
		In.DirectHitRadiusCm = 60.f;
		In.AimDir = DirFromYawDeg(3.5);
		TestTrue(TEXT("hit radius 60: 3.5 deg at 1000 cm (61 cm) -> OffTarget"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::OffTarget);
		In.AimDir = DirFromYawDeg(3.0);
		TestTrue(TEXT("hit radius 60: 3 deg at 1000 cm (52 cm) -> fire"), VeloxBotCombat::ShouldFire(In, Hard));
		In.TargetCentre = FVector(300.0, 0.0, 60.0);
		In.DesiredPoint = In.TargetCentre;
		In.AimDir = DirFromYawDeg(3.9);
		TestTrue(TEXT("hit radius at 300 cm: the preset cone (4 deg) is the tighter rule"), VeloxBotCombat::ShouldFire(In, Hard));
		In = DeadOn();
		In.DirectHitRadiusCm = 60.f;
		In.SplashRadiusCm = 120.f;
		In.AimDir = DirFromYawDeg(3.5);
		TestTrue(TEXT("splash weapons ignore the direct-hit radius"), VeloxBotCombat::ShouldFire(In, Hard));
	}
	// Range band.
	{
		FVeloxFireInput In = DeadOn();
		In.MinRangeCm = 0.f;
		In.MaxRangeCm = 1900.f; // LG
		TestTrue(TEXT("inside the band -> fire"), VeloxBotCombat::ShouldFire(In, Hard));
		In.TargetCentre = FVector(2500.0, 0.0, 60.0);
		In.DesiredPoint = In.TargetCentre;
		TestTrue(TEXT("LG at 2500 cm -> OutOfRange"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::OutOfRange);
		In = DeadOn();
		In.MinRangeCm = 400.f; // RG
		In.MaxRangeCm = 20480.f;
		In.TargetCentre = FVector(300.0, 0.0, 60.0);
		In.DesiredPoint = In.TargetCentre;
		TestTrue(TEXT("RG at 300 cm (MinRange 400) -> OutOfRange"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::OutOfRange);
	}
	// Splash weapons (RL 120 cm): self-splash safety and ground rockets.
	{
		FVeloxFireInput In = DeadOn();
		In.SplashRadiusCm = 120.f;
		In.SelfSplashMarginCm = 50.f;
		In.bAimHit = true;
		In.AimHitPoint = FVector(1000.0, 0.0, 0.0);
		TestTrue(TEXT("RL: target 1000 cm, hit at its feet -> fire"), VeloxBotCombat::ShouldFire(In, Hard));
		In.AimHitPoint = FVector(150.0, 0.0, 60.0);
		TestTrue(TEXT("RL: wall 150 cm in the face (<= 170) -> SelfSplash"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::SelfSplash);
		In.AimHitPoint = FVector(171.0, 0.0, 60.0);
		TestTrue(TEXT("RL: wall at 171 cm (> 170) is not a self-splash danger"), VeloxBotCombat::EvaluateFire(In, Hard) != EVeloxFireVerdict::SelfSplash);
		In = DeadOn();
		In.SplashRadiusCm = 120.f;
		In.TargetCentre = FVector(160.0, 0.0, 60.0);
		In.DesiredPoint = In.TargetCentre;
		TestTrue(TEXT("RL: target 160 cm away (inside 170) -> SelfSplash"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::SelfSplash);
		// Ground rockets: aim 10 degrees off, but the ray's world hit lands within 0.8 x splash of the target.
		In = DeadOn();
		In.SplashRadiusCm = 120.f;
		In.AimDir = DirFromYawDeg(10.0);
		In.bAimHit = true;
		In.AimHitPoint = FVector(960.0, 60.0, 30.0); // 75 cm from the centre (<= 96)
		TestTrue(TEXT("RL: off the cone but the floor hit is inside 0.8 x splash -> fire"), VeloxBotCombat::ShouldFire(In, Hard));
		In.AimHitPoint = FVector(900.0, 150.0, 0.0);
		TestTrue(TEXT("RL: off the cone and the hit outside 0.8 x splash -> OffTarget"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::OffTarget);
		In.bAimHit = false;
		In.AimHitPoint = FVector(960.0, 60.0, 30.0);
		TestTrue(TEXT("no world hit and off the cone -> OffTarget"), VeloxBotCombat::EvaluateFire(In, Hard) == EVeloxFireVerdict::OffTarget);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsCombatRulesTest, "Velox.Bots.CombatRules", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsCombatRulesTest::RunTest(const FString& Parameters)
{
	using VeloxBotTests05::DirFromYawDeg;
	// FOV (section 6): Hard 160 -> +-80 degrees.
	{
		const FVector Eye = FVector::ZeroVector;
		TestTrue(TEXT("79 deg off inside 160 FOV"), VeloxBotCombat::InFov(FVector::ForwardVector, Eye, DirFromYawDeg(79.0) * 1000.0, 160.f));
		TestFalse(TEXT("81 deg off outside 160 FOV"), VeloxBotCombat::InFov(FVector::ForwardVector, Eye, DirFromYawDeg(81.0) * 1000.0, 160.f));
		TestFalse(TEXT("behind outside 180 FOV"), VeloxBotCombat::InFov(FVector::ForwardVector, Eye, FVector(-1000.0, 1.0, 0.0), 180.f));
		TestTrue(TEXT("pitch counts: 60 deg up inside 130"), VeloxBotCombat::InFov(FVector::ForwardVector, Eye, FVector(500.0, 0.0, 866.0), 130.f));
		TestFalse(TEXT("pitch counts: 70 deg up outside 130"), VeloxBotCombat::InFov(FVector::ForwardVector, Eye, FVector(342.0, 0.0, 940.0), 130.f));
	}
	// Target choice: nearest visible, sticky by 1.5.
	{
		TArray<FVeloxTargetCandidate> C = {{1, true, 1000.f}, {2, true, 800.f}, {3, false, 100.f}};
		TestEqual(TEXT("no current target -> the nearest visible (2)"), VeloxBotCombat::SelectTarget(C, INDEX_NONE, 1.5f), 1);
		TestEqual(TEXT("current 1 at 1000 kept against 800 (> 1000/1.5)"), VeloxBotCombat::SelectTarget(C, 1, 1.5f), 0);
		C[1].DistanceCm = 600.f;
		TestEqual(TEXT("current 1 at 1000 replaced by 600 (< 666)"), VeloxBotCombat::SelectTarget(C, 1, 1.5f), 1);
		C[0].bVisible = false;
		C[1].bVisible = false;
		TestEqual(TEXT("nothing visible -> none (the invisible near one is not chosen)"), VeloxBotCombat::SelectTarget(C, 1, 1.5f), static_cast<int32>(INDEX_NONE));
	}
	// Overlay (section 10).
	{
		FVeloxCombatWishInput In;
		In.ToEnemy = FVector2D(1000.0, 0.0);
		In.BestMinCm = 300.f;
		In.BestMaxCm = 1500.f;
		In.StrafeSign = 1.f;
		In.StrafeAmplitude = 0.9f;
		FVector2D W = VeloxBotCombat::CombatWish(In);
		TestTrue(TEXT("in the band: pure strafe to the left (+Y)"), W.Equals(FVector2D(0.0, 1.0), 1e-4));
		In.StrafeSign = -1.f;
		TestTrue(TEXT("sign flips the side"), VeloxBotCombat::CombatWish(In).Equals(FVector2D(0.0, -1.0), 1e-4));
		In.ToEnemy = FVector2D(2000.0, 0.0);
		W = VeloxBotCombat::CombatWish(In);
		TestTrue(TEXT("beyond BestMax: strafe plus closing in"), W.X > 0.3 && W.Y < -0.5);
		In.ToEnemy = FVector2D(200.0, 0.0);
		W = VeloxBotCombat::CombatWish(In);
		TestTrue(TEXT("inside BestMin: strafe plus backing off"), W.X < -0.3 && W.Y < -0.5);
		In.ToEnemy = FVector2D(3000.0, 0.0);
		In.LeashCm = 2500.f;
		const FVector2D Leash = VeloxBotCombat::CombatWish(In);
		TestTrue(TEXT("beyond the leash closes in harder than beyond BestMax"), Leash.X > 0.6);
		In.ToEnemy = FVector2D(1000.0, 0.0);
		In.bBlendPath = true;
		In.PathWeight = 0.5f;
		In.PathWish = FVector2D(-1.0, 0.0);
		W = VeloxBotCombat::CombatWish(In);
		TestTrue(TEXT("item path blended in (away from the enemy)"), W.X < -0.3 && W.Y < -0.5 && FMath::IsNearlyEqual(W.Size(), 1.0, 1e-4));
		In.bDodge = true;
		In.DodgeDir = FVector2D(0.0, 1.0);
		In.bBlendPath = false;
		W = VeloxBotCombat::CombatWish(In);
		TestTrue(TEXT("dodge overrides the strafe side"), W.Equals(FVector2D(0.0, 1.0), 1e-4));
		FVeloxCombatWishInput Empty;
		TestTrue(TEXT("no enemy, no dodge, no path -> zero wish"), VeloxBotCombat::CombatWish(Empty).IsZero());
	}
	// Projectile threat: rocket at 2250 cm/s.
	{
		const FVector Bot(1000.0, 0.0, 0.0);
		FVeloxProjectileThreat T = VeloxBotCombat::ProjectileThreat(FVector::ZeroVector, FVector(2250.0, 0.0, 0.0), Bot, 220.f, 1.f);
		TestTrue(TEXT("rocket straight at the bot -> threat"), T.bThreat);
		TestTrue(TEXT("closest approach in 0.444 s"), FMath::IsNearlyEqual(T.TimeToClosest, 1000.f / 2250.f, 1e-3f));
		TestTrue(TEXT("away direction is perpendicular to the flight"), FMath::IsNearlyZero(T.AwayDir.X, 1e-4) && FMath::IsNearlyEqual(FMath::Abs(T.AwayDir.Y), 1.0, 1e-4));
		T = VeloxBotCombat::ProjectileThreat(FVector::ZeroVector, FVector(2250.0, 0.0, 0.0), FVector(1000.0, 150.0, 0.0), 220.f, 1.f);
		TestTrue(TEXT("150 cm to the left: threat, dodge further left"), T.bThreat && T.AwayDir.Y > 0.99);
		T = VeloxBotCombat::ProjectileThreat(FVector::ZeroVector, FVector(2250.0, 0.0, 0.0), FVector(1000.0, 400.0, 0.0), 220.f, 1.f);
		TestFalse(TEXT("400 cm to the side: no threat"), T.bThreat);
		T = VeloxBotCombat::ProjectileThreat(FVector::ZeroVector, FVector(2250.0, 0.0, 0.0), FVector(4000.0, 0.0, 0.0), 220.f, 1.f);
		TestFalse(TEXT("beyond the 1 s horizon (4000 cm): no threat"), T.bThreat);
		T = VeloxBotCombat::ProjectileThreat(FVector(2000.0, 0.0, 0.0), FVector(2250.0, 0.0, 0.0), Bot, 220.f, 1.f);
		TestFalse(TEXT("already past the bot: no threat"), T.bThreat);
		T = VeloxBotCombat::ProjectileThreat(FVector::ZeroVector, FVector::ZeroVector, Bot, 220.f, 1.f);
		TestFalse(TEXT("resting projectile: no threat"), T.bThreat);
	}
	// Chance rolls: 0.6 per second at 60 Hz over 1000 simulated seconds ~ 600 events.
	{
		FRandomStream Stream(1234);
		int32 Events = 0;
		for (int32 i = 0; i < 60000; ++i)
		{
			Events += VeloxBotCombat::RollChance(0.6f, 1.f / 60.f, Stream) ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("0.6/s over 1000 s: %d events in [540, 660]"), Events), Events >= 540 && Events <= 660);
		TestFalse(TEXT("chance 0 never fires"), VeloxBotCombat::RollChance(0.f, 1.f / 60.f, Stream));
		TestTrue(TEXT("chance x dt >= 1 always fires"), VeloxBotCombat::RollChance(120.f, 1.f / 60.f, Stream));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsRocketJumpAbortTest, "Velox.Bots.RocketJumpAbort", VeloxBotTestsShared::BotTestFlags)
bool FVeloxBotsRocketJumpAbortTest::RunTest(const FString& Parameters)
{
	using VeloxBotTestsShared::ManeuverIn;
	const double Dt = 1.0 / 60.0;
	FVeloxManeuverParams RJ;
	RJ.Type = EVeloxManeuverType::RocketJump;
	RJ.RunUpSeconds = 0.3f;
	RJ.RocketJumpPitchDeg = 80.f;

	// Review F1 / D11 regression row: a traversal begun on the ground at the start with bEligible true on its first tick
	// enters Switch (it used to read a one-tick-stale false and finish Ineligible).
	{
		FVeloxBotManeuver M;
		M.Begin(RJ, FVector::ZeroVector, FVector(0.0, 0.0, 350.0), 0.0);
		FVeloxManeuverInput In = ManeuverIn(0.0, 5.f, 650.f);
		In.bEligible = true;
		const FVeloxManeuverOutput O = M.Tick(In);
		TestTrue(TEXT("F1: tick 1 eligible at the start -> Switch, not Finished"), M.Phase == EVeloxManeuverPhase::Switch && !O.bAbort && !O.bDone);
	}
	// Launcher lost (or health below the link's minimum) while walking to the start.
	{
		FVeloxBotManeuver M;
		M.Begin(RJ, FVector::ZeroVector, FVector(0.0, 0.0, 350.0), 0.0);
		FVeloxManeuverOutput O = M.Tick(ManeuverIn(0.0, 300.f, 700.f));
		TestTrue(TEXT("approach while eligible"), !O.bAbort && O.bWantRocketLauncher);
		FVeloxManeuverInput In = ManeuverIn(Dt, 280.f, 690.f);
		In.bEligible = false;
		O = M.Tick(In);
		TestTrue(TEXT("approach: ineligible -> abort"), O.bAbort && O.Reason == EVeloxManeuverResult::Ineligible);
		TestFalse(TEXT("approach abort: no fire, no jump"), O.bFire || O.bJumpHeld);
		TestFalse(TEXT("approach abort: finished"), M.IsActive());
	}
	// Lost during the switch wait.
	{
		FVeloxBotManeuver M;
		M.Begin(RJ, FVector::ZeroVector, FVector(0.0, 0.0, 350.0), 0.0);
		M.Tick(ManeuverIn(0.0, 10.f, 600.f));
		TestTrue(TEXT("at the start without the switch: Switch phase"), M.Phase == EVeloxManeuverPhase::Switch);
		FVeloxManeuverInput In = ManeuverIn(Dt, 10.f, 600.f);
		In.bEligible = false;
		const FVeloxManeuverOutput O = M.Tick(In);
		TestTrue(TEXT("switch: ineligible -> abort"), O.bAbort && O.Reason == EVeloxManeuverResult::Ineligible && !O.bFire);
	}
	// Lost in the run-up, one tick before the launch: never fires.
	{
		FVeloxBotManeuver M;
		M.Begin(RJ, FVector::ZeroVector, FVector(0.0, 0.0, 350.0), 0.0);
		FVeloxManeuverInput In = ManeuverIn(0.0, 10.f, 600.f);
		In.bSwitchDone = true;
		M.Tick(In);
		TestTrue(TEXT("switch done: RunUp"), M.Phase == EVeloxManeuverPhase::RunUp);
		double T = 0.0;
		bool bFired = false;
		for (int32 i = 0; i < 16; ++i) // 0.267 s < RunUpSeconds
		{
			T += Dt;
			In.Time = T;
			bFired |= M.Tick(In).bFire;
		}
		TestFalse(TEXT("no launch before RunUpSeconds"), bFired);
		T += Dt;
		In.Time = T;
		In.bEligible = false;
		const FVeloxManeuverOutput O = M.Tick(In);
		TestTrue(TEXT("run-up: ineligible -> abort without firing"), O.bAbort && O.Reason == EVeloxManeuverResult::Ineligible && !O.bFire && !O.bForcePitch);
		TestEqual(TEXT("result reason text (VeloxBotLink reason=)"), FString(LexToString(O.Reason)), FString(TEXT("ineligible")));
	}
	// Once launched, losing eligibility no longer matters (the own splash took the health).
	{
		FVeloxBotManeuver M;
		M.Begin(RJ, FVector::ZeroVector, FVector(0.0, 0.0, 350.0), 0.0);
		FVeloxManeuverInput In = ManeuverIn(0.0, 10.f, 600.f);
		In.bSwitchDone = true;
		double T = 0.0;
		FVeloxManeuverOutput O = M.Tick(In);
		for (int32 i = 0; i < 30 && !O.bFire; ++i)
		{
			T += Dt;
			In.Time = T;
			O = M.Tick(In);
		}
		TestTrue(TEXT("launched"), O.bFire && M.Phase == EVeloxManeuverPhase::Launched);
		T += Dt;
		FVeloxManeuverInput After = ManeuverIn(T, 50.f, 500.f, false);
		After.bEligible = false;
		After.bImpulseReceived = true;
		O = M.Tick(After);
		TestFalse(TEXT("after the launch the self damage does not abort"), O.bAbort);
		TestTrue(TEXT("airborne after the impulse"), M.Phase == EVeloxManeuverPhase::Airborne);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
