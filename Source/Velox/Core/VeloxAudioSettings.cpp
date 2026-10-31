// Velox - audio tunables and play helpers.

#include "Core/VeloxAudioSettings.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreMisc.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundMix.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAudioSettings)

DEFINE_LOG_CATEGORY(LogVeloxAudio);

int32 UVeloxAudioSettings::NumPlayed = 0;

UVeloxAudioSettings::UVeloxAudioSettings()
{
	CategoryName = TEXT("Velox");
}

const UVeloxAudioSettings& UVeloxAudioSettings::Get()
{
	return *GetDefault<UVeloxAudioSettings>();
}

const FVeloxCueSound* UVeloxAudioSettings::FindCueSound(const FGameplayTag& Cue) const
{
	return CueSounds.FindByPredicate([&Cue](const FVeloxCueSound& Entry) { return Entry.Cue == Cue; });
}

TSoftObjectPtr<USoundBase> UVeloxAudioSettings::FindAnnouncerLine(FName Name) const
{
	const FVeloxNamedSound* Found = AnnouncerLines.FindByPredicate([Name](const FVeloxNamedSound& Entry) { return Entry.Name == Name; });
	return Found ? Found->Sound : TSoftObjectPtr<USoundBase>();
}

USoundAttenuation* UVeloxAudioSettings::LoadAttenuation(EVeloxAudioAttenuation Kind) const
{
	switch (Kind)
	{
	case EVeloxAudioAttenuation::WeaponFire: return AttenuationWeaponFire.LoadSynchronous();
	case EVeloxAudioAttenuation::Explosion: return AttenuationExplosion.LoadSynchronous();
	case EVeloxAudioAttenuation::Impact: return AttenuationImpact.LoadSynchronous();
	case EVeloxAudioAttenuation::Footstep: return AttenuationFootstep.LoadSynchronous();
	case EVeloxAudioAttenuation::Player: return AttenuationPlayer.LoadSynchronous();
	case EVeloxAudioAttenuation::Pickup: return AttenuationPickup.LoadSynchronous();
	case EVeloxAudioAttenuation::World: return AttenuationWorld.LoadSynchronous();
	default: return nullptr;
	}
}

USoundConcurrency* UVeloxAudioSettings::LoadConcurrency(EVeloxAudioConcurrency Kind) const
{
	switch (Kind)
	{
	case EVeloxAudioConcurrency::Weapons: return ConcurrencyWeapons.LoadSynchronous();
	case EVeloxAudioConcurrency::Footsteps: return ConcurrencyFootsteps.LoadSynchronous();
	case EVeloxAudioConcurrency::Impacts: return ConcurrencyImpacts.LoadSynchronous();
	default: return nullptr;
	}
}

bool UVeloxAudioSettings::CanPlayAudio(const UWorld* World)
{
	return World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer && !IsRunningDedicatedServer() && !IsRunningCommandlet();
}

void UVeloxAudioSettings::LoadAll()
{
	bLoadedOnce = true;
	auto Add = [this](const auto& Soft)
	{
		if (!Soft.IsNull())
		{
			if (UObject* Object = Soft.LoadSynchronous())
			{
				LoadedAssets.Add(Object);
			}
			else
			{
				UE_LOG(LogVeloxAudio, Warning, TEXT("VeloxAudio asset %s could not be loaded"), *Soft.ToString());
			}
		}
	};
	Add(AttenuationWeaponFire);
	Add(AttenuationExplosion);
	Add(AttenuationImpact);
	Add(AttenuationFootstep);
	Add(AttenuationPlayer);
	Add(AttenuationPickup);
	Add(AttenuationWorld);
	Add(ConcurrencyWeapons);
	Add(ConcurrencyFootsteps);
	Add(ConcurrencyImpacts);
	Add(AnnouncerMix);
	for (const FVeloxCueSound& Entry : CueSounds)
	{
		Add(Entry.Sound);
	}
	for (const FVeloxNamedSound& Entry : AnnouncerLines)
	{
		Add(Entry.Sound);
	}
	for (const TSoftObjectPtr<USoundBase>& Entry : FootstepSounds)
	{
		Add(Entry);
	}
	Add(BeamStartSound);
	Add(BeamStopSound);
	Add(HitConfirmSound);
	Add(DryFireSound);
	Add(WeaponSwitchSound);
	Add(JumpSound);
	Add(LandSoftSound);
	Add(LandHardSound);
	Add(PainLightSound);
	Add(PainMediumSound);
	Add(PainHeavySound);
	Add(DeathSound);
	Add(JumpPadSound);
	Add(ItemRespawnSound);
	Add(UIClickSound);
	UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio preloaded %d assets"), LoadedAssets.Num());
}

void UVeloxAudioSettings::EnsureLoaded(const UWorld* World)
{
	UVeloxAudioSettings* Settings = GetMutableDefault<UVeloxAudioSettings>();
	if (!CanPlayAudio(World))
	{
		if (World && !Settings->bLoggedServer && World->GetNetMode() == NM_DedicatedServer)
		{
			Settings->bLoggedServer = true;
			UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio dedicated server: no audio assets loaded"));
		}
		return;
	}
	if (!Settings->bLoadedOnce)
	{
		Settings->LoadAll();
	}
}

bool UVeloxAudioSettings::PlayAt(const UWorld* World, const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location, EVeloxAudioAttenuation Attenuation,
	EVeloxAudioConcurrency Concurrency, const TCHAR* DebugName, float VolumeMultiplier)
{
	if (!CanPlayAudio(World) || Sound.IsNull())
	{
		return false;
	}
	const UVeloxAudioSettings& Settings = Get();
	USoundBase* Loaded = Sound.LoadSynchronous();
	if (!Loaded)
	{
		return false;
	}
	++NumPlayed;
	UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio play %s sound=%s at %s atten=%d"), DebugName, *Loaded->GetName(), *Location.ToCompactString(), static_cast<int32>(Attenuation));
	UGameplayStatics::PlaySoundAtLocation(World, Loaded, Location, FRotator::ZeroRotator, VolumeMultiplier, 1.f, 0.f, Settings.LoadAttenuation(Attenuation),
		Settings.LoadConcurrency(Concurrency));
	return true;
}

bool UVeloxAudioSettings::Play2D(const UWorld* World, const TSoftObjectPtr<USoundBase>& Sound, const TCHAR* DebugName, float VolumeMultiplier)
{
	if (!CanPlayAudio(World) || Sound.IsNull())
	{
		return false;
	}
	USoundBase* Loaded = Sound.LoadSynchronous();
	if (!Loaded)
	{
		return false;
	}
	++NumPlayed;
	UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio play2d %s sound=%s"), DebugName, *Loaded->GetName());
	UGameplayStatics::PlaySound2D(World, Loaded, VolumeMultiplier);
	return true;
}
