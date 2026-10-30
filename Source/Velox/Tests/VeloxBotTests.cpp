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

// ------------------------------------------------------------------------------------------------ task 06: item routing
//   Velox.Bots.ItemRouting  world-free rules of Bots/VeloxBotItemRouting.h: scoring input assembly from bot and item facts
//                           (ownership, ammo, Duel denial), travel time from paths, partial path = score 0 and never
//                           chosen, path-query selection (budget, current goal first, Duel camp slot), committed-goal
//                           timing gate, Duel minimum score -> camping through ChooseGoal, evaluation due rule, camp
//                           point, refusal guard, item labels.

#include "Bots/VeloxBotItemRouting.h"

namespace VeloxBotRoutingTests
{
	FVeloxRoutingBotFacts Bot(int32 Health, int32 Armor)
	{
		FVeloxRoutingBotFacts B;
		B.Health = Health;
		B.MaxHealth = 100;
		B.HealthHardCap = 200;
		B.Armor = Armor;
		B.MaxArmor = 200;
		B.Ammo.Add(VeloxGameplayTags::Ammo_Rockets, FIntPoint(10, 200));
		B.Ammo.Add(VeloxGameplayTags::Ammo_Slugs, FIntPoint(0, 200));
		B.OwnedWeaponTags.Add(VeloxGameplayTags::Weapon_RocketLauncher);
		B.OwnedAmmoTags.Add(VeloxGameplayTags::Ammo_Rockets);
		return B;
	}

	FVeloxRoutingItemFacts Item(int32 Id, EVeloxItemType Type, float BaseValue, bool bMajor = false)
	{
		FVeloxRoutingItemFacts I;
		I.CandidateId = Id;
		I.Type = Type;
		I.BotBaseValue = BaseValue;
		I.bMajorItem = bMajor;
		return I;
	}

	FVeloxItemScoringParams Timing(float Skill, float MaxWait)
	{
		FVeloxItemScoringParams P;
		P.ItemTimingSkill = Skill;
		P.MaxWaitSeconds = MaxWait;
		return P;
	}

	FVeloxRoutingPathResult FullPath(double Cost)
	{
		FVeloxRoutingPathResult P;
		P.bFound = true;
		P.Cost = Cost;
		P.Length = Cost;
		return P;
	}

