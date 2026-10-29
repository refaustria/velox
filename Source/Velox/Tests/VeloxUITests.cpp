// Velox - HUD tests (ADR 0004 sections 7 and 10, task M3-07).
//
//   Velox.UI.HUDFormat         world-free: clock (Q3 ceil, count up, sudden death, unsynced), powerup seconds (ceil), health
//                              colour bands, weapon bar rows, kill feed line / expiry, phase banner, damage quadrants.
//   Velox.UI.DamageDirections  UVeloxCombatFeedbackComponent records the yaw to the damage source (ring buffer).
//   Velox.UI.HUDWidget         FT_HUDWidget as an automation test: UVeloxHUDWidget under NullRHI in a private world, a
//                              hand-filled UVeloxHUDViewModel, the bound texts / rows / visibility asserted.
//   velox.ui.FtHUDWidget       the same checks as a console command on the running game's world (`-game -nullrhi`), logging
//                              `VeloxUIFt check=... ok=` and `VeloxUIFt result=PASS|FAIL`; `quit` exits with 0/1.
//   velox.ui.FtDamageIndicator end to end on the running game (`-game -nullrhi L_ItemTest`, standalone): TestDamage with a shot
//                              direction of +X hits the local pawn; the server's GameplayCue.Character.Damage (Location = the
//                              source behind the pawn) must reach the feedback component (yaw 180) and the live HUD view model
//                              must show the Back quadrant (control yaw 0) and the widget its indicator. `quit` exits 0/1.
//   velox.ui.DumpHUD           logs the bound values of the live HUD widget of the first local player (`VeloxHUD bound ...`).

#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Character/VeloxCharacter.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "UI/VeloxHUD.h"
#include "UI/VeloxHUDFormat.h"
#include "UI/VeloxHUDViewModel.h"
#include "UI/VeloxHUDWidget.h"
#include "UI/VeloxUISettings.h"
#include "Weapons/VeloxCombatFeedbackComponent.h"

namespace VeloxUiTest
{
	/** Counts and logs checks; the console command and the automation test share it. */
	struct FChecks
	{
		int32 Failed = 0;
		TArray<FString> Failures;

		void Check(bool bOk, const TCHAR* What, const FString& Detail = FString())
		{
			UE_LOG(LogTemp, Log, TEXT("VeloxUIFt check=%s ok=%d %s"), What, bOk ? 1 : 0, *Detail);
			if (!bOk)
			{
				++Failed;
				Failures.Add(FString::Printf(TEXT("%s %s"), What, *Detail));
			}
		}

		void CheckText(const TCHAR* What, const FString& Actual, const FString& Expected)
		{
			Check(Actual == Expected, What, FString::Printf(TEXT("actual='%s' expected='%s'"), *Actual, *Expected));
		}
	};

	FString TextOf(const UTextBlock* Block)
	{
		return Block ? Block->GetText().ToString() : FString(TEXT("<null>"));
	}

	/** The hand-filled model of a player at 87 health with three weapons, two powerups, a feed and a damage from the right. */
	void FillModel(UVeloxHUDViewModel& Model)
	{
		Model.Reset();
		Model.bHasPlayer = true;
		Model.bAlive = true;
		Model.Health = 87;
		Model.Armor = 50;
		Model.MaxHealth = 100;

		TArray<VeloxHUDFormat::FWeaponBarInput> Inputs;
		Inputs.Add({ 0, 2, TEXT("Machinegun"), 100 });
		Inputs.Add({ 1, 5, TEXT("Rocket Launcher"), 25 });
		Inputs.Add({ 2, 7, TEXT("Railgun"), 4 });
		Inputs.Add({ 3, 3, TEXT("Shotgun"), 10 });
		Model.CurrentWeaponIndex = 1;
		Model.WeaponRows = VeloxHUDFormat::WeaponBarRows(Inputs, 0b0111, Model.CurrentWeaponIndex);
		Model.WeaponName = TEXT("Rocket Launcher");
		Model.Ammo = 25;

		FVeloxHUDPowerup Quad;
		Quad.Name = TEXT("Quad Damage");
		Quad.Remaining = 11.2f;
		Quad.Seconds = VeloxHUDFormat::PowerupSeconds(Quad.Remaining);
		Model.Powerups.Add(Quad);
		FVeloxHUDPowerup Haste;
		Haste.Name = TEXT("Haste");
		Haste.Remaining = 2.01f;
		Haste.Seconds = VeloxHUDFormat::PowerupSeconds(Haste.Remaining);
		Model.Powerups.Add(Haste);

		Model.ClockText = TEXT("04:59");
		Model.BannerText = TEXT("FIGHT");

		FVeloxHUDFeedLine First;
		First.Killer = TEXT("Anarki");
		First.Middle = TEXT(" [Rocket] ");
		First.Victim = TEXT("Sarge");
		First.Text = TEXT("Anarki [Rocket] Sarge");
		Model.Feed.Add(First);
		FVeloxHUDFeedLine Second;
		Second.Killer = TEXT("Sarge");
		Second.Middle = TEXT(" (suicide)");
		Second.bSuicide = true;
		Second.Text = TEXT("Sarge (suicide)");
		Model.Feed.Add(Second);

		Model.HitMarkerAlpha = 0.5f;
		Model.bHitMarkerKill = true;
		Model.DamageIntensity[static_cast<int32>(VeloxHUDFormat::EDamageQuadrant::Right)] = 0.7f;
		Model.CrosshairStyle = EVeloxCrosshairStyle::CrossDot;
		Model.CrosshairSize = 12.f;
		Model.CrosshairColor = FColor::Green;
	}

