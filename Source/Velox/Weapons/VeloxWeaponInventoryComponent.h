// Velox - weapon inventory on AVeloxPlayerState (ADR 0003 section 4): a bitmask over the weapon table, the current
// weapon index, and the granted fire ability per owned weapon. Owning a weapon = its fire ability is granted with
// SourceObject = the definition (ability specs replicate to the owner, so the first shot after a pickup or a switch is
// predictable); equipping grants nothing. Lyra's inventory/equipment/weapon-instance actors are deliberately not used.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "Engine/TimerHandle.h"

#include "VeloxWeaponInventoryComponent.generated.h"

class UAbilitySystemComponent;
class UVeloxAbilitySystemComponent;
class UVeloxCombatSettings;
class UVeloxGameplayAbility_WeaponSwitch;
class UVeloxWeaponDefinition;
class UVeloxWeaponTable;
struct FGameplayAbilitySpec;
struct FGameplayCueParameters;

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxWeaponInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVeloxWeaponInventoryComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static constexpr uint8 NoWeapon = 0xFF;

	/** The inventory of a PlayerState, or of the PlayerState of a pawn / controller. */
	static UVeloxWeaponInventoryComponent* Find(const AActor* Actor);

	const UVeloxWeaponTable* GetWeaponTable() const;
	const UVeloxCombatSettings* GetCombatSettings() const;
	int32 NumWeapons() const;
	const UVeloxWeaponDefinition* GetDefinition(int32 Index) const;
	int32 IndexOf(const UVeloxWeaponDefinition* Definition) const;

	bool OwnsIndex(int32 Index) const { return Index >= 0 && Index < 16 && (OwnedMask & (1u << Index)) != 0; }
	uint16 GetOwnedMask() const { return OwnedMask; }

	/** Current weapon; on the owning client the locally predicted switch result until the server confirms it. */
	int32 GetCurrentIndex() const;
	const UVeloxWeaponDefinition* GetCurrentDefinition() const { return GetDefinition(GetCurrentIndex()); }
	/** The replicated (server) value. */
	int32 GetAuthoritativeIndex() const { return CurrentIndex == NoWeapon ? INDEX_NONE : CurrentIndex; }
	/** Server: target of a running switch (INDEX_NONE otherwise). */
	int32 GetPendingIndex() const { return PendingIndex == NoWeapon ? INDEX_NONE : PendingIndex; }
	const UVeloxWeaponDefinition* GetPendingDefinition() const { return GetDefinition(GetPendingIndex()); }

	/** The fire ability spec of weapon Index (by SourceObject; works on the owning client). Null if not granted. */
	FGameplayAbilitySpec* FindFireSpec(int32 Index) const;

	/** Q3 weapon select: Slot<N> = the owned weapon in slot N, Next/Prev = cycle owned weapons from the current/pending one. */
	int32 ResolveSelectInput(const FGameplayTag& InputTag) const;

	/** Increments and returns the per-PlayerState shot counter (ADR 0003 section 5). Each side counts its own shots. */
	uint8 AllocateShotSeq() { return ++ShotSeqCounter; }

	// --- Server ---
	/** Owns Definition (grants its fire ability once) and adds Ammo (negative: PickupAmmo). Selects it if nothing is selected. */
	bool AddWeapon(const UVeloxWeaponDefinition* Definition, int32 Ammo = -1);
	/** Owns exactly the spawn loadout of the combat settings and selects its first entry (Q3: everything is lost on death). Ammo comes from GE_SpawnInit. */
	void ResetToLoadout();
	void RemoveAllWeapons();
	/** Server: the authoritative current weapon (also used by tests and cheats to skip the switch). Clears the pending index. */
	void SetCurrentIndex(int32 Index);
	void SetPendingIndex(int32 Index);
	/**
	 * Server: starts Definition's looping BeamCueTag (LG) if needed and keeps it for 2.5 fire intervals after this shot.
	 * Stopped on timeout (released), on a weapon change, and by RemoveAllGameplayCues on death (task 06, B8).
	 */
	void KeepBeamAlive(const UVeloxWeaponDefinition& Definition, const FGameplayCueParameters& Parameters);
	void StopBeam();

	// --- Owning client ---
	/** Local prediction of a switch result; replaced by the replicated value when it arrives or after PredictionTimeout. */
	void PredictCurrentIndex(int32 Index);

	/** (Old, New) whenever the effective current index changes on this machine. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnVeloxWeaponChanged, int32 /*OldIndex*/, int32 /*NewIndex*/);
	FOnVeloxWeaponChanged OnCurrentWeaponChanged;

	//~UActorComponent interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~End of UActorComponent interface

protected:
	UFUNCTION()
	void OnRep_CurrentIndex(uint8 OldIndex);

	UFUNCTION()
	void OnRep_OwnedMask();

	/** Granted once per PlayerState (one spec bound to InputTag.Weapon.Slot1..9/Next/Prev). */
	UPROPERTY(EditDefaultsOnly, Category = "Velox|Weapons")
	TSubclassOf<UVeloxGameplayAbility_WeaponSwitch> SwitchAbilityClass;

	/** Seconds a locally predicted switch result may stand without server confirmation. */
	UPROPERTY(EditDefaultsOnly, Category = "Velox|Weapons", Meta = (Units = "s"))
	float PredictionTimeout = 1.0f;

private:
	UVeloxAbilitySystemComponent* GetVeloxASC() const;
	void GrantFireAbility(int32 Index);
	void RemoveFireAbility(int32 Index);
	void GrantSwitchAbility();
	void StopBeamOf(int32 Index);

	/** Bit i = weapon table index i is owned. Owner only. */
	UPROPERTY(ReplicatedUsing = OnRep_OwnedMask)
	uint16 OwnedMask = 0;

	/** Table index of the current weapon, NoWeapon if none. Everyone (M5 third-person weapon models). */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentIndex)
	uint8 CurrentIndex = NoWeapon;

	uint8 PendingIndex = NoWeapon;

	/** Server: fire ability spec per table index (invalid when not owned or the weapon has no fire ability). */
	TArray<FGameplayAbilitySpecHandle> FireSpecHandles;
	FGameplayAbilitySpecHandle SwitchSpecHandle;

	FTimerHandle BeamTimer;
	FGameplayTag ActiveBeamCueTag;

	int32 PredictedIndex = INDEX_NONE;
	double PredictionExpireTime = 0.0;

	uint8 ShotSeqCounter = 0;
};
