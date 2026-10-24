// Velox - saved-move flag plumbing, CanCombineWith rules and jump latch (ADR 0002 sections 5 and 7, A12, A17). World-free.
//
// Replay model (A17): FSavedMove_Velox::PrepMoveFor is Super only. The jump latch, KnockbackGrace and the pending
// knockback are derived state that chains from the correction through the replayed moves; the saved copies are read by
// CanCombineWith only. The replay itself needs a world and is covered by the task-05 net soak (LatchRun, PadRun scripts).


#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Movement/VeloxMoveSim.h"
#include "Movement/VeloxMovementComponent.h"
#include "Movement/VeloxMovementSettings.h"
#include "Movement/VeloxNetworkMoveData.h"

namespace VeloxSavedMoveTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr float SavedMoveDt = 1.f / 125.f;
	constexpr float MaxCombineDelta = 0.125f;

	/** A ground move that the engine rules allow to combine with another one built the same way. */
	TSharedPtr<FSavedMove_Velox> MakeGroundMove()
	{
		TSharedPtr<FSavedMove_Velox> Move = MakeShared<FSavedMove_Velox>();
		Move->Clear();
		Move->DeltaTime = SavedMoveDt;
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
	uint8 MoveFlags = Move.GetCompressedFlags();
	TestTrue(TEXT("Jump held sets FLAG_JumpHeld"), (MoveFlags & FSavedMove_Velox::FLAG_JumpHeld) != 0);
	TestTrue(TEXT("Jump held does not set FLAG_JumpPressed"), (MoveFlags & FSavedMove_Character::FLAG_JumpPressed) == 0);

	Move.bSavedHasForwardInput = true;
	MoveFlags = Move.GetCompressedFlags();
	TestTrue(TEXT("Forward input sets FLAG_HasForwardInput"), (MoveFlags & FSavedMove_Velox::FLAG_HasForwardInput) != 0);
	TestTrue(TEXT("Forward input does not set FLAG_HasSideInput"), (MoveFlags & FSavedMove_Velox::FLAG_HasSideInput) == 0);
	Move.bSavedHasSideInput = true;
	TestTrue(TEXT("Side input sets FLAG_HasSideInput"), (Move.GetCompressedFlags() & FSavedMove_Velox::FLAG_HasSideInput) != 0);

	// A17: latch, pending knockback and grace are derived state, never sent and never restored on replay. The server
	// derives them from the flag stream and its own overlaps; the client chains them from the correction. So a saved move
	// with all three set must look exactly like an empty move on the wire. (PrepMoveFor is Super only; no world-free test
	// of the replay is possible, the task-05 net soak's LatchRun/PadRun scripts cover it.)
	Move.Clear();
	Move.bSavedJumpLatched = true;
	Move.SavedKnockbackGrace = 0.1f;
	Move.SavedPendingKnockback.Add(FVector(0.0, 0.0, 1000.0), true);
	TestEqual(TEXT("A17: latch/knockback/grace add no flags"), static_cast<int32>(Move.GetCompressedFlags()), 0);
	TestTrue(TEXT("A17: the saved copies stay readable for CanCombineWith"), Move.bSavedJumpLatched && Move.SavedPendingKnockback.IsPending());

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

	// Pending knockback accumulation (AddKnockback semantics). With the A17 chaining model a replayed pad overlap re-fires
	// on the chained state: the response's pending value (if the server had not consumed it) plus nothing else, because
	// the correction teleport's overlap is dropped (A19). An override re-fired in the same move stays idempotent.
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
	// The latch chains move to move (latch_n = (latch_n-1 && held_n) || jumped_n), which is exactly what a replay computes
	// from the correction's value (A17); nothing is restored per move.
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

	// A17 chaining: a replay starts from the correction's latch and recomputes it per move from the flags and the replayed
	// ground contact, exactly like the server. Case (b) of A17: the client predicted a landing jump the server never made
	// (missed ledge); the server's latch is false and the real landing comes three moves later with jump still held.
	{
		struct FReplayMove { bool bHeld; bool bLands; };
		auto ReplayJumps = [&P](bool bStartLatch, const TArray<FReplayMove>& Moves)
		{
			TArray<bool> Jumps;
			bool bLatched = bStartLatch;
			for (const FReplayMove& Move : Moves)
			{
				bLatched = UVeloxMovementComponent::JumpLatchAtMoveStart(bLatched, Move.bHeld);
				const bool bJumped = Move.bLands && UVeloxMovementComponent::WantsLandingJump(Move.bHeld, P.bAutoHop, bLatched);
				bLatched = bLatched || bJumped; // DoJump sets the latch
				Jumps.Add(bJumped);
			}
			return Jumps;
		};
		const TArray<FReplayMove> ServerMoves = { { true, false }, { true, false }, { true, true } };
		// Server and chained client replay run the same recurrence from the same start value (the correction's latch).
		const TArray<bool> ChainedReplay = ReplayJumps(false, ServerMoves);
		TestTrue(TEXT("A17: chained replay jumps on the real landing, like the server"), ChainedReplay == TArray<bool>({ false, false, true }));
		// The superseded restore model would have forced the recorded (mispredicted) latch true onto the landing move.
		const bool bRestoredLatch = UVeloxMovementComponent::JumpLatchAtMoveStart(true, true);
		TestFalse(TEXT("A17: restoring the recorded latch would eat the server's landing jump"), UVeloxMovementComponent::WantsLandingJump(true, P.bAutoHop, bRestoredLatch));
	}

	// The landing move that jumps: XY unchanged, Z = JumpVelocity + gravity for the rest of the frame.
	FVeloxMoveState S;
	S.Velocity = FVector(1000.0, 0.0, -300.0);
	S.bOnGround = true;
	FVeloxMoveInput In;
	In.bJumpHeld = true;
	VeloxMoveSim::StepFrame(S, In, SavedMoveDt, P);
	TestEqual(TEXT("landing jump keeps XY"), FVector(S.Velocity.X, S.Velocity.Y, 0.0), FVector(1000.0, 0.0, 0.0));
	TestTrue(TEXT("landing jump Z = JumpVelocity + GravityZ * dt"), FMath::IsNearlyEqual(S.Velocity.Z, static_cast<double>(P.JumpVelocity + P.GravityZ * SavedMoveDt), 1e-3));
	TestFalse(TEXT("airborne after the landing jump"), S.bOnGround);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementChooseOldMoveTest, "Velox.Movement.SavedMove.ChooseOldMove", VeloxSavedMoveTest::Flags)
