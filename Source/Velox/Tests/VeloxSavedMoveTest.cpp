// Velox - saved-move flag plumbing (world-free).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Movement/VeloxNetworkMoveData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementSavedMoveFlagsTest, "Velox.Movement.SavedMoveFlags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxMovementSavedMoveFlagsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FLAG_JumpHeld uses FLAG_Custom_0"), static_cast<int32>(FSavedMove_Velox::FLAG_JumpHeld), static_cast<int32>(FSavedMove_Character::FLAG_Custom_0));

	FSavedMove_Velox Move;
	Move.Clear();
	TestEqual(TEXT("Cleared move carries no flags"), static_cast<int32>(Move.GetCompressedFlags()), 0);

	Move.bSavedJumpHeld = true;
	const uint8 Flags = Move.GetCompressedFlags();
	TestTrue(TEXT("Jump held sets FLAG_JumpHeld"), (Flags & FSavedMove_Velox::FLAG_JumpHeld) != 0);
	TestTrue(TEXT("Jump held does not set FLAG_JumpPressed"), (Flags & FSavedMove_Character::FLAG_JumpPressed) == 0);

	Move.Clear();
	TestFalse(TEXT("Clear resets jump held"), static_cast<bool>(Move.bSavedJumpHeld));
	TestEqual(TEXT("Clear removes FLAG_JumpHeld"), static_cast<int32>(Move.GetCompressedFlags() & FSavedMove_Velox::FLAG_JumpHeld), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
