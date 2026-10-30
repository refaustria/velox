// Velox - bot controller and navigation tests (ADR 0005 section 14, task M4-04).
//
//   Velox.Bots.ControllerRules   automation, world-free: per-life seed (HashCombine, no collisions where XOR collides),
//                                key presence from a world wish (CPM snap), turn-rate clamp, stuck rule, bot names,
//                                ?Bots= capping, link type -> maneuver type mapping, corner angle.
//   Velox.Bots.FindNearestEnd    automation, world-free: UVeloxBotSubsystem::FindNearestEnd (radius, height, nearest).
//   velox.bots.FtNavigation      FT_BotNavigation (console, `UnrealEditor-Cmd Velox.uproject L_ItemTest -game -nullrhi
//                                -UseFixedTimeStep -FPS=60 -ExecCmds="velox.bots.Seed 1, velox.bots.FtNavigation quit"`):
//                                nav built < 5 s; a Medium bot with the forced goal Velox.Item.Health.Mega traverses the
//                                pad link (VeloxBotLink type=JumpPad result=ok) and takes the Mega within 25 s; then
//                                Velox.Item.Armor.Red from the Mega platform: a Drop traversal and the RA taken within
//                                35 s. The rocket-jump case is task 05's. Logs `VeloxBotFt check=... ok=` and
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
	/** The Mega's pad link ends at the item (create_item_test_map.py). */
	constexpr double MegaLinkEndToleranceCm = 200.0;

	struct FNavFt : FFtBase
	{
		enum class EStep : uint8 { WaitNav, Mega, RedArmor, Done };
		EStep Step = EStep::WaitNav;
		double StepStart = 0.0;
		TWeakObjectPtr<AVeloxPickup> Mega;
		TWeakObjectPtr<AVeloxPickup> RedArmor;
		bool bPadOk = false;
		bool bDropOk = false;
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
		Ft->Mega = FirstPickup(World, VeloxGameplayTags::Item_Health_Mega);
		Ft->RedArmor = FirstPickup(World, VeloxGameplayTags::Item_Armor_Red);
		AVeloxBotController* Bot = Bots ? Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Medium")), TEXT("FtNav")) : nullptr;
		Ft->Bot = Bot;
		if (!Bot || !Bot->GetPawn() || !Ft->Mega.IsValid() || !Ft->RedArmor.IsValid() || !Registry)
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
				F->Step = FNavFt::EStep::Done;
				F->Finish();
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
			const double Limit = F->Step == FNavFt::EStep::Mega ? MegaLimitSeconds : RedArmorLimitSeconds;
			if (Elapsed > Limit)
			{
				F->Check(false, F->Step == FNavFt::EStep::Mega ? TEXT("mega_taken") : TEXT("ra_taken"), FString::Printf(TEXT("timeout after %.1f s: %s"), Elapsed, *F->Bot->DescribeForDebug()));
				F->Finish();
				return false;
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
		TEXT("velox.bots.FtNavigation [quit]: FT_BotNavigation on L_ItemTest (-game -nullrhi -UseFixedTimeStep -FPS=60): pad and drop traversals to Mega and RA. Non-shipping."),
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
