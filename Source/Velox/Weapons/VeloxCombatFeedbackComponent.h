// Velox - native combat feedback on the pawn (ADR 0003 section 5): AVeloxCharacter::HandleGameplayCue forwards weapon
// cues here, AVeloxPlayerController::ClientConfirmHit the server's hit confirmations. M2 draws debug shapes only
// (non-shipping: tracer, impact sphere, beam state) and keeps the hit-marker state for the HUD; M5 adds GCN_* notifies
// under /Game/GameplayCues for the same tags without code changes.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayCueInterface.h"
#include "GameplayTagContainer.h"

#include "VeloxCombatFeedbackComponent.generated.h"

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxCombatFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVeloxCombatFeedbackComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	void HandleCue(const FGameplayTag& CueTag, EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters);

	/** Server-confirmed hit of this player's shot ShotSeq (Q3: no predicted marker). */
	void OnHitConfirmed(uint8 ShotSeq, uint8 TotalDamage, uint8 Flags);

	double GetLastHitConfirmTime() const { return LastHitConfirmTime; }
	int32 GetNumHitConfirms() const { return NumHitConfirms; }
	bool IsBeamActive() const { return bBeamActive; }

private:
	double LastHitConfirmTime = -1.0;
	int32 NumHitConfirms = 0;
	bool bBeamActive = false;
};
