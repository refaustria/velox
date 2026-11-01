// Velox - weapon cosmetics.

#include "Core/VeloxWeaponCosmetics.h"

#include "Engine/StaticMesh.h"
#include "Sound/SoundBase.h"
#include "UObject/SoftObjectPath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxWeaponCosmetics)

void UVeloxWeaponCosmetics::GetSoftReferences(TArray<FSoftObjectPath>& OutPaths) const
{
	auto Add = [&OutPaths](const FSoftObjectPath& Path)
	{
		if (!Path.IsNull())
		{
			OutPaths.AddUnique(Path);
		}
	};
	Add(WorldMesh.ToSoftObjectPath());
	Add(ViewMesh.ToSoftObjectPath());
	Add(ProjectileMesh.ToSoftObjectPath());
	Add(TracerMesh.ToSoftObjectPath());
	Add(TracerMaterial.ToSoftObjectPath());
	Add(FireSound.ToSoftObjectPath());
	Add(ImpactSound.ToSoftObjectPath());
	Add(BeamStart.ToSoftObjectPath());
	Add(BeamLoop.ToSoftObjectPath());
	Add(BeamStop.ToSoftObjectPath());
}
