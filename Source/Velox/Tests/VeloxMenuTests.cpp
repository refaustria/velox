// Velox - menu tests (ADR 0004 sections 7 and 10, task M3-10).
//
//   Velox.UI.MenuMath          world-free: connect address normalisation (host, host:port, bad characters, port range, IPv6),
//                              network / travel failure texts.
//   Velox.UI.MenuWidgets       FT_MenuWidgets as an automation test: UVeloxMainMenuWidget (elements, refused addresses show an
//                              error), UVeloxSettingsWidget (controls <-> UVeloxGameUserSettings round trip, defaults keep the
//                              name), UVeloxPauseMenuWidget (elements, Resume broadcasts), under NullRHI in a private world.
//   velox.menu.FtMenus         the same checks as a console command on the running game's world (`-game -nullrhi`), also
//                              requiring the key binding list to be filled; logs `VeloxMenuFt check=... ok=` and
//                              `VeloxMenuFt result=PASS|FAIL`; `quit` exits with 0/1.
//   velox.menu.FtEscape        injects Escape into the first local player controller: the pause menu must open (IA_Menu /
//                              InputTag.Menu bound), Resume must close it. `quit` exits 0/1.
//   velox.menu.Connect <addr>  UVeloxGameInstance::ConnectToServer (what the Connect button does). Logs the refusal.
//   velox.menu.Disconnect      UVeloxGameInstance::ReturnToFrontEnd (what the pause menu's Disconnect does).
//   velox.menu.Dump            logs the live menu widgets (`VeloxMenu dump ...`).

#include "Misc/AutomationTest.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Core/VeloxGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameModes/VeloxMenuPlayerController.h"
#include "GameModes/VeloxPlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "TimerManager.h"
#include "UI/VeloxGameUserSettings.h"
#include "UI/VeloxMainMenuWidget.h"
#include "UI/VeloxMenuMath.h"
#include "UI/VeloxPauseMenuWidget.h"
#include "UI/VeloxSettingsWidget.h"

namespace VeloxMenuTestPrivate
{
	/** Counts and logs checks; the console command and the automation test share it. */
	struct FMenuChecks
	{
		int32 Failed = 0;
		TArray<FString> Failures;

		void Check(bool bOk, const TCHAR* What, const FString& Detail = FString())
		{
			UE_LOG(LogTemp, Log, TEXT("VeloxMenuFt check=%s ok=%d %s"), What, bOk ? 1 : 0, *Detail);
			if (!bOk)
			{
				++Failed;
				Failures.Add(FString::Printf(TEXT("%s %s"), What, *Detail));
			}
		}
	};

	/** Private game world with a game instance (the menu widgets reach UVeloxGameInstance through the world). */
	class FMenuTestWorld
	{
	public:
		FMenuTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxMenuTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			GameInstance = NewObject<UVeloxGameInstance>(GEngine);
			GameInstance->AddToRoot();
			Context.OwningGameInstance = GameInstance;
			World->SetGameInstance(GameInstance);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FMenuTestWorld()
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
			if (GameInstance)
			{
				GameInstance->RemoveFromRoot();
				GameInstance = nullptr;
			}
		}

