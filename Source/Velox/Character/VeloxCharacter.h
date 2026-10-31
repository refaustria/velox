// Velox - player/bot character (ADR 0001 sections 1 and 2).

#pragma once

#include "AbilitySystemInterface.h"
#include "Character/VeloxAnimInstance.h"
#include "GameFramework/Character.h"
#include "GameplayCueInterface.h"
#include "Engine/TimerHandle.h"
#include "GameplayTagContainer.h"

#include "VeloxCharacter.generated.h"

class UAbilitySystemComponent;
class UInputComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UVeloxCombatSettings;
class UVeloxAbilitySystemComponent;
class UVeloxCombatFeedbackComponent;
class UVeloxHealthComponent;
class UVeloxFootstepComponent;
class UVeloxHitboxComponent;
class UVeloxMovementComponent;
class UVeloxScriptedInputComponent;
class UVeloxViewModelComponent;
class UVeloxWeaponInventoryComponent;
class UVeloxPawnData;
class UVeloxCharacterCosmetics;
class USkeletalMesh;
struct FActiveGameplayEffectHandle;
struct FGameplayEffectRemovalInfo;
struct FGameplayEffectSpec;
struct FInputActionValue;
struct FVeloxDamageEvent;

/** Class-default pawn geometry (cm), ADR 0003 section 6. Blueprint subclasses may override the resulting defaults. */
namespace VeloxCharacterGeometry
{
	inline constexpr float CapsuleRadius = 37.5f;
	inline constexpr float CapsuleHalfHeight = 70.f;
	inline constexpr float EyeHeight = 55.f;
	inline constexpr float CrouchedHalfHeight = 50.f;
	inline constexpr float CrouchedEyeHeight = 40.f;
	/** Class-default mesh placement (ADR 0006 section 4.1): feet at the capsule bottom, FBX forward. Crouch reads the CDO. */
	inline constexpr float MeshOffsetZ = -CapsuleHalfHeight;
	inline constexpr float MeshYawDeg = -90.f;
}

UCLASS(Config = Game)
class VELOX_API AVeloxCharacter : public ACharacter, public IAbilitySystemInterface, public IGameplayCueInterface
{
	GENERATED_BODY()

public:
	AVeloxCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~IAbilitySystemInterface
	/** Forwards to the PlayerState's ASC; null until a Velox PlayerState is assigned. */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End of IAbilitySystemInterface

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UVeloxAbilitySystemComponent* GetVeloxAbilitySystemComponent() const;

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UVeloxMovementComponent* GetVeloxMovementComponent() const;

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	const UVeloxPawnData* GetPawnData() const;

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UVeloxHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UVeloxHitboxComponent* GetHitboxComponent() const { return HitboxComponent; }

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UVeloxCombatFeedbackComponent* GetCombatFeedbackComponent() const { return CombatFeedbackComponent; }

	/** Dead body left behind for CorpseLifetime (ADR 0003 section 8): replicated, no collision, not an ability avatar. */
	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	bool IsCorpse() const { return bIsCorpse; }

	/**
	 * Jump key state, the single jump entry point of players and bots (ADR 0005 section 1): pressed = SetJumpHeld(true) +
	 * Jump(), released = SetJumpHeld(false) + StopJumping(). Input_JumpStarted/Completed call it; so does the bot controller.
	 */
	void SetJumpInput(bool bPressed);

	/** Tints PlaceholderBody with the PlayerState's colour (dark when a corpse). Server and clients; no-op without a body material. */
	void ApplyPlayerColor();

	/** First-person weapon view model of the local player pawn (ADR 0006 section 4.6); null on everything else. */
	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UVeloxViewModelComponent* GetViewModelComponent() const { return ViewModel; }

	/** The weapon in the pawn's hand as other players see it (socket weapon_r). */
	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UStaticMeshComponent* GetWeaponWorldMesh() const { return WeaponWorldMesh; }

	/** Test hook: the body's current colour parameter value (black when there is no body material). */
	FLinearColor GetBodyColor() const;