	/** FT_HUDWidget: builds the widget in World, binds the hand-filled model, asserts what the elements show. */
	void RunWidgetChecks(UWorld& World, FChecks& C)
	{
		const UVeloxUISettings& UI = UVeloxUISettings::Get();
		UVeloxHUDWidget* Widget = CreateWidget<UVeloxHUDWidget>(&World, UVeloxHUDWidget::StaticClass());
		C.Check(Widget != nullptr, TEXT("widget_created"));
		if (!Widget)
		{
			return;
		}
		Widget->AddToRoot();
		UVeloxHUDViewModel* Model = NewObject<UVeloxHUDViewModel>(GetTransientPackage());
		Model->AddToRoot();

		// Slate widget tree under NullRHI: RebuildWidget builds the default tree (no texture, no material, no renderer).
		const TSharedRef<SWidget> Slate = Widget->TakeWidget();
		C.Check(Widget->WidgetTree && Widget->WidgetTree->RootWidget, TEXT("tree_built"), Widget->WidgetTree && Widget->WidgetTree->RootWidget ? Widget->WidgetTree->RootWidget->GetName() : FString());
		const bool bElements = Widget->HealthText && Widget->ArmorText && Widget->AmmoText && Widget->WeaponNameText && Widget->ClockText && Widget->BannerText
			&& Widget->DeathTitleText && Widget->DeathPromptText && Widget->WeaponBar && Widget->PowerupBox && Widget->KillFeedBox
			&& Widget->CrosshairLeft && Widget->CrosshairRight && Widget->CrosshairUp && Widget->CrosshairDown && Widget->CrosshairDot
			&& Widget->HitMarkerTick0 && Widget->HitMarkerTick1 && Widget->HitMarkerTick2 && Widget->HitMarkerTick3
			&& Widget->DamageFront && Widget->DamageRight && Widget->DamageBack && Widget->DamageLeft;
		C.Check(bElements, TEXT("elements_bound"));

		// Without a model nothing happens and nothing crashes.
		Widget->RefreshFromViewModel();

		FillModel(*Model);
		Widget->SetViewModel(Model);
		Widget->RefreshFromViewModel();

		C.CheckText(TEXT("health_text"), TextOf(Widget->HealthText), TEXT("87"));
		C.Check(Widget->HealthText && Widget->HealthText->GetColorAndOpacity().GetSpecifiedColor().Equals(UI.HealthMidColor), TEXT("health_mid_colour"));
		C.CheckText(TEXT("armor_text"), TextOf(Widget->ArmorText), TEXT("50"));
		C.CheckText(TEXT("ammo_text"), TextOf(Widget->AmmoText), TEXT("25"));
		C.CheckText(TEXT("weapon_name"), TextOf(Widget->WeaponNameText), TEXT("Rocket Launcher"));
		C.CheckText(TEXT("clock_text"), TextOf(Widget->ClockText), TEXT("04:59"));
		C.CheckText(TEXT("banner_text"), TextOf(Widget->BannerText), TEXT("FIGHT"));

		C.Check(Widget->GetNumVisibleWeaponCells() == 3, TEXT("weapon_cells"), FString::Printf(TEXT("n=%d"), Widget->GetNumVisibleWeaponCells()));
		C.CheckText(TEXT("weapon_cell_0"), Widget->GetWeaponCellText(0), TEXT("2 Machinegun 100"));
		C.CheckText(TEXT("weapon_cell_1"), Widget->GetWeaponCellText(1), TEXT("5 Rocket Launcher 25"));
		C.CheckText(TEXT("weapon_cell_2"), Widget->GetWeaponCellText(2), TEXT("7 Railgun 4"));
		C.Check(!Widget->IsWeaponCellCurrent(0) && Widget->IsWeaponCellCurrent(1) && !Widget->IsWeaponCellCurrent(2), TEXT("weapon_current_highlight"));

		C.Check(Widget->GetNumVisiblePowerupRows() == 2, TEXT("powerup_rows"), FString::Printf(TEXT("n=%d"), Widget->GetNumVisiblePowerupRows()));
		C.CheckText(TEXT("powerup_quad_ceil"), Widget->GetPowerupRowText(0), TEXT("Quad Damage 12"));
		C.CheckText(TEXT("powerup_haste_ceil"), Widget->GetPowerupRowText(1), TEXT("Haste 3"));

		C.Check(Widget->GetNumVisibleFeedRows() == 2, TEXT("feed_rows"), FString::Printf(TEXT("n=%d"), Widget->GetNumVisibleFeedRows()));
		C.CheckText(TEXT("feed_row_0"), Widget->GetFeedRowText(0), TEXT("Anarki [Rocket] Sarge"));
		C.CheckText(TEXT("feed_row_1"), Widget->GetFeedRowText(1), TEXT("Sarge (suicide)"));

		C.Check(Widget->IsHitMarkerVisible(), TEXT("hit_marker_visible"));
		C.Check(Widget->IsDamageIndicatorVisible(1) && !Widget->IsDamageIndicatorVisible(0) && !Widget->IsDamageIndicatorVisible(2) && !Widget->IsDamageIndicatorVisible(3), TEXT("damage_right_only"));
		const bool bBarsVisible = Widget->CrosshairLeft && Widget->CrosshairLeft->GetVisibility() != ESlateVisibility::Collapsed;
		const bool bDotVisible = Widget->CrosshairDot && Widget->CrosshairDot->GetVisibility() != ESlateVisibility::Collapsed;
		C.Check(bBarsVisible && bDotVisible, TEXT("crosshair_cross_dot"));
		C.Check(Widget->DescribeBoundValues().Contains(TEXT("health=87")), TEXT("describe"), Widget->DescribeBoundValues());

		// Second state: low health, low ammo, nothing else; then dead with the respawn prompt.
		Model->Health = 25;
		Model->Ammo = 3;
		Model->WeaponRows.Reset();
		Model->Powerups.Reset();
		Model->Feed.Reset();
		Model->HitMarkerAlpha = 0.f;
		Model->DamageIntensity[1] = 0.f;
		Model->CrosshairStyle = EVeloxCrosshairStyle::Dot;
		Widget->RefreshFromViewModel();
		C.Check(Widget->HealthText && Widget->HealthText->GetColorAndOpacity().GetSpecifiedColor().Equals(UI.HealthLowColor), TEXT("health_low_colour"));
		C.Check(Widget->AmmoText && Widget->AmmoText->GetColorAndOpacity().GetSpecifiedColor().Equals(UI.HealthLowColor), TEXT("ammo_low_colour"));
		C.Check(Widget->GetNumVisibleWeaponCells() == 0 && Widget->GetNumVisiblePowerupRows() == 0 && Widget->GetNumVisibleFeedRows() == 0, TEXT("rows_cleared"));
		C.Check(!Widget->IsHitMarkerVisible() && !Widget->IsDamageIndicatorVisible(1), TEXT("feedback_hidden"));
		C.Check(Widget->CrosshairLeft && Widget->CrosshairLeft->GetVisibility() == ESlateVisibility::Collapsed && Widget->CrosshairDot && Widget->CrosshairDot->GetVisibility() != ESlateVisibility::Collapsed, TEXT("crosshair_dot_only"));

		Model->bAlive = false;
		Model->bShowDeath = true;
		Model->DeathTitle = TEXT("YOU WERE FRAGGED");
		Model->DeathPrompt = TEXT("Press FIRE to respawn");
		Widget->RefreshFromViewModel();
		C.CheckText(TEXT("death_title"), TextOf(Widget->DeathTitleText), TEXT("YOU WERE FRAGGED"));
		C.CheckText(TEXT("death_prompt"), TextOf(Widget->DeathPromptText), TEXT("Press FIRE to respawn"));
		C.Check(Widget->CrosshairDot && Widget->CrosshairDot->GetVisibility() == ESlateVisibility::Collapsed, TEXT("crosshair_hidden_dead"));

		Widget->SetViewModel(nullptr);
		Model->RemoveFromRoot();
		Widget->RemoveFromRoot();
	}

