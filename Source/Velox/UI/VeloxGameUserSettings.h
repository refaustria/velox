// Velox - user settings (ADR 0004 section 7). GameUserSettingsClassName in Config/DefaultEngine.ini; persisted by the
// engine in Saved/Config/<Platform>/GameUserSettings.ini. Key bindings live in Enhanced Input user settings (a save
// slot under Saved/SaveGames/), reached through the static helpers below.

#pragma once

#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "UI/VeloxSettingsMath.h"

#include "VeloxGameUserSettings.generated.h"

class UEnhancedInputUserSettings;
class UWorld;

UENUM(BlueprintType)
enum class EVeloxCrosshairStyle : uint8
{
	Cross,
	CrossDot,
	Dot
};

/** One row of the key binding list (first slot of one Enhanced Input mapping name). */
USTRUCT(BlueprintType)
struct FVeloxKeyBinding
{
	GENERATED_BODY()

	/** The mapping name to pass to RebindKey (the UPlayerMappableKeySettings name). */
	UPROPERTY(BlueprintReadOnly, Category = "Velox|Settings")
	FName MappingName;

	UPROPERTY(BlueprintReadOnly, Category = "Velox|Settings")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Velox|Settings")
	FKey CurrentKey;

	UPROPERTY(BlueprintReadOnly, Category = "Velox|Settings")
	FKey DefaultKey;
};

UCLASS(Config = GameUserSettings, ConfigDoNotCheckDefaults)
class VELOX_API UVeloxGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UVeloxGameUserSettings(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** The project's settings object (null when GameUserSettingsClassName is not ours, e.g. in a commandlet). */
	static UVeloxGameUserSettings* Get();
	/** Same, never null: the class default object when the engine has none. */
	static const UVeloxGameUserSettings& GetOrDefault();

	//~UGameUserSettings interface
	virtual void SetToDefaults() override;
	virtual void ValidateSettings() override;
	virtual void ApplyNonResolutionSettings() override;
	//~End of UGameUserSettings interface

	/**
	 * Clamps the values and pushes them: AA and frame-rate cvars, sound class volumes (World = the world whose audio
	 * device receives the mix, null = the game world), logs `VeloxSettings applied ...`, broadcasts OnVeloxSettingsApplied.
	 * Sensitivity, FOV, invert, crosshair and auto-switch are read live from this object by the pawn, HUD and controller.
	 */
	UFUNCTION(BlueprintCallable, Category = "Velox|Settings")
	void ApplyVeloxSettings(UWorld* World = nullptr);

	/** Sound class volumes only (a new world needs the mix again). */
	void ApplyAudio(UWorld* World);

	/** Degrees per mouse count: 0.022 x MouseSensitivity. */
	double GetDegreesPerCount() const { return VeloxSettingsMath::DegreesPerCount(MouseSensitivity); }

	/** Settings screen: apply, then write GameUserSettings.ini. */
	UFUNCTION(BlueprintCallable, Category = "Velox|Settings")
	void ApplyAndSaveVeloxSettings(UWorld* World = nullptr);

	// ---- Key bindings (Enhanced Input user settings of the first local player)
	/** The Enhanced Input user settings of the first local player of World, null without one. */
	static UEnhancedInputUserSettings* FindInputUserSettings(const UWorld* World);
	/** All rebindable actions with their current and default key (first slot), sorted by display name. */
	static void GetKeyBindings(const UWorld* World, TArray<FVeloxKeyBinding>& OutBindings);
	/** Maps NewKey to the first slot of MappingName and saves the key profile. False (and OutFailure filled) when refused. */
	static bool RebindKey(const UWorld* World, FName MappingName, FKey NewKey, FString& OutFailure);
	/** Resets every binding to the mapping context default and saves. */
	static bool ResetKeyBindings(const UWorld* World);

	/** Fired by ApplyVeloxSettings (HUD crosshair, settings widget refresh). */
	DECLARE_MULTICAST_DELEGATE(FOnVeloxSettingsApplied);
	FOnVeloxSettingsApplied OnVeloxSettingsApplied;

	/** Q3 sensitivity: degrees per count = 0.022 x this. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Input")
	float MouseSensitivity = VeloxSettingsMath::DefaultSensitivity;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Input")
	bool bInvertMouseY = false;

	/** Horizontal field of view in degrees (CalcCamera, AspectRatio_MaintainXFOV). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Video")
	float FieldOfView = VeloxSettingsMath::DefaultFieldOfView;

	/** Q3 cg_autoswitch: a picked-up weapon is selected while holding the machine gun or gauntlet. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Gameplay")
	bool bAutoSwitchOnPickup = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Gameplay")
	EVeloxCrosshairStyle CrosshairStyle = EVeloxCrosshairStyle::Cross;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Gameplay")
	float CrosshairSize = VeloxSettingsMath::DefaultCrosshairSize;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Gameplay")
	FColor CrosshairColor = FColor::Green;

	/** Sent as `?Name=` on connect (UVeloxGameInstance::BuildConnectURL). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Gameplay")
	FString PlayerName = TEXT("Player");

	/** The last address typed into the main menu connect field. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Gameplay")
	FString LastConnectAddress;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Audio")
	float MasterVolume = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Audio")
	float EffectsVolume = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Audio")
	float MusicVolume = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Velox|Video")
	EVeloxAntiAliasing AntiAliasing = EVeloxAntiAliasing::MSAA4;

private:
	void ResetVeloxValues();
};