	// ---- ADR 0006 section 4 (task 08): third-person cosmetics
	/** The loaded cosmetics data asset (null on a dedicated server and until the async load finished). */
	const UVeloxCharacterCosmetics* GetCharacterCosmetics() const { return CharacterCosmetics; }
	/** Skin slot currently on the mesh (INDEX_NONE before cosmetics/skin). */
	int32 GetAppliedSkinSlot() const { return AppliedSkinSlot; }
	/** True when the mesh plays Sequences directly (no AnimClass, bUseSequencePlayback). */
	bool IsUsingSequencePlayback() const { return bSequencePlayback; }
	/** Leg state of the sequence-playback driver. */
	EVeloxLegState GetSequenceLegState() const { return SequenceAnimState.LegState; }
	/** Client: re-chooses the skin (own skin or forced enemy skin) and applies its colour; a corpse keeps its skin. */
	void ApplyCharacterSkin();
	// ---- end task 08

	/**
	 * Server (ADR 0004 section 3): writes the remaining time of the active Haste and Quad effects on the PlayerState ASC into
	 * the movement component's move timeline (UVeloxMovementComponent::ServerSetPowerupRemaining). A powerup without an
	 * active effect keeps its end time (a natural expiry happens in the move timeline itself, exactly on both ends); a
	 * premature removal clears it (the GE removal delegate). Called by the pickup grant and the powerup cheats; the GE
	 * delegates bound in InitAbilityActorInfo (applied, duration changed, removed) call it as well.
	 */
	void ServerRefreshPowerupMoveTimes();

	/**
	 * Server, non-shipping test hook (velox.move.Haste, the HasteRun soak's "serverHaste" row): Haste with exactly Seconds
	 * remaining through VeloxPowerups::GrantOrExtend, i.e. the pickup's GE path. Logs `VeloxNetSoak event=haste_server`.
	 */
	void ServerGiveTestHaste(float Seconds);

	//~IGameplayCueInterface
	/** Native cue handling (ADR 0003 section 5): forwards to UVeloxCombatFeedbackComponent, then to the interface default. */
	virtual void HandleGameplayCue(UObject* Self, FGameplayTag GameplayCueTag, EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters) override;
	//~End of IGameplayCueInterface

	//~APawn interface
	/** Server: InitAbilityActorInfo, then the PlayerState grants its default ability sets once and applies GE_SpawnInit. */
	virtual void PossessedBy(AController* NewController) override;
	/** Server: the pawn stops being the ASC avatar (a corpse is never an avatar). */
	virtual void UnPossessed() override;
	virtual void OnRep_PlayerState() override;
	virtual void OnRep_Controller() override;
	//~End of APawn interface

	//~ACharacter interface
	/** Logs `VeloxCrouch: start|end` with half height and eye height (server, owning client and proxies). */
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	/** Audio (ADR 0006 section 9): land and jump sounds through UVeloxFootstepComponent. */
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnJumped_Implementation() override;
	//~End of ACharacter interface

	//~AActor interface
	/** Corpses are viewed from a death camera: in front of the body, looking down at it (ADR 0003 section 8). */
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	/** Field of view and aspect constraint from the user settings (ADR 0004 section 7). */
	void ApplyViewSettings(FMinimalViewInfo& OutResult) const;
	/** Pits: 1000 environment damage on the pawn's own ASC (Q3: the world kills); a corpse that fell is left to its timer. */
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** `showdebug VeloxMovement` forwards to UVeloxMovementComponent::DisplayVeloxDebug (ACharacter only does so for Physics). */
	virtual void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;
	//~End of AActor interface

protected:
	//~AActor interface
	virtual void PostInitializeComponents() override;
	/** Non-server worlds: requests the pawn data's cosmetics (ADR 0006 section 4.3). */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Drives the sequence-playback fallback (ADR 0006 section 4.5) when it is active; nothing else. */
	virtual void Tick(float DeltaSeconds) override;
	//~End of AActor interface

	//~APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	//~End of APawn interface

	/** Server: PossessedBy. Owning client: OnRep_PlayerState. Owner = PlayerState, avatar = this pawn. */
	void InitAbilityActorInfo();

	void LogCrouchState(const TCHAR* Event) const;

