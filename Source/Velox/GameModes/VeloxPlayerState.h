// Velox - player state. Owns the ability system component so abilities and inventory survive respawn (ADR 0001 section 2).

#pragma once

#include "AbilitySystemInterface.h"
#include "Combat/VeloxAbilitySet.h"
#include "GameFramework/PlayerState.h"

#include "VeloxPlayerState.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UVeloxAbilitySystemComponent;
class UVeloxAmmoSet;
class UVeloxCharacterCosmetics;
class UVeloxCombatSettings;
class UVeloxHealthSet;
class UVeloxPawnData;
class UVeloxWeaponInventoryComponent;

/**
 * One major item's respawn time as the server sends it to an entitled PlayerState (ADR 0007 section 5.5): the pickup's
 * registry index and the server world time of the respawn (0 = available). 5 bytes of payload.
 */
USTRUCT()
struct FVeloxItemTimerEntry
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 ItemIndex = 0;

	UPROPERTY()
	float RespawnServerTime = 0.f;

	bool operator==(const FVeloxItemTimerEntry& Other) const { return ItemIndex == Other.ItemIndex && RespawnServerTime == Other.RespawnServerTime; }
};

UCLASS(Config = Game)
class VELOX_API AVeloxPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AVeloxPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UObject interface
	virtual void PostInitProperties() override;
	//~End of UObject interface

	//~AActor interface
	/** Preloads the combat settings on server and clients (ADR 0003 B1), before any replicated attribute is clamped. */
	virtual void PostInitializeComponents() override;
	/** Server: asks the game mode for a player colour (dummies and bots get a PlayerState too). */
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** AGameModeBase::ResetLevel (match restart, ADR 0004 section 5): the engine zeroes Score; this also clears ready, kills and deaths. */
	virtual void Reset() override;
	//~End of AActor interface

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End of IAbilitySystemInterface

	UFUNCTION(BlueprintPure, Category = "Velox|PlayerState")
	UVeloxAbilitySystemComponent* GetVeloxAbilitySystemComponent() const;

	const UVeloxHealthSet* GetHealthSet() const { return HealthSet; }
	const UVeloxAmmoSet* GetAmmoSet() const { return AmmoSet; }

	UFUNCTION(BlueprintPure, Category = "Velox|PlayerState")
	UVeloxWeaponInventoryComponent* GetWeaponInventory() const { return WeaponInventory; }

	/** Combat tunables (config asset, preloaded in PostInitializeComponents; lazy load as fallback); null if missing. */
	const UVeloxCombatSettings* GetCombatSettings() const;

	/**
	 * Server, every possession (ADR 0003 section 1): grants PawnData's ability sets the first time only (abilities live on
	 * the PlayerState and survive respawn), resets the weapon inventory to the spawn loadout (B5), then applies
	 * GE_SpawnInit. Called by AVeloxCharacter::PossessedBy after InitAbilityActorInfo.
	 */
	void OnPawnPossessed(const UVeloxPawnData* PawnData);

	/**
	 * Server: grants PawnData->AbilitySets once per PlayerState; later calls with the same pawn data are no-ops. A
	 * different pawn data (ADR 0003 B9) takes the previous grant away and grants the new one. Returns true if it granted.
	 */
	bool GrantDefaultAbilitySets(const UVeloxPawnData* PawnData);

	bool HasGrantedDefaultAbilitySets() const { return bDefaultAbilitySetsGranted; }

	/** Server: GE_SpawnInit (health/armor/ammo from the settings, clears Dead/SpawnProtected, restarts the decay). */
	void ApplySpawnInit();

	/**
	 * Server: GE_SpawnProtection for UVeloxCombatSettings::SpawnProtectionSeconds (0 = mechanism off). Applied after
	 * GE_SpawnInit on every possession; removed early by the first shot when bSpawnProtectionEndsOnFire.
	 */
	void ApplySpawnProtection();
	/** As above with explicit settings (tests, game modes with their own tunables). */
	void ApplySpawnProtection(const UVeloxCombatSettings& Settings);

	/**
	 * Name for log lines and the kill feed (ADR 0003 B29): the player name when set, else the object name ("-" for
	 * null). AI PlayerStates have no name until M4, humans get theirs with M6.
	 */
	static FString VeloxPlayerLabel(const APlayerState* PlayerState);

	/**
	 * Skin (ADR 0006 section 4.3, was PlayerColorIndex of ADR 0003 section 10 / B29): index chosen by the game mode
	 * (first unused palette entry), replicated as one byte. The skin list order is the old palette order, so the index
	 * still names the HUD/scoreboard/kill-feed colour.
	 */
	static constexpr uint8 NoSkin = 255;
	bool HasSkin() const { return SkinIndex != NoSkin; }
	uint8 GetSkinIndex() const { return SkinIndex; }
	/** Server: sets the index and refreshes the pawn's body and skin. */
	void SetSkinIndex(uint8 InIndex);

	/**
	 * The skin's BodyColor (sRGB) when the character cosmetics are loaded and list SkinIndex, else the palette entry of
	 * UVeloxCombatSettings (identical for the first skins; the only source on a dedicated server), else white.
	 */
	FColor GetPlayerColor() const;

	/** Client: the cosmetics the pawn loaded (GetPlayerColor reads the skin colours from it). Weak; not replicated. */
	void SetCharacterCosmetics(const UVeloxCharacterCosmetics* InCosmetics);

	/** Pre-ADR 0006 names, kept so the game mode, bots, HUD view model, kill feed and tests compile unchanged. */
	static constexpr uint8 NoPlayerColor = NoSkin;
	bool HasPlayerColor() const { return HasSkin(); }
	uint8 GetPlayerColorIndex() const { return SkinIndex; }
	void SetPlayerColorIndex(uint8 InIndex) { SetSkinIndex(InIndex); }

	/** Frags and deaths of the current match (ADR 0004 section 6); the engine Score keeps the +1/-1 rule. Zeroed by Reset. */
	int32 GetKills() const { return Kills; }
	int32 GetDeaths() const { return Deaths; }
	/** Server (AVeloxGameMode::PlayerDied only). */
	void AddKill();
	void AddDeath();

	/** Ready-up (ADR 0004 section 5): replicated, cleared by the game mode on phase changes and by Reset. */
	bool IsReady() const { return bReady; }
	/** Server. Returns true if the value changed. */
	bool SetReady(bool bInReady);

	/**
	 * Item timers this connection is entitled to (ADR 0007 section 5.5): owner-only, written by UVeloxItemRegistrySubsystem
	 * on the server, empty for players (alive or dead). Sorted by ItemIndex.
	 */
	const TArray<FVeloxItemTimerEntry>& GetItemTimers() const { return ItemTimers; }
	/** The entry of the pickup with this registry index; null when the server sent none. */
	const FVeloxItemTimerEntry* GetItemTimer(uint8 ItemIndex) const;
	/** Server: replaces the whole set (entitlement gained / re-evaluated). */
	void SetItemTimers(TArray<FVeloxItemTimerEntry>&& InTimers);
	/** Server: adds or updates one entry (a take or respawn of a major item). */
	void SetItemTimer(uint8 ItemIndex, float RespawnServerTime);
	/** Server: entitlement lost (ServerRequestJoin, logout) or level reset. */
	void ClearItemTimers();
	/** "n=<count> entries=[<item>#<index>@<time>,...]" (log lines; item names from this world's registry). */
	FString DescribeItemTimers() const;

