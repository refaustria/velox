// Velox - settings tests (ADR 0004 section 7 / task 09): Q3 sensitivity, clamps, AA mapping, connect options, the
// settings object, and the Enhanced Input user settings wiring that the key rebinding relies on.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EnhancedInputDeveloperSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Core/VeloxLocalPlayer.h"
#include "UI/VeloxGameUserSettings.h"
#include "UI/VeloxSettingsMath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxSettingsMathTest, "Velox.Settings.Math",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxSettingsMathTest::RunTest(const FString& Parameters)
{
	// Q3: sensitivity 5 -> 0.11 degrees per count.
	TestNearlyEqual(TEXT("default deg/count"), static_cast<float>(VeloxSettingsMath::DegreesPerCount(VeloxSettingsMath::DefaultSensitivity)), 0.11f, 1e-5f);
	TestNearlyEqual(TEXT("sens 10 doubles"), static_cast<float>(VeloxSettingsMath::DegreesPerCount(10.f)), 0.22f, 1e-5f);
	TestNearlyEqual(TEXT("sensitivity clamped low"), VeloxSettingsMath::ClampSensitivity(-3.f), VeloxSettingsMath::MinSensitivity);
	TestNearlyEqual(TEXT("sensitivity clamped high"), VeloxSettingsMath::ClampSensitivity(1e6f), VeloxSettingsMath::MaxSensitivity);

	const FVector2D Plain = VeloxSettingsMath::MouseDeltaToDegrees(FVector2D(100.0, 50.0), 5.f, false);
	TestNearlyEqual(TEXT("100 counts yaw = 11 deg"), static_cast<float>(Plain.X), 11.f, 1e-4f);
	TestNearlyEqual(TEXT("50 counts pitch = 5.5 deg"), static_cast<float>(Plain.Y), 5.5f, 1e-4f);
	const FVector2D Inverted = VeloxSettingsMath::MouseDeltaToDegrees(FVector2D(100.0, 50.0), 5.f, true);
	TestNearlyEqual(TEXT("invert flips pitch only (yaw)"), static_cast<float>(Inverted.X), 11.f, 1e-4f);
	TestNearlyEqual(TEXT("invert flips pitch only (pitch)"), static_cast<float>(Inverted.Y), -5.5f, 1e-4f);

	TestEqual(TEXT("fov default in range"), VeloxSettingsMath::ClampFieldOfView(VeloxSettingsMath::DefaultFieldOfView), VeloxSettingsMath::DefaultFieldOfView);
	TestEqual(TEXT("fov min"), VeloxSettingsMath::ClampFieldOfView(10.f), VeloxSettingsMath::MinFieldOfView);
	TestEqual(TEXT("fov max"), VeloxSettingsMath::ClampFieldOfView(500.f), VeloxSettingsMath::MaxFieldOfView);
	TestEqual(TEXT("volume low"), VeloxSettingsMath::ClampVolume(-1.f), 0.f);
	TestEqual(TEXT("volume high"), VeloxSettingsMath::ClampVolume(3.f), 1.f);

	int32 Method = -1;
	int32 Count = -1;
	VeloxSettingsMath::AntiAliasingCVars(EVeloxAntiAliasing::MSAA4, Method, Count);
	TestEqual(TEXT("MSAA method"), Method, 3);
	TestEqual(TEXT("MSAA count"), Count, 4);
	VeloxSettingsMath::AntiAliasingCVars(EVeloxAntiAliasing::FXAA, Method, Count);
	TestEqual(TEXT("FXAA method"), Method, 1);
	VeloxSettingsMath::AntiAliasingCVars(EVeloxAntiAliasing::Off, Method, Count);
	TestEqual(TEXT("AA off method"), Method, 0);

	TestEqual(TEXT("name option appended"), VeloxSettingsMath::AppendNameOption(TEXT("127.0.0.1:7777"), TEXT("Anarki")), FString(TEXT("127.0.0.1:7777?Name=Anarki")));
	TestEqual(TEXT("separators stripped"), VeloxSettingsMath::SanitizePlayerName(TEXT("  A?B=C#\"D\\ ")), FString(TEXT("ABCD")));
	TestEqual(TEXT("empty name leaves the address"), VeloxSettingsMath::AppendNameOption(TEXT("host"), TEXT("?=")), FString(TEXT("host")));
	TestEqual(TEXT("existing Name option wins"), VeloxSettingsMath::AppendNameOption(TEXT("host?Name=X"), TEXT("Y")), FString(TEXT("host?Name=X")));
	TestEqual(TEXT("name capped"), VeloxSettingsMath::SanitizePlayerName(FString::ChrN(100, TEXT('a'))).Len(), VeloxSettingsMath::MaxPlayerNameLength);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxSettingsObjectTest, "Velox.Settings.Object",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxSettingsObjectTest::RunTest(const FString& Parameters)
{
	// A transient instance: nothing here touches GameUserSettings.ini.
	UVeloxGameUserSettings* S = NewObject<UVeloxGameUserSettings>(GetTransientPackage());
	TestNotNull(TEXT("settings object"), S);
	if (!S)
	{
		return false;
	}
	S->SetToDefaults();
	TestEqual(TEXT("default sensitivity"), S->MouseSensitivity, VeloxSettingsMath::DefaultSensitivity);
	TestEqual(TEXT("default fov"), S->FieldOfView, VeloxSettingsMath::DefaultFieldOfView);
	TestTrue(TEXT("autoswitch on by default"), S->bAutoSwitchOnPickup);
	TestFalse(TEXT("invert off by default"), S->bInvertMouseY);

	S->MouseSensitivity = 9999.f;
	S->FieldOfView = 1.f;
	S->MasterVolume = 2.f;
	S->PlayerName = TEXT("A?B");
	S->PlayerName += TEXT("=");
	S->ValidateSettings();
	TestEqual(TEXT("sensitivity clamped"), S->MouseSensitivity, VeloxSettingsMath::MaxSensitivity);
	TestEqual(TEXT("fov clamped"), S->FieldOfView, VeloxSettingsMath::MinFieldOfView);
	TestEqual(TEXT("volume clamped"), S->MasterVolume, 1.f);
	TestEqual(TEXT("name sanitised"), S->PlayerName, FString(TEXT("AB")));

	// Defaults keep identity (name, address) and reset preferences.
	S->MouseSensitivity = 12.f;
	S->bAutoSwitchOnPickup = false;
	S->SetToDefaults();
	TestEqual(TEXT("defaults: sensitivity"), S->MouseSensitivity, VeloxSettingsMath::DefaultSensitivity);
	TestTrue(TEXT("defaults: autoswitch"), S->bAutoSwitchOnPickup);
	TestEqual(TEXT("defaults keep the name"), S->PlayerName, FString(TEXT("AB")));

	// F5: the login option carries the configured name (the server ignores the connect URL's ?Name=).
	TestEqual(TEXT("login option"), VeloxSettingsMath::BuildNameLoginOption(TEXT("Anarki")), FString(TEXT("Name=Anarki")));
	TestEqual(TEXT("login option sanitised"), VeloxSettingsMath::BuildNameLoginOption(TEXT(" A?B=C ")), FString(TEXT("Name=ABC")));
	TestTrue(TEXT("login option empty for an empty name"), VeloxSettingsMath::BuildNameLoginOption(TEXT("?=")).IsEmpty());
	{
		const UVeloxLocalPlayer* LocalPlayer = GetDefault<UVeloxLocalPlayer>();
		TestEqual(TEXT("UVeloxLocalPlayer login options = settings name"), LocalPlayer->GetGameLoginOptions(),
			VeloxSettingsMath::BuildNameLoginOption(UVeloxGameUserSettings::GetOrDefault().PlayerName));
		FString ConfiguredPlayerClass;
		GConfig->GetString(TEXT("/Script/Engine.Engine"), TEXT("LocalPlayerClassName"), ConfiguredPlayerClass, GEngineIni);
		TestEqual(TEXT("LocalPlayerClassName"), ConfiguredPlayerClass, FString(TEXT("/Script/Velox.VeloxLocalPlayer")));
	}

	// The project ships its own class as the engine's user settings class and has Enhanced Input user settings on.
	FString ConfiguredClass;
	GConfig->GetString(TEXT("/Script/Engine.Engine"), TEXT("GameUserSettingsClassName"), ConfiguredClass, GEngineIni);
	TestEqual(TEXT("GameUserSettingsClassName"), ConfiguredClass, FString(TEXT("/Script/Velox.VeloxGameUserSettings")));
	TestTrue(TEXT("Enhanced Input user settings enabled"), GetDefault<UEnhancedInputDeveloperSettings>()->bEnableUserSettings != 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