	/** Creates the view model once this pawn is a local player's and its PlayerState inventory exists (non-dedicated only). */
	void EnsureViewModel();
	/** Shows the current weapon's WorldMesh on weapon_r for every client (inventory index is replicated); hidden on corpses. */
	void RefreshWeaponWorldMesh();
	/** Once per pawn (clients and listen hosts): asks the asset manager for the resident weapon-cosmetics set, then refreshes the world and view meshes. */
	void RequestWeaponCosmetics(const UVeloxWeaponInventoryComponent* Inventory);
	void OnWeaponCosmeticsLoaded();
	/** Binds the PlayerState inventory's weapon change to RefreshWeaponWorldMesh (rebinding when the PlayerState changes). */
	void BindWeaponWorldMesh();
	void OnWeaponIndexChanged(int32 OldIndex, int32 NewIndex);

	/** Server: UVeloxHealthComponent::OnOutOfHealth, synchronously inside the killing damage execution (ADR 0003 B8). */
	void HandleOutOfHealth(const FVeloxDamageEvent& Event);
	/**
	 * Server, B8 order: GE_Dead, CancelAbilities, ClearAbilityInput, RemoveAllGameplayCues, then movement / collision /
	 * hitbox / corpse, death cue, game mode notification, UnPossess last. Everything synchronous, nothing on a timer
	 * except the corpse's removal. Logs `VeloxDeath step=<n> <what>` per step.
	 */
	void HandleDeath(const FVeloxDamageEvent& Event);
	/** Server and clients: collision off, movement off (server), health component detached (clients), body dark. */
	void ApplyCorpseState();
	/** Server, next tick after HandleDeath: floor drop, movement/collision off, UnPossess, death camera, respawn wait. */
	void CompleteDeath();
	/** Server: OnDeathFinished, then Destroy (CorpseLifetime after the death). */
	void FinishCorpse();

	UFUNCTION()
	void OnRep_IsCorpse();

	void Input_Move(const FInputActionValue& InputActionValue);
	void Input_MoveCompleted(const FInputActionValue& InputActionValue);
	void Input_LookMouse(const FInputActionValue& InputActionValue);
	void Input_JumpStarted(const FInputActionValue& InputActionValue);
	void Input_JumpCompleted(const FInputActionValue& InputActionValue);
	void Input_CrouchStarted(const FInputActionValue& InputActionValue);
	void Input_CrouchCompleted(const FInputActionValue& InputActionValue);
	void Input_AbilityInputTagPressed(FGameplayTag InputTag);
	void Input_AbilityInputTagReleased(FGameplayTag InputTag);

	/** Pawn data loaded in PostInitializeComponents. Set in DefaultGame.ini or a Blueprint subclass. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Pawn")
	TSoftObjectPtr<UVeloxPawnData> DefaultPawnData;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Pawn")
	TObjectPtr<const UVeloxPawnData> PawnData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Character", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UVeloxHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Character", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UVeloxHitboxComponent> HitboxComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Character", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UVeloxCombatFeedbackComponent> CombatFeedbackComponent;

	/** Footsteps, jump, landing, pain and death sounds (client audio only; ADR 0006 section 9). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Character", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UVeloxFootstepComponent> FootstepComponent;

	/** Bright capsule-sized mesh attached to the skeletal mesh (so it gets the smoothing offset), coloured per player (ADR 0003 section 10). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Character", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/** Unlit material with a vector parameter BodyColorParameter (M_PlayerBody, bootstrap script). Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Readability")
	TSoftObjectPtr<UMaterialInterface> PlayerBodyMaterial;

	/** Local player only (EnsureViewModel); owns the first-person weapon mesh. */
	UPROPERTY(Transient)
	TObjectPtr<UVeloxViewModelComponent> ViewModel;

	/** Current weapon in the right hand, hidden from the owner (the view model is theirs). Never created content-wise on a dedicated server. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Character", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UStaticMeshComponent> WeaponWorldMesh;

	/** Socket of the skeletal mesh that carries the weapon. */
	UPROPERTY(EditDefaultsOnly, Category = "Velox|Weapons")
	FName WeaponSocketName = TEXT("weapon_r");

