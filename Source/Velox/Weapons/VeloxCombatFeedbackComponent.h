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

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxCombatFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVeloxCombatFeedbackComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	void HandleCue(const FGameplayTag& CueTag, EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters);

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
