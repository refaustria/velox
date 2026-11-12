// Velox - hazard volume registry (ADR 0008 section 3.3).

#include "Combat/VeloxHazardSubsystem.h"

#include "Combat/VeloxHazardVolume.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxHazardSubsystem)

UVeloxHazardSubsystem* UVeloxHazardSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UVeloxHazardSubsystem>() : nullptr;
}

void UVeloxHazardSubsystem::RegisterVolume(AVeloxHazardVolume* Volume)
{
	if (Volume)
	{
		Volumes.RemoveAll([](const TWeakObjectPtr<AVeloxHazardVolume>& V) { return !V.IsValid(); });
		Volumes.AddUnique(Volume);
	}
}

void UVeloxHazardSubsystem::UnregisterVolume(AVeloxHazardVolume* Volume)
{
	Volumes.RemoveAll([Volume](const TWeakObjectPtr<AVeloxHazardVolume>& V) { return !V.IsValid() || V.Get() == Volume; });
}

bool UVeloxHazardSubsystem::HasHazards() const
{
	return Volumes.ContainsByPredicate([](const TWeakObjectPtr<AVeloxHazardVolume>& V) { return V.IsValid(); });
}

AVeloxHazardVolume* UVeloxHazardSubsystem::FindHazardAt(const FVector& Point) const
{
	for (const TWeakObjectPtr<AVeloxHazardVolume>& Weak : Volumes)
	{
		AVeloxHazardVolume* Volume = Weak.Get();
		if (Volume && Volume->ContainsPoint(Point))
		{
			return Volume;
		}
	}
	return nullptr;
}

bool UVeloxHazardSubsystem::IsInHazard(const FVector& Point, EVeloxHazardType Type) const
{
	for (const TWeakObjectPtr<AVeloxHazardVolume>& Weak : Volumes)
	{
		const AVeloxHazardVolume* Volume = Weak.Get();
		if (Volume && Volume->GetHazardType() == Type && Volume->ContainsPoint(Point))
		{
			return true;
		}
	}
	return false;
}

TArray<AVeloxHazardVolume*> UVeloxHazardSubsystem::GetVolumes() const
{
	TArray<AVeloxHazardVolume*> Out;
	for (const TWeakObjectPtr<AVeloxHazardVolume>& Weak : Volumes)
	{
		if (AVeloxHazardVolume* Volume = Weak.Get())
		{
			Out.Add(Volume);
		}
	}
	return Out;
}
