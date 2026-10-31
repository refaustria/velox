// Velox - native combat feedback on the pawn (ADR 0003 section 5): AVeloxCharacter::HandleGameplayCue forwards weapon
// cues here, AVeloxPlayerController::ClientConfirmHit the server's hit confirmations. M2 draws debug shapes only
// (non-shipping: tracer, impact sphere, beam state) and keeps the hit-marker state for the HUD; M5 adds GCN_* notifies
// under /Game/GameplayCues for the same tags without code changes. M3 (ADR 0004 section 3): the looping powerup cues
// GameplayCue.Powerup.<Name> tint the body (vector parameter PowerupTintParameter on the pawn's dynamic materials).

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayCueInterface.h"
#include "GameplayTagContainer.h"

#include "VeloxCombatFeedbackComponent.generated.h"

/** One damage the pawn took (ADR 0004 section 7): the world yaw towards the source and the local world time it was seen. */
struct FVeloxDamageDirection
{
	float WorldYaw = 0.f;
	/** Horizontal distance to the source (cm); the HUD ignores very close ones (own splash). */
	float Distance2D = 0.f;
	double Time = 0.0;
};

class UAbilitySystemComponent;
class UAudioComponent;
class UGameplayAbility;
class UVeloxWeaponCosmetics;
class UVeloxWeaponDefinition;
class UVeloxWeaponTable;

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxCombatFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVeloxCombatFeedbackComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~End of UActorComponent interface

	void HandleCue(const FGameplayTag& CueTag, EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters);

	/** Broadcast for every executed Fire cue of this pawn (view model recoil kick; ADR 0006 section 4.6). */
	DECLARE_MULTICAST_DELEGATE(FOnVeloxFireCue);
	FOnVeloxFireCue OnFireCue;

	/** Server-confirmed hit of this player's shot ShotSeq (Q3: no predicted marker). */
	void OnHitConfirmed(uint8 ShotSeq, uint8 TotalDamage, uint8 ConfirmFlags);

	double GetLastHitConfirmTime() const { return LastHitConfirmTime; }
	int32 GetNumHitConfirms() const { return NumHitConfirms; }
	bool IsBeamActive() const { return bBeamActive; }

	/** Velox.Powerup.<Name> tags whose looping cue is active on this pawn. */
	const FGameplayTagContainer& GetActivePowerups() const { return ActivePowerups; }

	/** The body tint shown for the active powerups (UVeloxCombatSettings::PowerupTints); alpha 0 = none. */
	FLinearColor GetPowerupTint() const { return PowerupTint; }

	/** The last records of GameplayCue.Character.Damage (oldest first, at most MaxDamageDirections); the HUD turns them into indicators. */
	const TArray<FVeloxDamageDirection>& GetDamageDirections() const { return DamageDirections; }
	/** Appends a record for damage from SourceLocation to a pawn at OwnerLocation seen at Now (world time). */
	void RecordDamageDirection(const FVector& OwnerLocation, const FVector& SourceLocation, double Now);

	/** Executed weapon cues seen by this pawn since spawn (tests, logs; ADR 0003 B21: one .Impact per hitscan shot). */
	int32 GetNumFireCues() const { return NumFireCues; }

	int32 GetNumImpactCues() const { return NumImpactCues; }

protected:
	/** Records kept for the damage indicators. */
	UPROPERTY(EditDefaultsOnly, Category = "Velox|Feedback", Meta = (ClampMin = "1"))
	int32 MaxDamageDirections = 8;

	/** Vector parameter of the body material that carries the powerup tint (M_PlayerBody, ADR 0004 section 3). */
	UPROPERTY(EditDefaultsOnly, Category = "Velox|Feedback")
	FName PowerupTintParameter = TEXT("PowerupTint");

private:
	/** M5 audio (ADR 0006 section 9): the fire / impact one-shots, the Lightning Gun beam loop and the muzzle light. Clients only. */
	void PlayCueAudio(const FGameplayTag& CueTag, const FString& TagName, const FVector& Start, const FVector& End);
	void UpdateBeamAudio(const FGameplayTag& CueTag, bool bStart);

	/**
	 * The weapon definition whose Fire / Impact / Beam cue tag is CueTag, from the owner's inventory table (review 0006 D4);
	 * null without an inventory or for other tags. The tag map is built once per table. Cosmetics are read with Get() only:
	 * until the asset manager's resident load completes the cue is silent.
	 */
	const UVeloxWeaponDefinition* FindCueDefinition(const FGameplayTag& CueTag);
	const UVeloxWeaponCosmetics* FindCueCosmetics(const FGameplayTag& CueTag);

	TWeakObjectPtr<const UVeloxWeaponTable> CueTable;
	TMap<FGameplayTag, TWeakObjectPtr<const UVeloxWeaponDefinition>> CueDefinitions;

	/**
	 * Dry fire (review 0006 D3): on the local player's pawn, binds AbilityFailedCallbacks of the ASC whose avatar is the owner.
	 * The PlayerState (and so the ASC) arrives after BeginPlay, so TickComponent retries until it is bound.
	 */
	void TryBindAbilityFailed();
	void UnbindAbilityFailed();
	void OnAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason);

	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystem;
	FDelegateHandle AbilityFailedHandle;
	double LastDryFireTime = -1000.0;

	/** The looping beam sound of this pawn's Lightning Gun while the beam cue is active. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BeamAudio;

	/** Looping powerup cue: OnActive/WhileActive add, Removed removes; returns false for any other cue. */
	bool HandlePowerupCue(const FGameplayTag& CueTag, EGameplayCueEvent::Type EventType);
	void ApplyPowerupTint();

	TArray<FVeloxDamageDirection> DamageDirections;
	FGameplayTagContainer ActivePowerups;
	FLinearColor PowerupTint = FLinearColor(0.f, 0.f, 0.f, 0.f);
	int32 NumFireCues = 0;
	int32 NumImpactCues = 0;
	double LastHitConfirmTime = -1.0;
	int32 NumHitConfirms = 0;
	bool bBeamActive = false;
};
