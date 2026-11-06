// Velox - combat feedback component.

#include "Weapons/VeloxCombatFeedbackComponent.h"

#include "Core/VeloxServerKind.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/VeloxAnimInstance.h"
#include "Character/VeloxViewModelComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/VeloxViewModelSettings.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "Components/AudioComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/VeloxGameplayTags.h"
#include "Core/VeloxAudioSettings.h"
#include "Core/VeloxWeaponCosmetics.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Items/VeloxPowerupEffects.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundBase.h"
#include "UI/VeloxAnnouncerComponent.h"
#include "UI/VeloxHUDFormat.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxMovableLights.h"
#include "Weapons/VeloxProjectile.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"
#include "Weapons/VeloxWeaponTable.h"

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
	// Ticks only until the dry-fire hook is bound (local player's pawn, clients and listen host); see TryBindAbilityFailed.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UVeloxCombatFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn && UVeloxAudioSettings::CanPlayAudio(GetWorld()) && Pawn->GetLocalRole() != ROLE_SimulatedProxy)
	{
		SetComponentTickEnabled(true);
	}
}

void UVeloxCombatFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindAbilityFailed();
	Super::EndPlay(EndPlayReason);
}

void UVeloxCombatFeedbackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		SetComponentTickEnabled(false);
		return;
	}
	if (const AController* Controller = Pawn->GetController(); Controller && !Cast<APlayerController>(Controller))
	{
		// A bot never hears its own dry fire.
		SetComponentTickEnabled(false);
		return;
	}
	TryBindAbilityFailed();
	if (BoundAbilitySystem.IsValid())
	{
		SetComponentTickEnabled(false);
	}
}

void UVeloxCombatFeedbackComponent::TryBindAbilityFailed()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (BoundAbilitySystem.IsValid() || !UVeloxAudioSettings::IsLocalPlayerPawn(Pawn))
	{
		return;
	}
	const IAbilitySystemInterface* Interface = Cast<IAbilitySystemInterface>(Pawn);
	UAbilitySystemComponent* ASC = Interface ? Interface->GetAbilitySystemComponent() : nullptr;
	if (!ASC || ASC->GetAvatarActor() != Pawn)
	{
		return;
	}
	BoundAbilitySystem = ASC;
	AbilityFailedHandle = ASC->AbilityFailedCallbacks.AddUObject(this, &ThisClass::OnAbilityFailed);
}

void UVeloxCombatFeedbackComponent::UnbindAbilityFailed()
{
	if (UAbilitySystemComponent* ASC = BoundAbilitySystem.Get())
	{
		ASC->AbilityFailedCallbacks.Remove(AbilityFailedHandle);
	}
	BoundAbilitySystem.Reset();
	AbilityFailedHandle.Reset();
}

void UVeloxCombatFeedbackComponent::OnAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	const UWorld* World = GetWorld();
	if (!World || !Ability || !Ability->IsA<UVeloxGameplayAbility_Fire>() || !FailureReason.HasTag(UAbilitySystemGlobals::Get().ActivateFailCostTag)
		|| !UVeloxAudioSettings::IsLocalPlayerPawn(Cast<APawn>(GetOwner())))
	{
		return;
	}
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	const double Now = World->GetTimeSeconds();
	if (Now - LastDryFireTime < Settings.DryFireMinInterval)
	{
		return;
	}
	LastDryFireTime = Now;
	UVeloxAudioSettings::Play2D(World, Settings.DryFireSound, TEXT("dry_fire"));
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
			UpdateBeamAudio(CueTag, bBeamActive);
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
			SpawnTracer(CueTag, Start, Parameters.Location);
			++NumFireCues;
			OnFireCue.Broadcast();
			// ADR 0006 section 4.5 (task 08): one-frame bFiring for the AnimGraph's Fire_Upper slot.
			const ACharacter* Character = Cast<ACharacter>(GetOwner());
			if (UVeloxAnimInstance* AnimInstance = Character && Character->GetMesh() ? Cast<UVeloxAnimInstance>(Character->GetMesh()->GetAnimInstance()) : nullptr)
			{
				AnimInstance->NotifyFired();
			}
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

