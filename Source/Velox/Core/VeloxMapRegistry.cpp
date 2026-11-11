// Velox - map registry (task M6-04).

#include "Core/VeloxMapRegistry.h"

#include "Engine/World.h"
#include "Misc/PackageName.h"

const UVeloxMapRegistry& UVeloxMapRegistry::Get()
{
	return *GetDefault<UVeloxMapRegistry>();
}

FString UVeloxMapRegistry::ShortNameOfPackage(const FString& PackageName)
{
	FString Name = FPackageName::GetShortName(PackageName);
	// PIE copies: UEDPIE_<instance>_<name>.
	static const FString PiePrefix = TEXT("UEDPIE_");
	if (Name.StartsWith(PiePrefix))
	{
		const int32 Sep = Name.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, PiePrefix.Len());
		if (Sep != INDEX_NONE)
		{
			Name = Name.Mid(Sep + 1);
		}
	}
	return Name;
}

FString UVeloxMapRegistry::ShortNameOfWorld(const UWorld* World)
{
	if (!World)
	{
		return FString();
	}
	const UPackage* Package = World->GetOutermost();
	return ShortNameOfPackage(Package ? Package->GetName() : World->GetMapName());
}

const FVeloxMapInfo* UVeloxMapRegistry::FindByShortName(const FString& ShortName) const
{
	const FString Wanted = ShortNameOfPackage(ShortName);
	for (const FVeloxMapInfo& Info : Maps)
	{
		if (Info.ShortName.Equals(Wanted, ESearchCase::IgnoreCase))
		{
			return &Info;
		}
	}
	return nullptr;
}

const FVeloxMapInfo* UVeloxMapRegistry::FindByWorld(const UWorld* World) const
{
	return World ? FindByShortName(ShortNameOfWorld(World)) : nullptr;
}

FString UVeloxMapRegistry::DisplayNameFor(const FString& ShortName) const
{
	const FVeloxMapInfo* Info = FindByShortName(ShortName);
	return Info && !Info->DisplayName.IsEmpty() ? Info->DisplayName.ToString() : ShortName;
}
