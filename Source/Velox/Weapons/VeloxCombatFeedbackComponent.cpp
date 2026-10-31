// Velox - combat feedback component.

#include "Weapons/VeloxCombatFeedbackComponent.h"

#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/MeshComponent.h"
#include "Core/VeloxGameplayTags.h"
#include "Core/VeloxAudioSettings.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Items/VeloxPowerupEffects.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundBase.h"
#include "UI/VeloxAnnouncerComponent.h"
#include "UI/VeloxHUDFormat.h"
#include "Weapons/VeloxProjectile.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxCombatFeedbackComponent)

#if !UE_BUILD_SHIPPING
namespace VeloxFeedbackCVars
{
	static TAutoConsoleVariable<int32> CVarDebugDraw(TEXT("velox.combat.DebugDraw"), 1,
		TEXT("Weapon cue debug draws (tracer, impact, beam): 0 off, 1 on. Non-shipping."), ECVF_Default);
}
#endif

namespace VeloxFeedbackAudio
{
	/** Movable muzzle lights alive on this client (all pawns): pruned lazily, so a destroyed world cannot leak the count. */
	TArray<TWeakObjectPtr<UPointLightComponent>> ActiveLights;
}

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
	if (CueTag == VeloxGameplayTags::GameplayCue_Character_Damage)
	{
		if (EventType == EGameplayCueEvent::Executed && GetOwner())
		{
			RecordDamageDirection(GetOwner()->GetActorLocation(), Parameters.Location, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
		}
		return;
	}
	const FVector Start = Parameters.EffectContext.IsValid() && Parameters.EffectContext.HasOrigin() ? Parameters.EffectContext.GetOrigin() : Parameters.Location;
	const FString TagName = CueTag.ToString();
	const bool bBeam = TagName.EndsWith(TEXT(".Beam"));
	if (bBeam && EventType != EGameplayCueEvent::Executed)
	{
		const bool bWasActive = bBeamActive;
		bBeamActive = EventType != EGameplayCueEvent::Removed;
		if (bWasActive != bBeamActive)
		{
			UpdateBeamAudio(bBeamActive);
		}
		UE_CLOG(bWasActive != bBeamActive, LogVeloxCombat, Log, TEXT("VeloxCue %s %s on %s"), *TagName, bBeamActive ? TEXT("start") : TEXT("stop"), *GetNameSafe(GetOwner()));
	}
	else if (EventType == EGameplayCueEvent::Executed)
	{
		const FVeloxGameplayEffectContext* Context = FVeloxGameplayEffectContext::Get(Parameters.EffectContext);
		UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxCue %s on %s from %s to %s seq=%u pattern=%u"), *TagName, *GetNameSafe(GetOwner()), *Start.ToCompactString(),
			*Parameters.Location.ToCompactString(), Context ? Context->ShotSeq : 0, Context ? Context->PatternSeq : 0);
		PlayCueAudio(CueTag, TagName, Start, Parameters.Location);
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

void UVeloxCombatFeedbackComponent::RecordDamageDirection(const FVector& OwnerLocation, const FVector& SourceLocation, double Now)
{
	FVeloxDamageDirection& Record = DamageDirections.AddDefaulted_GetRef();
	Record.WorldYaw = VeloxHUDFormat::YawFromTo(OwnerLocation, SourceLocation);
	Record.Distance2D = static_cast<float>(FVector::Dist2D(OwnerLocation, SourceLocation));
	Record.Time = Now;
	const int32 Excess = DamageDirections.Num() - FMath::Max(MaxDamageDirections, 1);
	if (Excess > 0)
	{
		DamageDirections.RemoveAt(0, Excess, EAllowShrinking::No);
	}
}

void UVeloxCombatFeedbackComponent::OnHitConfirmed(uint8 ShotSeq, uint8 TotalDamage, uint8 ConfirmFlags)
{
	const UWorld* World = GetWorld();
	LastHitConfirmTime = World ? World->GetTimeSeconds() : 0.0;
	++NumHitConfirms;
	UVeloxAudioSettings::Play2D(World, UVeloxAudioSettings::Get().HitConfirmSound, TEXT("hit_confirm"));
}

void UVeloxCombatFeedbackComponent::PlayCueAudio(const FGameplayTag& CueTag, const FString& TagName, const FVector& Start, const FVector& End)
{
	const UWorld* World = GetWorld();
	if (!UVeloxAudioSettings::CanPlayAudio(World))
	{
		return;
	}
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	const FVeloxCueSound* Entry = Settings.FindCueSound(CueTag);
	if (!Entry)
	{
		return;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const bool bIsFire = TagName.EndsWith(TEXT(".Fire"));
	if (bIsFire && Pawn && Pawn->IsLocallyControlled())
	{
		// The own weapon plays 2D at full volume, outside the concurrency groups (never stolen).
		UVeloxAudioSettings::Play2D(World, Entry->Sound, *TagName);
	}
	else
	{
		// Fire sounds come from the muzzle origin, impacts from the hit point.
		UVeloxAudioSettings::PlayAt(World, Entry->Sound, bIsFire ? Start : End, Entry->Attenuation, Entry->Concurrency, *TagName);
	}
	if (bIsFire)
	{
		SpawnMuzzleLight(Start);
	}
}

void UVeloxCombatFeedbackComponent::UpdateBeamAudio(bool bStart)
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!UVeloxAudioSettings::CanPlayAudio(World) || !Owner)
	{
		return;
	}
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	const FVeloxCueSound* Loop = Settings.FindCueSound(VeloxGameplayTags::GameplayCue_Weapon_LightningGun_Beam);
	const APawn* Pawn = Cast<APawn>(Owner);
	const bool bOwn = Pawn && Pawn->IsLocallyControlled();
	if (bStart)
	{
		if (BeamAudio)
		{
			return;
		}
		if (bOwn)
		{
			UVeloxAudioSettings::Play2D(World, Settings.BeamStartSound, TEXT("beam_start"));
		}
		else
		{
			UVeloxAudioSettings::PlayAt(World, Settings.BeamStartSound, Owner->GetActorLocation(), EVeloxAudioAttenuation::WeaponFire, EVeloxAudioConcurrency::Weapons, TEXT("beam_start"));
		}
		USoundBase* LoopSound = Loop ? Loop->Sound.LoadSynchronous() : nullptr;
		if (!LoopSound || !Owner->GetRootComponent())
		{
			return;
		}
		UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio beam loop start on %s own=%d"), *GetNameSafe(Owner), bOwn ? 1 : 0);
		BeamAudio = bOwn
			? UGameplayStatics::SpawnSound2D(World, LoopSound)
			: UGameplayStatics::SpawnSoundAttached(LoopSound, Owner->GetRootComponent(), NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, 1.f, 1.f, 0.f,
				Settings.LoadAttenuation(Loop->Attenuation), Settings.LoadConcurrency(Loop->Concurrency));
		return;
	}
	if (BeamAudio)
	{
		UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio beam loop stop on %s"), *GetNameSafe(Owner));
		BeamAudio->Stop();
		BeamAudio = nullptr;
	}
	if (bOwn)
	{
		UVeloxAudioSettings::Play2D(World, Settings.BeamStopSound, TEXT("beam_stop"));
	}
	else
	{
		UVeloxAudioSettings::PlayAt(World, Settings.BeamStopSound, Owner->GetActorLocation(), EVeloxAudioAttenuation::WeaponFire, EVeloxAudioConcurrency::Weapons, TEXT("beam_stop"));
	}
}

