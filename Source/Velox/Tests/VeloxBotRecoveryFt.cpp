// Velox - bot airborne recovery, pad discipline, hazard escape and lava avoidance (ADR 0008 sections 3.6.3, 3.6.4 and 3.7,
// tasks M7 H5 and H7).
//
//   Velox.Bots.HazardRules        automation, world-free: foreign-flight rule, ballistic time to a level, the air-recovery
//                                 wish (on course / correction), the hazard box exit, the pad-flight predicate and what
//                                 TickAirborne wishes on a pad (End, scale 1; the follower replaces it by the arrive wish),
//                                 the H7 pad arrive wish, the hazard nav area (H8: footprint only), the H8 hop ground window
//                                 (flat, 30 degree chute, ramp up, void), hazard damage is not heard (F8) and the lava tick
//                                 clock (F11).
//   velox.bots.FtBotLavaEscape [quit] [difficulty=Name]
//       console on L_HazardTest (-game -nullrhi -UseFixedTimeStep -FPS=60): a bot is put into the lava channel at three
//       spots (centre, north end, south-east corner); each time its feet must be out of the lava (on the ground outside
//       the volume) within 1 s. Health is restored between the spots.
//   velox.bots.FtBotKnockOff [quit] [trials=N] [pads=N] [difficulty=Name]
//       console on L_HazardTest: N (10) seeded trials. The bot stands 45..70 cm inside a platform edge (side, place along
//       the edge, inset, lift and a +-20 degree yaw drawn from FRandomStream(Crc32(1000 + trial))) and is knocked off it with
//       300 cm/s horizontal plus a 300..675 cm/s lift (AddKnockback, override). A trial lands when the bot stands on the
//       platform again, falls when its feet are 150 cm below the platform top (then it is put back before the void).
//       Pass: >= 8 of 10 land (ADR 0008 task H5). Then pads (10) seeded pad crossings (start on the main floor east of the
//       channel, Crc32(2000 + trial)) to the platform centre: one lands when the bot stands on the platform after an Ok
//       traversal of Pad_Gap's JUMP_PAD link with pad-discipline ticks in the flight (H7: hard, >= 8 of 10). Then padknocks
//       (10) seeded pad crossings (Crc32(4000 + trial)) in which the bot gets a 300 cm/s sideways impulse 0.1..0.4 s into
//       the pad flight (AddKnockback, additive; side seeded): one lands when the bot stands on the platform (task M7 H8,
//       ruling 3: the pad arrive wish replaced the zero input; >= 8 of 10).
//   velox.bots.FtBotLavaAvoid [quit] [trials=N] [difficulty=Name]
//       console on L_HazardTest (H7): N (16) seeded trials (Crc32(3000 + trial)), the bot resting 60..250 cm off a rim of the
//       lava channel. The first N/2 are routes to a point 60..250 cm beyond the other rim (sides alternate, 10 s each); the
//       rest are 6 s fights from the west side against the invulnerable dummy strafing east of the channel (x -800).
//       Pass: 0 lava entries (feet in the volume, i.e. a contact hit) in all trials, no death, routes arrive (at most 2
//       time out), the dummy in sight >= 50 % of the fight ticks.
//   All log `VeloxBotRecoveryFt test=... check=... ok=` and `VeloxBotRecoveryFt test=... result=PASS|FAIL`; quit exits
//   0/1. `velox.bots.EdgeAware 0` (A/B) runs the same trials without the ADR 0008 3.6 behaviours; the H7 lava nav area is
//   applied at load, so its A/B is `-dpcvars=velox.bots.EdgeAware=0` on the command line.

#include "CoreMinimal.h"

#include "Misc/AutomationTest.h"

