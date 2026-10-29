// Velox - every pickup of a world (ADR 0004 sections 1, 4, 9), on the server and on clients. The HUD's item timers and the
// M4 bots read item state only through this API. Pickups register in BeginPlay and unregister in EndPlay; the take /
// respawn delegates fire on the server from AVeloxPickup and on clients from its replication.

#pragma once

#include "GameplayTagContainer.h"
#include "Items/VeloxPickupRules.h"
#include "Subsystems/WorldSubsystem.h"

#include "VeloxItemRegistrySubsystem.generated.h"

class APlayerState;
class AVeloxPickup;
class UWorld;

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
	TArray<TWeakObjectPtr<AVeloxPickup>> Pickups;
	FVeloxItemModeRules ModeRules;
};
