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
class UVeloxGameplayAbility_FireProjectile;
class UVeloxGameplayAbility_WeaponSwitch;
class UVeloxMovementComponent;
class UVeloxWeaponDefinition;
class UVeloxWeaponTable;
struct FGameplayAbilitySpec;
struct FGameplayCueParameters;
struct FVeloxMoveShot;

/**
 * ADR 0003 B35, server: a projectile shot registered from the owner's move stream (the fire move carried it). The owner's
 * self splash is already scheduled (EventId); the fire RPC adopts the record and only spawns the projectile, pays ammo,
 * plays the cue and finalises the switch.
 */
struct FVeloxMoveShotRecord
{
	uint8 ShotSeq = 0;
	int32 WeaponIndex = INDEX_NONE;
	/** After the B15 origin rule (snapped to the server eye at the move start if needed). */
	FVector Origin = FVector::ZeroVector;
	/** Unit direction. */
	FVector Dir = FVector::ForwardVector;
	/** As received in the move (the fire RPC is cross-checked against these). */
	FVector ClientOrigin = FVector::ZeroVector;
	FVector ClientDir = FVector::ForwardVector;
	float ClientFireMoveTimeStamp = 0.f;
	/** After ClampFireMoveTimeStamp against the move's start. */
	float FireMoveTimeStamp = 0.f;
	/** The scheduled self splash on the owner's movement component, and its explosion origin. */
	int32 EventId = 0;
	FVector SplashOrigin = FVector::ZeroVector;
	/** Server world time the move was processed (orphan expiry). */
	double ReceiveTime = 0.0;
	/** The fire RPC took it (a record is adopted once). */
	bool bAdopted = false;
};

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

	/** Owned per the replicated mask or, on the owning client, a pickup's unexpired prediction (PredictOwned). */
	bool OwnsIndex(int32 Index) const { return Index >= 0 && Index < 16 && (GetEffectiveOwnedMask() & (1u << Index)) != 0; }
	/** The replicated (server) mask. */
	uint16 GetOwnedMask() const { return OwnedMask; }
	/** OwnedMask | the unexpired predicted bits (owning client; equals OwnedMask on the server). */
	uint16 GetEffectiveOwnedMask() const;

	/**
	 * Owning client (ADR 0004 section 2): a predicted weapon pickup owns Index until local world time Until or until the
	 * replicated mask has it, so a slot press switches at once (Q3 predicts STAT_WEAPONS). The server validates the switch
	 * against its own mask as always. DropPredictedOwned removes the bit (pickup taken by someone else / refused).
	 */
	void PredictOwned(int32 Index, double Until);
	void DropPredictedOwned(int32 Index);

	/**
	 * Q3 cg_autoswitch (ADR 0004 section 2), world-free: the table index to select when the owned mask changes from
	 * OldMask to NewMask, or INDEX_NONE. Exactly one newly owned weapon (a pickup; GiveAllWeapons and a first replication
	 * from an empty mask never switch), the setting on, a current weapon that allows it (machine gun or gauntlet), and a
	 * target other than the current weapon.
	 */
	static int32 SelectAutoSwitchTarget(bool bEnabled, bool bCurrentAllowsAutoSwitch, int32 CurrentIndex, uint16 OldMask, uint16 NewMask);
	/** Weapons the autoswitch leaves (Q3 default behaviour: the machine gun and the gauntlet). */
	static bool AllowsAutoSwitchFrom(const UVeloxWeaponDefinition* Current);
	/** InputTag.Weapon.Slot<Slot> (1-9), invalid otherwise. */
	static FGameplayTag SlotInputTag(int32 Slot);

	/**
	 * Owning machine with a local player controller: presses the slot of the SelectAutoSwitchTarget result through the
	 * normal switch ability input. Clients: from OnRep_OwnedMask. Listen-server host / standalone (no OnRep on the
	 * authority): the weapon pickup calls it after AddWeapon.
	 */
	void RunAutoSwitch(uint16 OldMask, uint16 NewMask);

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

	/**
	 * ADR 0003 B21, server only: increments and returns the server's own pattern counter, once per accepted hitscan shot
	 * (remote client, listen host and bots alike). It seeds the MG spread / SG pellet pattern (VeloxShotDirs::MakeSeed with
	 * the PlayerId) and reaches receivers as FVeloxGameplayEffectContext::PatternSeq; the client's ShotSeq seeds nothing.
	 * Not replicated.
	 */
	uint8 AllocateServerPatternSeq() { return ++ServerPatternSeqCounter; }

	/** The last value AllocateServerPatternSeq returned (0 before the first shot). */
	uint8 GetServerPatternSeq() const { return ServerPatternSeqCounter; }

	/** Tests only: puts the server pattern counter into a known state (the next shot uses Value + 1). */
	void SetServerPatternSeqForTest(uint8 Value) { ServerPatternSeqCounter = Value; }

	/**
	 * ADR 0003 B25: the shot counter restarts per life, so the first shot of every life is 1 on both ends and matches the
	 * fresh movement component's ConfirmedSelfKnockbackSeq = 0. Server: ResetToLoadout; owning client: the first
	 * ability-actor-info init of each new pawn (AVeloxCharacter::InitAbilityActorInfo).
	 */
	void ResetShotSeq() { ShotSeqCounter = 0; }

	/**
	 * ADR 0003 B18: local world time at which the running switch ends on this machine (Now + Wait + Drop + Raise, set by
	 * the switch ability on every machine that runs it); 0 = no switch. The shooter's machine fires only at or after it.
	 * Not replicated.
	 */
	double GetSwitchEndTime() const { return SwitchEndTime; }
	void SetSwitchEndTime(double Time) { SwitchEndTime = Time; }

	/**
	 * Server: true for the first RejectLogLimitPerSecond shot rejections of this player in the current one-second window
	 * (logged at Log), false afterwards (Verbose): a hostile client cannot flood the log (review of task 04, nit 12).
	 */
	bool ShouldLogRejectionAtLog(double Now);

	// --- Server ---
	/** Owns Definition (grants its fire ability once) and adds Ammo (negative: PickupAmmo). Selects it if nothing is selected. */
	bool AddWeapon(const UVeloxWeaponDefinition* Definition, int32 Ammo = -1);
	/**
	 * Owns exactly the spawn loadout of the combat settings and selects its first entry (Q3: everything is lost on death).
	 * Ammo comes from GE_SpawnInit. Clears SwitchEndTime and the shot counter (ResetShotSeq).
	 */
	void ResetToLoadout();
	void RemoveAllWeapons();
	/**
	 * Server: the authoritative current weapon (also used by tests and cheats to skip the switch). Clears the pending
	 * index and the pending drop timer.
	 */
	void SetCurrentIndex(int32 Index);
	/**
	 * Server (ADR 0003 B37 iii): Index becomes the target of a running switch whose drop ends DropEndSeconds from now. The
	 * inventory owns that drop end: PendingDropTimer runs CompletePendingDrop then (DropEndSeconds <= 0: at once), so a
	 * superseding switch activation that ends the switch instance (B33) cannot orphan the pending switch. GE_WeaponSwitching
	 * keeps running through the raise; SwitchEndTime is not touched. INDEX_NONE clears the pending index and the timer.
	 */
	void SetPendingIndex(int32 Index, float DropEndSeconds);
	/** Tests and logs: the server's pending drop timer is armed. */
	bool IsPendingDropTimerActive() const;
	/**
	 * Server (B18, task 08b D4): an accepted shot of the pending weapon completes the running switch: the pending index
	 * becomes current, the switch ability instance still waiting for its (latency-shifted) drop end is ended, and
	 * GE_WeaponSwitching is removed. Without this a switch arriving in the same frame as that shot was rejected (the
	 * instanced-per-actor switch was still active). No-op without a pending index.
	 */
	void CompletePendingSwitch();
	/**
	 * Server (ADR 0003 B33): the inventory part of CompletePendingSwitch only: the pending index becomes current,
	 * GE_WeaponSwitching is removed and SwitchEndTime cleared; switch ability instances are left alone. Used by the switch
	 * ability when a superseding activation completes the superseded switch (under InstancedPerActor the active instance
	 * is that activation itself, which CompletePendingSwitch would end). No-op without a pending index.
	 */
	void CompletePendingSwitchIndexOnly();
	/**
	 * Server: starts Definition's looping BeamCueTag (LG) if needed and keeps it for BeamKeepAliveSeconds (combat settings,
	 * at least one fire interval) after this shot (B23).
	 * Stopped on timeout (released), on a weapon change, and by RemoveAllGameplayCues on death (task 06, B8).
	 */
	void KeepBeamAlive(const UVeloxWeaponDefinition& Definition, const FGameplayCueParameters& Parameters);
	void StopBeam();

	// --- Server: the move-carried projectile shot (ADR 0003 B35, ADR 0002 A36) ---
	/**
	 * Binds OnServerMoveShot / OnServerMoveProcessed of Movement (the possessed pawn's movement component; called from
	 * AVeloxCharacter::InitAbilityActorInfo on the authority) and unbinds a previously bound one. A new binding starts a
	 * new move stream: records, pending RPC shots and the processed-move state are cleared. Null: unbind only.
	 */
	void BindServerMoveStream(UVeloxMovementComponent* Movement);
	/** Unbinds Movement if it is the bound one (AVeloxCharacter::UnPossessed). */
	void UnbindServerMoveStream(const UVeloxMovementComponent* Movement);
	UVeloxMovementComponent* GetBoundServerMovement() const;

	/**
	 * OnServerMoveShot: the fire move of shot Shot.ShotSeq is about to be simulated (MoveEndStamp = its client time stamp,
	 * MoveDeltaTime = its server delta). B35 steps 1-6: owned projectile weapon with splash, sequence not recorded,
	 * stamp clamped against the move's start, origin through the B15 rule against the live eye (= the eye at the move
	 * start), the weapon's fire-rate token and ammo affordable (both skipped when the fire RPC overtook the move and is
	 * pending: it paid already), then the owner's self splash scheduled and the record kept. Logs VeloxMoveShot or
	 * VeloxMoveShotIgnored reason=.
	 */
	void HandleServerMoveShot(const FVeloxMoveShot& Shot, float MoveEndStamp, float MoveDeltaTime);

	/**
	 * OnServerMoveProcessed: resumes pending fire RPCs whose move-shot is now recorded (source=move) or whose fire stamp
	 * the move stream passed without one (source=rpc), and expires records whose RPC never came (VeloxMoveShotOrphan).
	 */
	void HandleServerMoveProcessed(float MoveEndStamp);

	/** The unadopted record of Seq, or null. */
	const FVeloxMoveShotRecord* FindMoveShotRecord(uint8 Seq) const;
	/** Takes the unadopted record of Seq (marks it adopted); false without one. */
	bool AdoptMoveShotRecord(uint8 Seq, FVeloxMoveShotRecord& OutRecord);

	/**
	 * A projectile fire RPC with FireMoveTimeStamp and no record must wait for its fire move: the move stream is bound,
	 * has processed a move, and has not yet passed the stamp (the RPC overtook the move, normally inside one packet).
	 */
	bool ShouldWaitForMoveShot(float FireMoveTimeStamp) const;

	/** The fire RPC of Seq waits for its move (UVeloxGameplayAbility_FireProjectile keeps the validated shot). */
	void AddPendingRpcShot(uint8 Seq, float FireMoveTimeStamp, UVeloxGameplayAbility_FireProjectile* Ability);
	void RemovePendingRpcShot(uint8 Seq, const UVeloxGameplayAbility_FireProjectile* Ability);
	bool HasPendingRpcShot(uint8 Seq) const;

	/**
	 * ADR 0003 B38: the rpc path (source=rpc) launched Seq and scheduled its self splash from the RPC's data; a fire move
	 * carrying Seq that arrives afterwards is ignored (reason=rpc_done) before the ammo/token step. Ring of
	 * MaxRpcLaunchedShotSeqs per life, cleared with the records.
	 */
	void NoteRpcLaunchedShot(uint8 Seq);
	bool WasShotLaunchedByRpc(uint8 Seq) const;

	/** Records whose RPC never came, this binding (an M6 anti-cheat signal). */
	int32 GetNumMoveShotOrphans() const { return NumMoveShotOrphans; }
	int32 GetNumMoveShotRecords() const { return ServerMoveShots.Num(); }
	/** Last client move time stamp the bound move stream processed; false before the first move. */
	bool GetLastProcessedMoveStamp(float& OutStamp) const { OutStamp = LastProcessedMoveStamp; return bServerMoveStreamSeen; }

	/** Drops every record and pending RPC shot (ResetToLoadout, rebinding); pending abilities end cancelled. */
	void ClearMoveShots();

	/** Records kept per life (ring). */
	static constexpr int32 MaxMoveShotRecords = 8;
	/** Sequences launched on the rpc path remembered per life (ring). */
	static constexpr int32 MaxRpcLaunchedShotSeqs = 8;

	// --- Owning client ---
	/** Local prediction of a switch result; replaced by the replicated value when it arrives or after PredictionTimeout. */
	void PredictCurrentIndex(int32 Index);

	/** (Old, New) whenever the effective current index changes on this machine. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnVeloxWeaponChanged, int32 /*OldIndex*/, int32 /*NewIndex*/);
	FOnVeloxWeaponChanged OnCurrentWeaponChanged;

	//~UActorComponent interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Clears the pending drop timer (B37) and the beam timer. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of UActorComponent interface