	/** Where the weapon sits on the capsule when the mesh has no weapon socket (placeholder body period). */
	UPROPERTY(EditDefaultsOnly, Category = "Velox|Weapons")
	FVector WeaponFallbackOffset = FVector(30.0, 25.0, 15.0);

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Readability")
	FName BodyColorParameter = TEXT("Color");

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	/** Copied from the PlayerState while alive so a corpse (no PlayerState) still knows its tunables. */
	UPROPERTY(Transient)
	TObjectPtr<const UVeloxCombatSettings> CachedCombatSettings;

	UPROPERTY(ReplicatedUsing = OnRep_IsCorpse)
	bool bIsCorpse = false;

private:
	TArray<uint32> AbilityInputBindHandles;

	FTimerHandle CorpseTimer;
	TWeakObjectPtr<AController> DyingController;
	FColor LastBodyColor = FColor::White;
	TWeakObjectPtr<UVeloxAbilitySystemComponent> LastAbilitySystem;

	/** Test tooling (task 05): scripted input replay and test knockback RPC. Created in non-shipping builds only. */
	UPROPERTY()
	TObjectPtr<UVeloxScriptedInputComponent> ScriptedInput;

	// ADR 0004 section 3, server: the PlayerState ASC's powerup GEs drive the move timeline (ServerRefreshPowerupMoveTimes).
	void BindPowerupMoveTimes(UAbilitySystemComponent& ASC);
	void UnbindPowerupMoveTimes();
	/** Per active Haste/Quad effect: duration changes (in-place extension) and removal (premature: clear the end time). */
	void BindPowerupEffectHandle(UAbilitySystemComponent& ASC, const FActiveGameplayEffectHandle& Handle);
	void OnPowerupEffectApplied(UAbilitySystemComponent* ASC, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle);
	void OnPowerupEffectTimeChanged(FActiveGameplayEffectHandle Handle, float NewStartTime, float NewDuration);
	void OnPowerupEffectRemoved(const FGameplayEffectRemovalInfo& Info);
	/** This pawn is the avatar of ASC on the authority (a corpse's late callbacks are ignored). */
	bool IsPowerupAvatarOf(const UAbilitySystemComponent* ASC) const;

	TWeakObjectPtr<UVeloxWeaponInventoryComponent> WeaponMeshInventory;
	FDelegateHandle WeaponMeshHandle;

	TWeakObjectPtr<UAbilitySystemComponent> PowerupWatchedASC;
	FDelegateHandle PowerupAppliedHandle;

	// ---- ADR 0006 section 4 (task 08): third-person cosmetics (non-server worlds only)
	/** Once per pawn: async-load PawnData->Cosmetics through UVeloxAssetManager (dedicated server: Verbose log, nothing). */
	void RequestCharacterCosmetics();
	void OnCharacterCosmeticsLoaded(const UVeloxCharacterCosmetics* Loaded);
	/** Mesh, offset/yaw, anim class or sequence playback, placeholder visibility. */
	void SetUpCosmeticMesh();
	/** Moves the mesh to OffsetZ/YawDeg keeping the engine's base offsets (smoothing, crouch) consistent. */
	void ApplyCosmeticMeshOffset(float OffsetZ, float YawDeg);
	/** After the engine crouch code reset the mesh Z from the class default: re-adds CosmeticMeshZDelta. */
	void ReapplyCosmeticMeshZDelta();
	void PlaySequenceForState();
	bool IsOwnedByLocalPlayer() const;

	UPROPERTY(Transient)
	TObjectPtr<const UVeloxCharacterCosmetics> CharacterCosmetics;

	/** Skin material instance on the mesh (BodyColorParameter = body colour, darkened on corpses). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkinMaterial;

	bool bCosmeticsRequested = false;
	bool bWeaponCosmeticsRequested = false;
	bool bSequencePlayback = false;
	int32 AppliedSkinSlot = INDEX_NONE;
	FLinearColor AppliedSkinColor = FLinearColor::White;
	/** CosmeticsMeshOffsetZ minus the class-default mesh Z, re-added after the engine's crouch offset reset. */
	float CosmeticMeshZDelta = 0.f;
	FVeloxAnimStateData SequenceAnimState;
	FVeloxAnimSampler SequenceAnimSampler;
	FDelegateHandle SettingsAppliedHandle;
	// ---- end task 08
};
