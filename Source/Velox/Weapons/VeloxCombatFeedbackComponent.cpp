// Velox - combat feedback component.

#include "Weapons/VeloxCombatFeedbackComponent.h"

#include "Combat/VeloxCombatLog.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxCombatFeedbackComponent)

#if !UE_BUILD_SHIPPING
namespace VeloxFeedbackCVars
{
	static TAutoConsoleVariable<int32> CVarDebugDraw(TEXT("velox.combat.DebugDraw"), 1,
		TEXT("Weapon cue debug draws (tracer, impact, beam): 0 off, 1 on. Non-shipping."), ECVF_Default);
}
#endif

UVeloxCombatFeedbackComponent::UVeloxCombatFeedbackComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVeloxCombatFeedbackComponent::HandleCue(const FGameplayTag& CueTag, EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters)
{
	const FVector Start = Parameters.EffectContext.IsValid() && Parameters.EffectContext.HasOrigin() ? Parameters.EffectContext.GetOrigin() : Parameters.Location;
	const FString TagName = CueTag.ToString();
	const bool bBeam = TagName.EndsWith(TEXT(".Beam"));
	if (bBeam && EventType != EGameplayCueEvent::Executed)
	{
		const bool bWasActive = bBeamActive;
		bBeamActive = EventType != EGameplayCueEvent::Removed;
		UE_CLOG(bWasActive != bBeamActive, LogVeloxCombat, Log, TEXT("VeloxCue %s %s on %s"), *TagName, bBeamActive ? TEXT("start") : TEXT("stop"), *GetNameSafe(GetOwner()));
	}
	else if (EventType == EGameplayCueEvent::Executed)
	{
		UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxCue %s on %s from %s to %s"), *TagName, *GetNameSafe(GetOwner()), *Start.ToCompactString(), *Parameters.Location.ToCompactString());
	}

#if !UE_BUILD_SHIPPING
	const UWorld* World = GetWorld();
	if (!World || VeloxFeedbackCVars::CVarDebugDraw.GetValueOnGameThread() == 0 || EventType != EGameplayCueEvent::Executed)
	{
		return;
	}
	if (TagName.EndsWith(TEXT(".Fire")))
	{
		DrawDebugLine(World, Start, Parameters.Location, FColor::Yellow, false, 0.25f, 0, 1.f);
	}
	else if (TagName.EndsWith(TEXT(".Impact")))
	{
		DrawDebugSphere(World, Parameters.Location, 6.f, 8, FColor::Orange, false, 0.5f);
	}
#endif
}

void UVeloxCombatFeedbackComponent::OnHitConfirmed(uint8 ShotSeq, uint8 TotalDamage, uint8 Flags)
{
	const UWorld* World = GetWorld();
	LastHitConfirmTime = World ? World->GetTimeSeconds() : 0.0;
	++NumHitConfirms;
}
