// Velox - UI tunables.

#include "UI/VeloxUISettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxUISettings)

UVeloxUISettings::UVeloxUISettings()
{
	CategoryName = TEXT("Velox");
}

const UVeloxUISettings& UVeloxUISettings::Get()
{
	return *GetDefault<UVeloxUISettings>();
}