protected:
	/**
	 * Replication rate for this PlayerState (Hz). The 1 Hz engine default is far too low because the ASC lives here;
	 * the server tick caps the effective rate. Set in DefaultGame.ini; values <= 0 keep the engine default.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Net")
	float ConfigNetUpdateFrequency = 0.0f;

	/** Combat settings asset. Set in DefaultGame.ini (ADR 0003 names the game mode; the PlayerState exists on clients too). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Combat")
	TSoftObjectPtr<UVeloxCombatSettings> CombatSettingsAsset;

private:
	UFUNCTION()
	void OnRep_SkinIndex();

	UFUNCTION()
	void OnRep_ItemTimers();

	void RefreshPawnColor() const;
	void HandleAbilityActivated(UGameplayAbility* Ability);

	UPROPERTY(ReplicatedUsing = OnRep_SkinIndex)
	uint8 SkinIndex = NoSkin;

	TWeakObjectPtr<const UVeloxCharacterCosmetics> CharacterCosmetics;

	UPROPERTY(Replicated)
	bool bReady = false;

	UPROPERTY(Replicated)
	int32 Kills = 0;

	UPROPERTY(Replicated)
	int32 Deaths = 0;

	/** ADR 0007 section 5.5: COND_OwnerOnly, push model. */
	UPROPERTY(ReplicatedUsing = OnRep_ItemTimers)
	TArray<FVeloxItemTimerEntry> ItemTimers;

	UPROPERTY(VisibleAnywhere, Category = "Velox|PlayerState")
	TObjectPtr<UVeloxAbilitySystemComponent> AbilitySystemComponent;

	/** Attribute sets are default subobjects (found by the ASC in InitializeComponent), not granted. */
	UPROPERTY()
	TObjectPtr<UVeloxHealthSet> HealthSet;

	UPROPERTY()
	TObjectPtr<UVeloxAmmoSet> AmmoSet;

	UPROPERTY(VisibleAnywhere, Category = "Velox|PlayerState")
	TObjectPtr<UVeloxWeaponInventoryComponent> WeaponInventory;

	UPROPERTY(Transient)
	mutable TObjectPtr<const UVeloxCombatSettings> CombatSettings;

	/** What GrantDefaultAbilitySets granted (server). UPROPERTY: holds TObjectPtr<UAttributeSet> (ADR 0003 B9). */
	UPROPERTY()
	FVeloxAbilitySet_GrantedHandles DefaultAbilitySetHandles;

	/** The pawn data the default ability sets were granted from (server). */
	UPROPERTY()
	TObjectPtr<const UVeloxPawnData> GrantedPawnData;

	bool bDefaultAbilitySetsGranted = false;
};
