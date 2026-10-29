// Velox - user settings (ADR 0004 section 7).

#include "UI/VeloxGameUserSettings.h"

#include "AudioDevice.h"
#include "Core/VeloxLog.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UserSettings/EnhancedInputUserSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameUserSettings)

namespace VeloxSettingsPrivate
{
	// Assets created by Tools/editor/create_bootstrap_content.py (sounds arrive in M5; the classes exist from M3).
	const TCHAR* const MasterClassPath = TEXT("/Game/Audio/SC_Master.SC_Master");
	const TCHAR* const EffectsClassPath = TEXT("/Game/Audio/SC_Effects.SC_Effects");
	const TCHAR* const MusicClassPath = TEXT("/Game/Audio/SC_Music.SC_Music");
	const TCHAR* const MixPath = TEXT("/Game/Audio/SM_Default.SM_Default");

	/** Seconds the volume change fades in; 0 = immediate (a slider should not lag). */
	constexpr float VolumeFadeSeconds = 0.f;

	const TCHAR* AntiAliasingName(EVeloxAntiAliasing AntiAliasing)
	{
		switch (AntiAliasing)
		{
		case EVeloxAntiAliasing::FXAA:
			return TEXT("FXAA");
		case EVeloxAntiAliasing::Off:
			return TEXT("Off");
		default:
			return TEXT("MSAA4");
		}
	}

	void SetCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			// SetByGameSetting ranks below SetByProjectSetting (DefaultEngine.ini RendererSettings) and would be ignored.
			CVar->Set(Value, ECVF_SetByCode);
		}
	}

	UWorld* FindGameWorld()
	{
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}
}

UVeloxGameUserSettings::UVeloxGameUserSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UVeloxGameUserSettings* UVeloxGameUserSettings::Get()
{
	return GEngine ? Cast<UVeloxGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

const UVeloxGameUserSettings& UVeloxGameUserSettings::GetOrDefault()
{
	const UVeloxGameUserSettings* Settings = Get();
	return Settings ? *Settings : *GetDefault<UVeloxGameUserSettings>();
}

void UVeloxGameUserSettings::ResetVeloxValues()
{
	// Not read from the CDO: the CDO is config-loaded from GameUserSettings.ini, so it would hand back the saved values.
	MouseSensitivity = VeloxSettingsMath::DefaultSensitivity;
	bInvertMouseY = false;
	FieldOfView = VeloxSettingsMath::DefaultFieldOfView;
	bAutoSwitchOnPickup = true;
	CrosshairStyle = EVeloxCrosshairStyle::Cross;
	CrosshairSize = VeloxSettingsMath::DefaultCrosshairSize;
	CrosshairColor = FColor::Green;
	MasterVolume = 1.f;
	EffectsVolume = 1.f;
	MusicVolume = 1.f;
	AntiAliasing = EVeloxAntiAliasing::MSAA4;
	// PlayerName and LastConnectAddress are identity, not preferences: "defaults" keeps them.
}

void UVeloxGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	ResetVeloxValues();
}

void UVeloxGameUserSettings::ValidateSettings()
{
	Super::ValidateSettings();
	MouseSensitivity = VeloxSettingsMath::ClampSensitivity(MouseSensitivity);
	FieldOfView = VeloxSettingsMath::ClampFieldOfView(FieldOfView);
	CrosshairSize = VeloxSettingsMath::ClampCrosshairSize(CrosshairSize);
	MasterVolume = VeloxSettingsMath::ClampVolume(MasterVolume);
	EffectsVolume = VeloxSettingsMath::ClampVolume(EffectsVolume);
	MusicVolume = VeloxSettingsMath::ClampVolume(MusicVolume);
	PlayerName = VeloxSettingsMath::SanitizePlayerName(PlayerName);
}

void UVeloxGameUserSettings::ApplyNonResolutionSettings()
{
	// Super validates, applies vsync and the frame-rate limit (t.MaxFPS, SetByGameSetting: a console value still wins).
	Super::ApplyNonResolutionSettings();
	ApplyVeloxSettings(nullptr);
}

void UVeloxGameUserSettings::ApplyAudio(UWorld* World)
{
	using namespace VeloxSettingsPrivate;
	World = World ? World : FindGameWorld();
	if (!World || !World->GetAudioDeviceRaw())
	{
		return;
	}
	USoundMix* Mix = Cast<USoundMix>(FSoftObjectPath(MixPath).TryLoad());
	if (!Mix)
	{
		UE_LOG(LogVelox, Verbose, TEXT("VeloxSettings: %s not found, volumes not applied (run Tools/editor/create_bootstrap_content.py)."), MixPath);
		return;
	}
	const struct FClassVolume
	{
		const TCHAR* Path;
		float Volume;
	} Classes[] = {{MasterClassPath, MasterVolume}, {EffectsClassPath, EffectsVolume}, {MusicClassPath, MusicVolume}};
	for (const FClassVolume& Entry : Classes)
	{
		if (USoundClass* SoundClass = Cast<USoundClass>(FSoftObjectPath(Entry.Path).TryLoad()))
		{
			// Not applied to children: the class hierarchy already multiplies SC_Master into SC_Effects and SC_Music.
			UGameplayStatics::SetSoundMixClassOverride(World, Mix, SoundClass, VeloxSettingsMath::ClampVolume(Entry.Volume), 1.f, VolumeFadeSeconds, false);
		}
	}
	UGameplayStatics::PushSoundMixModifier(World, Mix);
}