void UVeloxCombatFeedbackComponent::SpawnMuzzleLight(const FVector& Location)
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	if (!World || !Owner || Settings.MaxMovableLights <= 0 || Settings.MuzzleLightSeconds <= 0.f)
	{
		return;
	}
	VeloxFeedbackAudio::ActiveLights.RemoveAll([](const TWeakObjectPtr<UPointLightComponent>& Light) { return !Light.IsValid(); });
	if (VeloxFeedbackAudio::ActiveLights.Num() >= Settings.MaxMovableLights)
	{
		return;
	}
	UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner, NAME_None, RF_Transient);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetCastShadows(false);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(Settings.MuzzleLightIntensity);
	Light->SetLightColor(Settings.MuzzleLightColor);
	Light->SetAttenuationRadius(Settings.MuzzleLightRadius);
	Light->SetWorldLocation(Location);
	Light->RegisterComponent();
	VeloxFeedbackAudio::ActiveLights.Add(Light);
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakLight = TWeakObjectPtr<UPointLightComponent>(Light)]()
	{
		if (UPointLightComponent* Expired = WeakLight.Get())
		{
			Expired->DestroyComponent();
		}
	}), Settings.MuzzleLightSeconds, false);
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
		// Announcer: "Quad Damage" when the local player's pawn gets the powerup (ADR 0006 section 9).
		const APawn* Pawn = Cast<APawn>(GetOwner());
		if (!bWasActive && Powerup == VeloxGameplayTags::Powerup_Quad && Pawn && Pawn->IsLocallyControlled() && Pawn->GetController())
		{
			if (UVeloxAnnouncerComponent* Announcer = Pawn->GetController()->FindComponentByClass<UVeloxAnnouncerComponent>())
			{
				Announcer->OnQuadDamage();
			}
		}
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
