// Velox - nav areas of the Velox nav links (ADR 0005 section 4: DefaultCost / FixedAreaEnteringCost per type).

#include "Bots/VeloxNavAreas.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxNavAreas)

UVeloxNavArea_JumpPad::UVeloxNavArea_JumpPad(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultCost = 1.f;
	FixedAreaEnteringCost = 0.f;
	DrawColor = FColor(25, 255, 50);
}

UVeloxNavArea_Drop::UVeloxNavArea_Drop(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultCost = 1.f;
	FixedAreaEnteringCost = 0.f;
	DrawColor = FColor(255, 200, 0);
}

UVeloxNavArea_Jump::UVeloxNavArea_Jump(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultCost = 1.f;
	FixedAreaEnteringCost = 50.f;
	DrawColor = FColor(0, 160, 255);
}

UVeloxNavArea_JumpFast::UVeloxNavArea_JumpFast(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultCost = 1.f;
	FixedAreaEnteringCost = 100.f;
	DrawColor = FColor(160, 0, 255);
}

UVeloxNavArea_RocketJump::UVeloxNavArea_RocketJump(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultCost = 1.f;
	FixedAreaEnteringCost = 400.f;
	DrawColor = FColor(255, 40, 40);
}

UVeloxNavArea_Hazard::UVeloxNavArea_Hazard(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Per cm 50 x a floor poly plus 5000 on entering: a 300 cm channel costs ~20000, any walk around it on L_HazardTest
	// (<= 2000 cm extra) is cheaper (task M7 H7).
	DefaultCost = 50.f;
	FixedAreaEnteringCost = 5000.f;
	DrawColor = FColor(255, 90, 0);
}