	FVeloxRoutingPreScore Pre(int32 Id, float Score, bool bMajor = false, bool bAvailable = true, float Respawn = 0.f, float Straight = 1.f)
	{
		FVeloxRoutingPreScore P;
		P.CandidateId = Id;
		P.PreScore = Score;
		P.bMajorItem = bMajor;
		P.bAvailable = bAvailable;
		P.TimeUntilRespawn = Respawn;
		P.StraightSeconds = Straight;
		return P;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsItemRoutingTest, "Velox.Bots.ItemRouting", VeloxBotTestsShared::BotTestFlags)

bool FVeloxBotsItemRoutingTest::RunTest(const FString& Parameters)
{
	using namespace VeloxBotRoutingTests;
	const FVeloxItemScoringParams NoTiming = Timing(0.f, 0.f);

	// Scoring input from the facts: health / armor, weapon ownership, the ammo of the item's tag.
	{
		const FVeloxRoutingBotFacts B = Bot(50, 20);
		FVeloxRoutingItemFacts Mega = Item(1, EVeloxItemType::Health, 100.f, true);
		Mega.HealthCap = EVeloxHealthCap::HardCap;
		const FVeloxItemScoringInput In = VeloxBotItemRouting::MakeScoringInput(Mega, B, 2.f);
		TestEqual(TEXT("health copied"), In.Health, 50);
		TestEqual(TEXT("hard cap copied"), In.HealthHardCap, 200);
		TestEqual(TEXT("armor copied"), In.Armor, 20);
		TestEqual(TEXT("travel copied"), In.TravelSeconds, 2.f);
		TestEqual(TEXT("Mega need at 50 / hard cap 200 = 0.75"), VeloxItemValue::Need(In), 0.75f);

		FVeloxRoutingItemFacts RL = Item(2, EVeloxItemType::Weapon, 60.f);
		RL.WeaponTag = VeloxGameplayTags::Weapon_RocketLauncher;
		RL.AmmoTag = VeloxGameplayTags::Ammo_Rockets;
		const FVeloxItemScoringInput RLIn = VeloxBotItemRouting::MakeScoringInput(RL, B, 1.f);
		TestTrue(TEXT("owned weapon -> bOwnsWeapon"), RLIn.bOwnsWeapon);
		TestEqual(TEXT("weapon item: ammo of its ammo tag"), RLIn.Ammo, 10);
		TestEqual(TEXT("weapon item: ammo cap"), RLIn.AmmoMax, 200);
		TestEqual(TEXT("owned weapon need = 0.3 x ammo need"), VeloxItemValue::Need(RLIn), 0.3f * 190.f / 200.f, 1e-5f);

		FVeloxRoutingItemFacts RG = Item(3, EVeloxItemType::Weapon, 50.f);
		RG.WeaponTag = VeloxGameplayTags::Weapon_Railgun;
		RG.AmmoTag = VeloxGameplayTags::Ammo_Slugs;
		TestFalse(TEXT("unowned weapon"), VeloxBotItemRouting::MakeScoringInput(RG, B, 1.f).bOwnsWeapon);
		TestEqual(TEXT("unowned weapon need 1"), VeloxItemValue::Need(VeloxBotItemRouting::MakeScoringInput(RG, B, 1.f)), 1.f);

		FVeloxRoutingItemFacts Slugs = Item(4, EVeloxItemType::Ammo, 10.f);
		Slugs.AmmoTag = VeloxGameplayTags::Ammo_Slugs;
		FVeloxRoutingItemFacts Rockets = Item(5, EVeloxItemType::Ammo, 10.f);
		Rockets.AmmoTag = VeloxGameplayTags::Ammo_Rockets;
		TestFalse(TEXT("slugs without a railgun: no weapon uses them"), VeloxBotItemRouting::MakeScoringInput(Slugs, B, 1.f).bOwnsWeaponUsingAmmo);
		TestTrue(TEXT("rockets with the launcher"), VeloxBotItemRouting::MakeScoringInput(Rockets, B, 1.f).bOwnsWeaponUsingAmmo);
		TestEqual(TEXT("unusable ammo need x 0.1"), VeloxItemValue::Need(VeloxBotItemRouting::MakeScoringInput(Slugs, B, 1.f)), 0.1f, 1e-5f);

		FVeloxRoutingItemFacts Unknown = Item(6, EVeloxItemType::Ammo, 10.f);
		Unknown.AmmoTag = VeloxGameplayTags::Ammo_Slugs;
		FVeloxRoutingBotFacts NoAmmo = B;
		NoAmmo.Ammo.Reset();
		TestEqual(TEXT("ammo type without a row: cap 0, need 0"), VeloxItemValue::Need(VeloxBotItemRouting::MakeScoringInput(Unknown, NoAmmo, 1.f)), 0.f);
	}

	// Duel denial: a major item counts with at least MajorItemMinNeed while it would grant something.
	{
		FVeloxRoutingBotFacts B = Bot(150, 0);
		FVeloxRoutingItemFacts Mega = Item(1, EVeloxItemType::Health, 100.f, true);
		Mega.HealthCap = EVeloxHealthCap::HardCap;
		const float Plain = VeloxItemValue::Score(VeloxBotItemRouting::MakeScoringInput(Mega, B, 1.f), NoTiming);
		TestEqual(TEXT("FFA: Mega at 150 = 100 x 0.25 / 2"), Plain, 12.5f, 1e-4f);
		B.MajorItemMinNeed = 0.8f;
		const float Denied = VeloxItemValue::Score(VeloxBotItemRouting::MakeScoringInput(Mega, B, 1.f), NoTiming);
		TestEqual(TEXT("Duel: Mega at 150 = 100 x 0.8 / 2"), Denied, 40.f, 1e-3f);
		FVeloxRoutingBotFacts Full = Bot(200, 0);
		Full.MajorItemMinNeed = 0.8f;
		TestEqual(TEXT("Duel: Mega at the hard cap stays 0 (the pickup would refuse)"), VeloxItemValue::Score(VeloxBotItemRouting::MakeScoringInput(Mega, Full, 1.f), NoTiming), 0.f);
		FVeloxRoutingBotFacts Low = Bot(20, 0);
		Low.MajorItemMinNeed = 0.8f;
		TestEqual(TEXT("Duel: a need above the floor is unchanged (0.9)"), VeloxItemValue::Score(VeloxBotItemRouting::MakeScoringInput(Mega, Low, 1.f), NoTiming), 45.f, 1e-3f);
		FVeloxRoutingItemFacts Medium = Item(2, EVeloxItemType::Health, 40.f, false);
		FVeloxRoutingBotFacts Hurt = Bot(90, 0);
		Hurt.MajorItemMinNeed = 0.8f;
		TestEqual(TEXT("Duel: minor items are not raised"), VeloxItemValue::Score(VeloxBotItemRouting::MakeScoringInput(Medium, Hurt, 1.f), NoTiming), 40.f * 0.1f / 2.f, 1e-4f);
	}

	// Travel time from the path: cost (length + link entering costs) over the preset speed.
	{
		FVeloxRoutingPathResult P = FullPath(3000.0);
		P.Length = 2500.0;
		TestEqual(TEXT("cost above length wins (entering costs)"), VeloxBotItemRouting::PathTravelSeconds(P, 1000.f), 3.f, 1e-5f);
		P.Cost = 0.0;
		TestEqual(TEXT("no cost: the length"), VeloxBotItemRouting::PathTravelSeconds(P, 1000.f), 2.5f, 1e-5f);
		TestTrue(TEXT("zero speed: effectively unreachable"), VeloxBotItemRouting::TravelSeconds(100.0, 0.f) > 1000.f);
	}

	// Partial / missing path: unreachable, score 0, never chosen even when it is by far the best item.
	{
		const FVeloxRoutingBotFacts B = Bot(100, 0);
		const FVeloxRoutingItemFacts Quad = Item(7, EVeloxItemType::Powerup, 110.f, true);
		FVeloxRoutingPathResult Partial = FullPath(500.0);
		Partial.bPartial = true;
		const FVeloxGoalItemCandidate C = VeloxBotItemRouting::MakeGoalCandidate(Quad, B, Partial, 1000.f, NoTiming);
		TestFalse(TEXT("partial path: unreachable"), C.bReachable);
		TestEqual(TEXT("partial path: score 0"), C.Score, 0.f);
		const FVeloxGoalItemCandidate None = VeloxBotItemRouting::MakeGoalCandidate(Quad, B, FVeloxRoutingPathResult(), 1000.f, NoTiming);
		TestFalse(TEXT("no path: unreachable"), None.bReachable);
		const FVeloxGoalItemCandidate Ok = VeloxBotItemRouting::MakeGoalCandidate(Quad, B, FullPath(2000.0), 1000.f, NoTiming);
		TestEqual(TEXT("full path: Score at cost / speed"), Ok.Score, 110.f / 3.f, 1e-3f);
		TestEqual(TEXT("full path: travel"), Ok.TravelSeconds, 2.f, 1e-5f);

		FVeloxRoutingItemFacts Shard = Item(8, EVeloxItemType::Armor, 5.f);
		const FVeloxGoalItemCandidate Minor = VeloxBotItemRouting::MakeGoalCandidate(Shard, B, FullPath(1000.0), 1000.f, NoTiming);
		FVeloxBotDecisionInput In;
		In.Items = {C, Minor};
		FRandomStream Stream(1);
		const FVeloxBotGoalChoice Choice = VeloxBotDecision::ChooseGoal(In, VeloxBotTestsShared::HardParams(), Stream);
		TestEqual(TEXT("the unreachable Quad is not chosen; the reachable shard is"), Choice.ItemId, 8);
		TestTrue(TEXT("item goal"), Choice.Type == EVeloxBotGoal::Item);
	}

	// Path-query selection: at most MaxQueries, current goal first, Duel camp slot, best pre-scores, ties by id.
	{
		TArray<int32> Ids;
		const TArray<FVeloxRoutingPreScore> P = {Pre(1, 5.f), Pre(2, 9.f), Pre(3, 0.f), Pre(4, 7.f), Pre(5, 9.f), Pre(6, 1.f), Pre(7, 0.f, true, false, 20.f, 3.f),
			Pre(8, 0.f, true, false, 30.f, 1.f)};
		VeloxBotItemRouting::SelectQueryCandidates(P, 4, INDEX_NONE, false, Ids);
		TestEqual(TEXT("budget: 4 queries"), Ids.Num(), 4);
		TestTrue(TEXT("best first, ties by id (2 before 5), then 4, 1"), Ids == TArray<int32>({2, 5, 4, 1}));
		VeloxBotItemRouting::SelectQueryCandidates(P, 4, 6, false, Ids);
		TestTrue(TEXT("current goal (6, low score) first"), Ids.Num() == 4 && Ids[0] == 6 && Ids[1] == 2 && Ids[2] == 5 && Ids[3] == 4);
		VeloxBotItemRouting::SelectQueryCandidates(P, 4, 3, false, Ids);
		TestEqual(TEXT("current goal is queried even at pre-score 0"), Ids[0], 3);
		VeloxBotItemRouting::SelectQueryCandidates(P, 4, 99, false, Ids);
		TestTrue(TEXT("a current goal not in the list is ignored"), Ids == TArray<int32>({2, 5, 4, 1}));
		VeloxBotItemRouting::SelectQueryCandidates(P, 4, INDEX_NONE, true, Ids);
		TestTrue(TEXT("Duel: the next major (lead 17 s beats 29 s) takes a slot"), Ids.Num() == 4 && Ids[0] == 7 && Ids[1] == 2 && Ids[2] == 5 && Ids[3] == 4);
		VeloxBotItemRouting::SelectQueryCandidates(P, 4, 6, true, Ids);
		TestTrue(TEXT("Duel: current goal, camp candidate, then two best"), Ids == TArray<int32>({6, 7, 2, 5}));
		VeloxBotItemRouting::SelectQueryCandidates(P, 0, 6, true, Ids);
		TestEqual(TEXT("budget 0: no queries"), Ids.Num(), 0);
		VeloxBotItemRouting::SelectQueryCandidates(TArray<FVeloxRoutingPreScore>({Pre(3, 0.f)}), 4, INDEX_NONE, false, Ids);
		TestEqual(TEXT("nothing worth a query"), Ids.Num(), 0);
	}

	// Committed goal: the timing gate widens by the travel time at commit (no turn-back when closing in fast).
	{
		const FVeloxItemScoringParams Hard = Timing(1.f, 8.f);
		FVeloxItemScoringInput In;
		In.Type = EVeloxItemType::Powerup;
		In.BotBaseValue = 110.f;
		In.bAvailable = false;
		In.TimeUntilRespawn = 12.f;
		In.TravelSeconds = 2.f;
		TestEqual(TEXT("wait 10 > 8: plain score 0"), VeloxItemValue::Score(In, Hard), 0.f);
		TestEqual(TEXT("committed with 5 s travel at commit: wait gate 13"), VeloxBotItemRouting::CommittedScore(In, Hard, 5.f), 110.f / 13.f, 1e-3f);
		TestEqual(TEXT("commit 0 = plain"), VeloxBotItemRouting::CommittedScore(In, Hard, 0.f), 0.f);
		TestEqual(TEXT("skill 0 ignores unavailable items even when committed"), VeloxBotItemRouting::CommittedScore(In, Timing(0.f, 0.f), 5.f), 0.f);
		In.TimeUntilRespawn = 20.f;
		TestEqual(TEXT("beyond the widened gate: 0"), VeloxBotItemRouting::CommittedScore(In, Hard, 5.f), 0.f);
		In.bAvailable = true;
		TestEqual(TEXT("available: same as Score"), VeloxBotItemRouting::CommittedScore(In, Hard, 5.f), VeloxItemValue::Score(In, Hard));
	}

	// Duel minimum score -> camping: minor items below the floor count as 0, ChooseGoal camps the next major.
	{
		FVeloxGoalItemCandidate Ammo;
		Ammo.CandidateId = 1;
		Ammo.Score = 1.2f;
		FVeloxGoalItemCandidate Mega;
		Mega.CandidateId = 2;
		Mega.bMajorItem = true;
		Mega.bAvailable = false;
		Mega.TimeUntilRespawn = 20.f;
		Mega.TravelSeconds = 3.f;
		Mega.Location = FVector(100, 200, 300);
		FVeloxGoalItemCandidate RA = Mega;
		RA.CandidateId = 3;
		RA.TimeUntilRespawn = 25.f;
		TArray<FVeloxGoalItemCandidate> Items = {Ammo, Mega, RA};
		VeloxBotItemRouting::ApplyMinScore(Items, 0.f);
		TestEqual(TEXT("floor 0: unchanged"), Items[0].Score, 1.2f);
		VeloxBotItemRouting::ApplyMinScore(Items, 3.f);
		TestEqual(TEXT("below the floor: 0"), Items[0].Score, 0.f);
		FVeloxBotDecisionInput In;
		In.bDuel = true;
		In.Items = Items;
		FRandomStream Stream(1);
		const FVeloxBotGoalChoice Camp = VeloxBotDecision::ChooseGoal(In, VeloxBotTestsShared::HardParams(), Stream);
		TestTrue(TEXT("Duel, nothing worth it: Roam (camp)"), Camp.Type == EVeloxBotGoal::Roam);
		TestEqual(TEXT("camps the Mega (17 s lead before the RA's 22 s)"), Camp.ItemId, 2);
		In.bDuel = false;
		In.RoamPoints = {FVector(1, 2, 3)};
		const FVeloxBotGoalChoice Roam = VeloxBotDecision::ChooseGoal(In, VeloxBotTestsShared::HardParams(), Stream);
		TestTrue(TEXT("FFA: plain roam, no camp item"), Roam.Type == EVeloxBotGoal::Roam && Roam.ItemId == INDEX_NONE);
	}

	// Evaluation due, camp point, refusal guard, labels.
	{
		TestTrue(TEXT("cadence up"), VeloxBotItemRouting::IsEvaluationDue(10.0, 10.0, false, false));
		TestFalse(TEXT("cadence not up"), VeloxBotItemRouting::IsEvaluationDue(9.9, 10.0, false, false));
		TestTrue(TEXT("goal lost (taken / reached): immediately"), VeloxBotItemRouting::IsEvaluationDue(9.0, 10.0, true, false));
		TestTrue(TEXT("event (respawn, filter change): immediately"), VeloxBotItemRouting::IsEvaluationDue(9.0, 10.0, false, true));

		const FVector CampP = VeloxBotItemRouting::CampPoint(FVector(0, 0, 300), FVector(1000, 0, 0), 200.f);
		TestTrue(TEXT("camp point 200 cm toward the bot, at the item's height"), CampP.Equals(FVector(200, 0, 300), 1e-3));
		TestTrue(TEXT("bot on the item: the item"), VeloxBotItemRouting::CampPoint(FVector(5, 5, 0), FVector(5, 5, 90), 200.f).Equals(FVector(5, 5, 0), 1e-3));

		TestFalse(TEXT("refusal: not yet"), VeloxBotItemRouting::IsRefusedAtArrival(true, true, 10.0, 11.0, 1.5f));
		TestTrue(TEXT("refusal: 1.5 s at an available item"), VeloxBotItemRouting::IsRefusedAtArrival(true, true, 10.0, 11.5, 1.5f));
		TestFalse(TEXT("waiting at an unavailable item is not a refusal"), VeloxBotItemRouting::IsRefusedAtArrival(true, false, 10.0, 20.0, 1.5f));
		TestFalse(TEXT("not arrived"), VeloxBotItemRouting::IsRefusedAtArrival(false, true, 10.0, 20.0, 1.5f));

		TestEqual(TEXT("label"), VeloxBotItemRouting::ItemLabel(VeloxGameplayTags::Item_Health_Mega), FString(TEXT("Health.Mega")));
		TestEqual(TEXT("label of no tag"), VeloxBotItemRouting::ItemLabel(FGameplayTag()), FString(TEXT("-")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
