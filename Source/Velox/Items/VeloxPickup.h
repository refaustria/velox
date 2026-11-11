// Velox - one placed item (ADR 0004 sections 1, 2, 4). Server-authoritative grant through the taker's PlayerState ASC,
// Q3 rules from VeloxPickupRules.h, respawn timer on the server, availability and a take counter replicated to everyone
// (always relevant, dormant between state changes). The respawn time never leaves the server through this actor (ADR 0007
// section 5.5): entitled spectators get it owner-only on their own PlayerState (UVeloxItemRegistrySubsystem). The owning
// client predicts only the take event (hide, sound, a predicted weapon slot), never attributes; the replicated TakeCount /
// LastTakenBy confirm or revoke it.
//
// Placement: the actor origin is the floor point; the trigger (Q3 ITEM_RADIUS 15 u box, z -15..+21 u around the Q3 item
// origin, which Q3 drops to 15 u above the floor) spans 0..90 cm above it.

#pragma once

#include "GameFramework/Actor.h"
#include "Items/VeloxPickupRules.h"
#include "TimerManager.h"

#include "VeloxPickup.generated.h"

class APlayerState;
class AVeloxCharacter;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UVeloxItemDefinition;
class UVeloxWeaponInventoryComponent;
struct FHitResult;

UCLASS(Config = Game)
class VELOX_API AVeloxPickup : public AActor
{
	GENERATED_BODY()

public:
	AVeloxPickup(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Server: spawns a pickup of Definition with its origin at FloorLocation (cheats, tests; maps place them instead). */
	static AVeloxPickup* SpawnPickup(UWorld* World, const UVeloxItemDefinition* Definition, const FVector& FloorLocation);

	/** /Game/Items/DA_Item_<Type>_<Name> for "Health.Medium", "Health_Medium" or the asset name; null if not found. */
	static const UVeloxItemDefinition* LoadDefinitionByName(const FString& ItemName);

	const UVeloxItemDefinition* GetDefinition() const { return Definition; }
	/** Before BeginPlay only (deferred spawn: cheat, tests); placed pickups set it in the level. */
	void SetDefinition(const UVeloxItemDefinition* InDefinition);

	/** Replicated availability (ignores a local prediction; see IsPredictedTaken). */
	bool IsAvailable() const { return bAvailable; }
	/** Server only (not replicated, ADR 0007 section 5.5): world time at which the item respawns; 0 = available. 0 on clients. */
	float GetRespawnServerTime() const { return RespawnServerTime; }
	/**
	 * Seconds until the respawn; 0 when available. Server: from the timer (bots, tests, cheats). Client: from the local
	 * player's owner-only AVeloxPlayerState::ItemTimers entry when it is entitled to one, else -1 (unknown on this machine).
	 */
	double GetTimeUntilRespawn() const;
	/** Server-assigned index (UVeloxItemRegistrySubsystem), replicated initial-only: the key of FVeloxItemTimerEntry. */
	uint8 GetItemIndex() const { return ItemIndex; }
	/** Server: set by the registry at registration, before the first replication. */
	void SetItemIndex(uint8 InIndex);
	APlayerState* GetLastTakenBy() const { return LastTakenBy; }
	uint8 GetTakeCount() const { return TakeCount; }
	/** Owning client: this machine predicted a take that the server has not confirmed or refused yet. */
	bool IsPredictedTaken() const { return bPredictionPending; }
	/** Whether the placeholder is drawn (available and not predicted taken). */
	bool IsPlaceholderVisible() const;
	/** Client: the definition's Mesh is shown (task M5-13) instead of the placeholder cube. Never true on a dedicated server. */
	bool IsShowingItemMesh() const;
	const UStaticMeshComponent* GetItemMeshComponent() const { return ItemMesh; }

	/**
	 * Server: Pawn takes the item if it is available, the pawn is alive and the Q3 rule grants something. Applies the
	 * grant through GAS, updates the replicated state, logs `VeloxPickup`, starts the respawn timer. False = the item
	 * stays (refused or unavailable). bLogRefusal logs a refusal (`VeloxPickupRefused`) at Log, else at Verbose.
	 */
	bool TryPickup(AVeloxCharacter* Pawn, bool bLogRefusal = true);

	/** Owning client: predicts the take of the locally controlled Pawn (cosmetics + predicted weapon slot). Idempotent. */
	bool TryPredictTake(AVeloxCharacter* Pawn);

	/** The rule query for Pawn's current attribute values (both sides; the client reads its replicated attributes). */
	bool BuildQuery(const AVeloxCharacter* Pawn, FVeloxPickupQuery& OutQuery) const;

	//~AActor interface
	/** Level reset (AGameModeBase::ResetLevel): back to the initial state, including a fresh initial spawn delay. */
	virtual void Reset() override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~End of AActor interface

	//~UObject interface
	/** ADR 0007 section 5.5 baseline rule: a new bunch may announce events again. */
	virtual void PreNetReceive() override;
	/** First bunch this actor ever received: adopt the state silently (engine: before CallRepNotifies, DataReplication.cpp:1604/1609). */
	virtual void PostNetReceive() override;
	//~End of UObject interface

#if WITH_DEV_AUTOMATION_TESTS
	/** Tests: forget the client baseline (as if no bunch had been received yet). */
	void TestResetNetBaseline();
	/**
	 * Tests: one received bunch in the engine's order (PreNetReceive, properties, PostNetReceive, the rep notifies of the
	 * changed properties). Run on a pickup switched to ROLE_SimulatedProxy.
	 */
	void TestReceiveBunch(uint8 InTakeCount, bool bInAvailable, APlayerState* InLastTakenBy);
	bool TestIsSuppressingNetEvents() const { return bSuppressNetEvents; }
#endif

protected:
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_Available();
	UFUNCTION()
	void OnRep_LastTakenBy();
	UFUNCTION()
	void OnRep_TakeCount();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Pickup")
	TObjectPtr<USceneComponent> Root;

	/** Overlaps Pawn only; ignores the Weapon trace channel and the Projectile object channel (ADR 0003 B15). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Pickup")
	TObjectPtr<UBoxComponent> Trigger;

	/** Greybox look from the definition (unlit colour, scale); no collision; rotates on clients. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Pickup")
	TObjectPtr<UStaticMeshComponent> Placeholder;

	/**
	 * The item's model (UVeloxItemDefinition::Mesh), a child of Placeholder so it turns with it; centred on its bounds so a weapon
	 * (grip origin) and a base-origin item both spin about their middle. Empty until the mesh is set; client only.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Pickup")
	TObjectPtr<UStaticMeshComponent> ItemMesh;

	/** The item. Placed pickups: set per instance in the level; initial-only replication for spawned ones. */
	UPROPERTY(EditInstanceOnly, Replicated, BlueprintReadOnly, Category = "Velox|Pickup")
	TObjectPtr<const UVeloxItemDefinition> Definition;

	/** No placeholder rotation (M5: suspended items do not bob). */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Velox|Pickup")
	bool bSuspended = false;

	/** Q3 cg.autoAngles: one turn per 2048 ms. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Pickup", Meta = (Units = "deg"))
	float PlaceholderRotationDegreesPerSecond = 360.f / 2.048f;

	/** Unlit material with a vector parameter PlaceholderColorParameter (the player body material). DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Pickup")
	TSoftObjectPtr<UMaterialInterface> PlaceholderMaterial;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Pickup")
	FName PlaceholderColorParameter = TEXT("Color");

private:
	double GetServerNow() const;
	bool IsPawnEligible(const AVeloxCharacter* Pawn) const;
	/** Refused now, but worth re-checking every frame while it stands inside (full health, intermission freeze). */
	bool ShouldRecheck(const AVeloxCharacter* Pawn) const;
	/** Pawns whose capsule overlaps the trigger (server: all; clients: only the locally controlled one). */
	void GetOverlappingPawns(TArray<AVeloxCharacter*>& OutPawns) const;
	/**
	 * Server: TryPickup for every pawn standing in the trigger (Q3 evaluates touches every frame); client: TryPredictTake
	 * for the local pawn. Keeps the actor ticking while an eligible pawn stands in an available item it was refused.
	 */
	void CheckOverlappingPawns(bool bLogRefusals);
	void SetOverlapRecheck(bool bEnable);

	bool GrantTo(AVeloxCharacter& Pawn, const FVeloxPickupGain& Gain);
	bool GrantPowerup(AVeloxCharacter& Pawn, const FVeloxPickupGain& Gain);
	void ScheduleInitialSpawn();
	void StartRespawnTimer(double Seconds);
	void ServerRespawn();
	/** Writes the timer into the replicated state (flushes dormancy first). */
	void CommitServerState();

	void HandleReplicatedState();
	void ClearPrediction(const TCHAR* Outcome);
	void OnPredictionTimeout();
	void DropPredictedWeapon();
	void UpdatePlaceholder();

	/** Server only: world time of the respawn; 0 = available. Not replicated (ADR 0007 section 5.5). */
	float RespawnServerTime = 0.f;

	/** Replicated availability (server: RespawnServerTime == 0). Clients hide / show and play the respawn cue on it. */
	UPROPERTY(ReplicatedUsing = OnRep_Available)
	bool bAvailable = true;

	/** Registry index (FVeloxItemTimerEntry::ItemIndex); initial-only. */
	UPROPERTY(Replicated)
	uint8 ItemIndex = 0;

	/** The last taker (spectator feed, prediction confirmation). */
	UPROPERTY(ReplicatedUsing = OnRep_LastTakenBy)
	TObjectPtr<APlayerState> LastTakenBy;

	/** +1 per take (wraps), so two takes between two replications are both seen. */
	UPROPERTY(ReplicatedUsing = OnRep_TakeCount)
	uint8 TakeCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PlaceholderMID;

	// Server.
	FVeloxPickupTimer Timer;
	FTimerHandle RespawnTimerHandle;
	/** F1: the standing-pawn check after Reset(), one tick later. */
	FTimerHandle ResetCheckTimerHandle;
	bool bOverlapRecheck = false;

	// Clients: the last replicated state handled, and the pending prediction.
	uint8 SeenTakeCount = 0;
	bool bSeenAvailable = true;
	/** ADR 0007 section 5.5: set by the first PostNetReceive, never cleared (survives dormancy channel re-opens). */
	bool bReceivedInitialState = false;
	/** Set for the first bunch (between PostNetReceive and the next PreNetReceive): rep notifies adopt silently. */
	bool bSuppressNetEvents = false;
	bool bPredictionPending = false;
	uint8 PredictedTakeCount = 0;
	double PredictedAt = 0.0;
	int32 PredictedWeaponIndex = INDEX_NONE;
	TWeakObjectPtr<UVeloxWeaponInventoryComponent> PredictedInventory;
	FTimerHandle PredictionTimerHandle;
};