void UVeloxGameUserSettings::ApplyVeloxSettings(UWorld* World)
{
	ValidateSettings();

	int32 AAMethod = 0;
	int32 MSAACount = 0;
	VeloxSettingsMath::AntiAliasingCVars(AntiAliasing, AAMethod, MSAACount);
	if (!IsRunningDedicatedServer())
	{
		VeloxSettingsPrivate::SetCVar(TEXT("r.AntiAliasingMethod"), AAMethod);
		VeloxSettingsPrivate::SetCVar(TEXT("r.MSAACount"), MSAACount);
	}
	ApplyAudio(World);

	UE_LOG(LogVelox, Log,
		TEXT("VeloxSettings applied sens=%.3f deg_per_count=%.4f invert_y=%d fov=%.1f autoswitch=%d crosshair=%d/%.0f name=%s aa=%s r.AntiAliasingMethod=%d r.MSAACount=%d fps_limit=%.0f vol master=%.2f effects=%.2f music=%.2f"),
		MouseSensitivity, GetDegreesPerCount(), bInvertMouseY ? 1 : 0, FieldOfView, bAutoSwitchOnPickup ? 1 : 0,
		static_cast<int32>(CrosshairStyle), CrosshairSize, *PlayerName, VeloxSettingsPrivate::AntiAliasingName(AntiAliasing), AAMethod, MSAACount,
		GetFrameRateLimit(), MasterVolume, EffectsVolume, MusicVolume);

	OnVeloxSettingsApplied.Broadcast();
}

void UVeloxGameUserSettings::ApplyAndSaveVeloxSettings(UWorld* World)
{
	ApplyVeloxSettings(World);
	SaveSettings();
}

UEnhancedInputUserSettings* UVeloxGameUserSettings::FindInputUserSettings(const UWorld* World)
{
	const ULocalPlayer* LocalPlayer = World ? World->GetFirstLocalPlayerFromController() : nullptr;
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
	return Subsystem ? Subsystem->GetUserSettings() : nullptr;
}

void UVeloxGameUserSettings::GetKeyBindings(const UWorld* World, TArray<FVeloxKeyBinding>& OutBindings)
{
	OutBindings.Reset();
	const UEnhancedInputUserSettings* InputSettings = FindInputUserSettings(World);
	const UEnhancedPlayerMappableKeyProfile* Profile = InputSettings ? InputSettings->GetActiveKeyProfile() : nullptr;
	if (!Profile)
	{
		return;
	}
	for (const TPair<FName, FKeyMappingRow>& Row : Profile->GetPlayerMappingRows())
	{
		const FPlayerKeyMapping* Best = nullptr;
		for (const FPlayerKeyMapping& Mapping : Row.Value.Mappings)
		{
			if (!Best || static_cast<uint8>(Mapping.GetSlot()) < static_cast<uint8>(Best->GetSlot()))
			{
				Best = &Mapping;
			}
		}
		if (Best)
		{
			FVeloxKeyBinding& Binding = OutBindings.AddDefaulted_GetRef();
			Binding.MappingName = Row.Key;
			Binding.DisplayName = Best->GetDisplayName();
			Binding.CurrentKey = Best->GetCurrentKey();
			Binding.DefaultKey = Best->GetDefaultKey();
		}
	}
	OutBindings.Sort([](const FVeloxKeyBinding& A, const FVeloxKeyBinding& B) { return A.MappingName.LexicalLess(B.MappingName); });
}

bool UVeloxGameUserSettings::RebindKey(const UWorld* World, FName MappingName, FKey NewKey, FString& OutFailure)
{
	UEnhancedInputUserSettings* InputSettings = FindInputUserSettings(World);
	if (!InputSettings)
	{
		OutFailure = TEXT("no Enhanced Input user settings (no local player, or bEnableUserSettings is off)");
		return false;
	}
	FMapPlayerKeyArgs Args;
	Args.MappingName = MappingName;
	Args.Slot = EPlayerMappableKeySlot::First;
	Args.NewKey = NewKey;
	FGameplayTagContainer Failure;
	InputSettings->MapPlayerKey(Args, Failure);
	if (!Failure.IsEmpty())
	{
		OutFailure = Failure.ToStringSimple();
		return false;
	}
	InputSettings->SaveSettings();
	return true;
}

bool UVeloxGameUserSettings::ResetKeyBindings(const UWorld* World)
{
	UEnhancedInputUserSettings* InputSettings = FindInputUserSettings(World);
	const UEnhancedPlayerMappableKeyProfile* Profile = InputSettings ? InputSettings->GetActiveKeyProfile() : nullptr;
	if (!Profile)
	{
		return false;
	}
	FGameplayTagContainer Failure;
	InputSettings->ResetKeyProfileIdToDefault(Profile->GetProfileIdString(), Failure);
	InputSettings->SaveSettings();
	return Failure.IsEmpty();
}
