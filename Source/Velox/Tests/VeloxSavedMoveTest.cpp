// Velox - saved-move flag plumbing, CanCombineWith rules and jump latch (ADR 0002 sections 5 and 7, A12). World-free.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Movement/VeloxMoveSim.h"
#include "Movement/VeloxMovementComponent.h"
#include "Movement/VeloxMovementSettings.h"
#include "Movement/VeloxNetworkMoveData.h"

namespace VeloxSavedMoveTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr float Dt = 1.f / 125.f;
	constexpr float MaxCombineDelta = 0.125f;

	/** A ground move that the engine rules allow to combine with another one built the same way. */
	TSharedPtr<FSavedMove_Velox> MakeGroundMove()
	{
		TSharedPtr<FSavedMove_Velox> Move = MakeShared<FSavedMove_Velox>();
		Move->Clear();
		Move->DeltaTime = Dt;
		Move->MaxSpeed = 800.f;
		Move->StartVelocity = FVector(800.0, 0.0, 0.0);
		Move->Acceleration = FVector(800.0, 0.0, 0.0);
		Move->AccelMag = 800.f;
		Move->AccelNormal = FVector(1.0, 0.0, 0.0);
		Move->StartPackedMovementMode = MOVE_Walking;
		Move->EndPackedMovementMode = MOVE_Walking;
		return Move;
	}

	bool Combines(const TSharedPtr<FSavedMove_Velox>& Old, const TSharedPtr<FSavedMove_Velox>& New)
	{
		return Old->CanCombineWith(New, nullptr, MaxCombineDelta);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementSavedMoveFlagsTest, "Velox.Movement.SavedMoveFlags", VeloxSavedMoveTest::Flags)

bool FVeloxMovementSavedMoveFlagsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FLAG_JumpHeld uses FLAG_Custom_0"), static_cast<int32>(FSavedMove_Velox::FLAG_JumpHeld), static_cast<int32>(FSavedMove_Character::FLAG_Custom_0));
	TestEqual(TEXT("FLAG_HasForwardInput uses FLAG_Custom_1"), static_cast<int32>(FSavedMove_Velox::FLAG_HasForwardInput), static_cast<int32>(FSavedMove_Character::FLAG_Custom_1));
	TestEqual(TEXT("FLAG_HasSideInput uses FLAG_Custom_2"), static_cast<int32>(FSavedMove_Velox::FLAG_HasSideInput), static_cast<int32>(FSavedMove_Character::FLAG_Custom_2));

	FSavedMove_Velox Move;
	Move.Clear();
	TestEqual(TEXT("Cleared move carries no flags"), static_cast<int32>(Move.GetCompressedFlags()), 0);

	Move.bSavedJumpHeld = true;
	uint8 Flags = Move.GetCompressedFlags();
	TestTrue(TEXT("Jump held sets FLAG_JumpHeld"), (Flags & FSavedMove_Velox::FLAG_JumpHeld) != 0);
	TestTrue(TEXT("Jump held does not set FLAG_JumpPressed"), (Flags & FSavedMove_Character::FLAG_JumpPressed) == 0);

	Move.bSavedHasForwardInput = true;
	Flags = Move.GetCompressedFlags();
	TestTrue(TEXT("Forward input sets FLAG_HasForwardInput"), (Flags & FSavedMove_Velox::FLAG_HasForwardInput) != 0);
	TestTrue(TEXT("Forward input does not set FLAG_HasSideInput"), (Flags & FSavedMove_Velox::FLAG_HasSideInput) == 0);
	Move.bSavedHasSideInput = true;
	TestTrue(TEXT("Side input sets FLAG_HasSideInput"), (Move.GetCompressedFlags() & FSavedMove_Velox::FLAG_HasSideInput) != 0);

	// Latch, knockback and grace are not flags: the server derives them.
	Move.Clear();
	Move.bSavedJumpLatched = true;
	Move.SavedKnockbackGrace = 0.1f;
	Move.SavedPendingKnockback.Add(FVector(0.0, 0.0, 1000.0), true);
	TestEqual(TEXT("Latch/knockback/grace add no flags"), static_cast<int32>(Move.GetCompressedFlags()), 0);

	Move.bSavedJumpHeld = true;
	Move.bSavedHasForwardInput = true;
	Move.bSavedHasSideInput = true;
	Move.Clear();
	TestEqual(TEXT("Clear removes all flags"), static_cast<int32>(Move.GetCompressedFlags()), 0);
	TestFalse(TEXT("Clear resets the latch"), static_cast<bool>(Move.bSavedJumpLatched));
	TestFalse(TEXT("Clear resets the pending knockback"), Move.SavedPendingKnockback.IsPending());
	TestEqual(TEXT("Clear resets the grace"), Move.SavedKnockbackGrace, 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementSavedMoveCombineTest, "Velox.Movement.SavedMoveCombine", VeloxSavedMoveTest::Flags)

