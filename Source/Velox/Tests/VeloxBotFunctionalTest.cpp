// Velox - bot controller and navigation tests (ADR 0005 section 14, task M4-04).
//
//   Velox.Bots.ControllerRules   automation, world-free: per-life seed (HashCombine, no collisions where XOR collides),
//                                key presence from a world wish (CPM snap), turn-rate clamp, stuck rules (speed, D18 progress), D18 hop headroom, bot names,
//                                ?Bots= capping, link type -> maneuver type mapping, corner angle.
//   Velox.Bots.FindNearestEnd    automation, world-free: UVeloxBotSubsystem::FindNearestEnd (radius, height, nearest).
//   velox.bots.FtNavigation      FT_BotNavigation (console, `UnrealEditor-Cmd Velox.uproject L_ItemTest -game -nullrhi
//                                -UseFixedTimeStep -FPS=60 -ExecCmds="velox.bots.Seed 1, velox.bots.FtNavigation quit"`):
//                                nav built < 5 s; a Medium bot with the forced goal Velox.Item.Health.Mega traverses the
//                                pad link (VeloxBotLink type=JumpPad result=ok) and takes the Mega within 25 s; then
//                                Velox.Item.Armor.Red from the Mega platform: a Drop traversal and the RA taken within
//                                35 s. Task 05: then (preset Hard, velox.combat.GiveAllWeapons for the bot) goal
//                                Velox.Item.Ammo.Rockets on the third platform: VeloxBotLink type=RocketJump result=ok and
//                                the pickup taken within 25 s (apex and self damage logged); then the abort path: the bot
//                                walks back to the link's run-up side, is sent to the rockets again and loses its rocket
//                                launcher (test harness) once the RocketJump traversal begins: VeloxBotLink type=RocketJump
//                                result=aborted reason=ineligible within 20 s. Logs `VeloxBotFt check=... ok=` and
//                                `VeloxBotFt result=PASS|FAIL`; quit exits 0/1.
//   velox.bots.FtStrafeCorridor  FT_BotStrafeCorridor (console, L_MovementTest, same flags): a Hard bot from the corridor
//                                start (x 300) to its end (x 5000): peak speed >= 1.25 x MaxGroundSpeed inside the corridor
//                                and arrival <= 7 s; an Easy bot: arrival <= 9 s and never above MaxGroundSpeed + 1.
// The tests position the pawn at the corridor start and read its velocity as a test harness; the bot itself only gets
// input (the controller never touches velocity or location).

#include "Misc/AutomationTest.h"

#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotDifficulty.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotPathFollowingComponent.h"
#include "Bots/VeloxBotSettings.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxNavLink.h"
#include "Character/VeloxCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "Movement/VeloxMovementComponent.h"
#include "Combat/VeloxHealthSet.h"
#include "GameModes/VeloxPlayerState.h"
#include "Weapons/VeloxWeaponCheats.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