bool FVeloxMovementChooseOldMoveTest::RunTest(const FString& Parameters)
{
	// ADR 0002 A32: which saved move rides along as OldMove with an RPC.
	struct FMoves
	{
		TArray<FSavedMovePtr> Saved;
		FSavedMovePtr LastAcked;

		/** Appends a move with the given time stamp (and event flag) to the saved moves, oldest first. */
		const FSavedMove_Character* Add(float TimeStamp, bool bEnteredTrigger = false, bool bJumpedHeld = false, bool bBeforeReset = false)
		{
			TSharedPtr<FSavedMove_Velox> Move = MakeShared<FSavedMove_Velox>();
			Move->Clear();
			Move->TimeStamp = TimeStamp;
			Move->bSavedEnteredTrigger = bEnteredTrigger;
			Move->bSavedJumpedHeld = bJumpedHeld;
			Move->bOldTimeStampBeforeReset = bBeforeReset;
			Saved.Add(Move);
			return Move.Get();
		}
	};
	auto MakeAcked = [](float TimeStamp)
	{
		TSharedPtr<FSavedMove_Velox> Move = MakeShared<FSavedMove_Velox>();
		Move->Clear();
		Move->TimeStamp = TimeStamp;
		return FSavedMovePtr(Move);
	};

	{
		// Single-move RPC: the previous RPC's move.
		FMoves M;
		M.LastAcked = MakeAcked(1.0f);
		M.Add(1.0166f);
		const FSavedMove_Character* Previous = M.Add(1.0333f);
		const FSavedMove_Character* New = M.Add(1.05f);
		bool bEvent = true;
		TestTrue(TEXT("single-move RPC -> previous move"), FSavedMove_Velox::ChooseOldMove(M.Saved, M.LastAcked, New, nullptr, nullptr, &bEvent) == Previous);
		TestFalse(TEXT("single-move RPC: not an event move"), bEvent);
	}
	{
		// Dual RPC: the move before the pending one; the pending move itself travels in the RPC.
		FMoves M;
		M.LastAcked = MakeAcked(1.0f);
		const FSavedMove_Character* Previous = M.Add(1.0166f);
		const FSavedMove_Character* Pending = M.Add(1.025f, /*bEnteredTrigger*/ true);
		const FSavedMove_Character* New = M.Add(1.0333f);
		TestTrue(TEXT("dual RPC -> the move before the pending one (an event pending move is never returned)"),
			FSavedMove_Velox::ChooseOldMove(M.Saved, M.LastAcked, New, Pending, nullptr) == Previous);
	}
	{
		// An unacked event move two RPCs back wins over the previous move; the newest event move wins.
		FMoves M;
		M.LastAcked = MakeAcked(1.0f);
		M.Add(1.0083f, false, /*bJumpedHeld*/ true);
		const FSavedMove_Character* Entry = M.Add(1.0166f, /*bEnteredTrigger*/ true);
		M.Add(1.0333f);
		const FSavedMove_Character* New = M.Add(1.05f);
		bool bEvent = false;
		TestTrue(TEXT("event move two RPCs back wins over the previous move"), FSavedMove_Velox::ChooseOldMove(M.Saved, M.LastAcked, New, nullptr, nullptr, &bEvent) == Entry);
		TestTrue(TEXT("event move reported"), bEvent);
	}
	{
		// Acked and reset-flagged moves are skipped (an acked event move too).
		FMoves M;
		M.LastAcked = MakeAcked(1.02f);
		M.Add(1.01f, /*bEnteredTrigger*/ true);
		M.Add(1.02f);
		const FSavedMove_Character* Valid = M.Add(1.03f);
		M.Add(1.04f, /*bEnteredTrigger*/ true, false, /*bBeforeReset*/ true);
		const FSavedMove_Character* New = M.Add(1.05f);
		TestTrue(TEXT("acked and reset-flagged moves skipped"), FSavedMove_Velox::ChooseOldMove(M.Saved, M.LastAcked, New, nullptr, nullptr) == Valid);
	}
	{
		// Nothing eligible: the engine's choice (here: none, and some move), also without any acked move.
		FMoves M;
		M.LastAcked = MakeAcked(1.0f);
		M.Add(0.99f, false, false, /*bBeforeReset*/ true);
		const FSavedMove_Character* New = M.Add(1.05f);
		TestTrue(TEXT("nothing eligible -> engine's choice (null)"), FSavedMove_Velox::ChooseOldMove(M.Saved, M.LastAcked, New, nullptr, nullptr) == nullptr);
		const FSavedMove_Character* EngineChoice = M.Saved[0].Get();
		TestTrue(TEXT("nothing eligible -> engine's choice (kept)"), FSavedMove_Velox::ChooseOldMove(M.Saved, M.LastAcked, New, nullptr, EngineChoice) == EngineChoice);
		FMoves NoAck;
		NoAck.Add(1.0166f);
		const FSavedMove_Character* NoAckNew = NoAck.Add(1.0333f);
		TestTrue(TEXT("no acked move yet -> engine's choice"), FSavedMove_Velox::ChooseOldMove(NoAck.Saved, NoAck.LastAcked, NoAckNew, nullptr, nullptr) == nullptr);
	}
	{
		// NewMove / PendingMove are never returned, even as the only (event) moves.
		FMoves M;
		M.LastAcked = MakeAcked(1.0f);
		const FSavedMove_Character* Pending = M.Add(1.0083f, /*bEnteredTrigger*/ true);
		const FSavedMove_Character* New = M.Add(1.0166f, false, /*bJumpedHeld*/ true);
		TestTrue(TEXT("NewMove/PendingMove never returned"), FSavedMove_Velox::ChooseOldMove(M.Saved, M.LastAcked, New, Pending, nullptr) == nullptr);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