	/** Private game world (no game mode); the widget needs a world and nothing else. */
	class FUiTestWorld
	{
	public:
		FUiTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxUITestWorld"), EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FUiTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::LevelTransition);
				GEngine->ShutdownWorldNetDriver(World);
				World->DestroyWorld(true);
				World->SetPhysicsScene(nullptr);
				GEngine->DestroyWorldContext(World);
				World->RemoveFromRoot();
				World = nullptr;
			}
		}

		UWorld* World = nullptr;
	};
}

// -------------------------------------------------------------------------------------------------------- automation

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxUIHUDFormatTest, "Velox.UI.HUDFormat", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxUIHUDFormatTest::RunTest(const FString& Parameters)
{
	using namespace VeloxHUDFormat;

	// Clock: Q3 ceil for a countdown (the second still running), floor for a count-up, never negative.
	TestEqual(TEXT("clock 300"), Clock(300.0, true), FString(TEXT("05:00")));
	TestEqual(TEXT("clock 299.01 ceil"), Clock(299.01, true), FString(TEXT("05:00")));
	TestEqual(TEXT("clock 59.2 ceil"), Clock(59.2, true), FString(TEXT("01:00")));
	TestEqual(TEXT("clock 0.01 ceil"), Clock(0.01, true), FString(TEXT("00:01")));
	TestEqual(TEXT("clock 0"), Clock(0.0, true), FString(TEXT("00:00")));
	TestEqual(TEXT("clock negative"), Clock(-3.0, true), FString(TEXT("00:00")));
	TestEqual(TEXT("clock count up floor"), Clock(61.9, false), FString(TEXT("01:01")));

	FClockInput Input;
	Input.Phase = EVeloxMatchPhase::Live;
	Input.PhaseStart = 100.0;
	Input.LiveStart = 100.0;
	Input.PhaseEnd = 400.0;
	Input.TimeLimitSeconds = 300;
	Input.Now = 100.0;
	TestEqual(TEXT("live start shows the limit"), MatchClockText(Input), FString(TEXT("05:00")));
	Input.Now = 160.5;
	TestEqual(TEXT("live 60.5 s in"), MatchClockText(Input), FString(TEXT("03:60")).Replace(TEXT("03:60"), TEXT("04:00")));
	Input.Now = 399.2;
	TestEqual(TEXT("live last second"), MatchClockText(Input), FString(TEXT("00:01")));
	Input.TimeLimitSeconds = 0;
	Input.Now = 190.0;
	TestEqual(TEXT("no limit counts up"), MatchClockText(Input), FString(TEXT("01:30")));
	Input.Phase = EVeloxMatchPhase::Warmup;
	TestEqual(TEXT("warmup has no clock"), MatchClockText(Input), FString());
	Input.Phase = EVeloxMatchPhase::Countdown;
	Input.PhaseStart = 180.0;
	Input.PhaseEnd = 185.0;
	Input.Now = 181.5;
	TestEqual(TEXT("countdown to phase end"), MatchClockText(Input), FString(TEXT("00:04")));
	Input.Phase = EVeloxMatchPhase::Overtime;
	Input.PhaseEnd = 0.0;
	Input.LiveStart = 100.0;
	Input.Now = 220.0;
	TestEqual(TEXT("sudden death counts up"), MatchClockText(Input), FString(TEXT("02:00")));
	Input.PhaseEnd = 250.0;
	TestEqual(TEXT("timed overtime counts down"), MatchClockText(Input), FString(TEXT("00:30")));
	Input.Phase = EVeloxMatchPhase::Live;
	Input.PhaseStart = 5000.0;
	Input.Now = 0.09;
	TestEqual(TEXT("unsynced client time"), MatchClockText(Input), FString(TEXT("--:--")));

	// Banner.
	FClockInput Banner;
	Banner.Phase = EVeloxMatchPhase::Warmup;
	TestEqual(TEXT("banner warmup"), PhaseBanner(Banner, 1.0, FString()), FString(TEXT("WARMUP")));
	Banner.Phase = EVeloxMatchPhase::Countdown;
	Banner.PhaseStart = 10.0;
	Banner.PhaseEnd = 13.0;
	Banner.Now = 10.2;
	TestEqual(TEXT("countdown 3"), PhaseBanner(Banner, 1.0, FString()), FString(TEXT("3")));
	Banner.Now = 11.5;
	TestEqual(TEXT("countdown 2"), PhaseBanner(Banner, 1.0, FString()), FString(TEXT("2")));
	Banner.Now = 12.99;
	TestEqual(TEXT("countdown 1"), PhaseBanner(Banner, 1.0, FString()), FString(TEXT("1")));
	Banner.Phase = EVeloxMatchPhase::Live;
	Banner.LiveStart = 13.0;
	Banner.PhaseStart = 13.0;
	Banner.Now = 13.4;
	TestEqual(TEXT("fight"), PhaseBanner(Banner, 1.0, FString()), FString(TEXT("FIGHT")));
	Banner.Now = 14.5;
	TestEqual(TEXT("live has no banner"), PhaseBanner(Banner, 1.0, FString()), FString());
	Banner.Phase = EVeloxMatchPhase::Overtime;
	Banner.PhaseEnd = 0.0;
	TestEqual(TEXT("sudden death"), PhaseBanner(Banner, 1.0, FString()), FString(TEXT("SUDDEN DEATH")));
	Banner.PhaseEnd = 100.0;
	TestEqual(TEXT("overtime"), PhaseBanner(Banner, 1.0, FString()), FString(TEXT("OVERTIME")));
	Banner.Phase = EVeloxMatchPhase::PostMatch;
	TestEqual(TEXT("winner"), PhaseBanner(Banner, 1.0, TEXT("Anarki")), FString(TEXT("Anarki WINS")));
	TestEqual(TEXT("draw"), PhaseBanner(Banner, 1.0, FString()), FString(TEXT("DRAW")));

	// Powerup seconds: ceil (Q3), 0 when gone.
	TestEqual(TEXT("powerup 30"), PowerupSeconds(30.f), 30);
	TestEqual(TEXT("powerup 29.01"), PowerupSeconds(29.01f), 30);
	TestEqual(TEXT("powerup 0.2"), PowerupSeconds(0.2f), 1);
	TestEqual(TEXT("powerup 0"), PowerupSeconds(0.f), 0);
	TestEqual(TEXT("powerup negative"), PowerupSeconds(-1.f), 0);

	// Health bands (Q3 steps).
	TestTrue(TEXT("100 is high"), HealthBand(100, 100, 30) == EHealthBand::High);
	TestTrue(TEXT("99 is mid"), HealthBand(99, 100, 30) == EHealthBand::Mid);
	TestTrue(TEXT("30 is mid"), HealthBand(30, 100, 30) == EHealthBand::Mid);
	TestTrue(TEXT("29 is low"), HealthBand(29, 100, 30) == EHealthBand::Low);
	TestTrue(TEXT("negative is low"), HealthBand(-20, 100, 30) == EHealthBand::Low);

	// Weapon bar: owned only, ordered by slot, current flagged, ammo in the label.
	TArray<FWeaponBarInput> Weapons;
	Weapons.Add({ 0, 5, TEXT("Rocket Launcher"), 25 });
	Weapons.Add({ 1, 2, TEXT("Machinegun"), 100 });
	Weapons.Add({ 2, 7, TEXT("Railgun"), 0 });
	Weapons.Add({ 3, 1, TEXT("Gauntlet"), -1 });
	const TArray<FWeaponBarRow> Rows = WeaponBarRows(Weapons, 0b1011, 0);
	if (TestEqual(TEXT("three owned"), Rows.Num(), 3))
	{
		TestEqual(TEXT("row 0 gauntlet first by slot"), Rows[0].Label, FString(TEXT("1 Gauntlet")));
		TestEqual(TEXT("row 1 machinegun"), Rows[1].Label, FString(TEXT("2 Machinegun 100")));
		TestEqual(TEXT("row 2 rocket"), Rows[2].Label, FString(TEXT("5 Rocket Launcher 25")));
		TestTrue(TEXT("rocket is current"), !Rows[0].bCurrent && !Rows[1].bCurrent && Rows[2].bCurrent);
	}
	TestEqual(TEXT("no weapons"), WeaponBarRows(Weapons, 0, 0).Num(), 0);
	TestEqual(TEXT("out-of-range index ignored"), WeaponBarRows({ FWeaponBarInput{ 20, 1, TEXT("X"), 1 } }, 0xFFFF, 0).Num(), 0);
	TestEqual(TEXT("empty ammo is shown as 0"), WeaponBarRows(Weapons, 0b0100, INDEX_NONE)[0].Label, FString(TEXT("7 Railgun 0")));

	// Kill feed.
	FVeloxKillFeedEntry Entry;
	Entry.KillerName = TEXT("Anarki");
	Entry.VictimName = TEXT("Sarge");
	Entry.DamageTag = FGameplayTag::RequestGameplayTag(TEXT("Velox.Damage.Rocket"), false);
	if (Entry.DamageTag.IsValid())
	{
		TestEqual(TEXT("feed line"), FeedLine(Entry), FString(TEXT("Anarki [Rocket] Sarge")));
		Entry.bSuicide = true;
		Entry.KillerName = Entry.VictimName;
		TestEqual(TEXT("feed suicide"), FeedLine(Entry), FString(TEXT("Sarge [Rocket] (suicide)")));
	}
	else
	{
		AddWarning(TEXT("Velox.Damage.Rocket tag not registered; the weapon rows of the feed test were skipped"));
	}
	Entry.DamageTag = FGameplayTag();
	Entry.bSuicide = false;
	Entry.KillerName = TEXT("Anarki");
	TestEqual(TEXT("feed without tag"), FeedLine(Entry), FString(TEXT("Anarki killed Sarge")));
	TestEqual(TEXT("tag leaf"), TagLeaf(TEXT("Velox.Damage.Rocket")), FString(TEXT("Rocket")));
	TestEqual(TEXT("tag leaf without dot"), TagLeaf(TEXT("Rocket")), FString(TEXT("Rocket")));
	TestTrue(TEXT("fresh entry visible"), FeedEntryVisible(100.0, 98.0, 6.0));
	TestFalse(TEXT("old entry expired"), FeedEntryVisible(100.0, 93.0, 6.0));
	TestTrue(TEXT("late joiner (unsynced time) keeps the history until sync"), FeedEntryVisible(0.09, 400.0, 6.0));

	// Damage direction.
	TestNearlyEqual(TEXT("yaw to +X"), YawFromTo(FVector::ZeroVector, FVector(100, 0, 0)), 0.f, 1e-3f);
	TestNearlyEqual(TEXT("yaw to +Y"), YawFromTo(FVector::ZeroVector, FVector(0, 100, 0)), 90.f, 1e-3f);
	TestNearlyEqual(TEXT("yaw ignores height"), YawFromTo(FVector::ZeroVector, FVector(100, 0, 5000)), 0.f, 1e-3f);
	TestNearlyEqual(TEXT("yaw coincident"), YawFromTo(FVector(1, 2, 3), FVector(1, 2, 900)), 0.f, 1e-3f);
	TestTrue(TEXT("ahead is front"), QuadrantFor(10.f, 0.f) == EDamageQuadrant::Front);
	TestTrue(TEXT("right"), QuadrantFor(90.f, 0.f) == EDamageQuadrant::Right);
	TestTrue(TEXT("behind"), QuadrantFor(180.f, 0.f) == EDamageQuadrant::Back);
	TestTrue(TEXT("left"), QuadrantFor(-90.f, 0.f) == EDamageQuadrant::Left);
	TestTrue(TEXT("view yaw turns the quadrant"), QuadrantFor(90.f, 90.f) == EDamageQuadrant::Front);
	TestTrue(TEXT("wrap-around: 350 vs view 10 is front"), QuadrantFor(350.f, 10.f) == EDamageQuadrant::Front);
	TestTrue(TEXT("wrap-around: -170 vs view 170 is back-ish front edge"), QuadrantFor(-170.f, 170.f) == EDamageQuadrant::Front);
	TestNearlyEqual(TEXT("intensity at once"), DamageIntensity(0.0, 0.6), 1.f, 1e-5f);
	TestNearlyEqual(TEXT("intensity half"), DamageIntensity(0.3, 0.6), 0.5f, 1e-5f);
	TestEqual(TEXT("intensity expired"), DamageIntensity(0.6, 0.6), 0.f);
	TestEqual(TEXT("intensity future"), DamageIntensity(-1.0, 0.6), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxUIDamageDirectionsTest, "Velox.UI.DamageDirections", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxUIDamageDirectionsTest::RunTest(const FString& Parameters)
{
	UVeloxCombatFeedbackComponent* Feedback = NewObject<UVeloxCombatFeedbackComponent>(GetTransientPackage());
	Feedback->RecordDamageDirection(FVector(0, 0, 100), FVector(0, 1000, 100), 5.0);
	if (TestEqual(TEXT("one record"), Feedback->GetDamageDirections().Num(), 1))
	{
		const FVeloxDamageDirection& Record = Feedback->GetDamageDirections()[0];
		TestNearlyEqual(TEXT("yaw towards +Y"), Record.WorldYaw, 90.f, 1e-3f);
		TestNearlyEqual(TEXT("horizontal distance"), Record.Distance2D, 1000.f, 1e-2f);
		TestEqual(TEXT("time"), Record.Time, 5.0);
	}
	// Splash from straight below: close horizontally, the HUD ignores it by distance.
	Feedback->RecordDamageDirection(FVector(0, 0, 100), FVector(10, 0, -50), 5.1);
	TestNearlyEqual(TEXT("below: small horizontal distance"), Feedback->GetDamageDirections().Last().Distance2D, 10.f, 1e-3f);
	// The buffer keeps the newest records (default 8).
	for (int32 Index = 0; Index < 20; ++Index)
	{
		Feedback->RecordDamageDirection(FVector::ZeroVector, FVector(100, 0, 0), 6.0 + Index);
	}
	TestEqual(TEXT("ring buffer cap"), Feedback->GetDamageDirections().Num(), 8);
	TestEqual(TEXT("newest kept"), Feedback->GetDamageDirections().Last().Time, 25.0);
	TestEqual(TEXT("oldest kept"), Feedback->GetDamageDirections()[0].Time, 18.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxUIHUDWidgetTest, "Velox.UI.HUDWidget", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxUIHUDWidgetTest::RunTest(const FString& Parameters)
{
	if (!GEngine)
	{
		AddWarning(TEXT("no engine"));
		return true;
	}
	VeloxUiTest::FUiTestWorld TestWorld;
	VeloxUiTest::FChecks Checks;
	VeloxUiTest::RunWidgetChecks(*TestWorld.World, Checks);
	for (const FString& Failure : Checks.Failures)
	{
		AddError(Failure);
	}
	TestEqual(TEXT("failed widget checks"), Checks.Failed, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// ------------------------------------------------------------------------------------------------ console commands

#if !UE_BUILD_SHIPPING

namespace VeloxUiTest
{
	void FtHUDWidgetCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		bool bQuit = false;
		for (const FString& Arg : Args)
		{
			bQuit |= Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase);
		}
		FChecks Checks;
		if (!InWorld)
		{
			Checks.Check(false, TEXT("world"), TEXT("no world"));
		}
		else
		{
			RunWidgetChecks(*InWorld, Checks);
		}
		UE_LOG(LogTemp, Log, TEXT("VeloxUIFt result=%s failed=%d"), Checks.Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), Checks.Failed);
		if (bQuit)
		{
			FPlatformMisc::RequestExitWithStatus(false, Checks.Failed == 0 ? 0 : 1);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs FtHUDWidgetCmd(
		TEXT("velox.ui.FtHUDWidget"),
		TEXT("velox.ui.FtHUDWidget [quit]: FT_HUDWidget on the running world (`-game -nullrhi`): builds a UVeloxHUDWidget, binds a hand-filled view model and asserts the texts. Logs VeloxUIFt result=. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FtHUDWidgetCommand));

	struct FDamageFtState
	{
		TWeakObjectPtr<UWorld> World;
		bool bQuit = false;
		int32 RetriesLeft = 100;
	};

	void FinishDamageFt(const TSharedRef<FDamageFtState>& State, FChecks& C)
	{
		UE_LOG(LogTemp, Log, TEXT("VeloxUIFt damage result=%s failed=%d"), C.Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), C.Failed);
		if (State->bQuit)
		{
			FPlatformMisc::RequestExitWithStatus(false, C.Failed == 0 ? 0 : 1);
		}
	}

	void DamageFtCheck(const TSharedRef<FDamageFtState>& State)
	{
		FChecks C;
		UWorld* World = State->World.Get();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AVeloxCharacter* Pawn = PC ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
		const UVeloxCombatFeedbackComponent* Feedback = Pawn ? Pawn->GetCombatFeedbackComponent() : nullptr;
		AVeloxHUD* HUD = PC ? Cast<AVeloxHUD>(PC->GetHUD()) : nullptr;
		C.Check(Feedback && HUD && HUD->GetViewModel() && HUD->GetHUDWidget(), TEXT("damage_setup"));
		if (Feedback && HUD && HUD->GetViewModel() && HUD->GetHUDWidget())
		{
			const TArray<FVeloxDamageDirection>& Records = Feedback->GetDamageDirections();
			C.Check(Records.Num() >= 1, TEXT("damage_cue_recorded"), FString::Printf(TEXT("records=%d"), Records.Num()));
			if (Records.Num() >= 1)
			{
				const FVeloxDamageDirection& Last = Records.Last();
				C.Check(FMath::Abs(FMath::FindDeltaAngleDegrees(Last.WorldYaw, 180.f)) < 1.f && Last.Distance2D > 500.f, TEXT("damage_direction_behind"),
					FString::Printf(TEXT("yaw=%.1f dist2d=%.0f"), Last.WorldYaw, Last.Distance2D));
			}
			const UVeloxHUDViewModel& Model = *HUD->GetViewModel();
			const float ViewYaw = PC->GetControlRotation().Yaw;
			const int32 Expected = static_cast<int32>(VeloxHUDFormat::QuadrantFor(180.f, ViewYaw));
			C.Check(Model.DamageIntensity[Expected] > 0.f, TEXT("damage_view_model"), FString::Printf(TEXT("view_yaw=%.1f quadrant=%d intensity=%.2f"), ViewYaw, Expected, Model.DamageIntensity[Expected]));
			C.Check(HUD->GetHUDWidget()->IsDamageIndicatorVisible(Expected), TEXT("damage_widget_indicator"));
		}
		FinishDamageFt(State, C);
	}

	void DamageFtStart(const TSharedRef<FDamageFtState>& State)
	{
		UWorld* World = State->World.Get();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const AVeloxCharacter* Pawn = PC ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
		if (!Pawn || !PC->GetHUD() || !Pawn->GetVelocity().IsNearlyZero(1.0))
		{
			if (World && --State->RetriesLeft > 0)
			{
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([State]() { DamageFtStart(State); }), 0.1f, false);
				return;
			}
			FChecks C;
			C.Check(false, TEXT("damage_setup"), TEXT("no settled local pawn"));
			FinishDamageFt(State, C);
			return;
		}
		// 10 damage along +X: the shooter was behind the pawn at -X (world yaw 180).
		GEngine->Exec(World, TEXT("velox.combat.TestDamage 10 -1 0 1 1 0 0"));
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([State]() { DamageFtCheck(State); }), 0.3f, false);
	}

	void FtDamageIndicatorCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		TSharedRef<FDamageFtState> State = MakeShared<FDamageFtState>();
		State->World = InWorld;
		for (const FString& Arg : Args)
		{
			State->bQuit |= Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase);
		}
		DamageFtStart(State);
	}

	FAutoConsoleCommandWithWorldAndArgs FtDamageIndicatorCmd(
		TEXT("velox.ui.FtDamageIndicator"),
		TEXT("velox.ui.FtDamageIndicator [quit]: damages the local pawn from behind and asserts the cue, the feedback record, the HUD view model quadrant and the widget indicator (standalone, `-game -nullrhi L_ItemTest`). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FtDamageIndicatorCommand));

	void DumpHUDCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		for (TActorIterator<AVeloxHUD> It(InWorld); It; ++It)
		{
			if (const UVeloxHUDWidget* Widget = It->GetHUDWidget())
			{
				UE_LOG(LogTemp, Log, TEXT("VeloxHUD dump %s"), *Widget->DescribeBoundValues());
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("VeloxHUD dump: no widget on %s"), *It->GetName());
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DumpHUDCmd(
		TEXT("velox.ui.DumpHUD"),
		TEXT("velox.ui.DumpHUD: logs the bound values of the live HUD widget (VeloxHUD dump ...). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpHUDCommand));
}

#endif
