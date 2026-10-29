// Velox - non-shipping settings console commands (ADR 0004 task 09 acceptance).
//   velox.settings.Dump                       logs every setting and every key binding (`VeloxSettings dump ...`, `VeloxKeyBinding ...`).
//   velox.settings.Set <name> <value>         sens | fov | invert | autoswitch | name | master | effects | music | aa
//                                             (MSAA4|FXAA|Off) | crosshair (Cross|CrossDot|Dot) | fps; applies and saves.
//   velox.settings.Rebind <Mapping> <Key>     rebinds the first slot of an action ("Fire" "RightMouseButton") and saves.
//   velox.settings.ResetKeys                  every binding back to the mapping context default, saved.
//   velox.settings.RestartTest <write|verify|reset> [quit]
//                                             restart persistence check (infra/ci/settings-restart.sh): `write` sets a
//                                             known non-default profile and rebinds Fire to RightMouseButton; `verify`, in
//                                             a NEW process, checks the values loaded from disk; `reset` restores defaults.
//                                             Logs `VeloxSettingsTest phase=<p> result=PASS|FAIL`; quit exits with 0/1.

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Core/VeloxLog.h"
#include "TimerManager.h"
#include "UI/VeloxGameUserSettings.h"

#if !UE_BUILD_SHIPPING
namespace VeloxSettingsCheats
{
	// The restart test profile: every value differs from the default so a missed save is visible.
	constexpr float TestSensitivity = 7.5f;
	constexpr float TestFieldOfView = 95.f;
	constexpr float TestMasterVolume = 0.5f;
	constexpr float TestEffectsVolume = 0.25f;
	constexpr float TestMusicVolume = 0.75f;
	constexpr float TestCrosshairSize = 40.f;
	constexpr float TestFrameRateLimit = 144.f;
	const TCHAR* const TestPlayerName = TEXT("RestartTest");
	const TCHAR* const RebindMapping = TEXT("Fire");
	const TCHAR* const RebindKeyName = TEXT("RightMouseButton");

	/** Waiting for the local player's input profile (the controller registers IMC_Default when its input component is set up). */
	constexpr int32 MaxProfileRetries = 100;
	constexpr float ProfileRetrySeconds = 0.2f;

	void DumpSettings(UWorld* World)
	{
		const UVeloxGameUserSettings& S = UVeloxGameUserSettings::GetOrDefault();
		UE_LOG(LogVelox, Log,
			TEXT("VeloxSettings dump sens=%.3f invert_y=%d fov=%.1f autoswitch=%d crosshair=%d/%.0f/%s name=%s aa=%d fps_limit=%.0f vol=%.2f/%.2f/%.2f deg_per_count=%.4f"),
			S.MouseSensitivity, S.bInvertMouseY ? 1 : 0, S.FieldOfView, S.bAutoSwitchOnPickup ? 1 : 0, static_cast<int32>(S.CrosshairStyle),
			S.CrosshairSize, *S.CrosshairColor.ToHex(), *S.PlayerName, static_cast<int32>(S.AntiAliasing), S.GetFrameRateLimit(),
			S.MasterVolume, S.EffectsVolume, S.MusicVolume, S.GetDegreesPerCount());
		TArray<FVeloxKeyBinding> Bindings;
		UVeloxGameUserSettings::GetKeyBindings(World, Bindings);
		for (const FVeloxKeyBinding& Binding : Bindings)
		{
			UE_LOG(LogVelox, Log, TEXT("VeloxKeyBinding name=%s current=%s default=%s"), *Binding.MappingName.ToString(), *Binding.CurrentKey.ToString(), *Binding.DefaultKey.ToString());
		}
		UE_LOG(LogVelox, Log, TEXT("VeloxKeyBindings count=%d"), Bindings.Num());
	}