protected:
	UFUNCTION()
	void OnRep_CurrentIndex(uint8 OldIndex);

	/** Owning client: drops confirmed predicted bits and runs the autoswitch rule. */
	UFUNCTION()
	void OnRep_OwnedMask(uint16 OldMask);

	/** Granted once per PlayerState (one spec bound to InputTag.Weapon.Slot1..9/Next/Prev). */
	UPROPERTY(EditDefaultsOnly, Category = "Velox|Weapons")
	TSubclassOf<UVeloxGameplayAbility_WeaponSwitch> SwitchAbilityClass;

	/** Seconds a locally predicted switch result may stand without server confirmation. */
	UPROPERTY(EditDefaultsOnly, Category = "Velox|Weapons", Meta = (Units = "s"))
	float PredictionTimeout = 1.0f;

	/** Shot rejections per player and second logged at Log; further ones go to Verbose. */
	UPROPERTY(EditDefaultsOnly, Category = "Velox|Weapons", Meta = (ClampMin = "0"))
	int32 RejectLogLimitPerSecond = 5;

private:
	UVeloxAbilitySystemComponent* GetVeloxASC() const;
	void GrantFireAbility(int32 Index);
	void RemoveFireAbility(int32 Index);
	void GrantSwitchAbility();
	void StopBeamOf(int32 Index);
	/** B37 (iii), PendingDropTimer: Target becomes current if it is still the pending index. */
	void CompletePendingDrop(int32 Target);
	void ClearPendingDropTimer();

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
	/** Server: the drop end of the pending switch (B37 iii). */
	FTimerHandle PendingDropTimer;
	FGameplayTag ActiveBeamCueTag;

	int32 PredictedIndex = INDEX_NONE;
	double PredictionExpireTime = 0.0;

	/** Owning client: weapons owned by a predicted pickup and the local world time each prediction expires. */
	uint16 PredictedOwnedMask = 0;
	double PredictedOwnedUntil[16] = {};

	uint8 ShotSeqCounter = 0;

	/** B21: server-side pattern seed counter (never replicated, never driven by client data). */
	uint8 ServerPatternSeqCounter = 0;

	double SwitchEndTime = 0.0;

	double RejectLogWindowStart = -1.0;
	int32 RejectLogCount = 0;

	// --- Server: move-carried projectile shot (ADR 0003 B35) ---
	struct FPendingRpcShot
	{
		uint8 ShotSeq = 0;
		float FireMoveTimeStamp = 0.f;
		TWeakObjectPtr<UVeloxGameplayAbility_FireProjectile> Ability;
	};

	int32 FindMoveShotRecordIndex(uint8 Seq) const;
	/** Logs and counts an expired or evicted record that was never adopted. */
	void NoteMoveShotOrphan(const FVeloxMoveShotRecord& Record, const TCHAR* Why);
	/** Seconds a record waits for its RPC: FireRateJitterToleranceSeconds + MaxRewindSeconds. */
	double GetMoveShotOrphanSeconds() const;

	/** Oldest first; at most MaxMoveShotRecords. */
	TArray<FVeloxMoveShotRecord, TInlineAllocator<MaxMoveShotRecords>> ServerMoveShots;
	TArray<FPendingRpcShot, TInlineAllocator<2>> PendingRpcShots;
	/** B38: oldest first; at most MaxRpcLaunchedShotSeqs. */
	TArray<uint8, TInlineAllocator<MaxRpcLaunchedShotSeqs>> RpcLaunchedShotSeqs;
	TWeakObjectPtr<UVeloxMovementComponent> BoundServerMovement;
	FDelegateHandle MoveShotHandle;
	FDelegateHandle MoveProcessedHandle;
	float LastProcessedMoveStamp = 0.f;
	bool bServerMoveStreamSeen = false;
	int32 NumMoveShotOrphans = 0;
};
