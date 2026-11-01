// Velox - audio tunables and the client-side play helpers (ADR 0006 section 9, task M5-10).
// Config = Game, so Config/DefaultGame.ini [/Script/Velox.VeloxAudioSettings] holds the asset references (all soft: the
// dedicated server never loads audio, ADR 0006 section 1) and the numbers. Tools/editor/create_audio_content.py creates
// the assets at the paths the ini names. Every helper is a no-op on a dedicated server.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Logging/LogMacros.h"

#include "VeloxAudioSettings.generated.h"

class USoundAttenuation;
class USoundBase;
class USoundClass;
class USoundConcurrency;
class USoundMix;
class APawn;
class UObject;
class UWorld;

VELOX_API DECLARE_LOG_CATEGORY_EXTERN(LogVeloxAudio, Log, All);

/** Attenuation assets of ADR 0006 section 9 (None = 2D). */
UENUM(BlueprintType)
enum class EVeloxAudioAttenuation : uint8
{
	None,
	WeaponFire,
	Explosion,
	Impact,
	Footstep,
	Player,
	Pickup,
	World,
};

/** Concurrency groups of ADR 0006 section 9 (None = unlimited). */
UENUM(BlueprintType)
enum class EVeloxAudioConcurrency : uint8
{
	None,
	Weapons,
	Footsteps,
	Impacts,
};

/** A sound under a short name (announcer lines: "three", "fight", ...). */
USTRUCT(BlueprintType)
struct VELOX_API FVeloxNamedSound
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundBase> Sound;
};