const UVeloxWeaponDefinition* UVeloxCombatFeedbackComponent::FindCueDefinition(const FGameplayTag& CueTag)
{
	const UVeloxWeaponInventoryComponent* Inventory = UVeloxWeaponInventoryComponent::Find(GetOwner());
	const UVeloxWeaponTable* Table = Inventory ? Inventory->GetWeaponTable() : nullptr;
	if (!Table)
	{
		return nullptr;
	}
	if (CueTable.Get() != Table)
	{
		// Built once per table: cue tag -> the weapon that owns it.
		CueTable = Table;
		CueDefinitions.Reset();
		for (int32 Index = 0; Index < Table->Num(); ++Index)
		{
			const UVeloxWeaponDefinition* Definition = Table->Get(Index);
			if (!Definition)
			{
				continue;
			}
			for (const FGameplayTag& Tag : { Definition->FireCueTag, Definition->ImpactCueTag, Definition->BeamCueTag })
			{
				if (Tag.IsValid())
				{
					CueDefinitions.Add(Tag, Definition);
				}
			}
		}
	}
	const TWeakObjectPtr<const UVeloxWeaponDefinition>* Found = CueDefinitions.Find(CueTag);
	return Found ? Found->Get() : nullptr;
}

const UVeloxWeaponCosmetics* UVeloxCombatFeedbackComponent::FindCueCosmetics(const FGameplayTag& CueTag)
{
	const UVeloxWeaponDefinition* Definition = FindCueDefinition(CueTag);
	return Definition ? Definition->Cosmetics.Get() : nullptr;
}

void UVeloxCombatFeedbackComponent::PlayCueAudio(const FGameplayTag& CueTag, const FString& TagName, const FVector& Start, const FVector& End)
{
	const UWorld* World = GetWorld();
	if (!UVeloxAudioSettings::CanPlayAudio(World))
	{
		return;
	}
	const UVeloxWeaponDefinition* Definition = FindCueDefinition(CueTag);
	const UVeloxWeaponCosmetics* Cosmetics = Definition ? Definition->Cosmetics.Get() : nullptr;
	if (!Cosmetics)
	{
		return;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (CueTag == Definition->FireCueTag)
	{
		// Sounds are played only when resident (the asset manager's load is done): a cue never loads.
		if (Cosmetics->FireSound.Get())
		{
			if (UVeloxAudioSettings::IsLocalPlayerPawn(Pawn))
			{
				// The own weapon plays 2D at full volume, outside the concurrency groups (never stolen).
				UVeloxAudioSettings::Play2D(World, Cosmetics->FireSound, *TagName);
			}
			else
			{
				// Fire sounds come from the muzzle origin.
				UVeloxAudioSettings::PlayAt(World, Cosmetics->FireSound, Start, EVeloxAudioAttenuation::WeaponFire, EVeloxAudioConcurrency::Weapons, *TagName);
			}
		}
		// Muzzle light from the weapon's cosmetics; intensity 0 = none.
		if (Cosmetics->MuzzleLightIntensity > 0.f)
		{
			const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
			VeloxMovableLights::Acquire(GetWorld(), GetOwner(), Start, Cosmetics->MuzzleLightColor, Cosmetics->MuzzleLightIntensity, Settings.MuzzleLightRadius,
				Settings.MuzzleLightSeconds);
		}
	}
	else if (CueTag == Definition->ImpactCueTag && Cosmetics->ImpactSound.Get())
	{
		// Impacts come from the hit point.
		UVeloxAudioSettings::PlayAt(World, Cosmetics->ImpactSound, End, Cosmetics->ImpactAttenuation, EVeloxAudioConcurrency::Impacts, *TagName);
	}
}

void UVeloxCombatFeedbackComponent::SpawnTracer(const FGameplayTag& CueTag, const FVector& Start, const FVector& End)
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner || VeloxServerKind::IsServerLike(World))
	{
		return;
	}
	const UVeloxWeaponDefinition* Definition = FindCueDefinition(CueTag);
	const UVeloxWeaponCosmetics* Cosmetics = Definition ? Definition->Cosmetics.Get() : nullptr;
	if (!Cosmetics || Cosmetics->TracerLifetime <= 0.f || CueTag != Definition->FireCueTag)
	{
		return;
	}
	UStaticMesh* Mesh = Cosmetics->TracerMesh.Get();
	UMaterialInterface* Material = Cosmetics->TracerMaterial.Get();
	if (!Mesh || !Material)
	{
		return;
	}

	FVector From = Start;
	bool bFromViewModel = false;
	if (UVeloxAudioSettings::IsLocalPlayerPawn(Cast<APawn>(Owner)))
	{
		const UVeloxViewModelComponent* ViewModel = Owner->FindComponentByClass<UVeloxViewModelComponent>();
		const UStaticMeshComponent* ViewMesh = ViewModel ? ViewModel->GetViewMeshComponent() : nullptr;
		if (ViewMesh && ViewMesh->IsVisible() && ViewMesh->DoesSocketExist(Cosmetics->MuzzleSocket))
		{
			// The view model is rendered with FirstPersonScale (it shrinks toward the camera): scale the socket the same way, from the eye.
			const FVector Socket = ViewMesh->GetSocketLocation(Cosmetics->MuzzleSocket);
			From = Start + (Socket - Start) * ViewModel->GetSettings().FirstPersonScale;
			bFromViewModel = true;
		}
	}
	const FVector Delta = End - From;
	const float Length = static_cast<float>(Delta.Size());
	if (Length < 1.f || !Owner->GetRootComponent())
	{
		return;
	}

	LiveTracers.RemoveAll([](const UStaticMeshComponent* Tracer) { return !IsValid(Tracer); });
	UStaticMeshComponent* Tracer = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
	Tracer->SetStaticMesh(Mesh);
	Tracer->SetMobility(EComponentMobility::Movable);
	Tracer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tracer->SetGenerateOverlapEvents(false);
	Tracer->SetCastShadow(false);
	Tracer->bAffectDynamicIndirectLighting = false;
	Tracer->SetCanEverAffectNavigation(false);
	Tracer->SetupAttachment(Owner->GetRootComponent());
	Tracer->SetUsingAbsoluteLocation(true);
	Tracer->SetUsingAbsoluteRotation(true);
	Tracer->SetUsingAbsoluteScale(true);
	Tracer->RegisterComponent();
	// The unit beam runs from its origin along +X, 100 cm long, 1 cm radius.
	Tracer->SetWorldTransform(FTransform(Delta.ToOrientationQuat(), From, FVector(Length / 100.f, Cosmetics->TracerWidth, Cosmetics->TracerWidth)));
	if (UMaterialInstanceDynamic* Mid = Tracer->CreateDynamicMaterialInstance(0, Material))
	{
		Mid->SetVectorParameterValue(TEXT("Color"), Cosmetics->TracerColor);
	}
	LiveTracers.Add(Tracer);
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Tracer, [Tracer]() { Tracer->DestroyComponent(); }), Cosmetics->TracerLifetime, false);
	UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxTracer weapon=%s pawn=%s from=%s to=%s len_cm=%.0f life=%.2f muzzle=%d"), *GetNameSafe(Definition), *GetNameSafe(Owner), *From.ToCompactString(),
		*End.ToCompactString(), Length, Cosmetics->TracerLifetime, bFromViewModel ? 1 : 0);
}

