// Velox - player/bot character (ADR 0001 sections 1 and 2).

#pragma once

#include "AbilitySystemInterface.h"
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
class UVeloxHitboxComponent;
class UVeloxMovementComponent;
class UVeloxScriptedInputComponent;
class UVeloxPawnData;
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

	/** Tints PlaceholderBody with the PlayerState's colour (dark when a corpse). Server and clients; no-op without a body material. */
	void ApplyPlayerColor();

	/** Test hook: the body's current colour parameter value (black when there is no body material). */
	FLinearColor GetBodyColor() const;

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of AActor interface

	//~APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	//~End of APawn interface

	/** Server: PossessedBy. Owning client: OnRep_PlayerState. Owner = PlayerState, avatar = this pawn. */
	void InitAbilityActorInfo();

	void LogCrouchState(const TCHAR* Event) const;

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

	/** Bright capsule-sized mesh attached to the skeletal mesh (so it gets the smoothing offset), coloured per player (ADR 0003 section 10). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Character", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/** Unlit material with a vector parameter BodyColorParameter (M_PlayerBody, bootstrap script). Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Readability")
	TSoftObjectPtr<UMaterialInterface> PlayerBodyMaterial;

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

	TWeakObjectPtr<UAbilitySystemComponent> PowerupWatchedASC;
	FDelegateHandle PowerupAppliedHandle;
};