	void SetCommand(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxGameUserSettings* S = UVeloxGameUserSettings::Get();
		if (!S || Args.Num() < 2)
		{
			UE_LOG(LogVelox, Warning, TEXT("velox.settings.Set <sens|fov|invert|autoswitch|name|master|effects|music|aa|crosshair|fps> <value>"));
			return;
		}
		const FString Name = Args[0].ToLower();
		const FString& Value = Args[1];
		if (Name == TEXT("sens")) { S->MouseSensitivity = FCString::Atof(*Value); }
		else if (Name == TEXT("fov")) { S->FieldOfView = FCString::Atof(*Value); }
		else if (Name == TEXT("invert")) { S->bInvertMouseY = FCString::ToBool(*Value); }
		else if (Name == TEXT("autoswitch")) { S->bAutoSwitchOnPickup = FCString::ToBool(*Value); }
		else if (Name == TEXT("name")) { S->PlayerName = Value; }
		else if (Name == TEXT("master")) { S->MasterVolume = FCString::Atof(*Value); }
		else if (Name == TEXT("effects")) { S->EffectsVolume = FCString::Atof(*Value); }
		else if (Name == TEXT("music")) { S->MusicVolume = FCString::Atof(*Value); }
		else if (Name == TEXT("fps")) { S->SetFrameRateLimit(FCString::Atof(*Value)); }
		else if (Name == TEXT("aa"))
		{
			S->AntiAliasing = Value.Equals(TEXT("FXAA"), ESearchCase::IgnoreCase) ? EVeloxAntiAliasing::FXAA
				: Value.Equals(TEXT("Off"), ESearchCase::IgnoreCase) ? EVeloxAntiAliasing::Off : EVeloxAntiAliasing::MSAA4;
		}
		else if (Name == TEXT("crosshair"))
		{
			S->CrosshairStyle = Value.Equals(TEXT("Dot"), ESearchCase::IgnoreCase) ? EVeloxCrosshairStyle::Dot
				: Value.Equals(TEXT("CrossDot"), ESearchCase::IgnoreCase) ? EVeloxCrosshairStyle::CrossDot : EVeloxCrosshairStyle::Cross;
		}
		else
		{
			UE_LOG(LogVelox, Warning, TEXT("velox.settings.Set: unknown setting '%s'"), *Name);
			return;
		}
		S->ApplyAndSaveVeloxSettings(World);
	}