// ------------------------------------------------------------------------------------------------------ automation

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotControllerRulesTest, "Velox.Bots.ControllerRules", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxBotControllerRulesTest::RunTest(const FString& Parameters)
{
	// Seed: XOR collides (bot 1 life 2 == bot 2 life 1); HashCombine must not on a 16 x 64 grid.
	{
		TSet<int32> Seeds;
		int32 XorCollisions = 0;
		TSet<int32> XorSeeds;
		for (int32 Bot = 0; Bot < 16; ++Bot)
		{
			for (int32 Life = 1; Life <= 64; ++Life)
			{
				Seeds.Add(VeloxBotRules::BotSeed(1, Bot, Life));
				bool bAlready = false;
				XorSeeds.Add(1 ^ Bot ^ Life, &bAlready);
				XorCollisions += bAlready ? 1 : 0;
			}
		}
		TestEqual(TEXT("16 bots x 64 lives: distinct HashCombine seeds"), Seeds.Num(), 16 * 64);
		TestTrue(TEXT("the XOR seed it replaces does collide on the same grid"), XorCollisions > 0);
		TestEqual(TEXT("seed is a pure function"), VeloxBotRules::BotSeed(7, 3, 2), VeloxBotRules::BotSeed(7, 3, 2));
		TestNotEqual(TEXT("master seed changes the bot seed"), VeloxBotRules::BotSeed(7, 3, 2), VeloxBotRules::BotSeed(8, 3, 2));
		TestNotEqual(TEXT("bot 1 life 2 != bot 2 life 1"), VeloxBotRules::BotSeed(1, 1, 2), VeloxBotRules::BotSeed(1, 2, 1));
	}

	// Key presence (section 1 step 3): view yaw 90 (looking +Y).
	{
		const FVector2D Fwd = VeloxBotRules::MoveKeys(FVector2D(0, 1), 1.f, 90.f, false, 0.3f);
		TestTrue(TEXT("wish along the view = forward key"), Fwd.Equals(FVector2D(1, 0), 1e-4));
		const FVector2D Right = VeloxBotRules::MoveKeys(FVector2D(-1, 0), 0.5f, 90.f, false, 0.3f);
		TestTrue(TEXT("wish to the view's right (-X at yaw 90) = right key, scaled"), Right.Equals(FVector2D(0, 0.5), 1e-4));
		const FVector2D Diagonal = VeloxBotRules::MoveKeys(FVector2D(1, 1).GetSafeNormal(), 1.f, 0.f, false, 0.3f);
		TestTrue(TEXT("diagonal wish (+X+Y at yaw 0: forward and right, +Y is right in UE) keeps both keys without CPM"), Diagonal.X > 0.7 && Diagonal.Y > 0.7);
		const FVector2D AlmostForward = FVector2D(FMath::Cos(FMath::DegreesToRadians(10.0)), FMath::Sin(FMath::DegreesToRadians(10.0)));
		const FVector2D Snapped = VeloxBotRules::MoveKeys(AlmostForward, 1.f, 0.f, true, 0.3f);
		TestEqual(TEXT("CPM snap: 10 degrees off forward -> pure forward (right dropped)"), Snapped.Y, 0.0);
		TestTrue(TEXT("CPM snap keeps the major key"), Snapped.X > 0.9);
		const FVector2D NotSnapped = VeloxBotRules::MoveKeys(FVector2D(1, 1).GetSafeNormal(), 1.f, 0.f, true, 0.3f);
		TestTrue(TEXT("CPM snap leaves a 45 degree wish alone"), FMath::Abs(NotSnapped.X) > 0.5 && FMath::Abs(NotSnapped.Y) > 0.5);
		TestTrue(TEXT("zero wish -> no keys"), VeloxBotRules::MoveKeys(FVector2D::ZeroVector, 1.f, 0.f, false, 0.3f).IsZero());
	}

	// Turn rate: 420 deg/s at 1/60 s = 7 degrees per tick, the short way round the +-180 seam.
	{
		const FRotator Step = VeloxBotRules::TurnToward(FRotator(0, 170, 0), FRotator(0, -170, 0), 420.f, 1.f / 60.f);
		TestTrue(TEXT("170 -> -170 turns +7 through 180 (177)"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(Step.Yaw), 177.f, 0.01f));
		const FRotator Small = VeloxBotRules::TurnToward(FRotator(0, 0, 0), FRotator(10, 3, 0), 420.f, 1.f / 60.f);
		TestTrue(TEXT("small yaw reached exactly, pitch clamped to the step"), FMath::IsNearlyEqual(Small.Yaw, 3.f, 1e-3f) && FMath::IsNearlyEqual(Small.Pitch, 7.f, 1e-3f));
		const FRotator Down = VeloxBotRules::TurnToward(FRotator(0, 0, 0), FRotator(-80, 0, 0), 100000.f, 1.f / 60.f);
		TestTrue(TEXT("pitch -80 reachable"), FMath::IsNearlyEqual(Down.Pitch, -80.f, 1e-3f));
	}

	// Stuck: 2 s below 50 cm/s while tracking.
	{
		double SlowSince = -1.0;
		bool bStuck = false;
		for (int32 Tick = 0; Tick <= 120; ++Tick)
		{
			bStuck |= VeloxBotRules::UpdateStuck(SlowSince, true, 10.f, 50.f, Tick / 60.0, 2.f);
		}
		TestTrue(TEXT("stuck after 2 s slow"), bStuck);
		SlowSince = -1.0;
		bStuck = false;
		for (int32 Tick = 0; Tick <= 240; ++Tick)
		{
			const bool bTracking = true;
			const float Speed = (Tick % 90 == 0) ? 400.f : 10.f;
			bStuck |= VeloxBotRules::UpdateStuck(SlowSince, bTracking, Speed, 50.f, Tick / 60.0, 2.f);
		}
		TestFalse(TEXT("progress every 1.5 s resets the clock"), bStuck);
		SlowSince = 5.0;
		TestFalse(TEXT("not tracking (on a link / arrived) never counts"), VeloxBotRules::UpdateStuck(SlowSince, false, 0.f, 50.f, 100.0, 2.f));
		TestEqual(TEXT("not tracking resets the clock"), SlowSince, -1.0);
	}

	// D18 progress rule: the 2-D distance to the segment target must shrink by 100 cm within 2 s while tracking.
	{
		const FVector2D Target(1000.0, 0.0);
		auto Run = [&](TFunctionRef<float(int32)> DistAt, TFunctionRef<FVector2D(int32)> TargetAt, int32 Ticks, bool bTracking = true,
			TFunctionRef<bool(int32)> PausedAt = [](int32) { return false; })
		{
			double Since = -1.0;
			float StartDist = 0.f;
			FVector2D WindowTarget = FVector2D::ZeroVector;
			float PausedSeconds = 0.f;
			int32 Strikes = 0;
			for (int32 Tick = 0; Tick <= Ticks; ++Tick)
			{
				// D27: the pause cap is the ini value, 4 s.
				Strikes += VeloxBotRules::UpdateStuckProgress(Since, StartDist, WindowTarget, PausedSeconds, bTracking, PausedAt(Tick), TargetAt(Tick), DistAt(Tick),
					100.f, Tick / 60.0, 1.f / 60.f, 2.f, 4.f) ? 1 : 0;
			}
			return Strikes;
		};
		auto Fixed = [&](int32) { return Target; };
		// Oscillation under the ramp slab: 300 +- 80 cm, never 100 cm closer than the window start.
		TestEqual(TEXT("oscillation: one strike per 2 s window"),
			Run([](int32 T) { return 300.f + 80.f * FMath::Sin(T * 0.3f); }, Fixed, 240), 2);
		TestEqual(TEXT("walking 320 cm/s toward the target: no strike"), Run([](int32 T) { return 1000.f - T * (320.f / 60.f); }, Fixed, 180), 0);
		TestEqual(TEXT("slow but steady 60 cm/s: no strike"), Run([](int32 T) { return 1000.f - T * 1.f; }, Fixed, 600), 0);
		TestEqual(TEXT("40 cm/s: 80 cm per window, strikes"), Run([](int32 T) { return 1000.f - T * (40.f / 60.f); }, Fixed, 240), 2);
		TestEqual(TEXT("not tracking never strikes"), Run([](int32) { return 300.f; }, Fixed, 600, false), 0);
		// A new segment every 1.5 s (target moves, the distance jumps up): each restarts the window.
		TestEqual(TEXT("new segment target restarts the window"),
			Run([](int32 T) { return 300.f + 50.f * FMath::Sin(T * 0.3f); }, [](int32 T) { return FVector2D(1000.0 + 500.0 * (T / 90), 0.0); }, 600), 0);
		// Combat overlay owns the wish (paused) every other second: the clock only runs on follower ticks.
		auto HalfPaused = [](int32 T) { return (T / 60) % 2 == 1; };
		TestEqual(TEXT("paused half the time: 4 s for the first strike"),
			Run([](int32 T) { return 300.f + 80.f * FMath::Sin(T * 0.3f); }, Fixed, 239, true, HalfPaused), 0);
		TestEqual(TEXT("paused half the time: strike after 2 s of follower time"),
			Run([](int32 T) { return 300.f + 80.f * FMath::Sin(T * 0.3f); }, Fixed, 300, true, HalfPaused), 1);
		// Paused for 3.5 s at a standstill (under the D27 cap): no strike, the clock does not run.
		TestEqual(TEXT("pause under the cap: no strike"), Run([](int32) { return 300.f; }, Fixed, 210, true, [](int32 T) { return T > 0; }), 0);
		// The overlay pushes the bot 500 cm away while paused (0.5 s), the follower then closes at 300 cm/s: no strike
		// (measured from the pause end, 100 cm take 0.33 s; held against the pre-pause 300 cm it would strike at T 150).
		TestEqual(TEXT("distance the overlay added is not held against the follower"),
			Run([](int32 T) { return T < 30 ? 300.f : T < 60 ? 300.f + (T - 30) * (500.f / 30.f) : FMath::Max(0.f, 800.f - (T - 60) * 5.f); }, Fixed, 300, true,
				[](int32 T) { return T >= 30 && T < 60; }), 0);
		// Pause T 60..240 (3 s, under the cap): strike at T ~ 300 when the clock only stopped, at T ~ 360 if the pause had reset it.
		TestEqual(TEXT("pause does not reset the window"),
			Run([](int32) { return 300.f; }, Fixed, 330, true, [](int32 T) { return T >= 60 && T < 240; }), 1);
		double Since = 5.0;
		float StartDist = 300.f;
		FVector2D WindowTarget = Target;
		float PausedSeconds = 1.f;
		TestFalse(TEXT("not tracking: false"),
			VeloxBotRules::UpdateStuckProgress(Since, StartDist, WindowTarget, PausedSeconds, false, false, Target, 300.f, 100.f, 10.0, 1.f / 60.f, 2.f, 4.f));
		TestEqual(TEXT("not tracking clears the window"), Since, -1.0);
		TestEqual(TEXT("not tracking clears the pause"), PausedSeconds, 0.f);
	}

	// ADR 0005 D27: the pause is bounded (4 s per window), so a bot held in place under the overlay strikes at 4 + 2 s.
	{
		const FVector2D Target(1000.0, 0.0);
		double Since = -1.0;
		float StartDist = 0.f;
		FVector2D WindowTarget = FVector2D::ZeroVector;
		float PausedSeconds = 0.f;
		int32 Strikes = 0;
		double FirstStrike = -1.0;
		for (int32 Tick = 0; Tick <= 6 * 60 + 30; ++Tick)
		{
			const double T = Tick / 60.0;
			if (VeloxBotRules::UpdateStuckProgress(Since, StartDist, WindowTarget, PausedSeconds, true, true, Target, 300.f, 100.f, T, 1.f / 60.f, 2.f, 4.f))
			{
				++Strikes;
				FirstStrike = FirstStrike < 0.0 ? T : FirstStrike;
			}
		}
		AddInfo(FString::Printf(TEXT("D27: paused from t = 0 without progress: %d strike(s), first at %.3f s"), Strikes, FirstStrike));
		TestEqual(TEXT("D27: paused from t = 0, no progress -> exactly one strike"), Strikes, 1);
		TestTrue(TEXT("D27: the strike at 6.0 +- 0.1 s"), FMath::Abs(FirstStrike - 6.0) <= 0.1);

		// Paused 1 s, then unpaused and closing 100 cm (at 120 cm/s) -> no strike.
		Since = -1.0;
		PausedSeconds = 0.f;
		Strikes = 0;
		for (int32 Tick = 0; Tick <= 4 * 60; ++Tick)
		{
			const double T = Tick / 60.0;
			const bool bPaused = Tick < 60;
			const float Dist = bPaused ? 300.f : FMath::Max(0.f, 300.f - static_cast<float>(T - 1.0) * 120.f);
			Strikes += VeloxBotRules::UpdateStuckProgress(Since, StartDist, WindowTarget, PausedSeconds, true, bPaused, Target, Dist, 100.f, T, 1.f / 60.f, 2.f, 4.f) ? 1 : 0;
		}
		TestEqual(TEXT("D27: paused 1 s then unpaused with 100 cm progress -> no strike"), Strikes, 0);
		TestEqual(TEXT("D27: StuckProgressMaxPauseSeconds 4 (ini)"), GetDefault<AVeloxBotController>()->StuckProgressMaxPauseSeconds, 4.f);
		TestEqual(TEXT("D30: FightStallCooldownSeconds 4 (ini)"), GetDefault<AVeloxBotController>()->FightStallCooldownSeconds, 4.f);
	}

	// ADR 0005 D30: the speed rule watches a pathless fight (Goal Fight, no move, enemy visible, on the ground, no link).
	{
		auto RunFight = [](bool bEnemyVisible, bool bMoveActive)
		{
			double SlowSince = -1.0;
			double StrikeAt = -1.0;
			for (int32 Tick = 0; Tick <= 150 && StrikeAt < 0.0; ++Tick)
			{
				const bool bTracking = false; // no path: the plain rule does not track
				const bool bWatched = VeloxBotRules::IsFightWatched(EVeloxBotGoal::Fight, bMoveActive, bEnemyVisible, true, false);
				if (VeloxBotRules::UpdateStuck(SlowSince, bTracking || bWatched, 10.f, 50.f, Tick / 60.0, 2.f))
				{
					StrikeAt = Tick / 60.0;
				}
			}
			return StrikeAt;
		};
		const double At = RunFight(true, false);
		TestTrue(TEXT("D30: pathless fight, enemy visible, speed 10 -> strike at 2 s"), FMath::Abs(At - 2.0) <= 1.0 / 60.0 + 1e-6);
		TestTrue(TEXT("D30: enemy not visible -> no fight_stall strike (a chase has a path and the plain rule)"), RunFight(false, false) < 0.0);
		TestFalse(TEXT("D30: Item goal is not fight-watched"), VeloxBotRules::IsFightWatched(EVeloxBotGoal::Item, false, true, true, false));
		TestFalse(TEXT("D30: airborne is not fight-watched"), VeloxBotRules::IsFightWatched(EVeloxBotGoal::Fight, false, true, false, false));
		TestFalse(TEXT("D30: on a link is not fight-watched"), VeloxBotRules::IsFightWatched(EVeloxBotGoal::Fight, false, true, true, true));
		TestFalse(TEXT("D30: an active move is the plain rule's"), VeloxBotRules::IsFightWatched(EVeloxBotGoal::Fight, true, true, true, false));
	}

	// D18 hop headroom: JumpVelocity^2 / (2 |g|) + margin (675 cm/s, -2000 cm/s^2 -> 113.9 + 10).
	{
		TestTrue(TEXT("Velox defaults: 123.9 cm"), FMath::IsNearlyEqual(VeloxBotFollow::HopHeadroomCm(675.f, -2000.f, 10.f), 675.f * 675.f / 4000.f + 10.f, 1e-3f));
		TestTrue(TEXT("gravity sign ignored"), FMath::IsNearlyEqual(VeloxBotFollow::HopHeadroomCm(675.f, 2000.f, 10.f), VeloxBotFollow::HopHeadroomCm(675.f, -2000.f, 10.f), 1e-4f));
		TestTrue(TEXT("Q3: 270 ups at 800 ups^2 -> 45.6 + 0"), FMath::IsNearlyEqual(VeloxBotFollow::HopHeadroomCm(270.f, -800.f, 0.f), 45.5625f, 1e-3f));
		TestEqual(TEXT("zero gravity: the margin alone"), VeloxBotFollow::HopHeadroomCm(675.f, 0.f, 10.f), 10.f);
		TestEqual(TEXT("negative margin clamps to 0"), VeloxBotFollow::HopHeadroomCm(0.f, -2000.f, -5.f), 0.f);
		// The L_ItemTest ramp strip (clearance 130-190 cm) must block a 140 cm capsule hop; open floor (no ceiling) not.
		TestTrue(TEXT("140 cm capsule + headroom exceeds the 190 cm ramp clearance"), 140.f + VeloxBotFollow::HopHeadroomCm(675.f, -2000.f, 10.f) > 190.f);
		// Loaded tunables (DefaultGame.ini) equal the D18 values.
		TestEqual(TEXT("HopHeadroomMarginCm 10"), GetDefault<UVeloxBotPathFollowingComponent>()->HopHeadroomMarginCm, 10.f);
		TestEqual(TEXT("HopHeadroomCheckSeconds 0.1"), GetDefault<UVeloxBotPathFollowingComponent>()->HopHeadroomCheckSeconds, 0.1f);
		TestEqual(TEXT("StuckProgressCm 100"), GetDefault<AVeloxBotController>()->StuckProgressCm, 100.f);
	}

	// Task 04b (ADR 0005 D4) made these settings; task 04c-a (D13) changed the fight-score values. The loaded values
	// (DefaultGame.ini) equal the ADR values.
	{
		const UVeloxBotSettings& S = UVeloxBotSettings::Get();
		TestEqual(TEXT("FightBaseScore 12 (D13)"), S.FightBaseScore, 12.f);
		TestEqual(TEXT("FightFullStrength 200 (D13)"), S.FightFullStrength, 200.f);
		TestEqual(TEXT("FightWeakWeaponScale 0.25 (D13)"), S.FightWeakWeaponScale, 0.25f);
		TestEqual(TEXT("LinkStartAcceptanceCm 30"), S.LinkStartAcceptanceCm, 30.f);
		TestEqual(TEXT("JumpFastSpeedFraction 0.9"), S.JumpFastSpeedFraction, 0.9f);
		TestEqual(TEXT("JumpFastRunUpCm 600"), S.JumpFastRunUpCm, 600.f);
		TestEqual(TEXT("JumpFastRetries 1"), S.JumpFastRetries, 1);
		TestEqual(TEXT("RocketJumpImpulseTimeoutSeconds 0.3"), S.RocketJumpImpulseTimeoutSeconds, 0.3f);
	}

	// Off-mesh recovery (task 04b): grace 1 s off the mesh, a running recovery move is not replaced before its timeout.
	{
		double OffSince = -1.0;
		TestFalse(TEXT("off mesh: first check starts the clock"), VeloxBotRules::UpdateOffMesh(OffSince, false, false, 0.0, 10.0, 1.f));
		TestEqual(TEXT("off mesh: clock started"), OffSince, 10.0);
		TestFalse(TEXT("off mesh 0.5 s: inside the grace"), VeloxBotRules::UpdateOffMesh(OffSince, false, false, 0.0, 10.5, 1.f));
		TestTrue(TEXT("off mesh 1 s: recovery starts"), VeloxBotRules::UpdateOffMesh(OffSince, false, false, 0.0, 11.0, 1.f));
		TestFalse(TEXT("recovery move running: not replaced"), VeloxBotRules::UpdateOffMesh(OffSince, false, true, 15.0, 12.0, 1.f));
		TestTrue(TEXT("recovery move timed out: new attempt"), VeloxBotRules::UpdateOffMesh(OffSince, false, true, 15.0, 15.0, 1.f));
		TestTrue(TEXT("recovery move ended off the mesh: new attempt"), VeloxBotRules::UpdateOffMesh(OffSince, false, false, 15.0, 12.0, 1.f));
		TestFalse(TEXT("back on the mesh: no recovery"), VeloxBotRules::UpdateOffMesh(OffSince, true, true, 15.0, 12.5, 1.f));
		TestEqual(TEXT("back on the mesh: clock reset"), OffSince, -1.0);
	}

	// D45: the wall tangent of a blocked recovery fallback.
	{
		FVector2D Tangent;
		TestTrue(TEXT("wall tangent: wall normal (0,-1,0)"), VeloxBotRules::WallTangent(FVector(0.0, -1.0, 0.0), Tangent));
		TestTrue(TEXT("wall tangent: unit length"), FMath::IsNearlyEqual(Tangent.Size(), 1.0, 1e-4));
		TestTrue(TEXT("wall tangent: perpendicular to (0,-1)"), FMath::IsNearlyZero(FVector2D::DotProduct(Tangent, FVector2D(0.0, -1.0)), 1e-4));
		TestFalse(TEXT("wall tangent: floor normal (0,0,1) has none"), VeloxBotRules::WallTangent(FVector(0.0, 0.0, 1.0), Tangent));
		TestTrue(TEXT("wall tangent: sloped normal (0.6,0,0.8)"), VeloxBotRules::WallTangent(FVector(0.6, 0.0, 0.8), Tangent));
		TestTrue(TEXT("wall tangent: (0.6,0,0.8) -> (0,+-1)"), FMath::IsNearlyZero(Tangent.X, 1e-4) && FMath::IsNearlyEqual(FMath::Abs(Tangent.Y), 1.0, 1e-4));
	}

	// Names.
	{
		const TArray<FString> Names = { TEXT("Celer"), TEXT("Rapidus") };
		TestEqual(TEXT("first free"), VeloxBotRules::NextBotName(Names, { TEXT("Player") }, FString()), FString(TEXT("Celer")));
		TestEqual(TEXT("skips taken (case-insensitive)"), VeloxBotRules::NextBotName(Names, { TEXT("celer") }, FString()), FString(TEXT("Rapidus")));
		TestEqual(TEXT("exhausted -> suffix 2"), VeloxBotRules::NextBotName(Names, { TEXT("Celer"), TEXT("Rapidus") }, FString()), FString(TEXT("Celer2")));
		TestEqual(TEXT("exhausted -> next free suffix"), VeloxBotRules::NextBotName(Names, { TEXT("Celer"), TEXT("Rapidus"), TEXT("Celer2") }, FString()), FString(TEXT("Celer3")));
		TestEqual(TEXT("requested name"), VeloxBotRules::NextBotName(Names, {}, TEXT("Fulgur")), FString(TEXT("Fulgur")));
		TestEqual(TEXT("requested but taken -> suffix"), VeloxBotRules::NextBotName(Names, { TEXT("Fulgur") }, TEXT("Fulgur")), FString(TEXT("Fulgur2")));
		TestEqual(TEXT("no names configured"), VeloxBotRules::NextBotName({}, {}, FString()), FString(TEXT("Bot")));
	}

	// ?Bots= capping (section 12): one human slot stays unless BotsFill.
	{
		TestEqual(TEXT("Duel ?Bots=2 -> 1"), VeloxBotRules::UrlBotCount(2, 2, false), 1);
		TestEqual(TEXT("Duel ?Bots=2?BotsFill=1 -> 2"), VeloxBotRules::UrlBotCount(2, 2, true), 2);
		TestEqual(TEXT("FFA 16 ?Bots=3 -> 3"), VeloxBotRules::UrlBotCount(3, 16, false), 3);
		TestEqual(TEXT("FFA 16 ?Bots=40 -> 15"), VeloxBotRules::UrlBotCount(40, 16, false), 15);
		TestEqual(TEXT("no mode cap"), VeloxBotRules::UrlBotCount(5, 0, false), 5);
		TestEqual(TEXT("absent / zero"), VeloxBotRules::UrlBotCount(0, 16, true), 0);
	}

	// Link type -> maneuver type (task 02's mirror enum) and the corner angle.
	{
		TestTrue(TEXT("JumpPad"), VeloxBotFollow::ManeuverTypeFor(EVeloxNavLinkType::JumpPad) == EVeloxManeuverType::JumpPad);
		TestTrue(TEXT("Drop"), VeloxBotFollow::ManeuverTypeFor(EVeloxNavLinkType::Drop) == EVeloxManeuverType::Drop);
		TestTrue(TEXT("Jump"), VeloxBotFollow::ManeuverTypeFor(EVeloxNavLinkType::Jump) == EVeloxManeuverType::Jump);
		TestTrue(TEXT("JumpFast"), VeloxBotFollow::ManeuverTypeFor(EVeloxNavLinkType::JumpFast) == EVeloxManeuverType::JumpFast);
		TestTrue(TEXT("RocketJump"), VeloxBotFollow::ManeuverTypeFor(EVeloxNavLinkType::RocketJump) == EVeloxManeuverType::RocketJump);
		TestTrue(TEXT("straight = 0"), FMath::IsNearlyEqual(VeloxBotFollow::CornerAngleDeg(FVector(100, 0, 0), FVector(50, 0, 30)), 0.f, 1e-3f));
		TestTrue(TEXT("right angle = 90 (Z ignored)"), FMath::IsNearlyEqual(VeloxBotFollow::CornerAngleDeg(FVector(100, 0, 0), FVector(0, -40, 300)), 90.f, 1e-3f));
		TestTrue(TEXT("reverse = 180"), FMath::IsNearlyEqual(VeloxBotFollow::CornerAngleDeg(FVector(100, 0, 0), FVector(-1, 0, 0)), 180.f, 1e-3f));
		TestEqual(TEXT("vertical segment = 0"), VeloxBotFollow::CornerAngleDeg(FVector(0, 0, 100), FVector(1, 0, 0)), 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotFindNearestEndTest, "Velox.Bots.FindNearestEnd", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxBotFindNearestEndTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> Ends = {
		FVector(0, 0, 0),        // 0: floor
		FVector(0, 0, 300),      // 1: platform top above it
		FVector(500, 0, 0),      // 2
		FVector(530, 10, 20),    // 3: near 2
	};
	TestEqual(TEXT("exact match"), UVeloxBotSubsystem::FindNearestEnd(Ends, FVector(0, 0, 0), 60.0, 100.0), 0);
	TestEqual(TEXT("height filter picks the platform end, not the floor below"), UVeloxBotSubsystem::FindNearestEnd(Ends, FVector(10, 0, 290), 60.0, 100.0), 1);
	TestEqual(TEXT("nearest of two within the radius"), UVeloxBotSubsystem::FindNearestEnd(Ends, FVector(525, 5, 0), 60.0, 100.0), 3);
	TestEqual(TEXT("outside the radius"), UVeloxBotSubsystem::FindNearestEnd(Ends, FVector(200, 0, 0), 60.0, 100.0), INDEX_NONE);
	TestEqual(TEXT("radius is inclusive"), UVeloxBotSubsystem::FindNearestEnd(Ends, FVector(60, 0, 0), 60.0, 100.0), 0);
	TestEqual(TEXT("above the snap height"), UVeloxBotSubsystem::FindNearestEnd(Ends, FVector(500, 0, 150), 60.0, 100.0), INDEX_NONE);
	TestEqual(TEXT("empty list"), UVeloxBotSubsystem::FindNearestEnd(TArray<FVector>(), FVector::ZeroVector, 60.0, 100.0), INDEX_NONE);
	TestEqual(TEXT("unbounded radius finds the nearest anywhere (DumpNav / follower match)"),
		UVeloxBotSubsystem::FindNearestEnd(Ends, FVector(2000, 0, 0), TNumericLimits<float>::Max(), 100.0), 3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// ------------------------------------------------------------------------------------------------ functional tests

#if !UE_BUILD_SHIPPING
namespace VeloxBotFt
{
	struct FFtBase
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AVeloxBotController> Bot;
		bool bQuit = false;
		bool bPassed = true;
		bool bFinished = false;
		FTSTicker::FDelegateHandle TickHandle;
		FDelegateHandle LinkHandle;
		const TCHAR* Name = TEXT("?");

		virtual ~FFtBase() = default;

		void Check(bool bOk, const TCHAR* What, const FString& Detail)
		{
			bPassed &= bOk;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s check=%s ok=%d %s"), Name, What, bOk ? 1 : 0, *Detail);
		}

		double Now() const
		{
			const UWorld* W = World.Get();
			return W ? W->GetTimeSeconds() : 0.0;
		}

		void LogBotCost() const
		{
			if (const AVeloxBotController* B = Bot.Get())
			{
				const FVeloxBotState& S = B->GetBotState();
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s bot=%s tick_us_mean=%.1f tick_us_max=%.1f ticks=%d"), Name, *B->GetBotName(),
					S.TickCount > 0 ? S.TickMicrosSum / S.TickCount : 0.0, S.TickMicrosMax, S.TickCount);
			}
		}

		void Finish()
		{
			if (bFinished)
			{
				return;
			}
			bFinished = true;
			LogBotCost();
			if (AVeloxBotController* B = Bot.Get())
			{
				B->OnLinkFinished.Remove(LinkHandle);
			}
			OnFinish();
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s result=%s"), Name, bPassed ? TEXT("PASS") : TEXT("FAIL"));
			if (bQuit)
			{
				FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
			}
		}

		virtual void OnFinish() {}
	};

	bool ParseQuit(const TArray<FString>& Args)
	{
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	AVeloxPickup* FirstPickup(UWorld* World, const FGameplayTag& Tag)
	{
		const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		const TArray<AVeloxPickup*> Found = Registry ? Registry->FindByTag(Tag) : TArray<AVeloxPickup*>();
		return Found.Num() > 0 ? Found[0] : nullptr;
	}

	// ------------------------------------------------------------------------------------------ FT_BotNavigation

	/** ADR 0005 section 14 limits. */
	constexpr double NavBuildLimitSeconds = 5.0;
	constexpr double MegaLimitSeconds = 25.0;
	constexpr double RedArmorLimitSeconds = 35.0;
	/** Task 05: the rocket-jump case (ADR 0005 section 14) and its abort path. */
	constexpr double RocketsLimitSeconds = 25.0;
	constexpr double RocketWalkBackLimitSeconds = 25.0;
	constexpr double RocketAbortLimitSeconds = 20.0;
	/** The abort case's walk-back point: this far beyond the rocket-jump link start, away from the platform (cm). */
	constexpr double RocketWalkBackCm = 300.0;
	/** The Mega's pad link ends at the item (create_item_test_map.py). */
	constexpr double MegaLinkEndToleranceCm = 200.0;

	struct FNavFt : FFtBase
	{
		enum class EStep : uint8 { WaitNav, Mega, RedArmor, Rockets, RocketWalkBack, RocketAbort, Done };
		EStep Step = EStep::WaitNav;
		double StepStart = 0.0;
		TWeakObjectPtr<AVeloxPickup> Mega;
		TWeakObjectPtr<AVeloxPickup> RedArmor;
		TWeakObjectPtr<AVeloxPickup> Rockets;
		bool bPadOk = false;
		bool bDropOk = false;
		// Task 05: rocket jump.
		bool bRocketOk = false;
		bool bRocketAborted = false;
		bool bLauncherStripped = false;
		FVector RocketLinkStart = FVector::ZeroVector;
		FVector RocketLinkEnd = FVector::ZeroVector;
		double RocketStartZ = 0.0;
		double RocketApexZ = -1e9;
		float LastHealth = -1.f;
		float MaxHealthDrop = 0.f;
		int32 LinksOk = 0;
		int32 LinksFailed = 0;
		FDelegateHandle TakenHandle;

		void OnFinish() override
		{
			if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World.Get()))
			{
				Registry->OnPickupTaken.Remove(TakenHandle);
			}
			Check(true, TEXT("links"), FString::Printf(TEXT("ok=%d failed=%d"), LinksOk, LinksFailed));
		}
	};

	void NavFtStart(const TSharedRef<FNavFt>& Ft)
	{
		UWorld* World = Ft->World.Get();
		UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		Ft->Check(Bots && Bots->IsNavReady(), TEXT("nav_built"), FString::Printf(TEXT("seconds=%.3f limit=%.1f"), Bots ? Bots->GetNavBuildSeconds() : -1.0, NavBuildLimitSeconds));
		Ft->Check(Bots && Bots->GetNavBuildSeconds() >= 0.0 && Bots->GetNavBuildSeconds() < NavBuildLimitSeconds, TEXT("nav_build_time"),
			FString::Printf(TEXT("seconds=%.3f"), Bots ? Bots->GetNavBuildSeconds() : -1.0));
		// The FFA rules would start a 20 s countdown when the bot joins and ResetLevel (respawn everyone) at Live, which
		// lands in the middle of the rocket-jump case (task 05): enter Live first, before the bot exists.
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("velox.match.Phase Live"));
		}
		// A navigation test: the bot must not fight the local player (task 05 added combat to every bot).
		if (IConsoleVariable* Peaceful = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.bots.Peaceful")))
		{
			Peaceful->Set(1, ECVF_SetByCode);
		}
		Ft->Mega = FirstPickup(World, VeloxGameplayTags::Item_Health_Mega);
		Ft->RedArmor = FirstPickup(World, VeloxGameplayTags::Item_Armor_Red);
		Ft->Rockets = FirstPickup(World, VeloxGameplayTags::Item_Ammo_Rockets);
		for (const AVeloxNavLink* Link : Bots ? Bots->GetNavLinks() : TArray<AVeloxNavLink*>())
		{
			if (Link->GetLinkType() == EVeloxNavLinkType::RocketJump)
			{
				Ft->RocketLinkStart = Link->GetStartWorld();
				Ft->RocketLinkEnd = Link->GetEndWorld();
			}
		}
		AVeloxBotController* Bot = Bots ? Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Medium")), TEXT("FtNav")) : nullptr;
		Ft->Bot = Bot;
		if (!Bot || !Bot->GetPawn() || !Ft->Mega.IsValid() || !Ft->RedArmor.IsValid() || !Ft->Rockets.IsValid() || Ft->RocketLinkStart.IsZero() || !Registry)
		{
			Ft->Check(false, TEXT("setup"), FString::Printf(TEXT("bot=%s pawn=%s mega=%s ra=%s"), *GetNameSafe(Bot), Bot ? *GetNameSafe(Bot->GetPawn()) : TEXT("-"),
				*GetNameSafe(Ft->Mega.Get()), *GetNameSafe(Ft->RedArmor.Get())));
			Ft->Finish();
			return;
		}
		TWeakPtr<FNavFt> Weak = Ft;
		Ft->LinkHandle = Bot->OnLinkFinished.AddLambda([Weak](AVeloxBotController*, const FVeloxBotLinkReport& Report)
		{
			const TSharedPtr<FNavFt> F = Weak.Pin();
			if (!F)
			{
				return;
			}
			const bool bOk = Report.Result == EVeloxManeuverResult::Ok;
			bOk ? ++F->LinksOk : ++F->LinksFailed;
			// The Mega's pad link ends at the item: the pickup overlap can fire in the descent, a few ticks before the
			// landing ends the traversal, so the pad link is matched by its end, not by the step.
			const AVeloxPickup* MegaPickup = F->Mega.Get();
			if (bOk && Report.Type == EVeloxManeuverType::JumpPad && MegaPickup && FVector::Dist2D(Report.To, MegaPickup->GetActorLocation()) <= MegaLinkEndToleranceCm)
			{
				F->bPadOk = true;
			}
			if (bOk && F->Step == FNavFt::EStep::RedArmor && Report.Type == EVeloxManeuverType::Drop)
			{
				F->bDropOk = true;
			}
			if (Report.Type == EVeloxManeuverType::RocketJump)
			{
				// The pickup overlap can fire in the descent, a few ticks before the landing ends the traversal (as for the Mega).
				if (bOk && !F->bRocketOk && (F->Step == FNavFt::EStep::Rockets || F->Step == FNavFt::EStep::RocketWalkBack))
				{
					F->bRocketOk = true;
					F->Check(true, TEXT("rocket_jump_link"), FString::Printf(TEXT("seconds=%.2f apex_cm=%.0f (above start floor) max_health_drop=%.0f"), F->Now() - F->StepStart,
						F->RocketApexZ - F->RocketStartZ, F->MaxHealthDrop));
				}
				if (F->Step == FNavFt::EStep::RocketAbort && F->bLauncherStripped && Report.Result == EVeloxManeuverResult::Ineligible)
				{
					F->bRocketAborted = true;
				}
			}
		});
		Ft->TakenHandle = Registry->OnPickupTaken.AddLambda([Weak](AVeloxPickup* Pickup, APlayerState* TakenBy)
		{
			const TSharedPtr<FNavFt> F = Weak.Pin();
			AVeloxBotController* B = F ? F->Bot.Get() : nullptr;
			if (!F || !B || F->bFinished || TakenBy != B->PlayerState)
			{
				return;
			}
			const double Elapsed = F->Now() - F->StepStart;
			if (F->Step == FNavFt::EStep::Mega && Pickup == F->Mega.Get())
			{
				F->Check(Elapsed <= MegaLimitSeconds, TEXT("mega_taken"), FString::Printf(TEXT("seconds=%.2f limit=%.1f"), Elapsed, MegaLimitSeconds));
				// Same tick: the bot stands on the Mega platform, so the way to the RA starts with a drop.
				F->Step = FNavFt::EStep::RedArmor;
				F->StepStart = F->Now();
				B->SetForcedItemGoal(F->RedArmor.Get());
			}
			else if (F->Step == FNavFt::EStep::RedArmor && Pickup == F->RedArmor.Get())
			{
				F->Check(F->bPadOk, TEXT("mega_via_jump_pad"), TEXT("VeloxBotLink type=JumpPad result=ok on the Mega's pad link"));
				F->Check(F->bDropOk, TEXT("ra_via_drop"), TEXT("VeloxBotLink type=Drop result=ok before the take"));
				F->Check(Elapsed <= RedArmorLimitSeconds, TEXT("ra_taken"), FString::Printf(TEXT("seconds=%.2f limit=%.1f"), Elapsed, RedArmorLimitSeconds));
				// Task 05: the rocket-jump case. Preset Hard (rocket jumps on) and every weapon (velox.combat.GiveAllWeapons).
				AVeloxPlayerState* PS = B->GetPlayerState<AVeloxPlayerState>();
				const bool bGiven = PS && VeloxWeaponCheats::GiveAllWeapons(*PS);
				B->SetDifficulty(UVeloxBotSubsystem::LoadDifficulty(TEXT("Hard")));
				F->Check(bGiven && B->GetDifficultyParams().bRocketJumps, TEXT("rocket_setup"), FString::Printf(TEXT("weapons=%d difficulty=%s"), bGiven ? 1 : 0, *GetNameSafe(B->GetDifficulty())));
				F->Step = FNavFt::EStep::Rockets;
				F->StepStart = F->Now();
				F->RocketStartZ = F->RocketLinkStart.Z;
				F->RocketApexZ = -1e9;
				F->MaxHealthDrop = 0.f;
				F->LastHealth = -1.f;
				B->SetForcedItemGoal(F->Rockets.Get());
			}
			else if (F->Step == FNavFt::EStep::Rockets && Pickup == F->Rockets.Get())
			{
				F->Check(Elapsed <= RocketsLimitSeconds, TEXT("rockets_taken"), FString::Printf(TEXT("seconds=%.2f limit=%.1f apex_cm=%.0f max_health_drop=%.0f"), Elapsed, RocketsLimitSeconds,
					F->RocketApexZ - F->RocketStartZ, F->MaxHealthDrop));
				// Abort path: back to the floor beyond the link start, then toward the rockets again without a launcher.
				const FVector Away = (F->RocketLinkStart - F->RocketLinkEnd).GetSafeNormal2D();
				F->Step = FNavFt::EStep::RocketWalkBack;
				F->StepStart = F->Now();
				B->SetForcedPointGoal(F->RocketLinkStart + Away * RocketWalkBackCm);
			}
		});
		Ft->Step = FNavFt::EStep::Mega;
		Ft->StepStart = Ft->Now();
		Bot->SetForcedItemGoal(Ft->Mega.Get());
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s start bot=%s at=%s mega=%s ra=%s"), Ft->Name, *Bot->GetBotName(), *Bot->GetPawn()->GetActorLocation().ToCompactString(),
			*Ft->Mega->GetActorLocation().ToCompactString(), *Ft->RedArmor->GetActorLocation().ToCompactString());

		Ft->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
		{
			const TSharedPtr<FNavFt> F = Weak.Pin();
			if (!F || F->bFinished)
			{
				return false;
			}
			if (!F->World.IsValid() || !F->Bot.IsValid())
			{
				F->Check(false, TEXT("alive"), TEXT("world or bot gone"));
				F->Finish();
				return false;
			}
			const double Elapsed = F->Now() - F->StepStart;
			double Limit = RedArmorLimitSeconds;
			const TCHAR* What = TEXT("ra_taken");
			switch (F->Step)
			{
			case FNavFt::EStep::Mega: Limit = MegaLimitSeconds; What = TEXT("mega_taken"); break;
			case FNavFt::EStep::Rockets: Limit = RocketsLimitSeconds; What = TEXT("rockets_taken"); break;
			case FNavFt::EStep::RocketWalkBack: Limit = RocketWalkBackLimitSeconds; What = TEXT("rocket_walk_back"); break;
			case FNavFt::EStep::RocketAbort: Limit = RocketAbortLimitSeconds; What = TEXT("rocket_abort_logged"); break;
			default: break;
			}
			if (Elapsed > Limit)
			{
				F->Check(false, What, FString::Printf(TEXT("timeout after %.1f s: %s"), Elapsed, *F->Bot->DescribeForDebug()));
				F->Finish();
				return false;
			}
			AVeloxBotController* B = F->Bot.Get();
			const APawn* Pawn = B->GetPawn();
			if (F->Step == FNavFt::EStep::Rockets && Pawn)
			{
				// Report numbers: apex above the run-up floor and the largest one-tick health drop (the self splash).
				F->RocketApexZ = FMath::Max(F->RocketApexZ, Pawn->GetActorLocation().Z - static_cast<double>(Pawn->GetSimpleCollisionHalfHeight()));
				const AVeloxPlayerState* PS = B->GetPlayerState<AVeloxPlayerState>();
				const UVeloxHealthSet* Health = PS ? PS->GetHealthSet() : nullptr;
				if (Health)
				{
					if (F->LastHealth >= 0.f)
					{
						F->MaxHealthDrop = FMath::Max(F->MaxHealthDrop, F->LastHealth - Health->GetHealth());
					}
					F->LastHealth = Health->GetHealth();
				}
			}
			if (F->Step == FNavFt::EStep::RocketWalkBack && B->GetBotState().bGoalArrived)
			{
				F->Check(F->bRocketOk, TEXT("rockets_via_rocket_jump"), TEXT("VeloxBotLink type=RocketJump result=ok"));
				F->Check(true, TEXT("rocket_walk_back"), FString::Printf(TEXT("seconds=%.2f at=%s"), Elapsed, Pawn ? *Pawn->GetActorLocation().ToCompactString() : TEXT("-")));
				F->Step = FNavFt::EStep::RocketAbort;
				F->StepStart = F->Now();
				B->SetForcedItemGoal(F->Rockets.Get());
			}
			if (F->Step == FNavFt::EStep::RocketAbort)
			{
				const UVeloxBotPathFollowingComponent* Follow = B->GetBotPathFollowing();
				if (!F->bLauncherStripped && Follow && Follow->IsOnLink() && Follow->GetManeuver().Type == EVeloxManeuverType::RocketJump && Follow->GetManeuver().IsActive())
				{
					// Test harness: the launcher is gone mid-traversal -> the maneuver must abort as ineligible.
					const AVeloxPlayerState* PS = B->GetPlayerState<AVeloxPlayerState>();
					if (UVeloxWeaponInventoryComponent* Inventory = PS ? PS->GetWeaponInventory() : nullptr)
					{
						Inventory->RemoveAllWeapons();
						F->bLauncherStripped = true;
						UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s rocket launcher removed during the RocketJump traversal (phase=%d)"), F->Name, static_cast<int32>(Follow->GetManeuver().Phase));
					}
				}
				if (F->bRocketAborted)
				{
					F->Check(true, TEXT("rocket_abort_logged"), FString::Printf(TEXT("seconds=%.2f VeloxBotLink type=RocketJump result=aborted reason=ineligible"), Elapsed));
					F->Step = FNavFt::EStep::Done;
					F->Finish();
					return false;
				}
			}
			return true;
		}));
	}

	TSharedPtr<FNavFt> GNavFt;

	void NavFtCommand(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		if (!Bots)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtNavigation: needs the authority world of L_ItemTest (-game)."));
			return;
		}
		TSharedRef<FNavFt> Ft = MakeShared<FNavFt>();
		Ft->Name = TEXT("FT_BotNavigation");
		Ft->World = World;
		Ft->bQuit = ParseQuit(Args);
		GNavFt = Ft;
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s waiting for the nav build (seed=%d)"), Ft->Name, Bots->GetMasterSeed());
		TWeakPtr<FNavFt> Weak = Ft;
		Bots->CallWhenNavReady([Weak]()
		{
			if (const TSharedPtr<FNavFt> F = Weak.Pin())
			{
				NavFtStart(F.ToSharedRef());
			}
		});
	}

	FAutoConsoleCommandWithWorldAndArgs NavFtCmd(TEXT("velox.bots.FtNavigation"),
		TEXT("velox.bots.FtNavigation [quit]: FT_BotNavigation on L_ItemTest (-game -nullrhi -UseFixedTimeStep -FPS=60): pad and drop traversals to Mega and RA, rocket jump to the rockets platform and its abort path. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NavFtCommand));

	// ------------------------------------------------------------------------------------- FT_BotStrafeCorridor

	/** L_MovementTest corridor (Tools/editor/create_movement_test_map.py): x 0..5120, y -500..500. */
	const FVector CorridorStart(300.0, 0.0, 0.0);
	const FVector CorridorGoal(5000.0, 0.0, 0.0);
	constexpr double CorridorEndX = 5120.0;
	constexpr double CorridorHalfWidth = 500.0;
	/** A free spot outside the corridor for the local player's pawn (PlayerStart_Pads). */
	const FVector LocalPawnParking(-300.0, -2500.0, 0.0);
	constexpr double SettleSeconds = 0.5;
	constexpr double ArriveRadiusCm = 120.0;
	constexpr double HardLimitSeconds = 7.0;
	constexpr double EasyLimitSeconds = 9.0;
	constexpr double TimeoutMarginSeconds = 3.0;
	constexpr float HardPeakFactor = 1.25f;
	constexpr float EasySpeedMarginCmS = 1.f;

	struct FCorridorFt : FFtBase
	{
		enum class ECase : uint8 { Hard, Easy, Done };
		ECase Case = ECase::Hard;
		bool bRunning = false;
		double SettleUntil = 0.0;
		double RunStart = 0.0;
		float PeakSpeed = 0.f;
		float MaxSpeed = 0.f;
		float MaxGroundSpeed = 0.f;
		int32 Samples = 0;
	};

	void PlaceAtStart(AVeloxCharacter& Pawn, const FVector& FloorPoint)
	{
		// Test harness: put the pawn on the corridor start at rest (the bot never does this itself).
		Pawn.SetActorLocationAndRotation(FloorPoint + FVector(0.0, 0.0, Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0), FRotator::ZeroRotator, false, nullptr,
			ETeleportType::TeleportPhysics);
		if (UVeloxMovementComponent* Movement = Pawn.GetVeloxMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
	}

	bool CorridorStartCase(const TSharedRef<FCorridorFt>& Ft)
	{
		UWorld* World = Ft->World.Get();
		UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		if (!Bots)
		{
			return false;
		}
		if (AVeloxBotController* Old = Ft->Bot.Get())
		{
			Ft->LogBotCost();
			Old->OnLinkFinished.Remove(Ft->LinkHandle);
			Bots->KickBot(Old->GetBotName());
		}
		const bool bHard = Ft->Case == FCorridorFt::ECase::Hard;
		AVeloxBotController* Bot = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(bHard ? TEXT("Hard") : TEXT("Easy")), bHard ? TEXT("FtHard") : TEXT("FtEasy"));
		AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		if (!Pawn || !Pawn->GetVeloxMovementComponent())
		{
			Ft->Check(false, TEXT("setup"), FString::Printf(TEXT("bot=%s pawn=%s"), *GetNameSafe(Bot), *GetNameSafe(Pawn)));
			return false;
		}
		Ft->Bot = Bot;
		Bot->SetFrozen(true);
		PlaceAtStart(*Pawn, CorridorStart);
		Ft->MaxGroundSpeed = Pawn->GetVeloxMovementComponent()->GetSimParams().MaxGroundSpeed;
		Ft->SettleUntil = Ft->Now() + SettleSeconds;
		Ft->bRunning = false;
		Ft->PeakSpeed = 0.f;
		Ft->MaxSpeed = 0.f;
		Ft->Samples = 0;
		return true;
	}

	bool CorridorTick(const TSharedRef<FCorridorFt>& Ft)
	{
		AVeloxBotController* Bot = Ft->Bot.Get();
		const AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		const UVeloxMovementComponent* Movement = Pawn ? Pawn->GetVeloxMovementComponent() : nullptr;
		if (!Movement || !Ft->World.IsValid())
		{
			Ft->Check(false, TEXT("alive"), TEXT("bot pawn gone"));
			return false;
		}
		const bool bHard = Ft->Case == FCorridorFt::ECase::Hard;
		const double T = Ft->Now();
		if (!Ft->bRunning)
		{
			if (T < Ft->SettleUntil || !Movement->IsOnGround())
			{
				return true;
			}
			Ft->bRunning = true;
			Ft->RunStart = T;
			Bot->SetFrozen(false);
			Bot->SetForcedPointGoal(CorridorGoal);
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s case=%s start at=%s goal=%s"), Ft->Name, bHard ? TEXT("Hard") : TEXT("Easy"), *Pawn->GetActorLocation().ToCompactString(),
				*CorridorGoal.ToCompactString());
			return true;
		}
		const FVector Location = Pawn->GetActorLocation();
		const float Speed = static_cast<float>(Movement->Velocity.Size2D());
		++Ft->Samples;
		Ft->MaxSpeed = FMath::Max(Ft->MaxSpeed, Speed);
		const bool bInCorridor = Location.X < CorridorEndX && FMath::Abs(Location.Y) < CorridorHalfWidth;
		if (bInCorridor)
		{
			Ft->PeakSpeed = FMath::Max(Ft->PeakSpeed, Speed);
		}
		const double Elapsed = T - Ft->RunStart;
		const bool bArrived = FVector::Dist2D(Location, CorridorGoal) <= ArriveRadiusCm || Bot->GetBotState().bGoalArrived;
		const double Limit = bHard ? HardLimitSeconds : EasyLimitSeconds;
		if (!bArrived && Elapsed <= Limit + TimeoutMarginSeconds)
		{
			return true;
		}
		const FString Detail = FString::Printf(TEXT("seconds=%.2f limit=%.1f peak=%.0f max=%.0f ground=%.0f factor=%.3f samples=%d end=%s"), Elapsed, Limit, Ft->PeakSpeed, Ft->MaxSpeed,
			Ft->MaxGroundSpeed, Ft->MaxGroundSpeed > 0.f ? Ft->PeakSpeed / Ft->MaxGroundSpeed : 0.f, Ft->Samples, *Location.ToCompactString());
		if (bHard)
		{
			Ft->Check(bArrived && Elapsed <= HardLimitSeconds, TEXT("hard_arrival"), Detail);
			Ft->Check(Ft->PeakSpeed >= HardPeakFactor * Ft->MaxGroundSpeed, TEXT("hard_peak_speed"), Detail);
			Ft->Case = FCorridorFt::ECase::Easy;
			return CorridorStartCase(Ft);
		}
		Ft->Check(bArrived && Elapsed <= EasyLimitSeconds, TEXT("easy_arrival"), Detail);
		Ft->Check(Ft->MaxSpeed <= Ft->MaxGroundSpeed + EasySpeedMarginCmS, TEXT("easy_max_speed"), Detail);
		Ft->Case = FCorridorFt::ECase::Done;
		return false;
	}

	TSharedPtr<FCorridorFt> GCorridorFt;

	void CorridorFtCommand(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		if (!Bots)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtStrafeCorridor: needs the authority world of L_MovementTest (-game)."));
			return;
		}
		// A steering test: the bot must not fight the local player (task 05 added combat to every bot).
		if (IConsoleVariable* Peaceful = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.bots.Peaceful")))
		{
			Peaceful->Set(1, ECVF_SetByCode);
		}
		TSharedRef<FCorridorFt> Ft = MakeShared<FCorridorFt>();
		Ft->Name = TEXT("FT_BotStrafeCorridor");
		Ft->World = World;
		Ft->bQuit = ParseQuit(Args);
		GCorridorFt = Ft;
		TWeakPtr<FCorridorFt> Weak = Ft;
		Bots->CallWhenNavReady([Weak]()
		{
			const TSharedPtr<FCorridorFt> F = Weak.Pin();
			UWorld* W = F ? F->World.Get() : nullptr;
			if (!W)
			{
				return;
			}
			// The local player's pawn must not stand in the corridor.
			const APlayerController* PC = W->GetFirstPlayerController();
			if (AVeloxCharacter* Local = PC ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr)
			{
				const FVector L = Local->GetActorLocation();
				if (L.X > -200.0 && L.X < CorridorEndX && FMath::Abs(L.Y) < CorridorHalfWidth + 100.0)
				{
					PlaceAtStart(*Local, LocalPawnParking);
				}
			}
			if (!CorridorStartCase(F.ToSharedRef()))
			{
				F->Finish();
				return;
			}
			F->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				const TSharedPtr<FCorridorFt> Inner = Weak.Pin();
				if (!Inner || Inner->bFinished)
				{
					return false;
				}
				if (!CorridorTick(Inner.ToSharedRef()))
				{
					Inner->Finish();
					return false;
				}
				return true;
			}));
		});
	}

	FAutoConsoleCommandWithWorldAndArgs CorridorFtCmd(TEXT("velox.bots.FtStrafeCorridor"),
		TEXT("velox.bots.FtStrafeCorridor [quit]: FT_BotStrafeCorridor on L_MovementTest (-game -nullrhi -UseFixedTimeStep -FPS=60): Hard strafe-jumps, Easy walks. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CorridorFtCommand));
}
#endif // !UE_BUILD_SHIPPING

// ------------------------------------------------------------------------------------------------ task 06: FT_BotItemRouting
//   velox.bots.FtItemRouting  (console, `UnrealEditor-Cmd Velox.uproject L_ItemTest -game -nullrhi -UseFixedTimeStep
//                             -FPS=60 -ExecCmds="velox.bots.Seed 1, velox.bots.FtItemRouting quit"`): a Hard bot with every
//                             weapon, full ammo except one rocket, 200 health and 200 armor, so the only item worth going
//                             for is Ammo.Rockets on the rocket-jump platform (the launcher pickup is removed). Checks: the router chooses it with the Full
//                             filter and a full path (rocket-jump link) within 10 s; at most one routing FindPathSync per
//                             server frame throughout; the health is then set to 60 (< MinHealthForRocketJump 70): within
//                             3 s a `VeloxBotFilter from=Full to=Hop` change, an evaluation with the Hop filter, the rockets
//                             no longer reachable (partial path, score 0) and the goal off the rockets. The attribute writes
//                             are the test harness; the bot only gets input.

#if !UE_BUILD_SHIPPING

#include "Bots/VeloxBotItemRouting.h"
#include "Bots/VeloxNavFilters.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxHealthSet.h"
#include "GameModes/VeloxPlayerState.h"
#include "Weapons/VeloxWeaponCheats.h"
#include "Weapons/VeloxWeaponDefinition.h"

namespace VeloxBotRoutingFt
{
	constexpr double ChooseLimitSeconds = 10.0;
	constexpr double SwitchLimitSeconds = 3.0;
	constexpr float HurtHealth = 60.f;

	struct FRoutingFt : VeloxBotFt::FFtBase
	{
		enum class EStep : uint8 { Choose, Switch, Done };
		EStep Step = EStep::Choose;
		double StepStart = 0.0;
		double HurtTime = -1.0;
		TWeakObjectPtr<AVeloxPickup> Rockets;
		int64 LastPathCount = 0;
		int32 MaxPathsPerFrame = 0;
		int32 Frames = 0;
		/** Distinct evaluation records seen (the last record's time changes). */
		int32 Evaluations = 0;
		double LastEvaluationTime = -1.0;
	};

	TSharedPtr<FRoutingFt> GRoutingFt;

	UVeloxAbilitySystemComponent* BotAsc(const AVeloxBotController* Bot)
	{
		const AVeloxPlayerState* PS = Bot ? Bot->GetPlayerState<AVeloxPlayerState>() : nullptr;
		return PS ? PS->GetVeloxAbilitySystemComponent() : nullptr;
	}

	/** The rockets candidate of the last evaluation, or null. */
	const FVeloxGoalItemCandidate* RocketsCandidate(const FVeloxBotRouteRecord& Record, const AVeloxPickup* Rockets)
	{
		for (int32 i = 0; i < Record.Candidates.Num(); ++i)
		{
			if (Record.CandidatePickups.IsValidIndex(i) && Record.CandidatePickups[i].Get() == Rockets)
			{
				return &Record.Candidates[i];
			}
		}
		return nullptr;
	}

	bool Tick(const TSharedRef<FRoutingFt>& F)
	{
		AVeloxBotController* Bot = F->Bot.Get();
		UVeloxBotItemRouter* Router = UVeloxBotItemRouter::Get(F->World.Get());
		if (!Bot || !Router || !F->Rockets.IsValid())
		{
			F->Check(false, TEXT("alive"), TEXT("world, bot or router gone"));
			return false;
		}
		// D10: one routing path query per server frame (the ticker runs once per frame).
		const int64 Count = Router->GetPathQueryCount();
		F->MaxPathsPerFrame = FMath::Max(F->MaxPathsPerFrame, static_cast<int32>(Count - F->LastPathCount));
		F->LastPathCount = Count;
		++F->Frames;

		const FVeloxBotRouteRecord* Last = Router->FindLastEvaluation(Bot);
		if (Last && Last->Time != F->LastEvaluationTime)
		{
			F->LastEvaluationTime = Last->Time;
			++F->Evaluations;
		}
		const double Elapsed = F->Now() - F->StepStart;
		if (F->Step == FRoutingFt::EStep::Choose)
		{
			const bool bChosen = Last && Last->Time >= F->StepStart && Bot->GetBotState().Goal.Item.Get() == F->Rockets.Get();
			if (bChosen)
			{
				const FVeloxGoalItemCandidate* C = RocketsCandidate(*Last, F->Rockets.Get());
				F->Check(Last->Filter == UVeloxNavFilter_Full::StaticClass(), TEXT("full_filter"), FString::Printf(TEXT("seconds=%.2f"), Elapsed));
				F->Check(C && C->bReachable && C->Score > 0.f, TEXT("rockets_reachable_full"),
					FString::Printf(TEXT("score=%.2f travel=%.2f"), C ? C->Score : -1.f, C ? C->TravelSeconds : -1.f));
				// Hurt the bot below MinHealthForRocketJump before it reaches the link.
				if (UVeloxAbilitySystemComponent* ASC = BotAsc(Bot))
				{
					ASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), HurtHealth);
				}
				F->Step = FRoutingFt::EStep::Switch;
				F->StepStart = F->Now();
				return true;
			}
			if (Elapsed > ChooseLimitSeconds)
			{
				F->Check(false, TEXT("rockets_chosen"), FString::Printf(TEXT("timeout %.1f s: %s"), Elapsed, *Bot->DescribeForDebug()));
				return false;
			}
			return true;
		}
		if (F->Step == FRoutingFt::EStep::Switch)
		{
			const bool bHop = Bot->GetNavFilter() == UVeloxNavFilter_Hop::StaticClass();
			const bool bEvaluatedHop = Last && Last->Time >= F->StepStart && Last->Filter == UVeloxNavFilter_Hop::StaticClass();
			const bool bOffRockets = Bot->GetBotState().Goal.Item.Get() != F->Rockets.Get();
			if (bHop && bEvaluatedHop && bOffRockets)
			{
				const FVeloxGoalItemCandidate* C = RocketsCandidate(*Last, F->Rockets.Get());
				F->Check(true, TEXT("hop_filter"), FString::Printf(TEXT("seconds=%.2f"), Elapsed));
				// Either queried again and found partial (score 0), or skipped as known-unreachable (not a candidate).
				F->Check(!C || (!C->bReachable && C->Score == 0.f), TEXT("rockets_unreachable_hop"),
					FString::Printf(TEXT("candidate=%d reachable=%d score=%.2f"), C ? 1 : 0, C && C->bReachable ? 1 : 0, C ? C->Score : 0.f));
				F->Check(true, TEXT("goal_switched"), FString::Printf(TEXT("goal=%s item=%s"), LexToString(Bot->GetBotState().Goal.Type),
					*GetNameSafe(Bot->GetBotState().Goal.Item.Get())));
				F->Check(F->MaxPathsPerFrame <= 1, TEXT("paths_per_frame"), FString::Printf(TEXT("max=%d frames=%d total=%lld"), F->MaxPathsPerFrame, F->Frames, Router->GetPathQueryCount()));
				// F5: one path query per frame and PathQueryCandidates queries per evaluation, so a peaceful run needs at most
				// 3 x K frames per evaluation (the 04b run without Peaceful took 542 frames).
				const int32 FrameLimit = FMath::CeilToInt32(3.f * UVeloxBotSettings::Get().PathQueryCandidates) * FMath::Max(F->Evaluations, 1);
				F->Check(F->Frames <= FrameLimit, TEXT("frames"), FString::Printf(TEXT("frames=%d limit=%d evaluations=%d"), F->Frames, FrameLimit, F->Evaluations));
				F->Step = FRoutingFt::EStep::Done;
				return false;
			}
			if (Elapsed > SwitchLimitSeconds)
			{
				F->Check(false, TEXT("hop_switch"), FString::Printf(TEXT("timeout %.1f s hop=%d evaluated_hop=%d off_rockets=%d: %s"), Elapsed, bHop ? 1 : 0, bEvaluatedHop ? 1 : 0,
					bOffRockets ? 1 : 0, *Bot->DescribeForDebug()));
				return false;
			}
		}
		return true;
	}

	void Start(const TSharedRef<FRoutingFt>& Ft)
	{
		UWorld* World = Ft->World.Get();
		UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		UVeloxBotItemRouter* Router = UVeloxBotItemRouter::Get(World);
		Ft->Rockets = VeloxBotFt::FirstPickup(World, VeloxGameplayTags::Item_Ammo_Rockets);
		AVeloxBotController* Bot = Bots ? Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Hard")), TEXT("FtRoute")) : nullptr;
		Ft->Bot = Bot;
		AVeloxPlayerState* PS = Bot ? Bot->GetPlayerState<AVeloxPlayerState>() : nullptr;
		UVeloxAbilitySystemComponent* ASC = BotAsc(Bot);
		if (!Bot || !Bot->GetPawn() || !PS || !ASC || !Router || !Ft->Rockets.IsValid() || !VeloxWeaponCheats::GiveAllWeapons(*PS))
		{
			Ft->Check(false, TEXT("setup"), FString::Printf(TEXT("bot=%s router=%d rockets=%s"), *GetNameSafe(Bot), Router ? 1 : 0, *GetNameSafe(Ft->Rockets.Get())));
			Ft->Finish();
			return;
		}
		// Nothing worth going for but the rockets (harness writes, before the bot's first evaluation lands). The rocket
		// launcher pickup also grants rockets (weapon need 0.3 x ammo need at base value 60 beats the ammo's 10): the
		// fixture removes it.
		if (AVeloxPickup* Launcher = VeloxBotFt::FirstPickup(World, VeloxGameplayTags::Item_Weapon_RocketLauncher))
		{
			Launcher->Destroy();
		}
		ASC->SetNumericAttributeBase(UVeloxAmmoSet::GetRocketsAttribute(), 1.f);
		ASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), 200.f);
		ASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), 200.f);
		Router->RequestEvaluation(Bot);
		Ft->StepStart = Ft->Now();
		Ft->LastPathCount = Router->GetPathQueryCount();
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s start bot=%s at=%s rockets=%s"), Ft->Name, *Bot->GetBotName(), *Bot->GetPawn()->GetActorLocation().ToCompactString(),
			*Ft->Rockets->GetActorLocation().ToCompactString());
		TWeakPtr<FRoutingFt> Weak = Ft;
		Ft->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
		{
			const TSharedPtr<FRoutingFt> F = Weak.Pin();
			if (!F || F->bFinished)
			{
				return false;
			}
			if (!Tick(F.ToSharedRef()))
			{
				F->Finish();
				return false;
			}
			return true;
		}));
	}

	void Command(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		if (!Bots)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtItemRouting: needs the authority world of L_ItemTest (-game)."));
			return;
		}
		// A routing test: the bot must not fight the local player (every visibility change would re-trigger an evaluation, F5).
		if (IConsoleVariable* Peaceful = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.bots.Peaceful")))
		{
			Peaceful->Set(1, ECVF_SetByCode);
		}
		TSharedRef<FRoutingFt> Ft = MakeShared<FRoutingFt>();
		Ft->Name = TEXT("FT_BotItemRouting");
		Ft->World = World;
		Ft->bQuit = VeloxBotFt::ParseQuit(Args);
		GRoutingFt = Ft;
		TWeakPtr<FRoutingFt> Weak = Ft;
		Bots->CallWhenNavReady([Weak]()
		{
			if (const TSharedPtr<FRoutingFt> F = Weak.Pin())
			{
				Start(F.ToSharedRef());
			}
		});
	}

	FAutoConsoleCommandWithWorldAndArgs Cmd(TEXT("velox.bots.FtItemRouting"),
		TEXT("velox.bots.FtItemRouting [quit]: FT_BotItemRouting on L_ItemTest (-game -nullrhi -UseFixedTimeStep -FPS=60): rocket-jump route chosen with _Full, "
			 "dropped with _Hop under MinHealthForRocketJump, one routing path query per frame. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));
}

#endif // !UE_BUILD_SHIPPING

// ------------------------------------------------------------------------------------------------ task 04b: FT_BotOffMesh
//   velox.bots.FtOffMesh  (console, `UnrealEditor-Cmd Velox.uproject L_ItemTest -game -nullrhi -UseFixedTimeStep -FPS=60
//                         -ExecCmds="velox.bots.Seed 1, velox.bots.FtOffMesh quit"`): a Medium bot (peaceful) is put where
//                         the nav mesh does not reach: the 100 cm strips between the corridor wall and the north sides of
//                         the RA, Mega and ramp-top platforms (closed to the south and north, open to the east and west;
//                         the ramp-top case touches the wall; its exit runs under the ramp slab, D18), and the top
//                         of the central cross cover (200 cm up, too narrow for the agent radius). Each case has a point
//                         goal south of it. Checks per case: a `VeloxBotOffMesh ... recovered` (the controller's recovery
//                         count rises) within 10 s and the goal reached within 25 s. The placement is the test harness;
//                         the bot only gets input.
//                         Case "under_slab" (ADR 0005 D41, task 04i-B): a thin static slab is staged over open floor (the
//                         nav mesh rebuilt at runtime), its underside above the capsule but below the agent height, so
//                         the floor under it carries no mesh while the default query extent (Z 200) from the feet still
//                         reaches the slab top (setup check). The bot stands beneath it with a forced Item goal. Checks:
//                         no `recovered` while the feet are deeper inside the footprint than the on-mesh extent can see
//                         past the eroded edge (half size - (extent XY - AgentRadius)), every recovery target Z <= feet Z
//                         + step height, the bot out of the footprint and `recovered` within 10 s, the item within 25 s.
//                         Every case (ADR 0005 D44): no `result=no_nav_point` (FVeloxBotState::OffMeshNoNavPoints unchanged).
//                         Cases "slot_centre" / "slot_tangent" (D45): the worst point of the 100 cm slot north of the ramp
//                         top (exits ~525/530 cm away along the wall); slot_tangent with OffMeshRecoverDirections 0 (no ring
//                         probe: the swept fallback is blocked by the corridor wall and the tangent probes find the exit).
//                         Checks: recovered on attempt 1, no stuck strike, no `no_clear_point`, the recovery target south of
//                         Y 1450 and >= 500 cm along X; slot_tangent: exactly one `target=tangent`.

#if !UE_BUILD_SHIPPING

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "StaticMeshCompiler.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

namespace VeloxBotOffMeshFt
{
	struct FCase
	{
		const TCHAR* Label;
		/** Floor point (feet) the pawn is put on. */
		FVector Start;
		/** Point goal on the mesh. */
		FVector Goal;
		/**
		 * The goal is asserted. Since task 04c-b (ADR 0005 D18) also for the ramp strip: its exit runs under the ramp slab,
		 * where a bunny-hopping bot used to hit the slab and oscillate; the hop headroom guard makes it walk there.
		 */
		bool bCheckGoal = true;
		/** D41: stage the slab over Start; the goal is then an Item goal, the Armor Shard nearest to Goal. */
		bool bUnderSlab = false;
		/** D45 test hook: the case's bot gets OffMeshRecoverDirections 0 (no ring probe; fallback + tangent only). */
		bool bNoRings = false;
		/** D45 slot checks: first attempt, no strike, no no_clear_point, exit along the slot. */
		bool bSlotChecks = false;
	};

	/**
	 * D46 item 2: the sealed D17 room of L_ItemTest (create_item_test_map.py ROOM_*): slots X 4100..5300, slot A Y 1600..1700
	 * (north wall; 207 cm block south with a 48 cm step along X 4100..4300), slot B Y 600..700 (south wall, 207 cm block north), lane Y 1000..1300 between.
	 */
	constexpr double RoomSlotCentreX = 4700.0;
	constexpr double RoomSlotAY1 = 1700.0;
	constexpr double RoomSlotBY0 = 600.0;
	/** Lane centre: both slot ends are meshed end zones joined by the lane. */
	const FVector RoomGoal(4700.0, 1150.0, 0.0);

	/** L_ItemTest (Tools/editor/create_item_test_map.py): corridor wall y 1500..1600, platforms y 1000..1400, cross 200 high. */
	const FCase Cases[] = {
		{ TEXT("ra_strip"), FVector(2100.0, 1450.0, 0.0), FVector(2100.0, 500.0, 0.0), true },
		{ TEXT("mega_strip"), FVector(-1500.0, 1450.0, 0.0), FVector(-1500.0, 500.0, 0.0), true },
		// D46 item 2: sealed D17 room (create_item_test_map.py build_d17_room). ramp_strip_wall moved with the slots: slot B
		// (207 cm block on its north side, the room wall on its south side), start 37.5 cm from the wall as before.
		{ TEXT("ramp_strip_wall"), FVector(RoomSlotCentreX, RoomSlotBY0 + 37.5, 0.0), RoomGoal, true },
		// D45: the slot's worst point (exits ~600 cm away along X), with the ring search and without it. Slot A: 207 cm
		// block on its south side (48 cm step along its west 200 cm), the room wall on its north side.
		{ TEXT("slot_centre"), FVector(RoomSlotCentreX, RoomSlotAY1 - 37.5, 0.0), RoomGoal, true, false, false, true },
		{ TEXT("slot_tangent"), FVector(RoomSlotCentreX, RoomSlotAY1 - 37.5, 0.0), RoomGoal, true, false, true, true },
		{ TEXT("cross_top"), FVector(0.0, 200.0, 200.0), FVector(800.0, 600.0, 0.0), true },
		// Open floor west of the cross, clear of the grid (y -1700), the Mega pad and PlayerStart_5 (-1200, -900); the
		// goal is the Armor Shard on the grid (-2250, -1700).
		{ TEXT("under_slab"), FVector(-1500.0, -500.0, 0.0), FVector(-2250.0, -1700.0, 0.0), true, true },
	};

	/** The under_slab case's slab: 20 cm thick, footprint centred on the case's Start. */
	constexpr double SlabHalfSizeCm = 300.0;
	constexpr double SlabThicknessCm = 20.0;
	/**
	 * Underside above the floor: above the capsule (140) so the bot stands beneath it, below what Recast needs for a
	 * walkable span (AgentHeight 140 in CellHeight 10 cells plus rounding), so the floor beneath has no mesh: the D41 void.
	 */
	constexpr double SlabUndersideCm = 145.0;
	/** Nav rebuild after staging: at least this long, then until the navigation build is idle (s). */
	constexpr double SlabNavSettleSeconds = 3.0;
	constexpr double SlabNavLimitSeconds = 15.0;
	/** Part A's ramp probes (D41), logged with both extents for the record. */
	const FVector RampProbes[] = { FVector(10.0, 1190.0, 10.0), FVector(135.0, 1198.0, 10.0), FVector(-300.0, 1200.0, 10.0) };
	/** D45 slot exit (room-relative since D46): the recovery target lies on or south of slot A's centre line (the open room)... */
	constexpr double SlotExitMaxY = 1650.0;
	/** ...and at least this far along X from the start (every slot exit is ring-600 along the wall). */
	constexpr double SlotExitMinAlongCm = 500.0;
	constexpr double SettleSeconds = 0.5;
	constexpr double RecoverLimitSeconds = 10.0;
	constexpr double ArriveLimitSeconds = 25.0;
	constexpr double ArriveRadiusCm = 120.0;

	struct FOffMeshFt : VeloxBotFt::FFtBase
	{
		int32 CaseIndex = 0;
		bool bRunning = false;
		bool bRecovered = false;
		double SettleUntil = 0.0;
		double RunStart = 0.0;
		double RecoveredAt = -1.0;
		int32 RecoveriesAtStart = 0;
		/** D44: `result=no_nav_point` count when the case started (asserted unchanged per case). */
		int32 NoNavPointsAtStart = 0;
		/** D45 slot checks: counters at the start, the per-tick observations and the target at `recovered`. */
		int32 NoClearPointsAtStart = 0;
		int32 TangentTargetsAtStart = 0;
		int32 MaxAttempts = 0;
		int32 LastStrikes = 0;
		int32 StrikeRises = 0;
		FVector ExitTarget = FVector::ZeroVector;
		/** The pawn the case started with (a respawn mid-case fails it). */
		TWeakObjectPtr<APawn> CasePawn;
		/** under_slab: the staged slab, when the nav rebuild may be over, the item goal and the D41 observations. */
		TWeakObjectPtr<AStaticMeshActor> Slab;
		double SlabStagedAt = -1.0;
		TWeakObjectPtr<AVeloxPickup> ItemGoal;
		int32 EarlyRecoveries = 0;
		int32 TargetsChecked = 0;
		int32 TargetsAbove = 0;
		double MaxTargetRiseCm = -1.0e9;
		FVector LastTarget = FVector::ZeroVector;
		double LeftSlabAt = -1.0;

		void OnFinish() override
		{
			if (AStaticMeshActor* S = Slab.Get())
			{
				S->Destroy();
			}
		}
	};

	AStaticMeshActor* StageSlab(UWorld& World, const FVector& FloorCentre)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
#if WITH_EDITOR
		// An editor binary may still be compiling the mesh; a compiling mesh is not navigation relevant (no nav rebuild).
		UStaticMesh* const Meshes[] = { Cube };
		FStaticMeshCompilingManager::Get().FinishCompilation(Meshes);
#endif
		const FTransform Transform(FRotator::ZeroRotator, FloorCentre + FVector(0.0, 0.0, SlabUndersideCm + SlabThicknessCm * 0.5),
			FVector(SlabHalfSizeCm * 2.0 / 100.0, SlabHalfSizeCm * 2.0 / 100.0, SlabThicknessCm / 100.0));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Slab = World.SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, Params);
		if (!Slab)
		{
			return nullptr;
		}
		// The component is registered at spawn and a registered Static component refuses a new mesh (it would not reach
		// the nav octree): Movable, which still blocks and still feeds the runtime nav mesh (SetStaticMesh updates it).
		UStaticMeshComponent* Mesh = Slab->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		return Mesh->GetStaticMesh() == Cube ? Slab : nullptr;
	}

	bool InsideSlab(const FVector& Point, const FVector& Centre, double Inset)
	{
		const double Half = SlabHalfSizeCm - Inset;
		return FMath::Abs(Point.X - Centre.X) <= Half && FMath::Abs(Point.Y - Centre.Y) <= Half;
	}

	AVeloxPickup* NearestPickup(UWorld* World, const FGameplayTag& Tag, const FVector& Near)
	{
		const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		AVeloxPickup* Best = nullptr;
		double BestDist = TNumericLimits<double>::Max();
		for (AVeloxPickup* Pickup : Registry ? Registry->FindByTag(Tag) : TArray<AVeloxPickup*>())
		{
			const double Dist = Pickup ? FVector::Dist2D(Pickup->GetActorLocation(), Near) : BestDist;
			if (Dist < BestDist)
			{
				Best = Pickup;
				BestDist = Dist;
			}
		}
		return Best;
	}

	/** One projection with both extents, for the setup check and the log. */
	FString ProbeLabel(const UNavigationSystemV1& NavSys, const ANavigationData& NavData, const FVector& Point, const FVector& Extent, FNavLocation& Out, bool& bOutFound)
	{
		bOutFound = NavSys.ProjectPointToNavigation(Point, Out, Extent, &NavData);
		return bOutFound ? Out.Location.ToCompactString() : FString(TEXT("none"));
	}

	bool StartCase(const TSharedRef<FOffMeshFt>& Ft)
	{
		UWorld* World = Ft->World.Get();
		UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		if (!Bots)
		{
			return false;
		}
		if (AVeloxBotController* Old = Ft->Bot.Get())
		{
			Ft->LogBotCost();
			Bots->KickBot(Old->GetBotName());
		}
		AVeloxBotController* Bot = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Medium")), TEXT("FtOffMesh"));
		AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		if (!Pawn)
		{
			Ft->Check(false, TEXT("setup"), FString::Printf(TEXT("bot=%s"), *GetNameSafe(Bot)));
			return false;
		}
		Ft->Bot = Bot;
		Ft->CasePawn = Pawn;
		Bot->SetFrozen(true);
		const FCase& Case = Cases[Ft->CaseIndex];
		if (Case.bUnderSlab)
		{
			AStaticMeshActor* Slab = StageSlab(*World, Case.Start);
			AVeloxPickup* Item = NearestPickup(World, FGameplayTag(VeloxGameplayTags::Item_Armor_Shard), Case.Goal);
			if (!Slab || !Item)
			{
				Ft->Check(false, TEXT("under_slab_setup"), FString::Printf(TEXT("slab=%s item=%s"), *GetNameSafe(Slab), *GetNameSafe(Item)));
				return false;
			}
			Ft->Slab = Slab;
			Ft->ItemGoal = Item;
			Ft->SlabStagedAt = Ft->Now();
			Ft->EarlyRecoveries = 0;
			Ft->TargetsChecked = 0;
			Ft->TargetsAbove = 0;
			Ft->MaxTargetRiseCm = -1.0e9;
			Ft->LastTarget = FVector::ZeroVector;
			Ft->LeftSlabAt = -1.0;
		}
		if (Case.bNoRings)
		{
			// Test hook (D45): the freshly added bot only; it is kicked at the next case.
			Bot->OffMeshRecoverDirections = 0;
		}
		VeloxBotFt::PlaceAtStart(*Pawn, Case.Start);
		Ft->SettleUntil = Ft->Now() + (Case.bUnderSlab ? SlabNavSettleSeconds : SettleSeconds);
		Ft->bRunning = false;
		Ft->bRecovered = false;
		Ft->RecoveredAt = -1.0;
		return true;
	}

	/**
	 * under_slab, once the nav rebuild is idle: the setup check (the floor under the slab is a D41 void: nothing within
	 * the shared on-mesh extent, while the default query extent reaches the slab top above step height) and the ramp
	 * probes' log. False fails the case's setup.
	 */
	bool CheckSlabVoid(const TSharedRef<FOffMeshFt>& Ft, const AVeloxCharacter& Pawn)
	{
		UWorld* World = Ft->World.Get();
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		const ANavigationData* NavData = Bots ? Bots->GetNavData() : nullptr;
		if (!NavSys || !NavData)
		{
			Ft->Check(false, TEXT("under_slab_setup_void"), TEXT("no nav"));
			return false;
		}
		const FVector Shared = UVeloxBotItemRouter::GetOnMeshExtent();
		for (const FVector& Probe : RampProbes)
		{
			FNavLocation Out;
			bool bDefault = false;
			bool bShared = false;
			const FString Default = ProbeLabel(*NavSys, *NavData, Probe, INVALID_NAVEXTENT, Out, bDefault);
			const FString SharedLabel = ProbeLabel(*NavSys, *NavData, Probe, Shared, Out, bShared);
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s probe at=%s default_extent=%s shared_extent=%s (%s)"), Ft->Name, *Probe.ToCompactString(), *Default, *SharedLabel,
				*Shared.ToCompactString());
		}
		const FVector Feet = Pawn.GetNavAgentLocation();
		const float StepHeight = Pawn.GetVeloxMovementComponent()->MaxStepHeight;
		FNavLocation DefaultOut;
		FNavLocation SharedOut;
		bool bDefault = false;
		bool bShared = false;
		const FString Default = ProbeLabel(*NavSys, *NavData, Feet, INVALID_NAVEXTENT, DefaultOut, bDefault);
		const FString SharedLabel = ProbeLabel(*NavSys, *NavData, Feet, Shared, SharedOut, bShared);
		FNavLocation TopOut;
		bool bTop = false;
		const FVector SlabTop = Cases[Ft->CaseIndex].Start + FVector(0.0, 0.0, SlabUndersideCm + SlabThicknessCm + 10.0);
		const FString TopLabel = ProbeLabel(*NavSys, *NavData, SlabTop, FVector(50.0, 50.0, 50.0), TopOut, bTop);
		const bool bVoid = !bShared && bDefault && DefaultOut.Location.Z > Feet.Z + StepHeight;
		const UStaticMeshComponent* SlabMesh = Ft->Slab.IsValid() ? Ft->Slab->GetStaticMeshComponent() : nullptr;
		Ft->Check(bVoid, TEXT("under_slab_setup_void"), FString::Printf(TEXT("feet=%s default_extent=%s shared_extent=%s slab_top=%s slab_nav_relevant=%d mesh_nav=%d query_collision=%d nav_wait_s=%.2f"), *Feet.ToCompactString(),
			*Default, *SharedLabel, *TopLabel, SlabMesh && SlabMesh->IsNavigationRelevant() ? 1 : 0,
			SlabMesh && SlabMesh->GetStaticMesh() && SlabMesh->GetStaticMesh()->IsNavigationRelevant() ? 1 : 0, SlabMesh && SlabMesh->IsQueryCollisionEnabled() ? 1 : 0,
			Ft->Now() - Ft->SlabStagedAt));
		return bVoid;
	}

	/** under_slab, every tick while running: the D41 observations. */
	void ObserveSlab(const TSharedRef<FOffMeshFt>& Ft, const AVeloxBotController& Bot, const AVeloxCharacter& Pawn, double Elapsed)
	{
		const FCase& Case = Cases[Ft->CaseIndex];
		const FVeloxBotState& State = Bot.GetBotState();
		const FVector Feet = Pawn.GetNavAgentLocation();
		const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(UVeloxBotSubsystem::Get(Ft->World.Get()) ? UVeloxBotSubsystem::Get(Ft->World.Get())->GetNavData() : nullptr);
		const double AgentRadius = Recast ? Recast->AgentRadius : 0.0;
		// Deeper inside than the on-mesh query can see past the eroded edge: here "on the mesh" can only be the slab top.
		const double BlindInset = FMath::Max(0.0, UVeloxBotItemRouter::GetOnMeshExtent().X - AgentRadius);
		if (State.OffMeshRecoveries > Ft->RecoveriesAtStart && !Ft->bRecovered && InsideSlab(Feet, Case.Start, BlindInset))
		{
			++Ft->EarlyRecoveries;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s case=%s recovered_under_slab at=%s blind_inset=%.0f"), Ft->Name, Case.Label, *Feet.ToCompactString(), BlindInset);
		}
		if (State.bOffMeshRecovery && !State.OffMeshLastTarget.Equals(Ft->LastTarget, 0.1))
		{
			Ft->LastTarget = State.OffMeshLastTarget;
			const double Rise = State.OffMeshLastTarget.Z - Feet.Z;
			++Ft->TargetsChecked;
			Ft->MaxTargetRiseCm = FMath::Max(Ft->MaxTargetRiseCm, Rise);
			if (Rise > Pawn.GetVeloxMovementComponent()->MaxStepHeight)
			{
				++Ft->TargetsAbove;
			}
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s case=%s recovery_target to=%s feet=%s rise=%.1f"), Ft->Name, Case.Label, *State.OffMeshLastTarget.ToCompactString(),
				*Feet.ToCompactString(), Rise);
		}
		if (Ft->LeftSlabAt < 0.0 && !InsideSlab(Feet, Case.Start, 0.0))
		{
			Ft->LeftSlabAt = Elapsed;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s case=%s left_slab_s=%.2f at=%s"), Ft->Name, Case.Label, Elapsed, *Feet.ToCompactString());
		}
	}

	bool Tick(const TSharedRef<FOffMeshFt>& Ft)
	{
		AVeloxBotController* Bot = Ft->Bot.Get();
		const AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		const UVeloxMovementComponent* Movement = Pawn ? Pawn->GetVeloxMovementComponent() : nullptr;
		if (!Movement || !Ft->World.IsValid())
		{
			Ft->Check(false, TEXT("alive"), TEXT("bot pawn gone"));
			return false;
		}
		const FCase& Case = Cases[Ft->CaseIndex];
		const double T = Ft->Now();
		if (!Ft->bRunning)
		{
			if (T < Ft->SettleUntil || !Movement->IsOnGround())
			{
				return true;
			}
			if (Case.bUnderSlab)
			{
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Ft->World.Get());
				if (NavSys && NavSys->IsNavigationBuildInProgress() && T - Ft->SlabStagedAt < SlabNavLimitSeconds)
				{
					return true;
				}
				if (!CheckSlabVoid(Ft, *Pawn))
				{
					++Ft->CaseIndex;
					return Ft->CaseIndex < UE_ARRAY_COUNT(Cases) && StartCase(Ft);
				}
			}
			Ft->bRunning = true;
			Ft->RunStart = T;
			Ft->RecoveriesAtStart = Bot->GetBotState().OffMeshRecoveries;
			Ft->NoNavPointsAtStart = Bot->GetBotState().OffMeshNoNavPoints;
			Ft->NoClearPointsAtStart = Bot->GetBotState().OffMeshNoClearPoints;
			Ft->TangentTargetsAtStart = Bot->GetBotState().OffMeshTangentTargets;
			Ft->MaxAttempts = 0;
			Ft->LastStrikes = Bot->GetBotState().StuckStrikes;
			Ft->StrikeRises = 0;
			Ft->ExitTarget = FVector::ZeroVector;
			Bot->SetFrozen(false);
			if (Case.bUnderSlab)
			{
				Bot->SetForcedItemGoal(Ft->ItemGoal.Get());
			}
			else
			{
				Bot->SetForcedPointGoal(Case.Goal);
			}
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s case=%s start at=%s goal=%s"), Ft->Name, Case.Label, *Pawn->GetActorLocation().ToCompactString(),
				Case.bUnderSlab && Ft->ItemGoal.IsValid() ? *Ft->ItemGoal->GetActorLocation().ToCompactString() : *Case.Goal.ToCompactString());
			return true;
		}
		const double Elapsed = T - Ft->RunStart;
		if (Case.bUnderSlab)
		{
			ObserveSlab(Ft, *Bot, *Pawn, Elapsed);
		}
		if (Case.bSlotChecks)
		{
			// D45: attempts until recovered; strikes compared tick to tick (the counter wraps at StuckStrikesToRoam).
			const FVeloxBotState& St = Bot->GetBotState();
			if (!Ft->bRecovered)
			{
				Ft->MaxAttempts = FMath::Max(Ft->MaxAttempts, St.OffMeshAttempts);
			}
			Ft->StrikeRises += St.StuckStrikes > Ft->LastStrikes ? 1 : 0;
			Ft->LastStrikes = St.StuckStrikes;
		}
		if (!Ft->bRecovered && Bot->GetBotState().OffMeshRecoveries > Ft->RecoveriesAtStart)
		{
			Ft->bRecovered = true;
			Ft->RecoveredAt = Elapsed;
			Ft->ExitTarget = Bot->GetBotState().OffMeshLastTarget;
		}
		if (!Case.bCheckGoal && Ft->bRecovered && Elapsed >= Ft->RecoveredAt + SettleSeconds)
		{
			// Recovery only: back on the mesh and still there half a second later.
			Ft->Check(Ft->RecoveredAt <= RecoverLimitSeconds, *FString::Printf(TEXT("%s_recovered"), Case.Label),
				FString::Printf(TEXT("case=%s recovered_s=%.2f at=%s"), Case.Label, Ft->RecoveredAt, *Pawn->GetActorLocation().ToCompactString()));
			const int32 NoNav = Bot->GetBotState().OffMeshNoNavPoints - Ft->NoNavPointsAtStart;
			Ft->Check(NoNav == 0, *FString::Printf(TEXT("%s_no_nav_point"), Case.Label), FString::Printf(TEXT("case=%s no_nav_points=%d"), Case.Label, NoNav));
			++Ft->CaseIndex;
			return Ft->CaseIndex < UE_ARRAY_COUNT(Cases) && StartCase(Ft);
		}
		const FVector Location = Pawn->GetActorLocation();
		const bool bSamePawn = Pawn == Ft->CasePawn.Get();
		const FVector GoalAt = Case.bUnderSlab && Ft->ItemGoal.IsValid() ? Ft->ItemGoal->GetActorLocation() : Case.Goal;
		const bool bArrived = bSamePawn && (FVector::Dist2D(Location, GoalAt) <= ArriveRadiusCm || Bot->GetBotState().bGoalArrived);
		if (bSamePawn && !bArrived && Elapsed <= ArriveLimitSeconds)
		{
			return true;
		}
		const FString Detail = FString::Printf(TEXT("case=%s recovered_s=%.2f arrived=%d same_pawn=%d seconds=%.2f end=%s"), Case.Label, Ft->RecoveredAt, bArrived ? 1 : 0,
			bSamePawn ? 1 : 0, Elapsed, *Location.ToCompactString());
		Ft->Check(Ft->bRecovered && Ft->RecoveredAt <= RecoverLimitSeconds, *FString::Printf(TEXT("%s_recovered"), Case.Label), Detail);
		// D44: no attempt of the case may end without a reachable nav point (a respawned pawn starts a new tally: -1 fails).
		const int32 NoNav = bSamePawn ? Bot->GetBotState().OffMeshNoNavPoints - Ft->NoNavPointsAtStart : -1;
		Ft->Check(NoNav == 0, *FString::Printf(TEXT("%s_no_nav_point"), Case.Label), FString::Printf(TEXT("case=%s no_nav_points=%d"), Case.Label, NoNav));
		if (Case.bSlotChecks)
		{
			const FVeloxBotState& St = Bot->GetBotState();
			const int32 NoClear = bSamePawn ? St.OffMeshNoClearPoints - Ft->NoClearPointsAtStart : -1;
			const int32 Tangents = bSamePawn ? St.OffMeshTangentTargets - Ft->TangentTargetsAtStart : -1;
			const FString SlotDetail = FString::Printf(TEXT("case=%s max_attempts=%d strike_rises=%d no_clear_points=%d tangent_targets=%d exit_target=%s recovered_s=%.2f"), Case.Label,
				Ft->MaxAttempts, Ft->StrikeRises, NoClear, Tangents, *Ft->ExitTarget.ToCompactString(), Ft->RecoveredAt);
			Ft->Check(Ft->bRecovered && Ft->MaxAttempts == 1, *FString::Printf(TEXT("%s_first_attempt"), Case.Label), SlotDetail);
			Ft->Check(Ft->StrikeRises == 0, *FString::Printf(TEXT("%s_no_strike"), Case.Label), SlotDetail);
			Ft->Check(NoClear == 0, *FString::Printf(TEXT("%s_no_clear_point"), Case.Label), SlotDetail);
			Ft->Check(Ft->bRecovered && Ft->ExitTarget.Y <= SlotExitMaxY && FMath::Abs(Ft->ExitTarget.X - Case.Start.X) >= SlotExitMinAlongCm,
				*FString::Printf(TEXT("%s_exit"), Case.Label), SlotDetail);
			if (Case.bNoRings)
			{
				Ft->Check(Tangents == 1, *FString::Printf(TEXT("%s_tangent"), Case.Label), SlotDetail);
			}
		}
		if (Case.bCheckGoal)
		{
			Ft->Check(bArrived && Elapsed <= ArriveLimitSeconds, *FString::Printf(TEXT("%s_goal"), Case.Label), Detail);
		}
		if (Case.bUnderSlab)
		{
			const FString SlabDetail = FString::Printf(TEXT("case=%s early_recoveries=%d targets=%d targets_above_step=%d max_target_rise=%.1f left_slab_s=%.2f recovered_s=%.2f"), Case.Label,
				Ft->EarlyRecoveries, Ft->TargetsChecked, Ft->TargetsAbove, Ft->MaxTargetRiseCm, Ft->LeftSlabAt, Ft->RecoveredAt);
			Ft->Check(Ft->EarlyRecoveries == 0, TEXT("under_slab_no_recovered_under"), SlabDetail);
			Ft->Check(Ft->TargetsChecked > 0 && Ft->TargetsAbove == 0, TEXT("under_slab_target_z"), SlabDetail);
			Ft->Check(Ft->LeftSlabAt >= 0.0 && Ft->LeftSlabAt <= RecoverLimitSeconds, TEXT("under_slab_left"), SlabDetail);
			if (AStaticMeshActor* Slab = Ft->Slab.Get())
			{
				Slab->Destroy();
			}
		}
		++Ft->CaseIndex;
		if (Ft->CaseIndex >= UE_ARRAY_COUNT(Cases))
		{
			return false;
		}
		return StartCase(Ft);
	}

	TSharedPtr<FOffMeshFt> GOffMeshFt;

	void Command(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		if (!Bots)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtOffMesh: needs the authority world of L_ItemTest (-game)."));
			return;
		}
		// A navigation test: the bot must not fight the local player.
		if (IConsoleVariable* Peaceful = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.bots.Peaceful")))
		{
			Peaceful->Set(1, ECVF_SetByCode);
		}
		// The warmup's end resets the level (every pawn respawns); enter Live first so no reset lands inside a case.
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("velox.match.Phase Live"));
		}
		TSharedRef<FOffMeshFt> Ft = MakeShared<FOffMeshFt>();
		Ft->Name = TEXT("FT_BotOffMesh");
		Ft->World = World;
		Ft->bQuit = VeloxBotFt::ParseQuit(Args);
		GOffMeshFt = Ft;
		TWeakPtr<FOffMeshFt> Weak = Ft;
		Bots->CallWhenNavReady([Weak]()
		{
			const TSharedPtr<FOffMeshFt> F = Weak.Pin();
			if (!F)
			{
				return;
			}
			if (!StartCase(F.ToSharedRef()))
			{
				F->Finish();
				return;
			}
			F->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				const TSharedPtr<FOffMeshFt> Inner = Weak.Pin();
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

	FAutoConsoleCommandWithWorldAndArgs Cmd(TEXT("velox.bots.FtOffMesh"),
		TEXT("velox.bots.FtOffMesh [quit]: FT_BotOffMesh on L_ItemTest (-game -nullrhi -UseFixedTimeStep -FPS=60): a bot put off the nav mesh (wall strips, cover top) recovers and reaches its goal. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));
}

#endif // !UE_BUILD_SHIPPING

// --------------------------------------------------------------------------------------- task 04g: FT_BotSegmentBlocked
//   velox.bots.FtSegmentBlocked  (console, `UnrealEditor-Cmd Velox.uproject L_ItemTest -game -nullrhi -UseFixedTimeStep
//                                -FPS=60 -ExecCmds="velox.bots.Seed 1, velox.bots.FtSegmentBlocked quit"`; ADR 0005 D35): a
//                                Medium bot (peaceful) stands on the floor, gets a forced item goal and, once its move is
//                                active, is put into an inner corner ("pocket") of the central cross, from where the
//                                straight line to its stale segment target crosses a cross face. Checks per case: a
//                                `VeloxBotRepath ... reason=segment_blocked` (the controller's repath count rises) within
//                                1 s of the placement, no stuck strike for the whole case, 300 cm (2-D) away from the
//                                pocket within 3 s and the item reached within 25 s. Case 3 "edge hug" (ADR 0005 D43, task
//                                04i-B): the bot stands in the eroded band against the cross's north face (feet on the
//                                y ~ 114 mesh edge) and gets a live item goal south-west, so its first segment runs along
//                                that boundary edge to the arm's end: no `segment_blocked` repath and no stuck strike for
//                                3 s (and no repath for the whole case), and the item reached within 25 s (the regression test for the 2 cm ray inset and the
//                                guard for the shared on-mesh extent). The placement is the test harness; the bot only
//                                gets input.

#if !UE_BUILD_SHIPPING

namespace VeloxBotSegmentBlockedFt
{
	struct FCase
	{
		const TCHAR* Label;
		/** Floor point (feet) the bot starts on. */
		FVector Start;
		/** Index into GoalTag(): the forced item goal. */
		int32 GoalIndex;
		/** The item nearest to this point among those with the goal tag. */
		FVector GoalNear;
		/** Floor point (feet) in a cross pocket: the capsule touches both faces of the inner corner (cross arms 100 wide). */
		FVector Pocket;
		/** D43 edge hug: no pocket placement; Start is in the eroded band and the live path's first segment hugs the edge. */
		bool bEdgeHug = false;
	};

	/** L_ItemTest (Tools/editor/create_item_test_map.py): cross arms 600 x 100, 200 high, at the origin. */
	const FCase Cases[] = {
		{ TEXT("nw_pocket_rl"), FVector(-1000.0, 500.0, 0.0), 0, FVector(1050.0, -1700.0, 0.0), FVector(-87.5, 87.5, 0.0) },
		{ TEXT("se_pocket_ya"), FVector(1000.0, -500.0, 0.0), 1, FVector(-214.0, 1200.0, 0.0), FVector(87.5, -87.5, 0.0) },
		// North face of the X arm (y 50; capsule 37.5 -> feet y >= 87.5; mesh edge y ~ 114 after AgentRadius 45 erosion),
		// between the Y arm and the arm's west end; the Armor Shard (grid, -2250, -1700) lies behind the arm.
		{ TEXT("edge_hug_shard"), FVector(-150.0, 95.0, 0.0), 2, FVector(-2250.0, -1700.0, 0.0), FVector::ZeroVector, true },
	};

	FGameplayTag GoalTag(int32 Index)
	{
		switch (Index)
		{
		case 0: return FGameplayTag(VeloxGameplayTags::Item_Weapon_RocketLauncher);
		case 1: return FGameplayTag(VeloxGameplayTags::Item_Armor_Yellow);
		default: return FGameplayTag(VeloxGameplayTags::Item_Armor_Shard);
		}
	}

	constexpr double SettleSeconds = 0.5;
	/** The move must become active this soon after the goal is set (setup check). */
	constexpr double PathLimitSeconds = 5.0;
	constexpr double RepathLimitSeconds = 1.0;
	constexpr double LeaveDistanceCm = 300.0;
	constexpr double LeaveLimitSeconds = 3.0;
	/** Edge hug: no segment_blocked repath and no strike in this window after the goal is set. */
	constexpr double EdgeHugQuietSeconds = 3.0;
	/** Edge hug setup check: the first segment's end lies on the edge (|dY| from the projected start) and this far west. */
	constexpr double EdgeHugEdgeToleranceCm = 15.0;
	constexpr double EdgeHugMinSegmentCm = 100.0;
	/** FtOffMesh's limits. */
	constexpr double ArriveLimitSeconds = 25.0;
	constexpr double ArriveRadiusCm = 120.0;

	struct FSegmentBlockedFt : VeloxBotFt::FFtBase
	{
		enum class EStep : uint8 { Settle, WaitPath, Placed };
		int32 CaseIndex = 0;
		EStep Step = EStep::Settle;
		double SettleUntil = 0.0;
		double GoalSetAt = 0.0;
		double PlacedAt = 0.0;
		double RepathAt = -1.0;
		double LeaveAt = -1.0;
		int32 RepathsAtPlace = 0;
		int32 OffMeshAtPlace = 0;
		int32 LastStrikes = 0;
		int32 Strikes = 0;
		bool bTaken = false;
		TWeakObjectPtr<AVeloxPickup> Goal;
		TWeakObjectPtr<APawn> CasePawn;
		FDelegateHandle TakenHandle;

		void OnFinish() override
		{
			if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World.Get()))
			{
				Registry->OnPickupTaken.Remove(TakenHandle);
			}
		}
	};

	AVeloxPickup* NearestPickup(UWorld* World, const FGameplayTag& Tag, const FVector& Near)
	{
		const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		AVeloxPickup* Best = nullptr;
		double BestDist = TNumericLimits<double>::Max();
		for (AVeloxPickup* Pickup : Registry ? Registry->FindByTag(Tag) : TArray<AVeloxPickup*>())
		{
			const double Dist = Pickup ? FVector::Dist2D(Pickup->GetActorLocation(), Near) : BestDist;
			if (Dist < BestDist)
			{
				Best = Pickup;
				BestDist = Dist;
			}
		}
		return Best;
	}

	bool StartCase(const TSharedRef<FSegmentBlockedFt>& Ft)
	{
		UWorld* World = Ft->World.Get();
		UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		if (!Bots)
		{
			return false;
		}
		if (AVeloxBotController* Old = Ft->Bot.Get())
		{
			Ft->LogBotCost();
			Bots->KickBot(Old->GetBotName());
		}
		const FCase& Case = Cases[Ft->CaseIndex];
		AVeloxPickup* Goal = NearestPickup(World, GoalTag(Case.GoalIndex), Case.GoalNear);
		AVeloxBotController* Bot = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Medium")), TEXT("FtSegmentBlocked"));
		AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		if (!Pawn || !Goal)
		{
			Ft->Check(false, TEXT("setup"), FString::Printf(TEXT("case=%s bot=%s goal=%s"), Case.Label, *GetNameSafe(Bot), *GetNameSafe(Goal)));
			return false;
		}
		Ft->Bot = Bot;
		Ft->CasePawn = Pawn;
		Ft->Goal = Goal;
		Bot->SetFrozen(true);
		VeloxBotFt::PlaceAtStart(*Pawn, Case.Start);
		Ft->Step = FSegmentBlockedFt::EStep::Settle;
		Ft->SettleUntil = Ft->Now() + SettleSeconds;
		Ft->RepathAt = -1.0;
		Ft->LeaveAt = -1.0;
		Ft->LastStrikes = 0;
		Ft->Strikes = 0;
		Ft->bTaken = false;
		return true;
	}

	/** Ends the current case with its checks; false when the test is over. */
	bool EndCase(const TSharedRef<FSegmentBlockedFt>& Ft, const FString& Why)
	{
		const FCase& Case = Cases[Ft->CaseIndex];
		const AVeloxBotController* Bot = Ft->Bot.Get();
		const APawn* Pawn = Bot ? Bot->GetPawn() : nullptr;
		const double Elapsed = Ft->Now() - Ft->PlacedAt;
		const int32 OffMesh = Bot ? Bot->GetBotState().OffMeshRecoveries - Ft->OffMeshAtPlace : -1;
		const FString Detail = FString::Printf(TEXT("case=%s why=%s repath_s=%.2f leave_s=%.2f strikes=%d repaths=%d offmesh_recoveries=%d taken=%d seconds=%.2f end=%s"), Case.Label, *Why,
			Ft->RepathAt, Ft->LeaveAt, Ft->Strikes, Bot ? Bot->GetRepathCount() - Ft->RepathsAtPlace : -1, OffMesh, Ft->bTaken ? 1 : 0, Elapsed,
			Pawn ? *Pawn->GetActorLocation().ToCompactString() : TEXT("-"));
		if (Case.bEdgeHug)
		{
			// D43: a segment along a boundary edge is not blocked (the 2 cm inset): no repath in the first 3 s (the edge), and
			// none on the rest of the walk either (task 04i-B: without the inset the edge itself reads clear on this mesh,
			// but the walk's later stretch in the west wall's eroded band repaths on a t = 0 hit at ~3.1 s).
			Ft->Check(Ft->RepathAt < 0.0 || Ft->RepathAt > EdgeHugQuietSeconds, *FString::Printf(TEXT("%s_no_repath"), Case.Label), Detail);
			Ft->Check(Ft->RepathAt < 0.0, *FString::Printf(TEXT("%s_no_repath_case"), Case.Label), Detail);
		}
		else
		{
			Ft->Check(Ft->RepathAt >= 0.0 && Ft->RepathAt <= RepathLimitSeconds, *FString::Printf(TEXT("%s_repath"), Case.Label), Detail);
			Ft->Check(Ft->LeaveAt >= 0.0 && Ft->LeaveAt <= LeaveLimitSeconds, *FString::Printf(TEXT("%s_leave"), Case.Label), Detail);
		}
		Ft->Check(Ft->Strikes == 0, *FString::Printf(TEXT("%s_no_stuck"), Case.Label), Detail);
		Ft->Check(Why == TEXT("arrived") && Elapsed <= ArriveLimitSeconds, *FString::Printf(TEXT("%s_goal"), Case.Label), Detail);
		++Ft->CaseIndex;
		return Ft->CaseIndex < UE_ARRAY_COUNT(Cases) && StartCase(Ft);
	}

	bool Tick(const TSharedRef<FSegmentBlockedFt>& Ft)
	{
		AVeloxBotController* Bot = Ft->Bot.Get();
		AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		const UVeloxMovementComponent* Movement = Pawn ? Pawn->GetVeloxMovementComponent() : nullptr;
		const UVeloxBotPathFollowingComponent* Follow = Bot ? Bot->GetBotPathFollowing() : nullptr;
		if (!Movement || !Follow || !Ft->World.IsValid())
		{
			Ft->Check(false, TEXT("alive"), TEXT("bot pawn gone"));
			return false;
		}
		const FCase& Case = Cases[Ft->CaseIndex];
		const double T = Ft->Now();
		// No stuck strike for the whole case (the counter only rises on a strike; it resets on arrival / the third strike).
		const int32 StrikesNow = Bot->GetBotState().StuckStrikes;
		if (StrikesNow > Ft->LastStrikes)
		{
			Ft->Strikes += StrikesNow - Ft->LastStrikes;
		}
		Ft->LastStrikes = StrikesNow;

		switch (Ft->Step)
		{
		case FSegmentBlockedFt::EStep::Settle:
			if (T < Ft->SettleUntil || !Movement->IsOnGround())
			{
				return true;
			}
			Bot->SetFrozen(false);
			Bot->SetForcedItemGoal(Ft->Goal.Get());
			Ft->LastStrikes = Bot->GetBotState().StuckStrikes;
			Ft->Strikes = 0;
			Ft->GoalSetAt = T;
			Ft->Step = FSegmentBlockedFt::EStep::WaitPath;
			if (Case.bEdgeHug)
			{
				// The window starts with the goal: every segment check of the live path counts.
				Ft->PlacedAt = T;
				Ft->RepathsAtPlace = Bot->GetRepathCount();
				Ft->OffMeshAtPlace = Bot->GetBotState().OffMeshRecoveries;
			}
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s case=%s start at=%s goal=%s goal_at=%s"), Ft->Name, Case.Label, *Pawn->GetActorLocation().ToCompactString(),
				*GetNameSafe(Ft->Goal.Get()), Ft->Goal.IsValid() ? *Ft->Goal->GetActorLocation().ToCompactString() : TEXT("-"));
			return true;
		case FSegmentBlockedFt::EStep::WaitPath:
		{
			const FNavigationPath* Path = Follow->GetPath().Get();
			if (!Bot->IsMoveActive() || Follow->GetStatus() != EPathFollowingStatus::Moving || !Path || !Path->IsValid())
			{
				if (T - Ft->GoalSetAt > PathLimitSeconds)
				{
					Ft->Check(false, *FString::Printf(TEXT("%s_path"), Case.Label), FString::Printf(TEXT("no active move after %.1f s"), PathLimitSeconds));
					++Ft->CaseIndex;
					return Ft->CaseIndex < UE_ARRAY_COUNT(Cases) && StartCase(Ft);
				}
				return true;
			}
			if (Case.bEdgeHug)
			{
				// No placement: the live path from the eroded band. Setup check: its first segment runs along the edge
				// (start and segment end on one y within the tolerance, the end at least EdgeHugMinSegmentCm west).
				const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
				const FVector PathStart = Points.Num() > 0 ? Points[0].Location : FVector::ZeroVector;
				const FVector SegmentEnd = Points.Num() > 1 ? Points[1].Location : PathStart;
				const bool bAlongEdge = FMath::Abs(SegmentEnd.Y - PathStart.Y) <= EdgeHugEdgeToleranceCm && PathStart.X - SegmentEnd.X >= EdgeHugMinSegmentCm;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s case=%s live at=%s path_points=%d path_start=%s first_segment_end=%s along_edge=%d"), Ft->Name, Case.Label,
					*Pawn->GetActorLocation().ToCompactString(), Points.Num(), *PathStart.ToCompactString(), *SegmentEnd.ToCompactString(), bAlongEdge ? 1 : 0);
				Ft->Check(bAlongEdge, *FString::Printf(TEXT("%s_setup_edge"), Case.Label),
					FString::Printf(TEXT("path_start=%s first_segment_end=%s"), *PathStart.ToCompactString(), *SegmentEnd.ToCompactString()));
				Ft->Step = FSegmentBlockedFt::EStep::Placed;
				return true;
			}
			// Test harness: the pawn is put into the pocket at rest and the stale path stays (no PlaceAtStart: its
			// StopMovementImmediately would abort the move through the nav movement interface).
			const FVector From = Pawn->GetActorLocation();
			Pawn->SetActorLocation(Case.Pocket + FVector(0.0, 0.0, Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0), false, nullptr, ETeleportType::TeleportPhysics);
			if (UVeloxMovementComponent* PawnMovement = Pawn->GetVeloxMovementComponent())
			{
				PawnMovement->Velocity = FVector::ZeroVector;
			}
			Ft->PlacedAt = T;
			Ft->RepathsAtPlace = Bot->GetRepathCount();
			Ft->OffMeshAtPlace = Bot->GetBotState().OffMeshRecoveries;
			Ft->Step = FSegmentBlockedFt::EStep::Placed;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s case=%s placed from=%s at=%s path_points=%d segment_target=%s"), Ft->Name, Case.Label, *From.ToCompactString(),
				*Pawn->GetActorLocation().ToCompactString(), Path->GetPathPoints().Num(), *Follow->GetCurrentTargetLocation().ToCompactString());
			return true;
		}
		case FSegmentBlockedFt::EStep::Placed:
			break;
		}

		const double Elapsed = T - Ft->PlacedAt;
		const FVector Location = Pawn->GetActorLocation();
		if (Ft->RepathAt < 0.0 && Bot->GetRepathCount() > Ft->RepathsAtPlace)
		{
			Ft->RepathAt = Elapsed;
			const FVeloxSegmentBlockInfo& Block = Follow->GetLastSegmentBlock();
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s case=%s repath_s=%.2f from=%s target=%s hit=%s remaining=%.0f"), Ft->Name, Case.Label, Elapsed,
				*Block.From.ToCompactString(), *Block.Target.ToCompactString(), *Block.Hit.ToCompactString(), Block.RemainingCm);
		}
		if (!Case.bEdgeHug && Ft->LeaveAt < 0.0 && FVector::Dist2D(Location, Case.Pocket) >= LeaveDistanceCm)
		{
			Ft->LeaveAt = Elapsed;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFt test=%s case=%s leave_s=%.2f at=%s"), Ft->Name, Case.Label, Elapsed, *Location.ToCompactString());
		}
		if (Pawn != Ft->CasePawn.Get())
		{
			return EndCase(Ft, TEXT("respawned"));
		}
		const AVeloxPickup* Goal = Ft->Goal.Get();
		const bool bArrived = Ft->bTaken || (Goal && FVector::Dist2D(Location, Goal->GetActorLocation()) <= ArriveRadiusCm) || Bot->GetBotState().bGoalArrived;
		if (bArrived)
		{
			return EndCase(Ft, TEXT("arrived"));
		}
		if (Elapsed > ArriveLimitSeconds)
		{
			return EndCase(Ft, TEXT("timeout"));
		}
		return true;
	}

	TSharedPtr<FSegmentBlockedFt> GSegmentBlockedFt;

	void Command(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		if (!Bots || !Registry)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtSegmentBlocked: needs the authority world of L_ItemTest (-game)."));
			return;
		}
		// A navigation test: the bot must not fight the local player.
		if (IConsoleVariable* Peaceful = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.bots.Peaceful")))
		{
			Peaceful->Set(1, ECVF_SetByCode);
		}
		// The warmup's end resets the level (every pawn respawns); enter Live first so no reset lands inside a case.
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("velox.match.Phase Live"));
		}
		TSharedRef<FSegmentBlockedFt> Ft = MakeShared<FSegmentBlockedFt>();
		Ft->Name = TEXT("FT_BotSegmentBlocked");
		Ft->World = World;
		Ft->bQuit = VeloxBotFt::ParseQuit(Args);
		GSegmentBlockedFt = Ft;
		TWeakPtr<FSegmentBlockedFt> Weak = Ft;
		Ft->TakenHandle = Registry->OnPickupTaken.AddLambda([Weak](AVeloxPickup* Pickup, APlayerState* TakenBy)
		{
			const TSharedPtr<FSegmentBlockedFt> F = Weak.Pin();
			const AVeloxBotController* B = F ? F->Bot.Get() : nullptr;
			if (F && B && !F->bFinished && F->Step == FSegmentBlockedFt::EStep::Placed && TakenBy == B->PlayerState && Pickup == F->Goal.Get())
			{
				F->bTaken = true;
			}
		});
		Bots->CallWhenNavReady([Weak]()
		{
			const TSharedPtr<FSegmentBlockedFt> F = Weak.Pin();
			if (!F)
			{
				return;
			}
			if (!StartCase(F.ToSharedRef()))
			{
				F->Finish();
				return;
			}
			F->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				const TSharedPtr<FSegmentBlockedFt> Inner = Weak.Pin();
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

	FAutoConsoleCommandWithWorldAndArgs Cmd(TEXT("velox.bots.FtSegmentBlocked"),
		TEXT("velox.bots.FtSegmentBlocked [quit]: FT_BotSegmentBlocked on L_ItemTest (-game -nullrhi -UseFixedTimeStep -FPS=60): a bot put into a cross pocket with a stale path repaths within 1 s (no stuck strike), leaves and reaches its item; a bot hugging a mesh edge does not repath. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));
}

#endif // !UE_BUILD_SHIPPING

// ------------------------------------------------------------------------------------------- task 04c-c: FT_BotCombatEvents
//   velox.bots.FtCombatEvents  (console, `UnrealEditor-Cmd Velox.uproject L_CombatTest -game -nullrhi -UseFixedTimeStep
//                              -FPS=60 -ExecCmds="velox.bots.Seed 1, velox.bots.FtCombatEvents quit"`; ADR 0005 D15): the
//                              arena's dummies are removed. Case 1 (hear): a Hard bot standing still (forced goal on its own
//                              spot) takes 10 damage with a second, frozen bot behind it as the instigator: the hearing
//                              reaction is armed within ReactionSeconds x 1.3 (VeloxBotHear, the combat state's heard
//                              counter) and the bot's yaw is within 20 degrees of the instigator inside 1 s. Case 2
//                              (dodge): a rocket spawned from the rocket launcher definition 1500 cm away flying at the bot
//                              (owner = the second bot): VeloxBotDodge (dodge counter) within ProjectileDodgeReactionSeconds
//                              + 0.1 s and a lateral speed >= 200 cm/s inside 0.5 s of it. The placement, the damage and
//                              the rocket are the test harness; the bot only gets input.

#if !UE_BUILD_SHIPPING

#include "Bots/VeloxDummyController.h"
#include "Bots/VeloxDummySpawner.h"
#include "Combat/VeloxGameplayEffects.h"
#include "EngineUtils.h"
#include "Weapons/VeloxProjectile.h"

namespace VeloxBotCombatEventsFt
{
	/** Open floor of L_CombatTest between the cover blocks (Tools/editor/create_combat_test_map.py). */
	const FVector BotSpot(0.0, 650.0, 0.0);
	constexpr double SettleSeconds = 0.7;
	constexpr double RestSpeedCmS = 20.0;
	constexpr double RestTimeoutSeconds = 8.0;
	constexpr int32 HearDamage = 10;
	constexpr double DummyBehindCm = 500.0;
	constexpr double TurnLimitSeconds = 1.0;
	constexpr double TurnToleranceDeg = 20.0;
	constexpr double HearFactor = 1.3;
	constexpr double RocketDistanceCm = 1500.0;
	constexpr double DodgeSlackSeconds = 0.1;
	constexpr double LateralWindowSeconds = 0.5;
	constexpr double LateralMinCmS = 200.0;
	constexpr double CaseTimeoutSeconds = 4.0;

	struct FCombatEventsFt : VeloxBotFt::FFtBase
	{
		int32 CaseIndex = 0;
		/** 0 settle, 1 dummy placed (damage / rocket next), 2 observing. */
		int32 Phase = 0;
		double PhaseStart = 0.0;
		TWeakObjectPtr<AVeloxBotController> Dummy;
		int32 CountAtEvent = 0;
		double EventTime = 0.0;
		double EventYawToDummy = 0.0;
		bool bCounted = false;
		double CountedAt = 0.0;
		double MaxLateral = 0.0;
		TWeakObjectPtr<AVeloxProjectile> Rocket;
	};

	bool StartCase(const TSharedRef<FCombatEventsFt>& Ft)
	{
		UWorld* World = Ft->World.Get();
		UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		if (!Bots)
		{
			return false;
		}
		if (AVeloxBotController* Old = Ft->Bot.Get())
		{
			Ft->LogBotCost();
			Bots->KickBot(Old->GetBotName());
		}
		if (AVeloxBotController* OldDummy = Ft->Dummy.Get())
		{
			Bots->KickBot(OldDummy->GetBotName());
		}
		AVeloxBotController* Bot = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Hard")), TEXT("FtEventsBot"));
		AVeloxBotController* Dummy = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Easy")), TEXT("FtEventsDummy"));
		AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		AVeloxCharacter* DummyPawn = Dummy ? Cast<AVeloxCharacter>(Dummy->GetPawn()) : nullptr;
		AVeloxPlayerState* DummyPS = Dummy ? Dummy->GetPlayerState<AVeloxPlayerState>() : nullptr;
		if (!Pawn || !DummyPawn || !DummyPS || !VeloxWeaponCheats::GiveAllWeapons(*DummyPS))
		{
			Ft->Check(false, TEXT("setup"), FString::Printf(TEXT("bot=%s dummy=%s"), *GetNameSafe(Bot), *GetNameSafe(Dummy)));
			return false;
		}
		Ft->Bot = Bot;
		Ft->Dummy = Dummy;
		Dummy->SetFrozen(true);
		// Out of every sight line until placed: the far corner.
		VeloxBotFt::PlaceAtStart(*DummyPawn, FVector(1800.0, -1800.0, 0.0));
		VeloxBotFt::PlaceAtStart(*Pawn, BotSpot);
		Bot->SetForcedPointGoal(BotSpot);
		Ft->Phase = 0;
		Ft->PhaseStart = Ft->Now();
		Ft->bCounted = false;
		Ft->MaxLateral = 0.0;
		return true;
	}

	bool Advance(const TSharedRef<FCombatEventsFt>& Ft)
	{
		++Ft->CaseIndex;
		Ft->Phase = 0;
		return Ft->CaseIndex < 2 && StartCase(Ft);
	}

	bool Tick(const TSharedRef<FCombatEventsFt>& Ft)
	{
		AVeloxBotController* Bot = Ft->Bot.Get();
		AVeloxBotController* Dummy = Ft->Dummy.Get();
		AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		AVeloxCharacter* DummyPawn = Dummy ? Cast<AVeloxCharacter>(Dummy->GetPawn()) : nullptr;
		const UVeloxMovementComponent* Movement = Pawn ? Pawn->GetVeloxMovementComponent() : nullptr;
		UWorld* World = Ft->World.Get();
		if (!Movement || !DummyPawn || !World)
		{
			Ft->Check(false, TEXT("alive"), TEXT("bot or dummy pawn gone"));
			return false;
		}
		const double T = Ft->Now();
		const FVeloxBotCombatState& Combat = Bot->GetCombatState();
		const bool bHearCase = Ft->CaseIndex == 0;

		if (Ft->Phase == 0)
		{
			// At rest on its spot: the bot may carry its first path's speed past the goal and come back, so wait for the arrival.
			if (T - Ft->PhaseStart < SettleSeconds || !Movement->IsOnGround() || Movement->GetSpeed2D() > RestSpeedCmS || !Bot->GetBotState().bGoalArrived)
			{
				if (T - Ft->PhaseStart > RestTimeoutSeconds)
				{
					Ft->Check(false, TEXT("rest"), FString::Printf(TEXT("speed=%.0f arrived=%d at=%s"), Movement->GetSpeed2D(), Bot->GetBotState().bGoalArrived ? 1 : 0, *Pawn->GetActorLocation().ToCompactString()));
					return false;
				}
				return true;
			}
			// Behind the bot (opposite its view), outside the perception FOV.
			const FVector Back = -FRotator(0.f, Bot->GetBotState().View.Yaw, 0.f).Vector();
			const FVector Spot = Pawn->GetActorLocation() + Back * DummyBehindCm;
			VeloxBotFt::PlaceAtStart(*DummyPawn, FVector(Spot.X, Spot.Y, 0.0));
			Ft->Phase = 1;
			Ft->PhaseStart = T;
			return true;
		}
		if (Ft->Phase == 1)
		{
			if (T - Ft->PhaseStart < 0.2)
			{
				return true;
			}
			UVeloxAbilitySystemComponent* DummyASC = DummyPawn->GetVeloxAbilitySystemComponent();
			UVeloxAbilitySystemComponent* BotASC = Pawn->GetVeloxAbilitySystemComponent();
			if (!DummyASC || !BotASC)
			{
				Ft->Check(false, TEXT("asc"), TEXT("no ability system"));
				return false;
			}
			Ft->EventTime = T;
			if (bHearCase)
			{
				Ft->CountAtEvent = Combat.HeardCount;
				FVeloxDamageSpecParams Params;
				Params.Damage = HearDamage;
				Params.DamageTag = VeloxGameplayTags::Damage_MachineGun;
				Params.HitLocation = Pawn->GetActorLocation();
				Params.bNoKnockback = true;
				const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(DummyASC, Params);
				if (!Spec.IsValid())
				{
					Ft->Check(false, TEXT("damage_spec"), TEXT("no spec"));
					return false;
				}
				DummyASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), BotASC);
				const FVector ToDummy = DummyPawn->GetActorLocation() - Pawn->GetActorLocation();
				Ft->EventYawToDummy = ToDummy.Rotation().Yaw;
			}
			else
			{
				Ft->CountAtEvent = Combat.DodgeCount;
				const AVeloxPlayerState* DummyPS = DummyPawn->GetPlayerState<AVeloxPlayerState>();
				const UVeloxWeaponInventoryComponent* Inventory = DummyPS ? DummyPS->GetWeaponInventory() : nullptr;
				const UVeloxWeaponDefinition* Rocket = nullptr;
				for (int32 i = 0; Inventory && i < Inventory->NumWeapons() && !Rocket; ++i)
				{
					const UVeloxWeaponDefinition* Definition = Inventory->GetDefinition(i);
					Rocket = Definition && Definition->WeaponTag == VeloxGameplayTags::Weapon_RocketLauncher ? Definition : nullptr;
				}
				if (!Rocket)
				{
					Ft->Check(false, TEXT("rocket_definition"), TEXT("dummy has no rocket launcher"));
					return false;
				}
				FVeloxProjectileLaunch Launch;
				Launch.Definition = Rocket;
				Launch.Shooter = DummyPawn;
				Launch.SourceASC = DummyASC;
				Launch.Origin = Pawn->GetActorLocation() + FVector(RocketDistanceCm, 0.0, 0.0);
				Launch.Dir = -FVector::ForwardVector;
				AVeloxProjectile* Projectile = AVeloxProjectile::SpawnServerProjectile(*World, Rocket->ProjectileClass, Launch);
				Ft->Rocket = Projectile;
				if (!Projectile)
				{
					Ft->Check(false, TEXT("rocket_spawn"), TEXT("no projectile"));
					return false;
				}
			}
			Ft->Phase = 2;
			Ft->PhaseStart = T;
			return true;
		}

		const double Since = T - Ft->EventTime;
		if (bHearCase)
		{
			if (!Ft->bCounted && Combat.HeardCount > Ft->CountAtEvent)
			{
				Ft->bCounted = true;
				Ft->CountedAt = Since;
				const double Limit = Combat.AimParams.ReactionSeconds * HearFactor;
				Ft->Check(Since <= Limit + 1.0 / 30.0, TEXT("hear"), FString::Printf(TEXT("seconds=%.3f limit=%.3f"), Since, Limit));
			}
			const double YawError = FMath::Abs(FRotator::NormalizeAxis(Bot->GetBotState().View.Yaw - Ft->EventYawToDummy));
			if (Ft->bCounted && YawError <= TurnToleranceDeg)
			{
				Ft->Check(Since <= TurnLimitSeconds, TEXT("turn"), FString::Printf(TEXT("seconds=%.3f yaw_error=%.1f"), Since, YawError));
				return Advance(Ft);
			}
			if (Since > TurnLimitSeconds)
			{
				Ft->Check(Ft->bCounted, TEXT("hear"), FString::Printf(TEXT("counted=%d heard=%d"), Ft->bCounted ? 1 : 0, Combat.HeardCount));
				Ft->Check(false, TEXT("turn"), FString::Printf(TEXT("seconds=%.3f yaw_error=%.1f"), Since, YawError));
				return Advance(Ft);
			}
			return true;
		}

		// Dodge case.
		const double Lateral = FMath::Abs(Pawn->GetVelocity().Y);
		if (!Ft->bCounted && Combat.DodgeCount > Ft->CountAtEvent)
		{
			Ft->bCounted = true;
			Ft->CountedAt = Since;
			const double Limit = Bot->GetDifficultyParams().ProjectileDodgeReactionSeconds + DodgeSlackSeconds;
			Ft->Check(Since <= Limit, TEXT("dodge"), FString::Printf(TEXT("seconds=%.3f limit=%.3f"), Since, Limit));
		}
		if (Ft->bCounted)
		{
			Ft->MaxLateral = FMath::Max(Ft->MaxLateral, Lateral);
			if (Since - Ft->CountedAt >= LateralWindowSeconds || Ft->MaxLateral >= LateralMinCmS)
			{
				Ft->Check(Ft->MaxLateral >= LateralMinCmS, TEXT("lateral"), FString::Printf(TEXT("max_cm_s=%.0f window_s=%.2f"), Ft->MaxLateral, Since - Ft->CountedAt));
				if (AVeloxProjectile* Rocket = Ft->Rocket.Get())
				{
					Rocket->Destroy();
				}
				return Advance(Ft);
			}
		}
		if (Since > CaseTimeoutSeconds)
		{
			Ft->Check(Ft->bCounted, TEXT("dodge"), FString::Printf(TEXT("timeout dodges=%d"), Combat.DodgeCount));
			Ft->Check(false, TEXT("lateral"), TEXT("timeout"));
			return false;
		}
		return true;
	}

	TSharedPtr<FCombatEventsFt> GEventsFt;

	void Command(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		if (!Bots)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtCombatEvents: needs the authority world of L_CombatTest (-game)."));
			return;
		}
		// Hearing needs the bots to fight: Peaceful off.
		if (IConsoleVariable* Peaceful = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.bots.Peaceful")))
		{
			Peaceful->Set(0, ECVF_SetByCode);
		}
		// The arena's strafing dummies are perceived enemies: remove them.
		for (TActorIterator<AVeloxDummySpawner> It(World); It; ++It)
		{
			It->Destroy();
		}
		for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
		{
			// The -game local player's pawn is an enemy too: the Hard bot would fight it (strafe, range keeping) instead of standing still.
			if (Cast<AVeloxDummyController>(It->GetController()) || Cast<APlayerController>(It->GetController()))
			{
				It->Destroy();
			}
		}
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("velox.match.Phase Live"));
		}
		TSharedRef<FCombatEventsFt> Ft = MakeShared<FCombatEventsFt>();
		Ft->Name = TEXT("FT_BotCombatEvents");
		Ft->World = World;
		Ft->bQuit = VeloxBotFt::ParseQuit(Args);
		GEventsFt = Ft;
		TWeakPtr<FCombatEventsFt> Weak = Ft;
		Bots->CallWhenNavReady([Weak]()
		{
			const TSharedPtr<FCombatEventsFt> F = Weak.Pin();
			if (!F)
			{
				return;
			}
			if (!StartCase(F.ToSharedRef()))
			{
				F->Finish();
				return;
			}
			F->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				const TSharedPtr<FCombatEventsFt> Inner = Weak.Pin();
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

	FAutoConsoleCommandWithWorldAndArgs Cmd(TEXT("velox.bots.FtCombatEvents"),
		TEXT("velox.bots.FtCombatEvents [quit]: FT_BotCombatEvents on L_CombatTest (-game -nullrhi -UseFixedTimeStep -FPS=60): a Hard bot hears damage from behind and turns, dodges a rocket. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));
}

#endif // !UE_BUILD_SHIPPING