bool FVeloxMovementSavedMoveCombineTest::RunTest(const FString& Parameters)
{
	using namespace VeloxSavedMoveTest;

	TestTrue(TEXT("baseline: two identical ground moves combine"), Combines(MakeGroundMove(), MakeGroundMove()));

	{
		auto Old = MakeGroundMove(), New = MakeGroundMove();
		Old->bSavedJumpHeld = true; New->bSavedJumpHeld = true;
		TestTrue(TEXT("same jump held combines"), Combines(Old, New));
		New->bSavedJumpHeld = false;
		TestFalse(TEXT("jump held differs"), Combines(Old, New));
	}
	{
		auto Old = MakeGroundMove(), New = MakeGroundMove();
		New->bSavedJumpLatched = true;
		TestFalse(TEXT("latch differs"), Combines(Old, New));
		Old->bSavedJumpLatched = true;
		TestTrue(TEXT("same latch combines"), Combines(Old, New));
	}
	{
		auto Old = MakeGroundMove(), New = MakeGroundMove();
		Old->SavedPendingKnockback.Add(FVector(500.0, 0.0, 0.0), false);
		TestFalse(TEXT("old move has a pending knockback"), Combines(Old, New));
		Old->SavedPendingKnockback.Reset();
		New->SavedPendingKnockback.Add(FVector::ZeroVector, true);
		TestFalse(TEXT("new move has a pending (zero override) knockback"), Combines(Old, New));
	}
	{
		auto Old = MakeGroundMove(), New = MakeGroundMove();
		Old->SavedKnockbackGrace = 0.05f;
		TestFalse(TEXT("old move has grace"), Combines(Old, New));
		Old->SavedKnockbackGrace = 0.f;
		New->SavedKnockbackGrace = 0.05f;
		TestFalse(TEXT("new move has grace"), Combines(Old, New));
	}
	{
		auto Old = MakeGroundMove(), New = MakeGroundMove();
		Old->StartPackedMovementMode = MOVE_Falling; Old->EndPackedMovementMode = MOVE_Falling;
		New->StartPackedMovementMode = MOVE_Falling; New->EndPackedMovementMode = MOVE_Falling;
		TestFalse(TEXT("air moves do not combine without bCombineAirMoves"), Combines(Old, New));
		TestFalse(TEXT("Velox rules: air moves, bCombineAirMoves off"), Old->CanCombineVeloxState(*New, false));
		TestTrue(TEXT("Velox rules: air moves, bCombineAirMoves on"), Old->CanCombineVeloxState(*New, true));
		New->StartPackedMovementMode = MOVE_Walking;
		TestFalse(TEXT("Velox rules: one falling start is enough"), Old->CanCombineVeloxState(*New, false));
	}
	{
		auto Old = MakeGroundMove(), New = MakeGroundMove();
		New->bSavedHasForwardInput = true;
		TestFalse(TEXT("forward presence differs"), Combines(Old, New));
		TestFalse(TEXT("Velox rules: forward presence differs"), Old->CanCombineVeloxState(*New, true));
		New->bSavedHasForwardInput = false;
		New->bSavedHasSideInput = true;
		TestFalse(TEXT("Velox rules: side presence differs"), Old->CanCombineVeloxState(*New, true));
	}
	{
		// Engine rules still apply after the Velox ones.
		auto Old = MakeGroundMove(), New = MakeGroundMove();
		New->bForceNoCombine = true;
		TestFalse(TEXT("engine bForceNoCombine"), Combines(Old, New));
	}

	// Pending knockback accumulation (AddKnockback semantics).
	FVeloxPendingKnockback Pending;
	TestFalse(TEXT("empty pending"), Pending.IsPending());
	Pending.Add(FVector(100.0, 0.0, 0.0), false);
	Pending.Add(FVector(0.0, 50.0, 0.0), false);
	TestEqual(TEXT("additive knockbacks sum"), Pending.Velocity, FVector(100.0, 50.0, 0.0));
	Pending.Add(FVector(0.0, 0.0, 900.0), true);
	TestTrue(TEXT("override replaces earlier knockbacks"), Pending.bOverride && Pending.Velocity == FVector(0.0, 0.0, 900.0));
	Pending.Add(FVector(10.0, 0.0, 0.0), false);
	TestTrue(TEXT("additive after override keeps the override"), Pending.bOverride && Pending.Velocity == FVector(10.0, 0.0, 900.0));
	Pending.Reset();
	Pending.Add(FVector(0.0, 0.0, 900.0), true);
	const FVeloxPendingKnockback Once = Pending;
	Pending.Add(FVector(0.0, 0.0, 900.0), true);
	TestTrue(TEXT("re-fired override is idempotent"), Pending == Once);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementJumpLatchTest, "Velox.Movement.JumpLatch", VeloxSavedMoveTest::Flags)

