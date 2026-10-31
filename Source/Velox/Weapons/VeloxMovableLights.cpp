// Velox - movable light budget.

#include "Weapons/VeloxMovableLights.h"

#include "Components/PointLightComponent.h"
#include "Core/VeloxAudioSettings.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

namespace VeloxMovableLights
{
	/** Pruned lazily, so a destroyed world cannot leak the count. */
	static TArray<TWeakObjectPtr<UPointLightComponent>> ActiveLights;

	static void Prune()
	{
		ActiveLights.RemoveAll([](const TWeakObjectPtr<UPointLightComponent>& Light) { return !Light.IsValid(); });
	}

	int32 NumActive()
	{
		Prune();
		return ActiveLights.Num();
	}

	bool Acquire(UWorld* World, AActor* Owner, const FVector& Location, const FLinearColor& Color, float Intensity, float Radius, float Seconds)
	{
		const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
		if (!World || !Owner || Settings.MaxMovableLights <= 0 || Seconds <= 0.f || Intensity <= 0.f)
		{
			return false;
		}
		Prune();
		if (ActiveLights.Num() >= Settings.MaxMovableLights)
		{
			return false;
		}
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner, NAME_None, RF_Transient);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetCastShadows(false);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(Intensity);
		Light->SetLightColor(Color);
		Light->SetAttenuationRadius(Radius);
		Light->SetWorldLocation(Location);
		Light->RegisterComponent();
		ActiveLights.Add(Light);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakLight = TWeakObjectPtr<UPointLightComponent>(Light)]()
		{
			if (UPointLightComponent* Expired = WeakLight.Get())
			{
				Expired->DestroyComponent();
			}
		}), Seconds, false);
		return true;
	}
}