	void RebindCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogVelox, Warning, TEXT("velox.settings.Rebind <Mapping> <Key>"));
			return;
		}
		FString Failure;
		const bool bOk = UVeloxGameUserSettings::RebindKey(World, FName(*Args[0]), FKey(FName(*Args[1])), Failure);
		UE_LOG(LogVelox, Log, TEXT("VeloxRebind mapping=%s key=%s result=%s %s"), *Args[0], *Args[1], bOk ? TEXT("OK") : TEXT("FAIL"), *Failure);
	}

	void ResetKeysCommand(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogVelox, Log, TEXT("VeloxResetKeys result=%s"), UVeloxGameUserSettings::ResetKeyBindings(World) ? TEXT("OK") : TEXT("FAIL"));
	}

	bool FindBinding(UWorld* World, FName MappingName, FVeloxKeyBinding& Out)
	{
		TArray<FVeloxKeyBinding> Bindings;
		UVeloxGameUserSettings::GetKeyBindings(World, Bindings);
		for (const FVeloxKeyBinding& Binding : Bindings)
		{
			if (Binding.MappingName == MappingName)
			{
				Out = Binding;
				return true;
			}
		}
		return false;
	}

	struct FRestartTestState
	{
		TWeakObjectPtr<UWorld> World;
		FString Phase;
		bool bQuit = false;
		int32 Retries = 0;
	};

	void FinishRestartTest(const TSharedRef<FRestartTestState>& State, bool bPassed)
	{
		UE_LOG(LogVelox, Log, TEXT("VeloxSettingsTest phase=%s result=%s"), *State->Phase, bPassed ? TEXT("PASS") : TEXT("FAIL"));
		if (State->bQuit)
		{
			FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
		}
	}

	bool Expect(const TCHAR* What, bool bCondition)
	{
		if (!bCondition)
		{
			UE_LOG(LogVelox, Error, TEXT("VeloxSettingsTest mismatch: %s"), What);
		}
		return bCondition;
	}

	bool RunPhase(UWorld* World, const FString& Phase)
	{
		UVeloxGameUserSettings* S = UVeloxGameUserSettings::Get();
		if (!S)
		{
			return false;
		}
		FVeloxKeyBinding Fire;
		if (Phase == TEXT("write"))
		{
			S->MouseSensitivity = TestSensitivity;
			S->bInvertMouseY = true;
			S->FieldOfView = TestFieldOfView;
			S->bAutoSwitchOnPickup = false;
			S->CrosshairStyle = EVeloxCrosshairStyle::Dot;
			S->CrosshairSize = TestCrosshairSize;
			S->CrosshairColor = FColor::Red;
			S->PlayerName = TestPlayerName;
			S->MasterVolume = TestMasterVolume;
			S->EffectsVolume = TestEffectsVolume;
			S->MusicVolume = TestMusicVolume;
			S->AntiAliasing = EVeloxAntiAliasing::FXAA;
			S->SetFrameRateLimit(TestFrameRateLimit);
			S->ApplyAndSaveVeloxSettings(World);
			FString Failure;
			return UVeloxGameUserSettings::RebindKey(World, RebindMapping, FKey(RebindKeyName), Failure)
				&& FindBinding(World, RebindMapping, Fire) && Fire.CurrentKey == FKey(RebindKeyName);
		}
		if (Phase == TEXT("verify"))
		{
			bool bOk = true;
			bOk &= Expect(TEXT("sensitivity"), FMath::IsNearlyEqual(S->MouseSensitivity, TestSensitivity));
			bOk &= Expect(TEXT("invert"), S->bInvertMouseY);
			bOk &= Expect(TEXT("fov"), FMath::IsNearlyEqual(S->FieldOfView, TestFieldOfView));
			bOk &= Expect(TEXT("autoswitch"), !S->bAutoSwitchOnPickup);
			bOk &= Expect(TEXT("crosshair"), S->CrosshairStyle == EVeloxCrosshairStyle::Dot && FMath::IsNearlyEqual(S->CrosshairSize, TestCrosshairSize) && S->CrosshairColor == FColor::Red);
			bOk &= Expect(TEXT("name"), S->PlayerName == TestPlayerName);
			bOk &= Expect(TEXT("volumes"), FMath::IsNearlyEqual(S->MasterVolume, TestMasterVolume) && FMath::IsNearlyEqual(S->EffectsVolume, TestEffectsVolume) && FMath::IsNearlyEqual(S->MusicVolume, TestMusicVolume));
			bOk &= Expect(TEXT("anti-aliasing"), S->AntiAliasing == EVeloxAntiAliasing::FXAA);
			bOk &= Expect(TEXT("frame rate limit"), FMath::IsNearlyEqual(S->GetFrameRateLimit(), TestFrameRateLimit));
			bOk &= Expect(TEXT("rebound key"), FindBinding(World, RebindMapping, Fire) && Fire.CurrentKey == FKey(RebindKeyName) && Fire.DefaultKey != Fire.CurrentKey);
			// The loaded settings must also have been applied by the engine at startup.
			static const IConsoleVariable* AAMethod = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
			bOk &= Expect(TEXT("r.AntiAliasingMethod applied"), !AAMethod || AAMethod->GetInt() == VeloxSettingsMath::AAMethodFXAA);
			return bOk;
		}
		// reset
		S->SetToDefaults();
		S->SetFrameRateLimit(0.f);
		S->ApplyAndSaveVeloxSettings(World);
		return UVeloxGameUserSettings::ResetKeyBindings(World) && FindBinding(World, RebindMapping, Fire) && Fire.CurrentKey == Fire.DefaultKey;
	}

	void StepRestartTest(const TSharedRef<FRestartTestState>& State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return;
		}
		FVeloxKeyBinding Probe;
		if (!FindBinding(World, RebindMapping, Probe))
		{
			if (++State->Retries > MaxProfileRetries)
			{
				UE_LOG(LogVelox, Error, TEXT("VeloxSettingsTest: no key profile with mapping '%s' after %d retries (local player, bEnableUserSettings, UPlayerMappableKeySettings on the assets)"), RebindMapping, MaxProfileRetries);
				FinishRestartTest(State, false);
				return;
			}
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([State]() { StepRestartTest(State); }), ProfileRetrySeconds, false);
			return;
		}
		FinishRestartTest(State, RunPhase(World, State->Phase));
	}

	void RestartTestCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1 || (Args[0] != TEXT("write") && Args[0] != TEXT("verify") && Args[0] != TEXT("reset")))
		{
			UE_LOG(LogVelox, Warning, TEXT("velox.settings.RestartTest <write|verify|reset> [quit]"));
			return;
		}
		const TSharedRef<FRestartTestState> State = MakeShared<FRestartTestState>();
		State->World = World;
		State->Phase = Args[0];
		State->bQuit = Args.Contains(TEXT("quit"));
		StepRestartTest(State);
	}

	FAutoConsoleCommandWithWorld DumpCmd(
		TEXT("velox.settings.Dump"),
		TEXT("velox.settings.Dump: logs the user settings and key bindings. Non-shipping."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&DumpSettings));

	FAutoConsoleCommandWithWorldAndArgs SetCmd(
		TEXT("velox.settings.Set"),
		TEXT("velox.settings.Set <sens|fov|invert|autoswitch|name|master|effects|music|aa|crosshair|fps> <value>: sets, applies and saves. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetCommand));

	FAutoConsoleCommandWithWorldAndArgs RebindCmd(
		TEXT("velox.settings.Rebind"),
		TEXT("velox.settings.Rebind <Mapping> <Key>: rebinds the first slot of an action and saves. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RebindCommand));

	FAutoConsoleCommandWithWorldAndArgs ResetKeysCmd(
		TEXT("velox.settings.ResetKeys"),
		TEXT("velox.settings.ResetKeys: resets all key bindings and saves. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ResetKeysCommand));

	FAutoConsoleCommandWithWorldAndArgs RestartTestCmd(
		TEXT("velox.settings.RestartTest"),
		TEXT("velox.settings.RestartTest <write|verify|reset> [quit]: settings and rebind persistence across a restart. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RestartTestCommand));
}
#endif // !UE_BUILD_SHIPPING