bool FVeloxMovementJumpLatchTest::RunTest(const FString& Parameters)
{
	// The A12 latch rules on a hand-built VQ3 move sequence (bAutoHop off), with VeloxMoveSim for the landing frame. The
	// same rules drive UVeloxMovementComponent::PerformMovement/ProcessLanded; the CMC-level check is FT_MovementDeterminism.
	using namespace VeloxSavedMoveTest;
	FVeloxMoveParams P = GetDefault<UVeloxMovementSettings>()->BuildSimParams();
	P.bAutoHop = false;

	struct FMove { bool bHeld; };

	// Runs airborne moves (after a ground jump set the latch), returns whether the landing move jumps.
	auto LandingJumps = [&P](const TArray<FMove>& AirMoves, bool bHeldOnLanding)
	{
		bool bLatched = true; // set by the take-off jump (DoJump)
		for (const FMove& Move : AirMoves)
		{
			bLatched = UVeloxMovementComponent::JumpLatchAtMoveStart(bLatched, Move.bHeld);
		}
		bLatched = UVeloxMovementComponent::JumpLatchAtMoveStart(bLatched, bHeldOnLanding);
		return UVeloxMovementComponent::WantsLandingJump(bHeldOnLanding, P.bAutoHop, bLatched);
	};

	TestTrue(TEXT("VQ3: release then press in the air jumps on landing"), LandingJumps({ { true }, { false }, { true }, { true } }, true));
	TestFalse(TEXT("VQ3: held through the landing without release does not jump"), LandingJumps({ { true }, { true }, { true } }, true));
	TestFalse(TEXT("VQ3: released on landing does not jump"), LandingJumps({ { true }, { false } }, false));
	TestTrue(TEXT("VQ3: pressed exactly on the landing move after a release jumps"), LandingJumps({ { false } }, true));
	TestTrue(TEXT("CPM auto-hop: held through the landing jumps"), UVeloxMovementComponent::WantsLandingJump(true, true, true));
	TestFalse(TEXT("no jump without jump held"), UVeloxMovementComponent::WantsLandingJump(false, true, false));

	// The landing move that jumps: XY unchanged, Z = JumpVelocity + gravity for the rest of the frame.
	FVeloxMoveState S;
	S.Velocity = FVector(1000.0, 0.0, -300.0);
	S.bOnGround = true;
	FVeloxMoveInput In;
	In.bJumpHeld = true;
	VeloxMoveSim::StepFrame(S, In, Dt, P);
	TestEqual(TEXT("landing jump keeps XY"), FVector(S.Velocity.X, S.Velocity.Y, 0.0), FVector(1000.0, 0.0, 0.0));
	TestTrue(TEXT("landing jump Z = JumpVelocity + GravityZ * dt"), FMath::IsNearlyEqual(S.Velocity.Z, static_cast<double>(P.JumpVelocity + P.GravityZ * Dt), 1e-3));
	TestFalse(TEXT("airborne after the landing jump"), S.bOnGround);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