UCLASS(Config = Game, DefaultConfig, Meta = (DisplayName = "Velox Audio"))
class VELOX_API UVeloxAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UVeloxAudioSettings();

	//~UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return TEXT("Velox"); }
	//~End of UDeveloperSettings interface

	/** The project's settings (class defaults are valid without a config file, so this never returns null). */
	static const UVeloxAudioSettings& Get();

	// ---- Cadence and thresholds ----

	/** Ground travel between two footsteps (cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Footsteps", Meta = (ClampMin = "10", Units = "cm"))
	float FootstepDistanceCm = 170.f;

	/** Below this horizontal speed (cm/s) a pawn is silent. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Footsteps", Meta = (ClampMin = "0", Units = "cm/s"))
	float FootstepMinSpeed = 100.f;

	/** Foot sockets on the character mesh; without them the sound comes from the actor location, offset sideways. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Footsteps")
	FName FootSocketLeft = TEXT("foot_l");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Footsteps")
	FName FootSocketRight = TEXT("foot_r");

	/** Fallback foot offset from the actor location, sideways (cm), when the mesh has no foot sockets. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Footsteps", Meta = (ClampMin = "0", Units = "cm"))
	float FootFallbackSideCm = 15.f;

	/** Landing sounds: |Vz| at or above LandSoftVz is a soft landing, at or above LandHardVz a hard one (cm/s). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Landing", Meta = (ClampMin = "0", Units = "cm/s"))
	float LandSoftVz = 400.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Landing", Meta = (ClampMin = "0", Units = "cm/s"))
	float LandHardVz = 1000.f;

	/** Pain bands by remaining health (Q3): below PainHeavyBelow heavy, below PainMediumBelow medium, else light. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Pain", Meta = (ClampMin = "0"))
	float PainHeavyBelow = 25.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Pain", Meta = (ClampMin = "0"))
	float PainMediumBelow = 50.f;

	/** A health loss smaller than this is not pain (the 1 HP/s decay above max health must stay silent). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Pain", Meta = (ClampMin = "0"))
	float PainMinDrop = 2.f;

	/** Simulated proxies (no jump event): a ground-to-air transition with an upward speed in [Min, Max] (cm/s) is a jump. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Jump", Meta = (ClampMin = "0", Units = "cm/s"))
	float ProxyJumpMinVz = 200.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Jump", Meta = (ClampMin = "0", Units = "cm/s"))
	float ProxyJumpMaxVz = 600.f;

	/** Empty weapon: the dry-fire click plays at most once per this many seconds (Q3 clicks at the weapon rate; a fixed interval is indistinguishable). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Weapons", Meta = (ClampMin = "0", Units = "s"))
	float DryFireMinInterval = 0.25f;

	/** Movable lights of muzzle flashes and rockets alive at the same time (ADR 0006 section 9); colour and intensity come from UVeloxWeaponCosmetics. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Lights", Meta = (ClampMin = "0"))
	int32 MaxMovableLights = 2;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Lights", Meta = (ClampMin = "0", Units = "s"))
	float MuzzleLightSeconds = 0.08f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Lights", Meta = (ClampMin = "0", Units = "cm"))
	float MuzzleLightRadius = 600.f;

	// ---- Announcer ----

	/** Lines waiting behind the one playing; a newer line beyond this drops the oldest waiting one. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Announcer", Meta = (ClampMin = "0"))
	int32 AnnouncerQueueDepth = 2;

	/** Lead lines (taken / lost / tied) are at most one per this many seconds. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Announcer", Meta = (ClampMin = "0", Units = "s"))
	float LeadLineCooldown = 5.f;

	/** "Excellent": a second frag within this many seconds of the previous one. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Announcer", Meta = (ClampMin = "0", Units = "s"))
	float ExcellentWindowSeconds = 2.f;

	/** "Impressive": this many consecutive Railgun frags. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Announcer", Meta = (ClampMin = "1"))
	int32 ImpressiveRailCount = 2;

	/** "One minute": the Live phase announces it when this many seconds remain. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Announcer", Meta = (ClampMin = "1", Units = "s"))
	float OneMinuteSeconds = 60.f;

	/** Kill feed entries older than this when they arrive (late joiners get the earlier ones) are not announced. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Announcer", Meta = (ClampMin = "0", Units = "s"))
	float FragEventMaxAgeSeconds = 3.f;

	/** Announcer lines by name: three two one fight quad_damage excellent impressive lead_taken lead_lost lead_tied one_minute overtime match_over. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Announcer")
	TArray<FVeloxNamedSound> AnnouncerLines;

	/** Pushed while a line plays: lowers SC_Effects by 6 dB. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Announcer")
	TSoftObjectPtr<USoundMix> AnnouncerMix;

	// ---- Assets ----

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Attenuation")
	TSoftObjectPtr<USoundAttenuation> AttenuationWeaponFire;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Attenuation")
	TSoftObjectPtr<USoundAttenuation> AttenuationExplosion;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Attenuation")
	TSoftObjectPtr<USoundAttenuation> AttenuationImpact;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Attenuation")
	TSoftObjectPtr<USoundAttenuation> AttenuationFootstep;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Attenuation")
	TSoftObjectPtr<USoundAttenuation> AttenuationPlayer;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Attenuation")
	TSoftObjectPtr<USoundAttenuation> AttenuationPickup;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Attenuation")
	TSoftObjectPtr<USoundAttenuation> AttenuationWorld;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Concurrency")
	TSoftObjectPtr<USoundConcurrency> ConcurrencyWeapons;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Concurrency")
	TSoftObjectPtr<USoundConcurrency> ConcurrencyFootsteps;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Concurrency")
	TSoftObjectPtr<USoundConcurrency> ConcurrencyImpacts;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	TSoftObjectPtr<USoundBase> HitConfirmSound;
	/** The one global dry-fire click and weapon-switch click (Q3 parity). The weapons' own sounds live in UVeloxWeaponCosmetics (review 0006 D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	TSoftObjectPtr<USoundBase> DryFireSound;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	TSoftObjectPtr<USoundBase> WeaponSwitchSound;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player")
	TSoftObjectPtr<USoundBase> JumpSound;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player")
	TSoftObjectPtr<USoundBase> LandSoftSound;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player")
	TSoftObjectPtr<USoundBase> LandHardSound;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player")
	TSoftObjectPtr<USoundBase> PainLightSound;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player")
	TSoftObjectPtr<USoundBase> PainMediumSound;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player")
	TSoftObjectPtr<USoundBase> PainHeavySound;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player")
	TSoftObjectPtr<USoundBase> DeathSound;
	/** Footstep variations (one surface); picked in turn. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player")
	TArray<TSoftObjectPtr<USoundBase>> FootstepSounds;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "World")
	TSoftObjectPtr<USoundBase> JumpPadSound;
	/** Item respawn when the item definition names none. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "World")
	TSoftObjectPtr<USoundBase> ItemRespawnSound;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "World")
	TSoftObjectPtr<USoundBase> UIClickSound;

	// ---- Lookups ----

	/** The announcer line by short name (soft pointer, may be null). */
	TSoftObjectPtr<USoundBase> FindAnnouncerLine(FName Name) const;
	USoundAttenuation* LoadAttenuation(EVeloxAudioAttenuation Kind) const;
	USoundConcurrency* LoadConcurrency(EVeloxAudioConcurrency Kind) const;

	// ---- Play helpers (client only; Verbose "VeloxAudio" attempt lines) ----

	/** False on a dedicated server (and without a world): the server never plays or loads audio. */
	static bool CanPlayAudio(const UWorld* World);

	/**
	 * Loads every referenced sound once so a first shot does not hitch (clients only). The strong references live in
	 * UVeloxAudioPreloadSubsystem (an engine subsystem), never on this CDO: in a cooked build the CDO sits in the
	 * Disregard-for-GC pool, so references it gains at runtime are invisible to GC (task M6-01b).
	 * Logs `VeloxAudio dedicated server: no audio assets loaded` at Verbose on a dedicated server instead.
	 */
	static void EnsureLoaded(const UWorld* World);

	/** Every non-null soft reference above that EnsureLoaded preloads (attenuation, concurrency, mix, announcer lines, sounds). */
	void GetPreloadPaths(TArray<FSoftObjectPath>& OutPaths) const;

	/** 3D one-shot at Location with the attenuation asset and concurrency group; returns whether a sound was started. */
	static bool PlayAt(const UWorld* World, const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location, EVeloxAudioAttenuation Attenuation,
		EVeloxAudioConcurrency Concurrency, const TCHAR* DebugName, float VolumeMultiplier = 1.f);

	/** 2D one-shot (own weapon, own pickups, UI). Own sounds use no concurrency group, so they are never stolen. */
	static bool Play2D(const UWorld* World, const TSoftObjectPtr<USoundBase>& Sound, const TCHAR* DebugName, float VolumeMultiplier = 1.f);

	/**
	 * Whether Pawn is the pawn of a local human player: a player controller that is local. APawn::IsLocallyControlled alone is
	 * true for every AI controller on the authority (AController::IsLocalController), so bots would play "own" 2D sounds
	 * on a listen server or standalone (review 0006 D1).
	 */
	static bool IsLocalPlayerPawn(const APawn* Pawn);

	/** The UI click (menu buttons), 2D. WorldContext is any object in the game world. */
	static void PlayUIClick(const UObject* WorldContext);

	/** Number of play attempts since startup that reached a loaded sound (tests, logs). */
	static int32 GetNumPlayed() { return NumPlayed; }

private:
	// No runtime state that references UObjects here: this class's CDO is Disregard-for-GC in cooked builds (task M6-01b,
	// guarded by Velox.Core.SettingsCdoNoRuntimeRefs).
	static bool bLoggedServer;
	static int32 NumPlayed;
};
