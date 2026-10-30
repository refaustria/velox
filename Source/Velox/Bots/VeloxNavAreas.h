// Velox - nav areas of the Velox nav links (ADR 0005 section 4). One area per EVeloxNavLinkType; the area carries the
// traversal cost, the query filters (VeloxNavFilters.h) decide which link types a bot may use. C++ UNavArea subclasses
// are registered by the navigation system automatically. Costs are config properties (UNavArea is Config=Engine): the
// constructor values are the ADR defaults and can be overridden per class in DefaultEngine.ini
// ([/Script/Velox.VeloxNavArea_Jump] FixedAreaEnteringCost=...).

#pragma once

#include "NavAreas/NavArea.h"

#include "VeloxNavAreas.generated.h"

/** Pad link: the fastest route, no entering cost. */
UCLASS()
class VELOX_API UVeloxNavArea_JumpPad : public UNavArea
{
	GENERATED_BODY()

public:
	UVeloxNavArea_JumpPad(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/** Platform edge to the floor below ("walk off"). */
UCLASS()
class VELOX_API UVeloxNavArea_Drop : public UNavArea
{
	GENERATED_BODY()

public:
	UVeloxNavArea_Drop(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/** Gap any bot clears at MaxGroundSpeed (500 cm gap: 0.675 s x 800 cm/s = 540 cm). */
UCLASS()
class VELOX_API UVeloxNavArea_Jump : public UNavArea
{
	GENERATED_BODY()

public:
	UVeloxNavArea_Jump(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/** Gap that needs strafe speed (>= 1100 cm/s: the 700 cm gap). */
UCLASS()
class VELOX_API UVeloxNavArea_JumpFast : public UNavArea
{
	GENERATED_BODY()

public:
	UVeloxNavArea_JumpFast(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/** Rocket jump: costs health and a rocket, only worth a large detour (Q3 bots). */
UCLASS()
class VELOX_API UVeloxNavArea_RocketJump : public UNavArea
{
	GENERATED_BODY()

public:
	UVeloxNavArea_RocketJump(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