#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotCombat.h"
#include "Bots/VeloxBotManeuver.h"
#include "Bots/VeloxBotSettings.h"
#include "Bots/VeloxNavAreas.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Combat/VeloxHazardVolume.h"
#include "Core/VeloxGameplayTags.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsHazardRulesTest, "Velox.Bots.HazardRules", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxBotsHazardRulesTest::RunTest(const FString& Parameters)
{
	// Foreign flight: own hop (jump held at takeoff, no grace, not yet a flight) is not one; everything else is.
	TestFalse(TEXT("own hop"), VeloxBotRules::IsForeignFlight(true, 0.f, false));
	TestTrue(TEXT("walked / knocked off without jump"), VeloxBotRules::IsForeignFlight(false, 0.f, false));
	TestTrue(TEXT("knockback grace"), VeloxBotRules::IsForeignFlight(true, 0.1f, false));
	TestTrue(TEXT("longer than the hop"), VeloxBotRules::IsForeignFlight(true, 0.f, true));

	// Ballistics at g = 2000: back to the own level after 2 Vz / g; a level 100 below from rest after sqrt(2 h / g).
	TestNearlyEqual(TEXT("rise and return"), VeloxBotRules::FlightSecondsToLevel(500.f, 0.f, 2000.f), 0.5f, 1e-4f);
	TestNearlyEqual(TEXT("fall 100 from rest"), VeloxBotRules::FlightSecondsToLevel(0.f, 100.f, 2000.f), FMath::Sqrt(0.1f), 1e-4f);
	TestNearlyEqual(TEXT("falling at own level"), VeloxBotRules::FlightSecondsToLevel(-300.f, 0.f, 2000.f), 0.f, 1e-4f);
	TestNearlyEqual(TEXT("no gravity"), VeloxBotRules::FlightSecondsToLevel(500.f, 0.f, 0.f), 0.f, 1e-6f);

	// Air recovery wish: on course -> none; else the correction landing -> target (not feet -> target).
	TestTrue(TEXT("on course: no wish"), VeloxBotRules::AirRecoveryWish(FVector2D(100.0, 0.0), FVector2D(110.0, 0.0), 20.f).IsZero());
	const FVector2D Back = VeloxBotRules::AirRecoveryWish(FVector2D(120.0, 0.0), FVector2D(-10.0, 0.0), 20.f);
	TestTrue(TEXT("overshoot: wish back"), Back.Equals(FVector2D(-1.0, 0.0), 1e-6));

	// H13 ground edge guard: the edge runs along X, inward is -Y. A wish straight out turns straight in; a diagonal keeps its
	// part along the edge; a zero wish is the inward direction; no inward direction, no wish.
	TestTrue(TEXT("guard: straight out -> in"), VeloxBotRules::EdgeGuardWish(FVector2D(0.0, 1.0), FVector2D(0.0, -1.0)).Equals(FVector2D(0.0, -1.0), 1e-6));
	const FVector2D Diag = VeloxBotRules::EdgeGuardWish(FVector2D(1.0, 1.0), FVector2D(0.0, -2.0));
	TestTrue(TEXT("guard: diagonal keeps the edge part"), Diag.Equals(FVector2D(0.5774, -0.8165), 1e-3));
	TestTrue(TEXT("guard: zero wish -> in"), VeloxBotRules::EdgeGuardWish(FVector2D::ZeroVector, FVector2D(0.0, -1.0)).Equals(FVector2D(0.0, -1.0), 1e-6));
	TestTrue(TEXT("standoff kept Fight -> Fight"), VeloxBotRules::KeepsFightStandoff(EVeloxBotGoal::Fight, EVeloxBotGoal::Fight));
	TestFalse(TEXT("standoff reset Fight -> Item"), VeloxBotRules::KeepsFightStandoff(EVeloxBotGoal::Fight, EVeloxBotGoal::Item));
	TestFalse(TEXT("standoff reset Item -> Fight"), VeloxBotRules::KeepsFightStandoff(EVeloxBotGoal::Item, EVeloxBotGoal::Fight));
	TestTrue(TEXT("guard: no inward"), VeloxBotRules::EdgeGuardWish(FVector2D(1.0, 0.0), FVector2D::ZeroVector).IsZero());

	// Box exit: a 300 x 1200 channel (half 150 x 600), feet 40 cm west of the centre leave through the west side.
	const FVector2D West = VeloxBotRules::HazardBoxExit(FVector2D(-40.0, 0.0), FVector2D::ZeroVector, FVector2D(1.0, 0.0), 150.f, 600.f, 100.f);
	TestTrue(TEXT("exit west"), West.Equals(FVector2D(-250.0, 0.0), 1e-3));
	// The same box yawed 90 degrees (its X along world Y): feet at world y +500 are near its local +X side.
	const FVector2D North = VeloxBotRules::HazardBoxExit(FVector2D(0.0, 500.0), FVector2D::ZeroVector, FVector2D(0.0, 1.0), 600.f, 150.f, 100.f);
	TestTrue(TEXT("exit along the yawed axis"), North.Equals(FVector2D(0.0, 700.0), 1e-3));

	// Pad discipline: the pad maneuver's flight is the IsPadFlight phase; TickAirborne itself wishes End at scale 1 without
	// jump (the pre-M7 behaviour; the follower replaces it with the ballistic arrive wish after the launch, every map).
	{
		FVeloxManeuverParams Pad;
		Pad.Type = EVeloxManeuverType::JumpPad;
		FVeloxBotManeuver M;
		M.Begin(Pad, FVector::ZeroVector, FVector(1500.0, 0.0, 0.0), 0.0);
		FVeloxManeuverInput In;
		In.bOnGround = true;
		In.DistToStart = 200.f;
		In.DistToEnd = 1500.f;
		In.Time = 0.0;
		M.Tick(In);
		TestFalse(TEXT("pad approach is no pad flight"), M.IsPadFlight());
		In.bOnGround = false;
		In.bImpulseReceived = true;
		In.DistToStart = 10.f;
		In.Time = 1.0 / 60.0;
		FVeloxManeuverOutput O = M.Tick(In);
		TestTrue(TEXT("launched: pad flight"), M.IsPadFlight());
		TestTrue(TEXT("launched: own launch seen (H7 pad arrive)"), M.bLaunchImpulseSeen);
		In.bImpulseReceived = false;
		In.DistToEnd = 900.f;
		In.Time = 2.0 / 60.0;
		O = M.Tick(In);
		TestTrue(TEXT("pad flight: wish End"), O.WishToward == EVeloxManeuverWish::End);
		TestEqual(TEXT("pad flight: scale 1 (TickAirborne)"), O.WishScale, 1.f);
		TestFalse(TEXT("pad flight: no jump"), O.bJumpHeld);
		In.bOnGround = true;
		In.DistToEnd = 50.f;
		In.Time = 3.0 / 60.0;
		O = M.Tick(In);
		TestTrue(TEXT("pad landing ok"), O.bDone && M.Result == EVeloxManeuverResult::Ok);
		TestFalse(TEXT("after the landing no pad flight"), M.IsPadFlight());
	}
	// H7 pad arrive (the follower's pad flight wish): Sacrarium PadSouth's launch (0, 679, 1766) from z 70 lands at z 640 after
	// t = (1766 + sqrt(1766^2 - 2 x 2000 x 570)) / 2000; aimed at that landing no steering, aimed 300 cm short a brake (-Y).
	{
		const FVector Feet(0.0, 0.0, 70.0);
		const FVector Launch(0.0, 679.0, 1766.0);
		const double T = (1766.0 + FMath::Sqrt(1766.0 * 1766.0 - 2.0 * 2000.0 * 570.0)) / 2000.0;
		const FVector Landing(0.0, 679.0 * T, 640.0);
		TestTrue(TEXT("pad arrive on course: no wish"), VeloxBotRocketJump::ArriveWish(Feet, Launch, Landing, -2000.f, 30.f).WishDir.IsZero());
		const FVector2D Brake = VeloxBotRocketJump::ArriveWish(Feet, Launch, Landing - FVector(0.0, 300.0, 0.0), -2000.f, 30.f).WishDir;
		TestTrue(TEXT("pad arrive long: brake"), Brake.Equals(FVector2D(0.0, -1.0), 1e-3));
	}
	// H7 lava avoidance: the hazard nav area is costly but not excluded (a bot in the lava still paths out).
	{
		UVeloxNavArea_Hazard* Area = GetMutableDefault<UVeloxNavArea_Hazard>();
		TestTrue(TEXT("hazard area costs more than a floor poly"), Area->DefaultCost > 1.f && Area->GetFixedAreaEnteringCost() > 0.f);
		// H8 (review F1, ADR 0008 G5(b)): the area is the volume's own footprint, bed to HazardNavTopCm above the surface,
		// below the generator's 20 cm minimum rim drop (so no rim poly is marked).
		const FBox Lava(FVector(-1500.0, -600.0, -60.0), FVector(-1200.0, 600.0, -40.0));
		const float Top = UVeloxBotSettings::Get().HazardNavTopCm;
		const FBox Area2 = UVeloxBotSubsystem::LavaNavAreaBox(Lava, Top);
		TestTrue(TEXT("hazard nav top below the 20 cm rim drop"), Top >= 0.f && Top < 20.f);
		TestTrue(TEXT("hazard nav area: no horizontal padding"), FVector2D(Area2.Min).Equals(FVector2D(Lava.Min)) && FVector2D(Area2.Max).Equals(FVector2D(Lava.Max)));
		TestTrue(TEXT("hazard nav area: bed to surface + top"), FMath::IsNearlyEqual(Area2.Min.Z, -60.0) && FMath::IsNearlyEqual(Area2.Max.Z, -40.0 + Top));
	}
	// H8 (review F5, ADR 0008 G2): the ground window of a hop landing. A 580 cm hop with a 45 degree walkable limit reaches
	// 580 cm down (Fornax's 30 degree chute is 335 lower there) and the apex up (a 20 degree ramp: 211 higher, apex 230).
	{
		double TopZ = 0.0;
		double BottomZ = 0.0;
		const float Walkable45 = FMath::Cos(FMath::DegreesToRadians(45.f));
		VeloxBotCombat::HopGroundWindow(100.0, 230.f, 580.f, Walkable45, TopZ, BottomZ);
		TestNearlyEqual(TEXT("hop window top = apex"), TopZ, 330.0, 1e-3);
		TestNearlyEqual(TEXT("hop window bottom = distance x tan(45)"), BottomZ, 100.0 - 580.0, 0.5);
		const double Chute = 100.0 - 580.0 * FMath::Tan(FMath::DegreesToRadians(30.0));
		TestTrue(TEXT("30 degree chute landing inside the window"), Chute > BottomZ && Chute < TopZ);
		const double Ramp = 100.0 + 580.0 * FMath::Tan(FMath::DegreesToRadians(20.0));
		TestTrue(TEXT("20 degree ramp up landing inside the window"), Ramp > BottomZ && Ramp < TopZ);
		TestTrue(TEXT("flat landing inside the window"), 100.0 > BottomZ && 100.0 < TopZ);
		TestTrue(TEXT("void (1000 cm down) outside the window"), 100.0 - 1000.0 < BottomZ);
		VeloxBotCombat::HopGroundWindow(0.0, 230.f, 0.f, Walkable45, TopZ, BottomZ);
		TestTrue(TEXT("standing hop: flat ground still inside"), BottomZ < 0.0 && TopZ > 0.0);
	}
	// H8 (review F8, ADR 0008 G7): a credited lava tick is not heard; weapon damage is.
	{
		FGameplayTagContainer Lava;
		Lava.AddTag(VeloxGameplayTags::Damage_Hazard_Lava);
		FGameplayTagContainer Void;
		Void.AddTag(VeloxGameplayTags::Damage_Hazard_Void);
		FGameplayTagContainer Rocket;
		Rocket.AddTag(VeloxGameplayTags::Damage_Environment);
		TestFalse(TEXT("credited lava tick not heard"), VeloxBotCombat::IsHeardDamage(Lava));
		TestFalse(TEXT("void hit not heard"), VeloxBotCombat::IsHeardDamage(Void));
		TestTrue(TEXT("other damage heard"), VeloxBotCombat::IsHeardDamage(Rocket));
	}
	// H8 (review F11, ADR 0008 G7): the lava clock advances by the tick, not to the frame (8.5 ms frames: due at 0.25, the
	// hit lands on the frame at 0.2550, the next is due at 0.50 again); a hitch of more than one interval restarts at Now.
	{
		TestNearlyEqual(TEXT("lava clock advances by the tick"), AVeloxHazardVolume::AdvanceLavaTick(0.0, 0.2550, 0.25), 0.25, 1e-9);
		TestNearlyEqual(TEXT("lava clock after a hitch restarts at now"), AVeloxHazardVolume::AdvanceLavaTick(0.0, 0.80, 0.25), 0.80, 1e-9);
		double Clock = 0.0;
		int32 Hits = 0;
		for (int32 Frame = 1; Frame <= 1176; ++Frame) // 10 s at 8.5 ms
		{
			const double Now = Frame * 0.0085;
			if (Now - Clock >= 0.25 - 1.0e-3)
			{
				Clock = AVeloxHazardVolume::AdvanceLavaTick(Clock, Now, 0.25);
				++Hits;
			}
		}
		TestEqual(TEXT("lava ticks in 10 s at 8.5 ms frames"), Hits, 39);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#if !UE_BUILD_SHIPPING

#include "Bots/VeloxBotCombat.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotPathFollowingComponent.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxDummySpawner.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHazardSubsystem.h"
#include "Combat/VeloxHazardVolume.h"
#include "Combat/VeloxHealthSet.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Math/RandomStream.h"
#include "Misc/Crc.h"
#include "Movement/VeloxMovementComponent.h"

namespace VeloxBotRecoveryFt
{
	// L_HazardTest coordinates (Tools/editor/create_hazard_test_map.py).
	constexpr double PlatformX0 = 1000.0;
	constexpr double PlatformX1 = 1800.0;
	constexpr double PlatformHalfY = 400.0;
	constexpr double LavaBedZ = -60.0;
	/** Lava spots: channel x -1500..-1200, y -600..600 (centre, north end, south-east corner). */
	const FVector LavaSpots[] = { FVector(-1350.0, 0.0, LavaBedZ), FVector(-1420.0, 500.0, LavaBedZ), FVector(-1240.0, -540.0, LavaBedZ) };
	const FVector LavaProbe(-1350.0, 0.0, -50.0);
	/** Where the bot waits between the lava spots (main floor, east of the channel). */
	const FVector FloorSpot(-900.0, 0.0, 0.0);

	constexpr double EscapeLimitSeconds = 1.0;
	constexpr double EscapeTimeoutSeconds = 4.0;
	constexpr double SettleSeconds = 0.5;
	constexpr int32 DefaultTrials = 10;
	constexpr int32 MinLanded = 8;
	constexpr double KnockSpeed = 300.0;
	constexpr double FallenBelowCm = -150.0;
	constexpr double TrialTimeoutSeconds = 3.0;
	constexpr double PadTimeoutSeconds = 15.0;
	const FVector PlatformCentre(1400.0, 0.0, 0.0);
	/** H7 pad crossings: starts on the main floor east of the lava channel (x, y ranges), >= 8 of 10 take Pad_Gap's link and land. */
	constexpr int32 DefaultPadTrials = 10;
	constexpr int32 MinPadLanded = 8;
	/** On the platform, the link report is awaited this long (s). */
	constexpr double PadReportWaitSeconds = 0.5;
	constexpr double PadStartX0 = -1050.0;
	constexpr double PadStartX1 = -250.0;
	constexpr double PadStartHalfY = 800.0;
	/** H8 ruling 3: pad crossings with a sideways knock in the flight; >= 8 of 10 land on the platform. */
	constexpr int32 DefaultPadKnockTrials = 10;
	constexpr int32 MinPadKnockLanded = 8;
	constexpr double PadKnockSpeed = 300.0;
	constexpr float PadKnockDelayMin = 0.1f;
	constexpr float PadKnockDelayMax = 0.4f;
	/** H7 FT_BotLavaAvoid: lava channel x -1500..-1200, y -600..600; starts / goals AvoidOffMinCm..AvoidOffMaxCm off its rims, |y| <= 500. */
	constexpr double LavaX0 = -1500.0;
	constexpr double LavaX1 = -1200.0;
	constexpr int32 DefaultAvoidTrials = 16;
	constexpr float AvoidOffMinCm = 60.f;
	constexpr float AvoidOffMaxCm = 250.f;
	/** Routes that may time out (a route is judged only for its lava entries; the arrivals show the bot really went). */
	constexpr int32 MaxAvoidMissedRoutes = 2;
	constexpr double MinAvoidVisibleFraction = 0.5;
	constexpr double AvoidTimeoutSeconds = 10.0;
	/** The second half of the trials are fights: the invulnerable dummy strafes east of the channel, the bot starts west of it. */
	const FVector AvoidDummySpot(-800.0, 0.0, 110.0);
	constexpr float AvoidDummyStrafeHalfWidthCm = 450.f;
	constexpr double AvoidFightSeconds = 6.0;
	constexpr double AvoidArriveCm = 100.0;

	enum class EPhase : uint8
	{
		Settle,
		Run,
	};

	struct FRecoveryFt
	{
		TWeakObjectPtr<UWorld> World;
		const TCHAR* Name = TEXT("");
		bool bKnockOff = false;
		bool bQuit = false;
		bool bPassed = true;
		bool bFinished = false;
		FTSTicker::FDelegateHandle TickHandle;
		FDelegateHandle KillHandle;
		TWeakObjectPtr<AVeloxBotController> Bot;
		TWeakObjectPtr<AVeloxPlayerState> BotPS;
		int32 Deaths = 0;

		int32 Trial = -1;
		int32 NumTrials = 0;
		EPhase Phase = EPhase::Settle;
		double PhaseStart = 0.0;
		bool bSeenAirborne = false;
		int32 Landed = 0;
		int32 EscapedInTime = 0;
		double WorstEscape = 0.0;
		/** Knock-off trial: spot, outward direction and lift. */
		FVector Spot = FVector::ZeroVector;
		FVector2D Out = FVector2D::ZeroVector;
		double Lift = 0.0;
		int32 FlightsBefore = 0;
		/** Knock-off FT, after the trials: NumPadTrials seeded pad crossings from the main floor onto the platform (H7). */
		bool bPadTrial = false;
		int32 PadTrial = 0;
		int32 NumPadTrials = 0;
		int32 PadLanded = 0;
		int32 PadTicksBefore = 0;
		int32 LinksOkBefore = 0;
		int32 LinksFailedBefore = 0;
		double OnPlatformSince = -1.0;
		/** H8 ruling 3: the pad-knock phase (after the plain pad crossings): knock delay / side of this trial, applied yet. */
		bool bPadKnockPhase = false;
		int32 NumPadKnockTrials = 0;
		int32 PadKnockLanded = 0;
		int32 PadKnocksApplied = 0;
		float PadKnockDelay = 0.f;
		float PadKnockSign = 1.f;
		bool bPadKnocked = false;
		double PadFlightSince = -1.0;
		int32 PadArriveTicksBefore = 0;
		int32 RecoveryTicksBefore = 0;
		/** FT_BotLavaAvoid (H7): routed across the lava channel; lava entries (any overlap is a contact hit) per trial and total. */
		bool bLavaAvoid = false;
		/** FT_BotLavaAvoid: trials from this index on are fights against the dummy (the first ones are routes). */
		int32 FirstFightTrial = 0;
		bool bFight = false;
		int32 Fights = 0;
		int32 VisibleTicks = 0;
		int32 FightTicks = 0;
		/** Bot difficulty preset (difficulty=Name; default Hard). */
		FString Difficulty = TEXT("Hard");
		FVector Goal = FVector::ZeroVector;
		bool bWasInLava = false;
		int32 TrialLavaEntries = 0;
		int32 LavaEntries = 0;
		int32 LavaTicks = 0;
		int32 Arrived = 0;

		double Now() const
		{
			const UWorld* W = World.Get();
			return W ? W->GetTimeSeconds() : 0.0;
		}

		void Check(bool bOk, const TCHAR* What, const FString& Detail)
		{
			bPassed &= bOk;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s check=%s ok=%d %s"), Name, What, bOk ? 1 : 0, *Detail);
		}

		void Finish()
		{
			if (bFinished)
			{
				return;
			}
			bFinished = true;
			if (UWorld* W = World.Get())
			{
				if (AVeloxGameState* GS = W->GetGameState<AVeloxGameState>())
				{
					GS->OnKill.Remove(KillHandle);
				}
			}
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s result=%s"), Name, bPassed ? TEXT("PASS") : TEXT("FAIL"));
			if (bQuit)
			{
				FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
			}
		}
	};

	TSharedPtr<FRecoveryFt> GRecoveryFt;

	/** Test harness: the bot at rest with its feet at Feet. */
	void Place(AVeloxCharacter& Pawn, const FVector& Feet, float Yaw)
	{
		Pawn.SetActorLocationAndRotation(Feet + FVector(0.0, 0.0, Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0), FRotator(0.f, Yaw, 0.f), false, nullptr,
			ETeleportType::TeleportPhysics);
		if (UVeloxMovementComponent* Movement = Pawn.GetVeloxMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
	}

	/** Test harness: health back to full (GE_Heal; the pickup rule caps it). */
	void Heal(AVeloxCharacter& Pawn)
	{
		const AVeloxPlayerState* PS = Pawn.GetPlayerState<AVeloxPlayerState>();
		UVeloxAbilitySystemComponent* ASC = Pawn.GetVeloxAbilitySystemComponent();
		const UVeloxHealthSet* Health = PS ? PS->GetHealthSet() : nullptr;
		if (!ASC || !Health)
		{
			return;
		}
		const int32 Missing = FMath::RoundToInt32(100.f - Health->GetHealth());
		if (Missing > 0)
		{
			const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeHealSpec(ASC, Missing);
			if (Spec.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			}
		}
	}

	float HealthOf(const AVeloxCharacter& Pawn)
	{
		const AVeloxPlayerState* PS = Pawn.GetPlayerState<AVeloxPlayerState>();
		const UVeloxHealthSet* Health = PS ? PS->GetHealthSet() : nullptr;
		return Health ? Health->GetHealth() : -1.f;
	}

	void BeginKnockTrial(FRecoveryFt& Ft, AVeloxCharacter& Pawn)
	{
		// Hashed: consecutive raw seeds give correlated first draws.
		const uint32 Seed = 1000u + static_cast<uint32>(Ft.Trial);
		FRandomStream Rng(static_cast<int32>(FCrc::MemCrc32(&Seed, sizeof(Seed))));
		const int32 Side = Rng.RandRange(0, 3);
		const double Along = Rng.FRandRange(-250.f, 250.f);
		const double Inset = Rng.FRandRange(45.f, 70.f);
		Ft.Lift = Rng.FRandRange(300.f, 675.f);
		const double YawJitter = FMath::DegreesToRadians(Rng.FRandRange(-20.f, 20.f));
		const double CentreX = 0.5 * (PlatformX0 + PlatformX1);
		FVector2D Normal;
		switch (Side)
		{
		case 0: Ft.Spot = FVector(PlatformX1 - Inset, Along, 0.0); Normal = FVector2D(1.0, 0.0); break;
		case 1: Ft.Spot = FVector(PlatformX0 + Inset, Along, 0.0); Normal = FVector2D(-1.0, 0.0); break;
		case 2: Ft.Spot = FVector(CentreX + Along, PlatformHalfY - Inset, 0.0); Normal = FVector2D(0.0, 1.0); break;
		default: Ft.Spot = FVector(CentreX + Along, -PlatformHalfY + Inset, 0.0); Normal = FVector2D(0.0, -1.0); break;
		}
		Ft.Out = Normal.GetRotated(FMath::RadiansToDegrees(YawJitter));
		Place(Pawn, Ft.Spot, Rng.FRandRange(-180.f, 180.f));
		Heal(Pawn);
		if (AVeloxBotController* Bot = Ft.Bot.Get())
		{
			Bot->SetForcedPointGoal(Ft.Spot);
		}
		Ft.Phase = EPhase::Settle;
		Ft.PhaseStart = Ft.Now();
		Ft.bSeenAirborne = false;
	}

	/** H7: a seeded start on the main floor east of the channel, the goal the platform centre (over Pad_Gap's link). */
	void BeginPadTrial(FRecoveryFt& Ft, AVeloxCharacter& Pawn)
	{
		const uint32 Seed = (Ft.bPadKnockPhase ? 4000u : 2000u) + static_cast<uint32>(Ft.PadTrial);
		FRandomStream Rng(static_cast<int32>(FCrc::MemCrc32(&Seed, sizeof(Seed))));
		const FVector Start(Rng.FRandRange(PadStartX0, PadStartX1), Rng.FRandRange(-PadStartHalfY, PadStartHalfY), 0.0);
		Place(Pawn, Start, Rng.FRandRange(-180.f, 180.f));
		// H8: drawn after the pad trials' draws, so the plain crossings keep their H7 starts.
		Ft.PadKnockDelay = Rng.FRandRange(PadKnockDelayMin, PadKnockDelayMax);
		Ft.PadKnockSign = Rng.FRand() < 0.5f ? -1.f : 1.f;
		Ft.bPadKnocked = false;
		Ft.PadFlightSince = -1.0;
		Ft.PadArriveTicksBefore = Ft.Bot.IsValid() ? Ft.Bot->GetBotState().PadArriveTicks : 0;
		Ft.RecoveryTicksBefore = Ft.Bot.IsValid() ? Ft.Bot->GetBotState().AirRecoveryTicks : 0;
		Heal(Pawn);
		Ft.Spot = Start;
		Ft.PadTicksBefore = Ft.Bot.IsValid() ? Ft.Bot->GetBotState().PadDisciplineTicks : 0;
		Ft.LinksOkBefore = Ft.Bot.IsValid() ? Ft.Bot->GetBotState().LinksOk : 0;
		Ft.LinksFailedBefore = Ft.Bot.IsValid() ? Ft.Bot->GetBotState().LinksFailed : 0;
		Ft.OnPlatformSince = -1.0;
		if (AVeloxBotController* Bot = Ft.Bot.Get())
		{
			Bot->SetForcedPointGoal(PlatformCentre);
		}
		Ft.Phase = EPhase::Run;
		Ft.PhaseStart = Ft.Now();
	}

	/** H7 FT_BotLavaAvoid: start next to one rim of the channel, the goal beyond the other (sides alternate). */
	void BeginAvoidTrial(FRecoveryFt& Ft, AVeloxCharacter& Pawn)
	{
		const uint32 Seed = 3000u + static_cast<uint32>(Ft.Trial);
		FRandomStream Rng(static_cast<int32>(FCrc::MemCrc32(&Seed, sizeof(Seed))));
		Ft.bFight = Ft.Trial >= Ft.FirstFightTrial;
		if (Ft.bFight && Ft.Trial == Ft.FirstFightTrial)
		{
			// Test harness: the map's dummy spawner moved east of the channel; the dummy strafes along Y and cannot be killed.
			if (AVeloxDummySpawner* Spawner = TActorIterator<AVeloxDummySpawner>(Ft.World.Get()) ? *TActorIterator<AVeloxDummySpawner>(Ft.World.Get()) : nullptr)
			{
				Spawner->SetActorLocation(AvoidDummySpot);
				Spawner->StrafeHalfWidth = AvoidDummyStrafeHalfWidthCm;
				Spawner->bInvulnerable = true;
				const AVeloxCharacter* Dummy = Spawner->SpawnDummy();
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s fights from trial=%d dummy=%s"), Ft.Name, Ft.Trial, *GetNameSafe(Dummy));
			}
		}
		// Routes alternate sides; fights start west of the channel (the dummy is east of it).
		const bool bWestToEast = Ft.bFight || (Ft.Trial % 2) == 0;
		const double StartOff = Rng.FRandRange(AvoidOffMinCm, AvoidOffMaxCm);
		const double GoalOff = Rng.FRandRange(AvoidOffMinCm, AvoidOffMaxCm);
		const FVector Start(bWestToEast ? LavaX0 - StartOff : LavaX1 + StartOff, Rng.FRandRange(-500.f, 500.f), 0.0);
		Ft.Goal = FVector(bWestToEast ? LavaX1 + GoalOff : LavaX0 - GoalOff, Rng.FRandRange(-500.f, 500.f), 0.0);
		Place(Pawn, Start, Rng.FRandRange(-180.f, 180.f));
		Heal(Pawn);
		Ft.Spot = Start;
		if (AVeloxBotController* Bot = Ft.Bot.Get())
		{
			Bot->SetForcedPointGoal(Start);
		}
		Ft.bWasInLava = false;
		Ft.TrialLavaEntries = 0;
		Ft.Phase = EPhase::Settle;
		Ft.PhaseStart = Ft.Now();
	}

	/** FT_BotLavaAvoid verdict: no lava entry in any trial, the routes arrive, the fights were fights (dummy in sight). */
	void FinishAvoid(FRecoveryFt& Ft, const AVeloxBotController& Bot)
	{
		const int32 Routes = FMath::Min(Ft.FirstFightTrial, Ft.NumTrials);
		const int32 NeedArrived = FMath::Max(0, Routes - MaxAvoidMissedRoutes);
		const double Visible = Ft.FightTicks > 0 ? static_cast<double>(Ft.VisibleTicks) / Ft.FightTicks : 1.0;
		Ft.Check(Ft.LavaEntries == 0, TEXT("lava"), FString::Printf(TEXT("lava_entries=%d lava_ticks=%d over %d trials (need 0) escapes=%d"), Ft.LavaEntries, Ft.LavaTicks,
			Ft.NumTrials, Bot.GetBotState().HazardEscapes));
		Ft.Check(Ft.Arrived >= NeedArrived, TEXT("arrived"), FString::Printf(TEXT("arrived=%d of %d routes (need %d)"), Ft.Arrived, Routes, NeedArrived));
		Ft.Check(Ft.Fights == 0 || Visible >= MinAvoidVisibleFraction, TEXT("fought"), FString::Printf(TEXT("fights=%d dummy_visible=%.2f (need %.2f)"), Ft.Fights, Visible,
			MinAvoidVisibleFraction));
	}

	void BeginLavaTrial(FRecoveryFt& Ft, AVeloxCharacter& Pawn)
	{
		Place(Pawn, FloorSpot, 0.f);
		Heal(Pawn);
		Ft.Phase = EPhase::Settle;
		Ft.PhaseStart = Ft.Now();
	}

	bool Tick(const TSharedRef<FRecoveryFt>& Ft)
	{
		AVeloxBotController* Bot = Ft->Bot.Get();
		AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		UVeloxMovementComponent* Movement = Pawn ? Pawn->GetVeloxMovementComponent() : nullptr;
		const UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(Ft->World.Get());
		if (!Bot || !Pawn || !Movement || !Hazards || Pawn->IsCorpse() || Ft->Deaths > 0)
		{
			Ft->Check(false, TEXT("bot"), FString::Printf(TEXT("bot gone or dead deaths=%d trial=%d"), Ft->Deaths, Ft->Trial));
			return false;
		}
		const double Elapsed = Ft->Now() - Ft->PhaseStart;
		const FVector Feet = Movement->GetActorFeetLocation();
		if (Ft->bLavaAvoid)
		{
			if (Feet.X > LavaX0 - 250.0 && Feet.X < LavaX1 + 250.0 && FMath::Abs(Feet.Y) < 850.0)
			{
				const FVeloxBotState& S = Bot->GetBotState();
				UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotRecoveryFt trace trial=%d at=%s vel=%s ground=%d foreign=%d escape=%d wish=%s scale=%.2f jump=%d"), Ft->Trial, *Feet.ToCompactString(),
					*Movement->Velocity.ToCompactString(), Movement->IsOnGround() ? 1 : 0, S.bForeignFlight ? 1 : 0, S.bHazardEscape ? 1 : 0, *S.Pending.WishDirWorld.ToString(), S.Pending.WishScale,
					S.Pending.bJumpHeld ? 1 : 0);
			}
		}
		if (Ft->Phase == EPhase::Settle)
		{
			if (Elapsed < SettleSeconds)
			{
				return true;
			}
			Ft->Phase = EPhase::Run;
			Ft->PhaseStart = Ft->Now();
			Ft->FlightsBefore = Bot->GetBotState().AirRecoveryFlights;
			if (Ft->bLavaAvoid && Ft->bFight)
			{
				Bot->ClearForcedGoal();
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d fight from=%s"), Ft->Name, Ft->Trial, *Feet.ToCompactString());
			}
			else if (Ft->bLavaAvoid)
			{
				Bot->SetForcedPointGoal(Ft->Goal);
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d route from=%s to=%s"), Ft->Name, Ft->Trial, *Feet.ToCompactString(), *Ft->Goal.ToCompactString());
			}
			else if (Ft->bKnockOff)
			{
				const FVector Impulse(Ft->Out.X * KnockSpeed, Ft->Out.Y * KnockSpeed, Ft->Lift);
				Movement->AddKnockback(Impulse, true, EVeloxKnockbackSource::Server);
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d knock at=%s impulse=%s"), Ft->Name, Ft->Trial, *Feet.ToCompactString(), *Impulse.ToCompactString());
			}
			else
			{
				const FVector& Spot = LavaSpots[Ft->Trial];
				Place(*Pawn, Spot, 90.f * Ft->Trial);
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d lava at=%s health=%.0f"), Ft->Name, Ft->Trial, *Spot.ToCompactString(), HealthOf(*Pawn));
			}
			return true;
		}

		if (Ft->bPadTrial)
		{
			if (Ft->bPadKnockPhase && !Ft->bPadKnocked && !Movement->IsOnGround())
			{
				// H8 ruling 3: a sideways hit in mid-arc (the pad's own launch seen, so the maneuver is in its flight).
				const UVeloxBotPathFollowingComponent* Follow = Bot->GetBotPathFollowing();
				if (Follow && Follow->IsOnLink() && Follow->GetManeuver().IsPadFlight() && Follow->GetManeuver().bLaunchImpulseSeen)
				{
					if (Ft->PadFlightSince < 0.0)
					{
						Ft->PadFlightSince = Ft->Now();
					}
					else if (Ft->Now() - Ft->PadFlightSince >= Ft->PadKnockDelay)
					{
						const FVector2D Along = FVector2D(Movement->Velocity).GetSafeNormal();
						const FVector2D Side = FVector2D(-Along.Y, Along.X) * Ft->PadKnockSign;
						const FVector Impulse(Side.X * PadKnockSpeed, Side.Y * PadKnockSpeed, 0.0);
						Movement->AddKnockback(Impulse, false, EVeloxKnockbackSource::Server);
						Ft->bPadKnocked = true;
						++Ft->PadKnocksApplied;
						UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s pad_knock_trial=%d knock at=%s vel=%s impulse=%s delay=%.2f"), Ft->Name, Ft->PadTrial,
							*Feet.ToCompactString(), *Movement->Velocity.ToCompactString(), *Impulse.ToCompactString(), Ft->PadKnockDelay);
					}
				}
			}
			const bool bOnPlatform = Movement->IsOnGround() && Feet.Z > -50.0 && Feet.X >= PlatformX0 && Feet.X <= PlatformX1 && FMath::Abs(Feet.Y) <= PlatformHalfY;
			const bool bFell = Feet.Z < FallenBelowCm;
			// On the platform: wait for the link's report (the follower ends the traversal a tick or so after the landing).
			const bool bLinkReported = Bot->GetBotState().LinksOk + Bot->GetBotState().LinksFailed > Ft->LinksOkBefore + Ft->LinksFailedBefore;
			if (bOnPlatform && Ft->OnPlatformSince < 0.0)
			{
				Ft->OnPlatformSince = Ft->Now();
			}
			if (bOnPlatform && !bLinkReported && Ft->Now() - Ft->OnPlatformSince <= PadReportWaitSeconds)
			{
				return true;
			}
			if (!bOnPlatform && !bFell && Elapsed <= PadTimeoutSeconds)
			{
				Ft->OnPlatformSince = -1.0;
				return true;
			}
			const int32 PadTicks = Bot->GetBotState().PadDisciplineTicks - Ft->PadTicksBefore;
			const int32 LinksOk = Bot->GetBotState().LinksOk - Ft->LinksOkBefore;
			const bool bEdgeAware = VeloxBotCombatGlue::IsEdgeAware(Ft->World.Get());
			const int32 ArriveTicks = Bot->GetBotState().PadArriveTicks - Ft->PadArriveTicksBefore;
			const int32 RecoveryTicks = Bot->GetBotState().AirRecoveryTicks - Ft->RecoveryTicksBefore;
			if (Ft->bPadKnockPhase)
			{
				// H8 ruling 3: knocked sideways in the flight and still on the platform (the arrive wish or, once the hit ended
				// the link, the airborne recovery steered it back).
				const bool bOk = bOnPlatform && Ft->bPadKnocked;
				Ft->PadKnockLanded += bOk ? 1 : 0;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s pad_knock_trial=%d result=%s knocked=%d on_platform=%d fell=%d dt=%.2f at=%s start=%s pad_ticks=%d arrive_ticks=%d recovery_ticks=%d links_ok=%d"),
					Ft->Name, Ft->PadTrial, bOk ? TEXT("landed") : TEXT("missed"), Ft->bPadKnocked ? 1 : 0, bOnPlatform ? 1 : 0, bFell ? 1 : 0, Elapsed, *Feet.ToCompactString(),
					*Ft->Spot.ToCompactString(), PadTicks, ArriveTicks, RecoveryTicks, LinksOk);
				if (++Ft->PadTrial < Ft->NumPadKnockTrials)
				{
					BeginPadTrial(*Ft, *Pawn);
					return true;
				}
				Ft->Check(Ft->PadKnockLanded >= MinPadKnockLanded, TEXT("pad_knock"), FString::Printf(TEXT("landed=%d of %d (need %d) knocked=%d sideways %.0f cm/s in the pad flight"),
					Ft->PadKnockLanded, Ft->NumPadKnockTrials, MinPadKnockLanded, Ft->PadKnocksApplied, PadKnockSpeed));
				return false;
			}
			// H7 (the H5 finding fixed): the crossing counts when the bot stands on the platform after an Ok link traversal
			// (Pad_Gap's JUMP_PAD link, the only link of the map) and the pad discipline held the flight (pad_ticks > 0).
			const bool bOk = bOnPlatform && LinksOk > 0 && (!bEdgeAware || PadTicks > 0);
			Ft->PadLanded += bOk ? 1 : 0;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s pad_trial=%d result=%s on_platform=%d fell=%d dt=%.2f at=%s start=%s pad_ticks=%d arrive_ticks=%d links_ok=%d edge_aware=%d"), Ft->Name,
				Ft->PadTrial, bOk ? TEXT("landed") : TEXT("missed"), bOnPlatform ? 1 : 0, bFell ? 1 : 0, Elapsed, *Feet.ToCompactString(), *Ft->Spot.ToCompactString(), PadTicks,
				ArriveTicks, LinksOk, bEdgeAware ? 1 : 0);
			if (++Ft->PadTrial < Ft->NumPadTrials)
			{
				BeginPadTrial(*Ft, *Pawn);
				return true;
			}
			Ft->Check(Ft->PadLanded >= MinPadLanded, TEXT("pad"), FString::Printf(TEXT("landed=%d of %d (need %d) over Pad_Gap's link"), Ft->PadLanded, Ft->NumPadTrials, MinPadLanded));
			if (Ft->NumPadKnockTrials <= 0)
			{
				return false;
			}
			Ft->bPadKnockPhase = true;
			Ft->PadTrial = 0;
			BeginPadTrial(*Ft, *Pawn);
			return true;
		}
		if (Ft->bLavaAvoid)
		{
			const bool bInLava = Hazards->IsInHazard(Feet + FVector(0.0, 0.0, 1.0), EVeloxHazardType::Lava);
			if (bInLava && !Ft->bWasInLava)
			{
				++Ft->TrialLavaEntries;
				++Ft->LavaEntries;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d lava_entry at=%s vel=%s"), Ft->Name, Ft->Trial, *Feet.ToCompactString(),
					*Movement->Velocity.ToCompactString());
			}
			Ft->LavaTicks += bInLava ? 1 : 0;
			Ft->bWasInLava = bInLava;
			if (Ft->bFight)
			{
				++Ft->FightTicks;
				Ft->VisibleTicks += Bot->GetCombatState().HasVisibleTarget() ? 1 : 0;
				if (Elapsed < AvoidFightSeconds)
				{
					return true;
				}
				++Ft->Fights;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d result=fought at=%s start=%s lava_entries=%d"), Ft->Name, Ft->Trial, *Feet.ToCompactString(),
					*Ft->Spot.ToCompactString(), Ft->TrialLavaEntries);
				if (++Ft->Trial < Ft->NumTrials)
				{
					BeginAvoidTrial(*Ft, *Pawn);
					return true;
				}
				FinishAvoid(*Ft, *Bot);
				return false;
			}
			const bool bArrived = !Ft->bFight && Movement->IsOnGround() && FVector::Dist2D(Feet, Ft->Goal) <= AvoidArriveCm && FMath::Abs(Feet.Z - Ft->Goal.Z) <= 50.0;
			if (!Ft->bFight && !bArrived && Elapsed <= AvoidTimeoutSeconds)
			{
				return true;
			}
			Ft->Arrived += bArrived ? 1 : 0;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d result=%s dt=%.2f at=%s start=%s goal=%s lava_entries=%d"), Ft->Name, Ft->Trial,
				bArrived ? TEXT("arrived") : TEXT("timeout"), Elapsed, *Feet.ToCompactString(), *Ft->Spot.ToCompactString(), *Ft->Goal.ToCompactString(), Ft->TrialLavaEntries);
			if (++Ft->Trial < Ft->NumTrials)
			{
				BeginAvoidTrial(*Ft, *Pawn);
				return true;
			}
			FinishAvoid(*Ft, *Bot);
			return false;
		}
		bool bTrialDone = false;
		if (Ft->bKnockOff)
		{
			const bool bOnGround = Movement->IsOnGround();
			Ft->bSeenAirborne |= !bOnGround;
			const bool bLanded = Ft->bSeenAirborne && bOnGround && Feet.Z > -50.0;
			const bool bFell = Feet.Z < FallenBelowCm;
			if (bLanded || bFell || Elapsed > TrialTimeoutSeconds)
			{
				Ft->Landed += bLanded ? 1 : 0;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d result=%s dt=%.2f at=%s spot=%s lift=%.0f steered=%d"), Ft->Name, Ft->Trial,
					bLanded ? TEXT("landed") : bFell ? TEXT("fell") : TEXT("timeout"), Elapsed, *Feet.ToCompactString(), *Ft->Spot.ToCompactString(), Ft->Lift,
					Bot->GetBotState().AirRecoveryFlights - Ft->FlightsBefore);
				bTrialDone = true;
			}
		}
		else
		{
			const bool bInLava = Hazards->IsInHazard(Feet + FVector(0.0, 0.0, 1.0), EVeloxHazardType::Lava);
			const bool bOut = !bInLava && Movement->IsOnGround() && Elapsed > 0.05;
			if (bOut || Elapsed > EscapeTimeoutSeconds)
			{
				const bool bInTime = bOut && Elapsed <= EscapeLimitSeconds;
				Ft->EscapedInTime += bInTime ? 1 : 0;
				Ft->WorstEscape = FMath::Max(Ft->WorstEscape, Elapsed);
				Ft->Check(bInTime, TEXT("escape"), FString::Printf(TEXT("trial=%d out=%d dt=%.2f at=%s health=%.0f"), Ft->Trial, bOut ? 1 : 0, Elapsed,
					*Feet.ToCompactString(), HealthOf(*Pawn)));
				bTrialDone = true;
			}
		}
		if (!bTrialDone)
		{
			return true;
		}
		++Ft->Trial;
		if (Ft->Trial >= Ft->NumTrials)
		{
			if (Ft->bKnockOff)
			{
				Ft->Check(Ft->Landed >= MinLanded, TEXT("landed"), FString::Printf(TEXT("landed=%d of %d (need %d) flights_steered=%d"), Ft->Landed, Ft->NumTrials, MinLanded,
					Bot->GetBotState().AirRecoveryFlights));
				// Pad discipline (H7: a hard check): the bot walks from the main floor to the platform centre over Pad_Gap's
				// JUMP_PAD link with no move input in the flight and still lands on the platform, NumPadTrials seeded starts.
				Ft->bPadTrial = true;
				Ft->PadTrial = 0;
				if (Ft->NumPadTrials <= 0)
				{
					if (Ft->NumPadKnockTrials <= 0)
					{
						return false;
					}
					Ft->bPadKnockPhase = true;
				}
				BeginPadTrial(*Ft, *Pawn);
				return true;
			}
			else
			{
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s summary escaped=%d of %d worst=%.2f escapes=%d"), Ft->Name, Ft->EscapedInTime, Ft->NumTrials,
					Ft->WorstEscape, Bot->GetBotState().HazardEscapes);
			}
			return false;
		}
		Ft->bKnockOff ? BeginKnockTrial(*Ft, *Pawn) : BeginLavaTrial(*Ft, *Pawn);
		return true;
	}

	enum class EFtMode : uint8
	{
		LavaEscape,
		KnockOff,
		LavaAvoid,
	};

	void Command(const TArray<FString>& Args, UWorld* World, EFtMode Mode)
	{
		const bool bKnockOff = Mode == EFtMode::KnockOff;
		const bool bLavaAvoid = Mode == EFtMode::LavaAvoid;
		const TCHAR* Names[] = { TEXT("FT_BotLavaEscape"), TEXT("FT_BotKnockOff"), TEXT("FT_BotLavaAvoid") };
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		AVeloxGameState* GameState = World ? World->GetGameState<AVeloxGameState>() : nullptr;
		const UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(World);
		if (!Bots || !GameState)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.%s: needs the authority world of L_HazardTest (-game)."), Names[static_cast<int32>(Mode)]);
			return;
		}
		TSharedRef<FRecoveryFt> Ft = MakeShared<FRecoveryFt>();
		Ft->World = World;
		Ft->bKnockOff = bKnockOff;
		Ft->bLavaAvoid = bLavaAvoid;
		Ft->Name = Names[static_cast<int32>(Mode)];
		Ft->NumTrials = bKnockOff ? DefaultTrials : bLavaAvoid ? DefaultAvoidTrials : UE_ARRAY_COUNT(LavaSpots);
		Ft->NumPadTrials = DefaultPadTrials;
		Ft->NumPadKnockTrials = DefaultPadKnockTrials;
		Ft->FirstFightTrial = DefaultAvoidTrials / 2;
		for (const FString& Arg : Args)
		{
			FString Value;
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				Ft->bQuit = true;
			}
			else if (Arg.StartsWith(TEXT("difficulty="), ESearchCase::IgnoreCase) && Arg.Split(TEXT("="), nullptr, &Value))
			{
				Ft->Difficulty = Value;
			}
			else if ((bKnockOff || bLavaAvoid) && Arg.StartsWith(TEXT("trials="), ESearchCase::IgnoreCase) && Arg.Split(TEXT("="), nullptr, &Value))
			{
				Ft->NumTrials = FMath::Max(1, FCString::Atoi(*Value));
				Ft->FirstFightTrial = Ft->NumTrials / 2;
			}
			else if (bKnockOff && Arg.StartsWith(TEXT("pads="), ESearchCase::IgnoreCase) && Arg.Split(TEXT("="), nullptr, &Value))
			{
				Ft->NumPadTrials = FMath::Max(0, FCString::Atoi(*Value));
			}
			else if (bKnockOff && Arg.StartsWith(TEXT("padknocks="), ESearchCase::IgnoreCase) && Arg.Split(TEXT("="), nullptr, &Value))
			{
				Ft->NumPadKnockTrials = FMath::Max(0, FCString::Atoi(*Value));
			}
		}
		GRecoveryFt = Ft;
		const bool bLava = Hazards && Hazards->HasHazards() && Hazards->IsInHazard(LavaProbe, EVeloxHazardType::Lava);
		Ft->Check(bLava, TEXT("map_hazards"), FString::Printf(TEXT("volumes=%d"), Hazards ? Hazards->GetVolumes().Num() : 0));
		if (!bLava)
		{
			Ft->Finish();
			return;
		}
		for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
		{
			// The -game local player's pawn is no part of the test.
			if (Cast<APlayerController>(It->GetController()))
			{
				It->Destroy();
			}
		}
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("velox.match.Phase Live"));
		}
		TWeakPtr<FRecoveryFt> Weak = Ft;
		Ft->KillHandle = GameState->OnKill.AddLambda([Weak](const FVeloxKillEvent& Event)
		{
			const TSharedPtr<FRecoveryFt> F = Weak.Pin();
			if (F && F->BotPS.IsValid() && Event.Victim.Get() == F->BotPS.Get())
			{
				++F->Deaths;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s death trial=%d tags=%s"), F->Name, F->Trial, *Event.DamageTags.ToStringSimple());
			}
		});
		Bots->CallWhenNavReady([Weak, Bots]()
		{
			const TSharedPtr<FRecoveryFt> F = Weak.Pin();
			if (!F)
			{
				return;
			}
			AVeloxBotController* Bot = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(F->Difficulty), TEXT("FtRecoveryBot"));
			AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
			AVeloxPlayerState* PS = Bot ? Bot->GetPlayerState<AVeloxPlayerState>() : nullptr;
			if (!Pawn || !PS)
			{
				F->Check(false, TEXT("setup"), FString::Printf(TEXT("bot=%s"), *GetNameSafe(Bot)));
				F->Finish();
				return;
			}
			F->Bot = Bot;
			F->BotPS = PS;
			F->Trial = 0;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s start bot=%s trials=%d edge_aware=%d"), F->Name, *Bot->GetBotName(), F->NumTrials,
				VeloxBotCombatGlue::IsEdgeAware(F->World.Get()) ? 1 : 0);
			F->bLavaAvoid ? BeginAvoidTrial(*F, *Pawn) : F->bKnockOff ? BeginKnockTrial(*F, *Pawn) : BeginLavaTrial(*F, *Pawn);
			F->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				const TSharedPtr<FRecoveryFt> Inner = Weak.Pin();
				if (!Inner || Inner->bFinished)
				{
					return false;
				}
				if (!Tick(Inner.ToSharedRef()))
				{
					Inner->Finish();
					return false;
				}
				return true;
			}));
		});
	}

	FAutoConsoleCommandWithWorldAndArgs LavaCmd(TEXT("velox.bots.FtBotLavaEscape"),
		TEXT("velox.bots.FtBotLavaEscape [quit]: FT_BotLavaEscape on L_HazardTest (-game -nullrhi -UseFixedTimeStep -FPS=60): a bot put into the lava channel at three "
			 "spots is out within 1 s each time (ADR 0008 3.6.4). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Command(Args, World, EFtMode::LavaEscape); }));

	FAutoConsoleCommandWithWorldAndArgs KnockCmd(TEXT("velox.bots.FtBotKnockOff"),
		TEXT("velox.bots.FtBotKnockOff [quit] [trials=N] [pads=N] [padknocks=N]: FT_BotKnockOff on L_HazardTest: a bot knocked off the platform edge with 300 cm/s lands back in >= 8 of 10 "
			 "seeded trials (ADR 0008 3.6.3), then crosses Pad_Gap's link onto the platform in >= 8 of 10 seeded starts (H7), then lands on it in >= 8 of 10 "
			 "crossings knocked sideways (300 cm/s) in the pad flight (H8). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Command(Args, World, EFtMode::KnockOff); }));

	FAutoConsoleCommandWithWorldAndArgs AvoidCmd(TEXT("velox.bots.FtBotLavaAvoid"),
		TEXT("velox.bots.FtBotLavaAvoid [quit] [trials=N]: FT_BotLavaAvoid on L_HazardTest: a Hard bot routed from next to one rim of the lava channel to beyond the "
			 "other in N (10) seeded trials enters the lava 0 times and arrives >= 8 times (task M7 H7; A/B -dpcvars=velox.bots.EdgeAware=0). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Command(Args, World, EFtMode::LavaAvoid); }));
}

#endif // !UE_BUILD_SHIPPING
