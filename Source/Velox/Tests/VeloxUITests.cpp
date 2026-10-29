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
//   Velox.UI.ScoreboardFormat  world-free (task M3-08): row sort, row/limits text, ready prompt matrix, pickup message, item timer rule/text/order.
//                              The scoreboard rows, ready button, HUD ready prompt / pickup message / item timers are asserted inside the
//                              widget checks (Velox.UI.HUDWidget, velox.ui.FtHUDWidget).
//   velox.ui.FtScoreboard      live (`-game -nullrhi L_ItemTest`): scoreboard hidden, held, released, forced at PostMatch. `quit` exits 0/1.
//   velox.ui.Scoreboard 0|1    holds / releases the scoreboard key; velox.ui.DumpScoreboard logs what the live widget shows.
//   velox.ui.DumpHUD           logs the bound values of the live HUD widget of the first local player (`VeloxHUD bound ...`).

#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "Components/Border.h"
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
#include "UI/VeloxScoreboardFormat.h"
#include "UI/VeloxScoreboardWidget.h"
#include "GameModes/VeloxGameState.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "GameModes/VeloxPlayerController.h"
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

	/** The scoreboard widget against a hand-filled model: sorted rows, local highlight, winner, spectators, ready button. */
	void RunScoreboardChecks(UWorld& World, FChecks& C)
	{
		const UVeloxUISettings& UI = UVeloxUISettings::Get();
		UVeloxScoreboardWidget* Widget = CreateWidget<UVeloxScoreboardWidget>(&World, UVeloxScoreboardWidget::StaticClass());
		C.Check(Widget != nullptr, TEXT("scoreboard_created"));
		if (!Widget)
		{
			return;
		}
		Widget->AddToRoot();
		UVeloxHUDViewModel* Model = NewObject<UVeloxHUDViewModel>(GetTransientPackage());
		Model->AddToRoot();
		const TSharedRef<SWidget> Slate = Widget->TakeWidget();
		C.Check(Widget->ScoreboardPanel && Widget->TitleText && Widget->LimitsText && Widget->ClockText && Widget->WinnerText && Widget->RowsBox
			&& Widget->SpectatorsText && Widget->ReadyButton && Widget->ReadyButtonLabel, TEXT("scoreboard_elements_bound"));

		// Without a model nothing happens; with a model that does not show the scoreboard the panel stays collapsed.
		Widget->RefreshFromViewModel();
		Widget->SetViewModel(Model);
		Model->bHasPlayer = true;
		Widget->RefreshFromViewModel();
		C.Check(!Widget->IsPanelVisible(), TEXT("scoreboard_hidden_by_default"));

		// Four players handed in unsorted; the view model sorts with SortRows (score, kills, deaths, name).
		auto MakeRow = [](const TCHAR* Name, int32 Score, int32 Kills, int32 Deaths, int32 Ping, bool bReady, bool bLocal, const TCHAR* Powerups)
		{
			VeloxScoreboardFormat::FRow Row;
			Row.Name = Name;
			Row.Score = Score;
			Row.Kills = Kills;
			Row.Deaths = Deaths;
			Row.PingMs = Ping;
			Row.bReady = bReady;
			Row.bLocal = bLocal;
			Row.Powerups = Powerups;
			Row.Color = FColor::Cyan;
			return Row;
		};
		Model->ScoreRows = { MakeRow(TEXT("Sarge"), 3, 4, 5, 40, false, true, TEXT("")), MakeRow(TEXT("Anarki"), 7, 7, 1, 23, true, false, TEXT("QH")),
			MakeRow(TEXT("Bones"), 3, 5, 5, 0, false, false, TEXT("")), MakeRow(TEXT("Xaero"), 3, 4, 2, 80, true, false, TEXT("")) };
		VeloxScoreboardFormat::SortRows(Model->ScoreRows);
		Model->bScoreboardVisible = true;
		Model->bScoreboardForced = true;
		Model->ScoreboardTitle = TEXT("Free For All");
		Model->ScoreboardLimits = VeloxScoreboardFormat::LimitsText(20, 600);
		Model->ScoreboardClock = TEXT("00:15");
		Model->ScoreboardWinner = TEXT("Anarki WINS");
		Model->SpectatorNames = TEXT("Visor, Crash");
		Model->bShowReadyButton = true;
		Model->ReadyButtonText = VeloxScoreboardFormat::ReadyButtonText(false, TEXT("F3"));
		Widget->RefreshFromViewModel();

		C.Check(Widget->IsPanelVisible(), TEXT("scoreboard_visible"));
		C.Check(Widget->GetNumVisibleRows() == 4, TEXT("scoreboard_rows"), FString::Printf(TEXT("n=%d"), Widget->GetNumVisibleRows()));
		C.CheckText(TEXT("scoreboard_row_0_leader"), Widget->GetRowText(0), TEXT("Anarki [QH]  7  7  1  23  READY"));
		C.CheckText(TEXT("scoreboard_row_1_more_kills_wins_the_tie"), Widget->GetRowText(1), TEXT("Bones  3  5  5  0  -"));
		C.CheckText(TEXT("scoreboard_row_2_fewer_deaths_wins"), Widget->GetRowText(2), TEXT("Xaero  3  4  2  80  READY"));
		C.CheckText(TEXT("scoreboard_row_3"), Widget->GetRowText(3), TEXT("Sarge  3  4  5  40  -"));
		C.Check(!Widget->IsRowLocal(0) && !Widget->IsRowLocal(1) && !Widget->IsRowLocal(2) && Widget->IsRowLocal(3), TEXT("scoreboard_local_row_highlight"));
		C.CheckText(TEXT("scoreboard_title"), TextOf(Widget->TitleText), TEXT("Free For All"));
		C.CheckText(TEXT("scoreboard_limits"), TextOf(Widget->LimitsText), TEXT("Fraglimit 20   Timelimit 10:00"));
		C.CheckText(TEXT("scoreboard_clock"), TextOf(Widget->ClockText), TEXT("00:15"));
		C.CheckText(TEXT("scoreboard_winner"), TextOf(Widget->WinnerText), TEXT("Anarki WINS"));
		C.Check(Widget->WinnerText && Widget->WinnerText->GetVisibility() != ESlateVisibility::Collapsed, TEXT("scoreboard_winner_visible"));
		C.CheckText(TEXT("scoreboard_spectators"), TextOf(Widget->SpectatorsText), TEXT("Spectators: Visor, Crash"));
		C.Check(Widget->IsReadyButtonVisible(), TEXT("scoreboard_ready_button_visible"));
		C.CheckText(TEXT("scoreboard_ready_button_label"), Widget->GetReadyButtonLabel(), TEXT("Ready (F3)"));
		C.Check(Widget->ScoreboardPanel && Widget->ScoreboardPanel->GetBrushColor().Equals(UI.ScoreboardBackgroundColor), TEXT("scoreboard_background"));
		Widget->ClickReadyButton();
		C.Check(true, TEXT("scoreboard_ready_click_without_controller"));

		// Fewer players, no spectators, no winner, no button: the extra rows collapse.
		Model->ScoreRows.SetNum(2);
		Model->SpectatorNames.Reset();
		Model->ScoreboardWinner.Reset();
		Model->bShowReadyButton = false;
		Widget->RefreshFromViewModel();
		C.Check(Widget->GetNumVisibleRows() == 2, TEXT("scoreboard_rows_shrink"));
		C.Check(Widget->SpectatorsText && Widget->SpectatorsText->GetVisibility() == ESlateVisibility::Collapsed, TEXT("scoreboard_spectators_hidden"));
		C.Check(Widget->WinnerText && Widget->WinnerText->GetVisibility() == ESlateVisibility::Collapsed, TEXT("scoreboard_winner_hidden"));
		C.Check(!Widget->IsReadyButtonVisible(), TEXT("scoreboard_ready_button_hidden"));

		// Key released and no intermission: the panel goes away again.
		Model->bScoreboardVisible = false;
		Widget->RefreshFromViewModel();
		C.Check(!Widget->IsPanelVisible(), TEXT("scoreboard_hidden_again"));
		C.Check(Widget->DescribeBoundValues().Contains(TEXT("visible=0")), TEXT("scoreboard_describe"), Widget->DescribeBoundValues());

		Widget->SetViewModel(nullptr);
		Model->RemoveFromRoot();
		Widget->RemoveFromRoot();
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

		// Ready prompt, pickup message, spectator item timers (task M3-08).
		C.Check(!Widget->IsPickupMessageVisible() && Widget->GetNumVisibleItemTimerRows() == 0, TEXT("hud_ext_defaults_hidden"));
		Model->ReadyPrompt = TEXT("Press F3 when you are ready");
		Model->PickupMessage = TEXT("Mega Health");
		Model->PickupMessageAlpha = 1.f;
		Model->bShowItemTimers = true;
		VeloxScoreboardFormat::FItemTimer Mega;
		Mega.ItemName = TEXT("Mega Health");
		Mega.bAvailable = false;
		Mega.SecondsLeft = 22.4;
		Mega.TakenBy = TEXT("Anarki");
		Mega.Text = VeloxScoreboardFormat::ItemTimerText(Mega);
		VeloxScoreboardFormat::FItemTimer Armor;
		Armor.ItemName = TEXT("Red Armor");
		Armor.Text = VeloxScoreboardFormat::ItemTimerText(Armor);
		Model->ItemTimers = { Mega, Armor };
		Widget->RefreshFromViewModel();
		C.CheckText(TEXT("ready_prompt_text"), TextOf(Widget->ReadyPromptText), TEXT("Press F3 when you are ready"));
		C.Check(Widget->ReadyPromptText && Widget->ReadyPromptText->GetVisibility() != ESlateVisibility::Collapsed, TEXT("ready_prompt_visible"));
		C.CheckText(TEXT("pickup_message_text"), TextOf(Widget->PickupMessageText), TEXT("Mega Health"));
		C.Check(Widget->IsPickupMessageVisible(), TEXT("pickup_message_visible"));
		C.Check(Widget->GetNumVisibleItemTimerRows() == 2, TEXT("item_timer_rows"), FString::Printf(TEXT("n=%d"), Widget->GetNumVisibleItemTimerRows()));
		C.CheckText(TEXT("item_timer_taken_row"), Widget->GetItemTimerRowText(0), TEXT("Mega Health 00:23 (Anarki)"));
		C.CheckText(TEXT("item_timer_up_row"), Widget->GetItemTimerRowText(1), TEXT("Red Armor UP"));
		Model->PickupMessageAlpha = 0.f;
		Model->bShowItemTimers = false;
		Model->ReadyPrompt.Reset();
		Widget->RefreshFromViewModel();
		C.Check(!Widget->IsPickupMessageVisible() && Widget->GetNumVisibleItemTimerRows() == 0 && Widget->ReadyPromptText->GetVisibility() == ESlateVisibility::Collapsed, TEXT("hud_ext_hidden_again"));

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

		RunScoreboardChecks(World, C);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxUIScoreboardFormatTest, "Velox.UI.ScoreboardFormat", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxUIScoreboardFormatTest::RunTest(const FString& Parameters)
{
	using namespace VeloxScoreboardFormat;

	// Sorting (ADR 0004 section 6): score desc, kills desc, deaths asc, name (case-insensitive).
	auto MakeRow = [](const TCHAR* Name, int32 Score, int32 Kills, int32 Deaths)
	{
		FRow Row;
		Row.Name = Name;
		Row.Score = Score;
		Row.Kills = Kills;
		Row.Deaths = Deaths;
		return Row;
	};
	TArray<FRow> Rows = { MakeRow(TEXT("zed"), 2, 2, 0), MakeRow(TEXT("Bob"), 5, 6, 1), MakeRow(TEXT("amy"), 5, 7, 3), MakeRow(TEXT("Cat"), 5, 6, 0), MakeRow(TEXT("Abe"), 5, 6, 1), MakeRow(TEXT("neg"), -1, 0, 4) };
	SortRows(Rows);
	TArray<FString> Order;
	for (const FRow& Row : Rows)
	{
		Order.Add(Row.Name);
	}
	TestEqual(TEXT("sort order"), FString::Join(Order, TEXT(",")), FString(TEXT("amy,Cat,Abe,Bob,zed,neg")));
	TArray<FRow> Empty;
	SortRows(Empty);
	TestEqual(TEXT("sort empty"), Empty.Num(), 0);

	FRow Row = MakeRow(TEXT("Anarki"), 7, 8, 2);
	Row.PingMs = 23;
	Row.bReady = true;
	TestEqual(TEXT("row text"), RowText(Row), FString(TEXT("Anarki  7  8  2  23  READY")));
	Row.Powerups = TEXT("QR");
	Row.bReady = false;
	TestEqual(TEXT("row text with powerups"), RowText(Row), FString(TEXT("Anarki [QR]  7  8  2  23  -")));

	TestEqual(TEXT("limits both"), LimitsText(20, 600), FString(TEXT("Fraglimit 20   Timelimit 10:00")));
	TestEqual(TEXT("limits time only"), LimitsText(0, 90), FString(TEXT("Timelimit 01:30")));
	TestEqual(TEXT("limits frags only"), LimitsText(5, 0), FString(TEXT("Fraglimit 5")));
	TestEqual(TEXT("limits none"), LimitsText(0, 0), FString());

	// Ready prompt: only where ready means something.
	FReadyInput Ready;
	Ready.Phase = EVeloxMatchPhase::Warmup;
	Ready.bRequireReady = false;
	TestFalse(TEXT("FFA warmup: ready does nothing"), CanToggleReady(Ready));
	TestEqual(TEXT("FFA warmup: no prompt"), ReadyPromptText(Ready, TEXT("F3")), FString());
	Ready.bRequireReady = true;
	TestTrue(TEXT("duel warmup: ready counts"), CanToggleReady(Ready));
	TestEqual(TEXT("duel warmup prompt"), ReadyPromptText(Ready, TEXT("F3")), FString(TEXT("Press F3 when you are ready")));
	Ready.bReady = true;
	TestEqual(TEXT("duel warmup ready prompt"), ReadyPromptText(Ready, TEXT("F3")), FString(TEXT("READY - waiting for the opponent")));
	Ready.bSpectator = true;
	TestFalse(TEXT("spectators cannot ready"), CanToggleReady(Ready));
	TestEqual(TEXT("spectator: no prompt"), ReadyPromptText(Ready, TEXT("F3")), FString());
	Ready.bSpectator = false;
	Ready.Phase = EVeloxMatchPhase::PostMatch;
	Ready.bRequireReady = false;
	Ready.bReady = false;
	TestTrue(TEXT("postmatch: ready counts in any mode"), CanToggleReady(Ready));
	TestEqual(TEXT("postmatch prompt"), ReadyPromptText(Ready, TEXT("F3")), FString(TEXT("Press F3 to play the next match now")));
	Ready.bReady = true;
	TestEqual(TEXT("postmatch ready prompt"), ReadyPromptText(Ready, TEXT("F3")), FString(TEXT("READY - waiting for the others")));
	for (const EVeloxMatchPhase Phase : { EVeloxMatchPhase::Countdown, EVeloxMatchPhase::Live, EVeloxMatchPhase::Overtime })
	{
		Ready.Phase = Phase;
		Ready.bRequireReady = true;
		TestFalse(TEXT("no ready toggle while the match runs"), CanToggleReady(Ready));
	}
	TestEqual(TEXT("button text"), ReadyButtonText(false, TEXT("F3")), FString(TEXT("Ready (F3)")));
	TestEqual(TEXT("button text ready"), ReadyButtonText(true, TEXT("F3")), FString(TEXT("READY (F3 to cancel)")));

	// Pickup message.
	TestEqual(TEXT("own pickup"), PickupMessageText(TEXT("Mega Health"), TEXT("Anarki"), true, false), FString(TEXT("Mega Health")));
	TestEqual(TEXT("other's pickup, not watching"), PickupMessageText(TEXT("Mega Health"), TEXT("Anarki"), false, false), FString());
	TestEqual(TEXT("other's pickup, spectator"), PickupMessageText(TEXT("Mega Health"), TEXT("Anarki"), false, true), FString(TEXT("Anarki took Mega Health")));
	TestEqual(TEXT("no taker name"), PickupMessageText(TEXT("Mega Health"), FString(), false, true), FString());
	TestEqual(TEXT("no item name"), PickupMessageText(FString(), TEXT("Anarki"), true, true), FString());

	// Item timer visibility: the mode rule against the viewer's role.
	TestFalse(TEXT("nobody: spectator"), ItemTimersVisible(EVeloxItemTimerVisibility::Nobody, true));
	TestTrue(TEXT("spectators: spectator"), ItemTimersVisible(EVeloxItemTimerVisibility::Spectators, true));
	TestFalse(TEXT("spectators: player"), ItemTimersVisible(EVeloxItemTimerVisibility::Spectators, false));
	TestTrue(TEXT("everyone: player"), ItemTimersVisible(EVeloxItemTimerVisibility::Everyone, false));

	// Item timer lines and order.
	FItemTimer Mega;
	Mega.ItemName = TEXT("Mega Health");
	Mega.bAvailable = false;
	Mega.SecondsLeft = 22.4;
	TestEqual(TEXT("timer without taker (ceil)"), ItemTimerText(Mega), FString(TEXT("Mega Health 00:23")));
	Mega.TakenBy = TEXT("Anarki");
	Mega.SecondsLeft = 61.0;
	TestEqual(TEXT("timer with taker"), ItemTimerText(Mega), FString(TEXT("Mega Health 01:01 (Anarki)")));
	FItemTimer Armor;
	Armor.ItemName = TEXT("Red Armor");
	TestEqual(TEXT("available"), ItemTimerText(Armor), FString(TEXT("Red Armor UP")));
	FItemTimer Quad;
	Quad.ItemName = TEXT("Quad Damage");
	Quad.bAvailable = false;
	Quad.SecondsLeft = 9.0;
	FItemTimer Yellow;
	Yellow.ItemName = TEXT("Yellow Armor");
	TArray<FItemTimer> Timers = { Yellow, Mega, Armor, Quad };
	SortTimers(Timers);
	TArray<FString> TimerOrder;
	for (const FItemTimer& Timer : Timers)
	{
		TimerOrder.Add(Timer.ItemName);
	}
	TestEqual(TEXT("timer order: soonest first, then available by name"), FString::Join(TimerOrder, TEXT(",")), FString(TEXT("Quad Damage,Mega Health,Red Armor,Yellow Armor")));
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

	struct FScoreboardFtState
	{
		TWeakObjectPtr<UWorld> World;
		bool bQuit = false;
		int32 RetriesLeft = 100;
		FChecks C;
		FString ExpectedPickupMessage;
	};

	void FinishScoreboardFt(const TSharedRef<FScoreboardFtState>& State)
	{
		UE_LOG(LogTemp, Log, TEXT("VeloxUIFt scoreboard result=%s failed=%d"), State->C.Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), State->C.Failed);
		if (State->bQuit)
		{
			FPlatformMisc::RequestExitWithStatus(false, State->C.Failed == 0 ? 0 : 1);
		}
	}

	void ScoreboardFtAfter(const TSharedRef<FScoreboardFtState>& State, float Seconds, TFunction<void()> Next)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			FinishScoreboardFt(State);
			return;
		}
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(Next)), Seconds, false);
	}

	AVeloxHUD* ScoreboardFtHud(const TSharedRef<FScoreboardFtState>& State, AVeloxPlayerController** OutController = nullptr)
	{
		UWorld* World = State->World.Get();
		AVeloxPlayerController* PC = World ? Cast<AVeloxPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (OutController)
		{
			*OutController = PC;
		}
		return PC ? Cast<AVeloxHUD>(PC->GetHUD()) : nullptr;
	}

	/** PostMatch: forced open, with the winner line, the ready prompt and the button. */
	void ScoreboardFtPostMatch(const TSharedRef<FScoreboardFtState>& State)
	{
		AVeloxHUD* Hud = ScoreboardFtHud(State);
		if (!Hud || !Hud->GetViewModel() || !Hud->GetScoreboardWidget())
		{
			State->C.Check(false, TEXT("scoreboard_live_postmatch"), TEXT("lost the HUD"));
			FinishScoreboardFt(State);
			return;
		}
		const UVeloxHUDViewModel& Model = *Hud->GetViewModel();
		// Slate does not tick widgets under NullRHI: refresh by hand, as the widgets' NativeTick would.
		UVeloxScoreboardWidget& Board = *Hud->GetScoreboardWidget();
		Board.RefreshFromViewModel();
		if (Hud->GetHUDWidget())
		{
			Hud->GetHUDWidget()->RefreshFromViewModel();
		}
		const AVeloxGameState* GameState = State->World->GetGameState<AVeloxGameState>();
		State->C.Check(Model.bScoreboardForced && Model.bScoreboardVisible && Board.IsPanelVisible(), TEXT("scoreboard_live_forced_at_postmatch"),
			FString::Printf(TEXT("visible=%d forced=%d phase=%d"), Model.bScoreboardVisible ? 1 : 0, Model.bScoreboardForced ? 1 : 0, GameState ? static_cast<int32>(GameState->GetPhase()) : -1));
		State->C.Check(!Model.ScoreboardWinner.IsEmpty(), TEXT("scoreboard_live_winner_line"), Model.ScoreboardWinner);
		State->C.Check(Model.bShowReadyButton && Board.IsReadyButtonVisible() && !Model.ReadyPrompt.IsEmpty(), TEXT("scoreboard_live_ready_prompt_and_button"), Model.ReadyPrompt);
		State->C.Check(Hud->GetHUDWidget() && Hud->GetHUDWidget()->DescribeBoundValues().Contains(TEXT("ready_prompt='Press")), TEXT("scoreboard_live_hud_prompt"));
		UE_LOG(LogTemp, Log, TEXT("VeloxScoreboard live %s"), *Board.DescribeBoundValues());
		FinishScoreboardFt(State);
	}

	void ScoreboardFtReleased(const TSharedRef<FScoreboardFtState>& State)
	{
		AVeloxHUD* Hud = ScoreboardFtHud(State);
		if (Hud && Hud->GetScoreboardWidget())
		{
			Hud->GetScoreboardWidget()->RefreshFromViewModel();
		}
		State->C.Check(Hud && Hud->GetScoreboardWidget() && !Hud->GetScoreboardWidget()->IsPanelVisible(), TEXT("scoreboard_live_released_hidden"));
		if (!Hud)
		{
			FinishScoreboardFt(State);
			return;
		}
		// The registry's take event for the local player shows the item name as the pickup message.
		UWorld* World = State->World.Get();
		UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		TArray<AVeloxPickup*> Pickups = Registry ? Registry->GetPickups() : TArray<AVeloxPickup*>();
		State->C.Check(Pickups.Num() > 0 && Pickups[0]->GetDefinition(), TEXT("scoreboard_live_pickup_available"), FString::Printf(TEXT("n=%d"), Pickups.Num()));
		if (Pickups.Num() > 0 && Pickups[0]->GetDefinition())
		{
			State->ExpectedPickupMessage = Pickups[0]->GetDefinition()->DisplayName.ToString();
			Registry->OnPickupTaken.Broadcast(Pickups[0], World->GetFirstPlayerController()->PlayerState);
		}
		ScoreboardFtAfter(State, 0.2f, [State]()
		{
			AVeloxHUD* Hud2 = ScoreboardFtHud(State);
			if (Hud2 && Hud2->GetHUDWidget())
			{
				Hud2->GetHUDWidget()->RefreshFromViewModel();
				State->C.Check(Hud2->GetViewModel()->PickupMessage == State->ExpectedPickupMessage && Hud2->GetHUDWidget()->IsPickupMessageVisible(), TEXT("scoreboard_live_pickup_message"),
					FString::Printf(TEXT("message='%s' expected='%s'"), *Hud2->GetViewModel()->PickupMessage, *State->ExpectedPickupMessage));
			}
			GEngine->Exec(State->World.Get(), TEXT("velox.match.Phase PostMatch"));
			ScoreboardFtAfter(State, 0.5f, [State]() { ScoreboardFtPostMatch(State); });
		});
	}

	/** Held (Tab): visible, not forced, one row per player. */
	void ScoreboardFtHeld(const TSharedRef<FScoreboardFtState>& State)
	{
		AVeloxPlayerController* PC = nullptr;
		AVeloxHUD* Hud = ScoreboardFtHud(State, &PC);
		if (!Hud || !Hud->GetViewModel() || !Hud->GetScoreboardWidget())
		{
			State->C.Check(false, TEXT("scoreboard_live_held"), TEXT("lost the HUD"));
			FinishScoreboardFt(State);
			return;
		}
		const UVeloxHUDViewModel& Model = *Hud->GetViewModel();
		Hud->GetScoreboardWidget()->RefreshFromViewModel();
		State->C.Check(Model.bScoreboardVisible && !Model.bScoreboardForced && Hud->GetScoreboardWidget()->IsPanelVisible(), TEXT("scoreboard_live_held_visible"),
			FString::Printf(TEXT("visible=%d forced=%d"), Model.bScoreboardVisible ? 1 : 0, Model.bScoreboardForced ? 1 : 0));
		State->C.Check(Model.ScoreRows.Num() >= 1 && Hud->GetScoreboardWidget()->GetNumVisibleRows() == Model.ScoreRows.Num(), TEXT("scoreboard_live_rows"),
			FString::Printf(TEXT("model=%d widget=%d"), Model.ScoreRows.Num(), Hud->GetScoreboardWidget()->GetNumVisibleRows()));
		PC->SetScoreboardHeld(false);
		ScoreboardFtAfter(State, 0.3f, [State]() { ScoreboardFtReleased(State); });
	}

	/** The live HUD of the first local player: held scoreboard, then the one forced at PostMatch. */
	void ScoreboardFtRun(const TSharedRef<FScoreboardFtState>& State)
	{
		UWorld* World = State->World.Get();
		AVeloxPlayerController* PC = nullptr;
		AVeloxHUD* Hud = ScoreboardFtHud(State, &PC);
		if (!Hud || !Hud->GetViewModel() || !Hud->GetScoreboardWidget() || !PC->GetPawn() || !World->GetGameState<AVeloxGameState>())
		{
			if (World && --State->RetriesLeft > 0)
			{
				ScoreboardFtAfter(State, 0.1f, [State]() { ScoreboardFtRun(State); });
				return;
			}
			State->C.Check(false, TEXT("scoreboard_live_setup"), TEXT("no HUD, scoreboard widget, pawn or game state"));
			FinishScoreboardFt(State);
			return;
		}
		State->C.Check(true, TEXT("scoreboard_live_setup"));
		State->C.Check(!Hud->GetViewModel()->bScoreboardVisible && !Hud->GetScoreboardWidget()->IsPanelVisible(), TEXT("scoreboard_live_hidden_at_start"));
		PC->SetScoreboardHeld(true);
		ScoreboardFtAfter(State, 0.3f, [State]() { ScoreboardFtHeld(State); });
	}

	void FtScoreboardCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		TSharedRef<FScoreboardFtState> State = MakeShared<FScoreboardFtState>();
		State->World = InWorld;
		for (const FString& Arg : Args)
		{
			State->bQuit |= Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase);
		}
		ScoreboardFtRun(State);
	}

	FAutoConsoleCommandWithWorldAndArgs FtScoreboardCmd(
		TEXT("velox.ui.FtScoreboard"),
		TEXT("velox.ui.FtScoreboard [quit]: live scoreboard check (`-game -nullrhi L_ItemTest`, standalone): hidden at start, shown while held, hidden on release, forced open at PostMatch with winner line, ready prompt and button. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FtScoreboardCommand));

	void ScoreboardHoldCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (AVeloxPlayerController* PC = InWorld ? Cast<AVeloxPlayerController>(InWorld->GetFirstPlayerController()) : nullptr)
		{
			PC->SetScoreboardHeld(Args.Num() == 0 || Args[0] != TEXT("0"));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ScoreboardHoldCmd(
		TEXT("velox.ui.Scoreboard"),
		TEXT("velox.ui.Scoreboard [0|1]: holds (default) or releases the scoreboard key of the first local player. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ScoreboardHoldCommand));

	void DumpScoreboardCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		for (TActorIterator<AVeloxHUD> It(InWorld); It; ++It)
		{
			if (const UVeloxScoreboardWidget* Board = It->GetScoreboardWidget())
			{
				UE_LOG(LogTemp, Log, TEXT("VeloxScoreboard dump %s"), *Board->DescribeBoundValues());
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DumpScoreboardCmd(
		TEXT("velox.ui.DumpScoreboard"),
		TEXT("velox.ui.DumpScoreboard: logs what the live scoreboard widget shows (VeloxScoreboard dump ...). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpScoreboardCommand));
}

#endif
