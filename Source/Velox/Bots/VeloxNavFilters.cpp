// Velox - nav query filters (ADR 0005 section 4).

#include "Bots/VeloxNavFilters.h"

#include "Bots/VeloxNavAreas.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxNavFilters)

UVeloxNavFilter_Walk::UVeloxNavFilter_Walk(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AddExcludedArea(UVeloxNavArea_JumpFast::StaticClass());
	AddExcludedArea(UVeloxNavArea_RocketJump::StaticClass());
}

UVeloxNavFilter_Hop::UVeloxNavFilter_Hop(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AddExcludedArea(UVeloxNavArea_RocketJump::StaticClass());
}

UVeloxNavFilter_Full::UVeloxNavFilter_Full(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

namespace VeloxNavFilters
{
	TSubclassOf<UNavigationQueryFilter> FilterClassByName(const FString& Name)
	{
		if (Name.Equals(TEXT("Walk"), ESearchCase::IgnoreCase))
		{
			return UVeloxNavFilter_Walk::StaticClass();
		}
		if (Name.Equals(TEXT("Hop"), ESearchCase::IgnoreCase))
		{
			return UVeloxNavFilter_Hop::StaticClass();
		}
		if (Name.Equals(TEXT("Full"), ESearchCase::IgnoreCase))
		{
			return UVeloxNavFilter_Full::StaticClass();
		}
		return nullptr;
	}
}
