// Velox - every pickup of a world (ADR 0004 sections 1, 4, 9), on the server and on clients. The HUD's item timers and the
// M4 bots read item state only through this API. Pickups register in BeginPlay and unregister in EndPlay; the take /
// respawn delegates fire on the server from AVeloxPickup and on clients from its replication.
//
// ADR 0007 section 5.5: respawn times leave the server only toward entitled connections. The server registry writes the
// major items' times into AVeloxPlayerState::ItemTimers (owner-only) of every PlayerState the mode rule entitles; a dead
// player (NAME_Spectating, !bOnlySpectator) is a player and gets nothing.

#pragma once

#include "GameplayTagContainer.h"
#include "GameModes/VeloxMatchRules.h"
#include "Items/VeloxPickupRules.h"
#include "Subsystems/WorldSubsystem.h"

#include "VeloxItemRegistrySubsystem.generated.h"

class APlayerState;
class AVeloxPickup;
class AVeloxPlayerState;
class UVeloxGameModeDefinition;
class UWorld;

namespace VeloxItemTimerRules
{
	/**
	 * ADR 0007 section 5.5 entitlement, world-free: Spectators -> bOnlySpectator only (a dead player in the engine's
	 * spectating state is not one); Everyone -> every human; Nobody -> none. Bots have no connection and never get a copy.
	 */
	inline bool IsEntitled(EVeloxItemTimerVisibility Rule, bool bOnlySpectator, bool bIsBot)
	{
		if (bIsBot)
		{
			return false;
		}
		return Rule == EVeloxItemTimerVisibility::Everyone || (Rule == EVeloxItemTimerVisibility::Spectators && bOnlySpectator);
	}
}

UCLASS()
class VELOX_API UVeloxItemRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVeloxItemRegistrySubsystem* Get(const UWorld* World);

	void RegisterPickup(AVeloxPickup* Pickup);
	void UnregisterPickup(AVeloxPickup* Pickup);

	/** Every registered pickup (stale entries removed). */
	TArray<AVeloxPickup*> GetPickups() const;
	/** Pickups whose definition is bMajorItem (Mega, RA, YA, powerups: item timers, duel bots). */
	TArray<AVeloxPickup*> GetMajorPickups() const;
	/** Pickups whose definition's ItemTag matches ItemTag (exact or child: Velox.Item.Health finds every health item). */
	TArray<AVeloxPickup*> FindByTag(const FGameplayTag& ItemTag) const;

	/**
	 * Mode rules every pickup reads at take time (ItemRespawnScale, WeaponRespawnSeconds, weapon-stay). Server; the game
	 * mode sets them from its UVeloxGameModeDefinition (task 05). Defaults = the FFA/Duel values of ADR 0004 section 5.
	 */
	const FVeloxItemModeRules& GetModeRules() const { return ModeRules; }
	void SetModeRules(const FVeloxItemModeRules& InRules) { ModeRules = InRules; }
	/**
	 * Server: takes ModeRules from AVeloxGameState::GetGameModeDefinition() (null in the base mode and before the
	 * GameState exists: the FFA defaults). Called by AVeloxGameMode whenever it hands the definition to the GameState.
	 */
	void RefreshModeRules();
	/** Rules of a definition (null = the FFA defaults). */
	static FVeloxItemModeRules MakeModeRules(const UVeloxGameModeDefinition* Definition);

	/**
	 * Who sees item respawn timers (ADR 0004 section 4). Read live from the GameState's definition, so it is valid on
	 * clients too (the definition replicates initial-only); null definition = Spectators (the FFA/Duel default).
	 */
	EVeloxItemTimerVisibility GetItemTimerVisibility() const;

	/** The registered pickup with this server-assigned index (both sides; the index replicates initial-only). */
	AVeloxPickup* FindPickupByIndex(uint8 ItemIndex) const;

	/** Server: whether PlayerState may receive item timers under the running mode's rule (VeloxItemTimerRules::IsEntitled). */
	bool IsEntitledToItemTimers(const APlayerState* PlayerState) const;
	/**
	 * Server: re-evaluates PlayerState's entitlement and writes the full set of major-item timers into its ItemTimers when
	 * entitled, or clears them (PostLogin, ServerRequestJoin, the spectating-only switch).
	 */
	void PushTimersTo(AVeloxPlayerState* PlayerState) const;
	/** Server: PushTimersTo for every PlayerState (level reset, mode rule change). */
	void RefreshAllItemTimers() const;
	/** Server: a major pickup's respawn time changed (take, respawn, initial schedule, reset); updates entitled PlayerStates. */
	void HandleItemTimerChanged(const AVeloxPickup& Pickup) const;

	/** (Pickup, Taker). Server: on the take. Clients: when the take replicates (TakeCount changed). */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnVeloxPickupTaken, AVeloxPickup* /*Pickup*/, APlayerState* /*TakenBy*/);
	FOnVeloxPickupTaken OnPickupTaken;

	/** Server: the item is available again. Clients: RespawnServerTime replicated back to 0. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnVeloxPickupRespawned, AVeloxPickup* /*Pickup*/);
	FOnVeloxPickupRespawned OnPickupRespawned;

protected:
	//~UWorldSubsystem interface
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~End of UWorldSubsystem interface

private:
	void BuildTimerSet(TArray<struct FVeloxItemTimerEntry>& OutTimers) const;

	TArray<TWeakObjectPtr<AVeloxPickup>> Pickups;
	FVeloxItemModeRules ModeRules;
	/** Server: next ItemIndex (wraps at 256; a map has far fewer pickups). */
	uint8 NextItemIndex = 0;
};
