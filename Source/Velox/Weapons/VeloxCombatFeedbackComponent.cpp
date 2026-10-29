// Velox - combat feedback component.

#include "Weapons/VeloxCombatFeedbackComponent.h"

#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "Components/MeshComponent.h"
#include "Core/VeloxGameplayTags.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Items/VeloxPowerupEffects.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Weapons/VeloxProjectile.h"

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
	if (HandlePowerupCue(CueTag, EventType))
	{
		return;
	}
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
		const FVeloxGameplayEffectContext* Context = FVeloxGameplayEffectContext::Get(Parameters.EffectContext);
		UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxCue %s on %s from %s to %s seq=%u pattern=%u"), *TagName, *GetNameSafe(GetOwner()), *Start.ToCompactString(),
			*Parameters.Location.ToCompactString(), Context ? Context->ShotSeq : 0, Context ? Context->PatternSeq : 0);
		if (TagName.EndsWith(TEXT(".Fire")))
		{
			++NumFireCues;
		}
		else if (TagName.EndsWith(TEXT(".Impact")))
		{
			++NumImpactCues;
		}
		// Projectile explosions (task 05): the owning client ends its fake of that shot (ADR 0003 section 7).
		if (Parameters.RawMagnitude > 0.f && TagName.EndsWith(TEXT(".Impact")))
		{
			AVeloxProjectile::OnImpactCue(GetOwner(), Parameters);
		}
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
		// Projectile impacts carry the splash radius in RawMagnitude; hitscan impacts a small marker.
		DrawDebugSphere(World, Parameters.Location, Parameters.RawMagnitude > 0.f ? Parameters.RawMagnitude : 6.f, 8, FColor::Orange, false, 0.5f);
	}
#endif
}

void UVeloxCombatFeedbackComponent::OnHitConfirmed(uint8 ShotSeq, uint8 TotalDamage, uint8 ConfirmFlags)
{
	const UWorld* World = GetWorld();
	LastHitConfirmTime = World ? World->GetTimeSeconds() : 0.0;
	++NumHitConfirms;
}

bool UVeloxCombatFeedbackComponent::HandlePowerupCue(const FGameplayTag& CueTag, EGameplayCueEvent::Type EventType)
{
	const FGameplayTag Powerup = VeloxPowerupTags::PowerupTagForCue(CueTag);
	if (!Powerup.IsValid())
	{
		return false;
	}
	const bool bWasActive = ActivePowerups.HasTagExact(Powerup);
	if (EventType == EGameplayCueEvent::OnActive || EventType == EGameplayCueEvent::WhileActive)
	{
		ActivePowerups.AddTag(Powerup);
	}
	else if (EventType == EGameplayCueEvent::Removed)
	{
		ActivePowerups.RemoveTag(Powerup);
	}
	if (bWasActive != ActivePowerups.HasTagExact(Powerup))
	{
		ApplyPowerupTint();
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxPowerupTint pawn=%s powerup=%s %s tint=%s"), *GetNameSafe(GetOwner()), *Powerup.ToString(), bWasActive ? TEXT("off") : TEXT("on"),
			*PowerupTint.ToString());
	}
	return true;
}

void UVeloxCombatFeedbackComponent::ApplyPowerupTint()
{
	// One tint at a time, the most significant powerup first (Q3 draws the quad shell over the others).
	const FGameplayTag Priority[] = {
		VeloxGameplayTags::Powerup_Quad, VeloxGameplayTags::Powerup_BattleSuit, VeloxGameplayTags::Powerup_Regen, VeloxGameplayTags::Powerup_Haste,
	};
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AVeloxPlayerState* PlayerState = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PlayerState ? PlayerState->GetCombatSettings() : nullptr);
	PowerupTint = FLinearColor(0.f, 0.f, 0.f, 0.f);
	for (const FGameplayTag& Tag : Priority)
	{
		const FLinearColor* Color = ActivePowerups.HasTagExact(Tag) ? Settings.PowerupTints.Find(Tag) : nullptr;
		if (Color)
		{
			PowerupTint = *Color;
			break;
		}
	}
	if (!GetOwner())
	{
		return;
	}
	// The body's dynamic materials (AVeloxCharacter's M_PlayerBody instance); a material without the parameter ignores it.
	TInlineComponentArray<UMeshComponent*> Meshes(GetOwner());
	for (UMeshComponent* Mesh : Meshes)
	{
		for (int32 Index = 0; Mesh && Index < Mesh->GetNumMaterials(); ++Index)
		{
			if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Index)))
			{
				Material->SetVectorParameterValue(PowerupTintParameter, PowerupTint);
			}
		}
	}
}