void UVeloxCombatFeedbackComponent::UpdateBeamAudio(const FGameplayTag& CueTag, bool bStart)
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!UVeloxAudioSettings::CanPlayAudio(World) || !Owner)
	{
		return;
	}
	const UVeloxWeaponCosmetics* Cosmetics = FindCueCosmetics(CueTag);
	if (!Cosmetics)
	{
		return;
	}
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	const bool bOwn = UVeloxAudioSettings::IsLocalPlayerPawn(Cast<APawn>(Owner));
	auto PlayOneShot = [&](const TSoftObjectPtr<USoundBase>& Sound, const TCHAR* Name)
	{
		if (!Sound.Get())
		{
			return;
		}
		if (bOwn)
		{
			UVeloxAudioSettings::Play2D(World, Sound, Name);
		}
		else
		{
			UVeloxAudioSettings::PlayAt(World, Sound, Owner->GetActorLocation(), EVeloxAudioAttenuation::WeaponFire, EVeloxAudioConcurrency::Weapons, Name);
		}
	};
	if (bStart)
	{
		if (BeamAudio)
		{
			return;
		}
		PlayOneShot(Cosmetics->BeamStart, TEXT("beam_start"));
		USoundBase* LoopSound = Cosmetics->BeamLoop.Get();
		if (!LoopSound || !Owner->GetRootComponent())
		{
			return;
		}
		UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio beam loop start on %s own=%d"), *GetNameSafe(Owner), bOwn ? 1 : 0);
		BeamAudio = bOwn
			? UGameplayStatics::SpawnSound2D(World, LoopSound)
			: UGameplayStatics::SpawnSoundAttached(LoopSound, Owner->GetRootComponent(), NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, 1.f, 1.f, 0.f,
				Settings.LoadAttenuation(EVeloxAudioAttenuation::WeaponFire), Settings.LoadConcurrency(EVeloxAudioConcurrency::Weapons));
		return;
	}
	if (BeamAudio)
	{
		UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio beam loop stop on %s"), *GetNameSafe(Owner));
		BeamAudio->Stop();
		BeamAudio = nullptr;
	}
	PlayOneShot(Cosmetics->BeamStop, TEXT("beam_stop"));
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
		if (!bWasActive && Powerup == VeloxGameplayTags::Powerup_Quad && UVeloxAudioSettings::IsLocalPlayerPawn(Pawn))
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