		UWorld* World = nullptr;
		UVeloxGameInstance* GameInstance = nullptr;
	};

	FString TextOf(const UTextBlock* Block)
	{
		return Block ? Block->GetText().ToString() : FString(TEXT("<null>"));
	}

	void RunMainMenuChecks(UWorld& World, FMenuChecks& C)
	{
		UVeloxMainMenuWidget* Menu = CreateWidget<UVeloxMainMenuWidget>(&World, UVeloxMainMenuWidget::StaticClass());
		C.Check(Menu != nullptr, TEXT("main_menu_created"));
		if (!Menu)
		{
			return;
		}
		Menu->AddToRoot();
		Menu->TakeWidget();
		C.Check(Menu->AddressBox && Menu->ConnectButton && Menu->HostButton && Menu->SettingsButton && Menu->QuitButton && Menu->StatusText, TEXT("main_menu_elements"));
		C.Check(UVeloxUserWidget::GetButtonLabel(Menu->ConnectButton) != nullptr, TEXT("main_menu_button_label"), TextOf(UVeloxUserWidget::GetButtonLabel(Menu->ConnectButton)));

		if (Menu->AddressBox)
		{
			Menu->AddressBox->SetText(FText::FromString(TEXT("   ")));
			const bool bEmptyRefused = !Menu->Connect();
			C.Check(bEmptyRefused, TEXT("main_menu_empty_address_refused"));
			C.Check(Menu->GetStatusString().Contains(TEXT("Enter a server address")), TEXT("main_menu_empty_address_message"), Menu->GetStatusString());

			Menu->AddressBox->SetText(FText::FromString(TEXT("bad host/x")));
			C.Check(!Menu->Connect(), TEXT("main_menu_bad_address_refused"));
			C.Check(Menu->GetStatusString().Contains(TEXT("Invalid character")), TEXT("main_menu_bad_address_message"), Menu->GetStatusString());

			Menu->AddressBox->SetText(FText::FromString(TEXT("host:99999")));
			C.Check(!Menu->Connect(), TEXT("main_menu_bad_port_refused"), Menu->GetStatusString());
		}
		Menu->RemoveFromRoot();
	}

	void RunSettingsChecks(UWorld& World, FMenuChecks& C, bool bLive)
	{
		UVeloxSettingsWidget* Widget = CreateWidget<UVeloxSettingsWidget>(&World, UVeloxSettingsWidget::StaticClass());
		C.Check(Widget != nullptr, TEXT("settings_created"));
		if (!Widget)
		{
			return;
		}
		Widget->AddToRoot();
		Widget->TakeWidget();
		C.Check(Widget->SensitivitySlider && Widget->FovSlider && Widget->PlayerNameBox && Widget->MasterVolumeSlider && Widget->AntiAliasingCombo && Widget->WindowModeCombo
				&& Widget->ResolutionCombo && Widget->FrameRateCombo && Widget->VSyncCheck && Widget->KeyList && Widget->ApplyButton && Widget->DefaultsButton && Widget->BackButton,
			TEXT("settings_elements"));
		if (!Widget->SensitivitySlider || !Widget->PlayerNameBox || !Widget->ResolutionCombo || !Widget->FrameRateCombo)
		{
			Widget->RemoveFromRoot();
			return;
		}

		// Controls -> settings: a hand-filled transient settings object goes through the widget and back.
		UVeloxGameUserSettings* Input = NewObject<UVeloxGameUserSettings>(GetTransientPackage());
		Input->SetToDefaults();
		Input->MouseSensitivity = 7.5f;
		Input->bInvertMouseY = true;
		Input->FieldOfView = 110.f;
		Input->bAutoSwitchOnPickup = false;
		Input->PlayerName = TEXT("Anarki");
		Input->CrosshairStyle = EVeloxCrosshairStyle::Dot;
		Input->CrosshairSize = 32.f;
		Input->MasterVolume = 0.25f;
		Input->EffectsVolume = 0.5f;
		Input->MusicVolume = 0.75f;
		Input->AntiAliasing = EVeloxAntiAliasing::FXAA;
		Input->SetFullscreenMode(EWindowMode::Windowed);
		Input->SetScreenResolution(FIntPoint(1280, 720));
		Input->SetVSyncEnabled(true);
		Input->SetFrameRateLimit(144.f);
		Widget->ReadFromSettings(*Input);

		UVeloxGameUserSettings* Output = NewObject<UVeloxGameUserSettings>(GetTransientPackage());
		Output->SetToDefaults();
		Widget->WriteToSettings(*Output);
		C.Check(FMath::IsNearlyEqual(Output->MouseSensitivity, 7.5f), TEXT("settings_roundtrip_sensitivity"), FString::SanitizeFloat(Output->MouseSensitivity));
		C.Check(Output->bInvertMouseY && !Output->bAutoSwitchOnPickup, TEXT("settings_roundtrip_checks"));
		C.Check(FMath::IsNearlyEqual(Output->FieldOfView, 110.f), TEXT("settings_roundtrip_fov"), FString::SanitizeFloat(Output->FieldOfView));
		C.Check(Output->PlayerName == TEXT("Anarki"), TEXT("settings_roundtrip_name"), Output->PlayerName);
		C.Check(Output->CrosshairStyle == EVeloxCrosshairStyle::Dot && FMath::IsNearlyEqual(Output->CrosshairSize, 32.f), TEXT("settings_roundtrip_crosshair"));
		C.Check(FMath::IsNearlyEqual(Output->MasterVolume, 0.25f) && FMath::IsNearlyEqual(Output->EffectsVolume, 0.5f) && FMath::IsNearlyEqual(Output->MusicVolume, 0.75f), TEXT("settings_roundtrip_volumes"));
		C.Check(Output->AntiAliasing == EVeloxAntiAliasing::FXAA, TEXT("settings_roundtrip_aa"));
		C.Check(Output->GetFullscreenMode() == EWindowMode::Windowed, TEXT("settings_roundtrip_window_mode"));
		C.Check(Output->GetScreenResolution() == FIntPoint(1280, 720), TEXT("settings_roundtrip_resolution"), Output->GetScreenResolution().ToString());
		C.Check(Output->IsVSyncEnabled(), TEXT("settings_roundtrip_vsync"));
		C.Check(FMath::RoundToInt(Output->GetFrameRateLimit()) == 144, TEXT("settings_roundtrip_fps"), FString::SanitizeFloat(Output->GetFrameRateLimit()));
		C.Check(TextOf(Widget->SensitivityValue) == TEXT("7.5"), TEXT("settings_value_text_sensitivity"), TextOf(Widget->SensitivityValue));
		C.Check(TextOf(Widget->MasterVolumeValue) == TEXT("25%"), TEXT("settings_value_text_volume"), TextOf(Widget->MasterVolumeValue));

		// Defaults: the other values go back, the name stays.
		Widget->RestoreDefaults();
		UVeloxGameUserSettings* After = NewObject<UVeloxGameUserSettings>(GetTransientPackage());
		Widget->WriteToSettings(*After);
		C.Check(FMath::IsNearlyEqual(After->MouseSensitivity, 5.f), TEXT("settings_defaults_sensitivity"), FString::SanitizeFloat(After->MouseSensitivity));
		C.Check(After->PlayerName == TEXT("Anarki"), TEXT("settings_defaults_keep_name"), After->PlayerName);
		C.Check(After->AntiAliasing == EVeloxAntiAliasing::MSAA4 && After->bAutoSwitchOnPickup, TEXT("settings_defaults_values"));

		// Key rows exist where a local player has the mapping context registered (the running game, not the private world).
		C.Check(!bLive || Widget->GetKeyRowCount() > 0, TEXT("settings_key_rows"), FString::Printf(TEXT("rows=%d"), Widget->GetKeyRowCount()));
		if (Widget->GetKeyRowCount() > 0)
		{
			C.Check(Widget->BeginRebind(0) && Widget->IsRebinding(), TEXT("settings_rebind_mode"), Widget->DescribeKeyRow(0));
			Widget->CancelRebind();
			C.Check(!Widget->IsRebinding(), TEXT("settings_rebind_cancel"));
			if (bLive)
			{
				// The real thing through the Enhanced Input user settings: rebind row 0 to F9, read it back, restore the defaults.
				C.Check(Widget->BeginRebind(0) && Widget->CommitRebind(EKeys::F9), TEXT("settings_rebind_commit"), Widget->DescribeKeyRow(0));
				C.Check(Widget->DescribeKeyRow(0).EndsWith(TEXT("=F9")), TEXT("settings_rebind_row_text"), Widget->DescribeKeyRow(0));
				Widget->RebuildKeyRows();
				C.Check(Widget->DescribeKeyRow(0).EndsWith(TEXT("=F9")), TEXT("settings_rebind_persisted_in_profile"), Widget->DescribeKeyRow(0));
				Widget->RestoreDefaults();
				C.Check(!Widget->DescribeKeyRow(0).EndsWith(TEXT("=F9")), TEXT("settings_defaults_reset_keys"), Widget->DescribeKeyRow(0));
			}
		}
		else
		{
			C.Check(!Widget->BeginRebind(0), TEXT("settings_rebind_without_rows"));
		}

		bool bClosed = false;
		Widget->OnClosed.AddLambda([&bClosed]() { bClosed = true; });
		Widget->Close();
		C.Check(bClosed, TEXT("settings_close_broadcasts"));
		Widget->RemoveFromRoot();
	}

	void RunPauseChecks(UWorld& World, FMenuChecks& C)
	{
		UVeloxPauseMenuWidget* Pause = CreateWidget<UVeloxPauseMenuWidget>(&World, UVeloxPauseMenuWidget::StaticClass());
		C.Check(Pause != nullptr, TEXT("pause_created"));
		if (!Pause)
		{
			return;
		}
		Pause->AddToRoot();
		Pause->TakeWidget();
		C.Check(Pause->ResumeButton && Pause->SettingsButton && Pause->DisconnectButton, TEXT("pause_elements"));
		bool bResumed = false;
		Pause->OnResume.AddLambda([&bResumed]() { bResumed = true; });
		Pause->Resume();
		C.Check(bResumed, TEXT("pause_resume_broadcasts"));
		Pause->RemoveFromRoot();
	}

	void RunWidgetChecks(UWorld& World, FMenuChecks& C, bool bLive)
	{
		RunMainMenuChecks(World, C);
		RunSettingsChecks(World, C, bLive);
		RunPauseChecks(World, C);
	}
}

// -------------------------------------------------------------------------------------------------------- automation

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMenuMathTest, "Velox.UI.MenuMath", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxMenuMathTest::RunTest(const FString& Parameters)
{
	FString Address;
	FString Error;
	auto Normalize = [&](const TCHAR* Input) { Address.Reset(); Error.Reset(); return VeloxMenuMath::NormalizeConnectAddress(Input, 7777, Address, Error); };

	TestTrue(TEXT("host:port"), Normalize(TEXT("127.0.0.1:7860")));
	TestEqual(TEXT("host:port kept"), Address, FString(TEXT("127.0.0.1:7860")));
	TestTrue(TEXT("host only"), Normalize(TEXT("games.inceptum.at")));
	TestEqual(TEXT("default port added"), Address, FString(TEXT("games.inceptum.at:7777")));
	TestTrue(TEXT("trimmed"), Normalize(TEXT("  10.0.0.5 ")));
	TestEqual(TEXT("trim + default port"), Address, FString(TEXT("10.0.0.5:7777")));
	TestTrue(TEXT("bracketed v6 with port"), Normalize(TEXT("[::1]:7777")));
	TestEqual(TEXT("v6 kept"), Address, FString(TEXT("[::1]:7777")));
	TestTrue(TEXT("bracketed v6 without port"), Normalize(TEXT("[::1]")));
	TestEqual(TEXT("v6 default port"), Address, FString(TEXT("[::1]:7777")));
	TestTrue(TEXT("bare v6 left alone"), Normalize(TEXT("fe80::1")));
	TestEqual(TEXT("bare v6 unchanged"), Address, FString(TEXT("fe80::1")));

	TestFalse(TEXT("empty"), Normalize(TEXT("")));
	TestTrue(TEXT("empty message"), Error.Contains(TEXT("Enter a server address")));
	TestFalse(TEXT("blank"), Normalize(TEXT("   ")));
	TestFalse(TEXT("space inside"), Normalize(TEXT("a b")));
	TestFalse(TEXT("travel option smuggled"), Normalize(TEXT("host?listen")));
	TestFalse(TEXT("map path smuggled"), Normalize(TEXT("/Game/Maps/L_ItemTest")));
	TestFalse(TEXT("port zero"), Normalize(TEXT("host:0")));
	TestFalse(TEXT("port too large"), Normalize(TEXT("host:70000")));
	TestFalse(TEXT("port not numeric"), Normalize(TEXT("host:abc")));
	TestFalse(TEXT("port empty"), Normalize(TEXT("host:")));
	TestFalse(TEXT("host empty"), Normalize(TEXT(":7777")));
	TestFalse(TEXT("unterminated bracket"), Normalize(TEXT("[::1")));
	TestFalse(TEXT("garbage after bracket"), Normalize(TEXT("[::1]x")));

	TestEqual(TEXT("timeout text"), VeloxMenuMath::NetworkFailureMessage(ENetworkFailure::ConnectionTimeout, TEXT("UNetConnection::Tick: TIMED OUT")), FString(TEXT("Connection timed out")));
	TestEqual(TEXT("pending failure with reason"), VeloxMenuMath::NetworkFailureMessage(ENetworkFailure::PendingConnectionFailure, TEXT("Connection failed")), FString(TEXT("Could not connect to the server: Connection failed")));
	TestEqual(TEXT("refused"), VeloxMenuMath::NetworkFailureMessage(ENetworkFailure::FailureReceived, TEXT("Kicked")), FString(TEXT("The server refused the connection: Kicked")));
	TestEqual(TEXT("invalid url"), VeloxMenuMath::TravelFailureMessage(ETravelFailure::InvalidURL, TEXT("")), FString(TEXT("Invalid server address")));
	TestEqual(TEXT("generic travel"), VeloxMenuMath::TravelFailureMessage(ETravelFailure::TravelFailure, TEXT("boom")), FString(TEXT("Travel failed: boom")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMenuWidgetsTest, "Velox.UI.MenuWidgets", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxMenuWidgetsTest::RunTest(const FString& Parameters)
{
	if (!GEngine)
	{
		AddWarning(TEXT("no engine"));
		return true;
	}
	VeloxMenuTestPrivate::FMenuTestWorld TestWorld;
	VeloxMenuTestPrivate::FMenuChecks Checks;
	VeloxMenuTestPrivate::RunWidgetChecks(*TestWorld.World, Checks, false);
	for (const FString& Failure : Checks.Failures)
	{
		AddError(Failure);
	}
	TestEqual(TEXT("failed menu checks"), Checks.Failed, 0);

	// Game instance: a refused address must not travel or remember anything, and the message is consumed once.
	FString Error;
	TestFalse(TEXT("ConnectToServer refuses a blank address"), TestWorld.GameInstance->ConnectToServer(TEXT(" "), Error));
	TestTrue(TEXT("refusal has a reason"), !Error.IsEmpty());
	TestTrue(TEXT("nothing remembered"), TestWorld.GameInstance->GetLastConnectTarget().IsEmpty());
	TestFalse(TEXT("ConnectToServer without a local player cannot travel"), TestWorld.GameInstance->ConnectToServer(TEXT("127.0.0.1"), Error));
	TestEqual(TEXT("no local player reason"), Error, FString(TEXT("No local player to travel with.")));
	TestEqual(TEXT("default port added"), TestWorld.GameInstance->GetLastConnectTarget(), FString(TEXT("127.0.0.1:7777")));
	TestWorld.GameInstance->SetPendingMenuMessage(TEXT("Connection lost"));
	TestEqual(TEXT("message returned"), TestWorld.GameInstance->ConsumeMenuMessage(), FString(TEXT("Connection lost")));
	TestEqual(TEXT("message consumed once"), TestWorld.GameInstance->ConsumeMenuMessage(), FString());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// ------------------------------------------------------------------------------------------------ console commands

#if !UE_BUILD_SHIPPING

namespace VeloxMenuTestPrivate
{
	bool HasQuit(const TArray<FString>& Args)
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

	void FtMenusCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		FMenuChecks Checks;
		if (!InWorld)
		{
			Checks.Check(false, TEXT("world"), TEXT("no world"));
		}
		else
		{
			RunWidgetChecks(*InWorld, Checks, true);
		}
		UE_LOG(LogTemp, Log, TEXT("VeloxMenuFt result=%s failed=%d"), Checks.Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), Checks.Failed);
		if (HasQuit(Args))
		{
			FPlatformMisc::RequestExitWithStatus(false, Checks.Failed == 0 ? 0 : 1);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs FtMenusCmd(
		TEXT("velox.menu.FtMenus"),
		TEXT("velox.menu.FtMenus [quit]: FT_MenuWidgets on the running world (`-game -nullrhi`): main menu, settings and pause widgets. Logs VeloxMenuFt result=. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FtMenusCommand));

	struct FEscapeFtState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AVeloxPlayerController> Controller;
		bool bQuit = false;
		int32 Step = 0;
		FTimerHandle Timer;
		FMenuChecks Checks;
	};

	void StepEscapeFt(const TSharedRef<FEscapeFtState>& State)
	{
		AVeloxPlayerController* PC = State->Controller.Get();
		UWorld* World = State->World.Get();
		if (!PC || !World)
		{
			State->Checks.Check(false, TEXT("escape_controller_alive"));
			State->Step = 99;
		}
		if (State->Step == 0)
		{
			// Escape is down: IA_Menu Started fires on the next controller tick.
			State->Checks.Check(PC->GetPauseMenuWidget() == nullptr || !PC->IsMenuOpen(), TEXT("escape_menu_closed_before"));
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Escape, IE_Pressed, 1.0f));
			State->Step = 1;
			World->GetTimerManager().SetTimer(State->Timer, FTimerDelegate::CreateLambda([State]() { StepEscapeFt(State); }), 0.3f, false);
			return;
		}
		if (State->Step == 1)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Escape, IE_Released, 0.0f));
			State->Checks.Check(PC->IsMenuOpen(), TEXT("escape_opens_pause_menu"));
			State->Checks.Check(PC->bShowMouseCursor, TEXT("escape_shows_cursor"));
			if (UVeloxPauseMenuWidget* Pause = PC->GetPauseMenuWidget())
			{
				State->Checks.Check(Pause->IsInViewport(), TEXT("escape_pause_in_viewport"));
				Pause->Resume();
			}
			State->Checks.Check(!PC->IsMenuOpen(), TEXT("resume_closes_pause_menu"));
			State->Checks.Check(!PC->bShowMouseCursor, TEXT("resume_hides_cursor"));
			State->Step = 99;
		}
		UE_LOG(LogTemp, Log, TEXT("VeloxMenuFt escape result=%s failed=%d"), State->Checks.Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), State->Checks.Failed);
		if (State->bQuit)
		{
			FPlatformMisc::RequestExitWithStatus(false, State->Checks.Failed == 0 ? 0 : 1);
		}
	}

	void FtEscapeCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		TSharedRef<FEscapeFtState> State = MakeShared<FEscapeFtState>();
		State->bQuit = HasQuit(Args);
		State->World = InWorld;
		State->Controller = InWorld ? Cast<AVeloxPlayerController>(InWorld->GetFirstPlayerController()) : nullptr;
		if (!State->Controller.IsValid())
		{
			UE_LOG(LogTemp, Log, TEXT("VeloxMenuFt escape result=FAIL failed=1 (no AVeloxPlayerController: run on a match map)"));
			if (State->bQuit)
			{
				FPlatformMisc::RequestExitWithStatus(false, 1);
			}
			return;
		}
		StepEscapeFt(State);
	}

	FAutoConsoleCommandWithWorldAndArgs FtEscapeCmd(
		TEXT("velox.menu.FtEscape"),
		TEXT("velox.menu.FtEscape [quit]: injects Escape: the pause menu must open (IA_Menu bound), Resume must close it. Run on a match map. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FtEscapeCommand));

	void ConnectCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		UVeloxGameInstance* GameInstance = InWorld ? Cast<UVeloxGameInstance>(InWorld->GetGameInstance()) : nullptr;
		if (!GameInstance || Args.IsEmpty())
		{
			UE_LOG(LogTemp, Log, TEXT("velox.menu.Connect <address>: needs a Velox game instance and an address."));
			return;
		}
		FString Error;
		const bool bOk = GameInstance->ConnectToServer(Args[0], Error);
		UE_LOG(LogTemp, Log, TEXT("VeloxMenu connect command ok=%d error='%s'"), bOk ? 1 : 0, *Error);
	}

	FAutoConsoleCommandWithWorldAndArgs ConnectCmd(
		TEXT("velox.menu.Connect"),
		TEXT("velox.menu.Connect <host[:port]>: what the main menu Connect button does (UVeloxGameInstance::ConnectToServer). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ConnectCommand));

	void DisconnectCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		UVeloxGameInstance* GameInstance = InWorld ? Cast<UVeloxGameInstance>(InWorld->GetGameInstance()) : nullptr;
		if (GameInstance)
		{
			GameInstance->ReturnToFrontEnd(Args.IsEmpty() ? FString() : FString::Join(Args, TEXT(" ")));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DisconnectCmd(
		TEXT("velox.menu.Disconnect"),
		TEXT("velox.menu.Disconnect [message]: what the pause menu Disconnect does (UVeloxGameInstance::ReturnToFrontEnd). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DisconnectCommand));

	void DumpCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (!InWorld)
		{
			return;
		}
		const UVeloxGameInstance* GameInstance = Cast<UVeloxGameInstance>(InWorld->GetGameInstance());
		FString MenuInfo = TEXT("no menu controller");
		for (TActorIterator<AVeloxMenuPlayerController> It(InWorld); It; ++It)
		{
			const UVeloxMainMenuWidget* Menu = It->GetMainMenuWidget();
			MenuInfo = Menu ? FString::Printf(TEXT("main_menu in_viewport=%d status='%s' address='%s'"), Menu->IsInViewport() ? 1 : 0, *Menu->GetStatusString(), Menu->AddressBox ? *Menu->AddressBox->GetText().ToString() : TEXT("")) : FString(TEXT("main_menu=null"));
		}
		FString PauseInfo = TEXT("no match controller");
		for (TActorIterator<AVeloxPlayerController> It(InWorld); It; ++It)
		{
			PauseInfo = FString::Printf(TEXT("pause_menu open=%d"), It->IsMenuOpen() ? 1 : 0);
		}
		UE_LOG(LogTemp, Log, TEXT("VeloxMenu dump map=%s %s | %s | pending_message='%s' last_target=%s"), *InWorld->GetMapName(), *MenuInfo, *PauseInfo,
			GameInstance ? *GameInstance->GetPendingMenuMessage() : TEXT(""), GameInstance ? *GameInstance->GetLastConnectTarget() : TEXT(""));
	}

	FAutoConsoleCommandWithWorldAndArgs DumpCmd(
		TEXT("velox.menu.Dump"),
		TEXT("velox.menu.Dump: logs the live menu state (VeloxMenu dump ...). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpCommand));
}

#endif // !UE_BUILD_SHIPPING
