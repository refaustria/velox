// Velox - saved-move flag plumbing, CanCombineWith rules and jump latch (ADR 0002 sections 5 and 7, A12, A17). World-free.
//
// Replay model (A17): FSavedMove_Velox::PrepMoveFor restores nothing Velox-specific except the recorded predicted
// self-knockback (ADR 0003 section 3, M2 task 05). The jump latch, KnockbackGrace and the pending knockback are derived
// state that chains from the correction through the replayed moves; the saved copies are read by CanCombineWith only.
// The replay itself needs a world and is covered by the task-05 net soak (LatchRun, PadRun scripts).


#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
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
		// ADR 0003 section 3: a recorded predicted self-knockback forbids combining on either side.
		auto Old = MakeGroundMove(), New = MakeGroundMove();
		Old->SavedPredictedKnockback.Add(FVector(0.0, 0.0, 1100.0), 7);
		TestFalse(TEXT("old move has a predicted knockback"), Combines(Old, New));
		TestFalse(TEXT("Velox rules: old predicted"), Old->CanCombineVeloxState(*New, true));
		Old->SavedPredictedKnockback.Reset();
		New->SavedPredictedKnockback.Add(FVector(0.0, 0.0, 75.0), 8);
		TestFalse(TEXT("new move has a predicted knockback"), Combines(Old, New));
		TestFalse(TEXT("Velox rules: new predicted"), Old->CanCombineVeloxState(*New, true));
		New->SavedPredictedKnockback.Reset();
		TestTrue(TEXT("no prediction: combines again"), Combines(Old, New));
	}
	{
		// A29: the move at whose end a predicted self-knockback was queued is an event move (resent until acked, A32).
		auto Move = MakeGroundMove();
		TestFalse(TEXT("plain move is no event move"), Move->IsEventMove());
		Move->bSavedQueuedPredictedKnockback = true;
		TestTrue(TEXT("queued predicted knockback makes an event move"), Move->IsEventMove());
		Move->Clear();
		TestFalse(TEXT("Clear resets the queued flag"), static_cast<bool>(Move->bSavedQueuedPredictedKnockback));
		TestFalse(TEXT("Clear resets the predicted knockback"), Move->SavedPredictedKnockback.IsPending());
	}
	{
		// ADR 0002 A36: a fire move (it carries a projectile shot) combines with nothing, in both directions, and is an
		// event move (A29 list).
		auto Old = MakeGroundMove(), New = MakeGroundMove();
		Old->bSavedHasShot = true;
		Old->SavedMoveShot.ShotSeq = 3;
		TestFalse(TEXT("old move carries a shot"), Combines(Old, New));
		TestFalse(TEXT("Velox rules: old shot"), Old->CanCombineVeloxState(*New, true));
		TestTrue(TEXT("a fire move is an event move"), Old->IsEventMove());
		Old->Clear();
		Old->DeltaTime = New->DeltaTime;
		TestFalse(TEXT("Clear resets the shot flag"), static_cast<bool>(Old->bSavedHasShot));
		TestEqual(TEXT("Clear resets the shot"), Old->SavedMoveShot.ShotSeq, static_cast<uint8>(0));
		Old = MakeGroundMove();
		New->bSavedHasShot = true;
		TestFalse(TEXT("new move carries a shot"), Combines(Old, New));
		TestFalse(TEXT("Velox rules: new shot"), Old->CanCombineVeloxState(*New, true));
		New->bSavedHasShot = false;
		TestTrue(TEXT("no shot: combines again"), Combines(Old, New));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementChooseExtraOldMoveTest, "Velox.Movement.SavedMove.ChooseExtraOldMove", VeloxSavedMoveTest::Flags)
bool FVeloxMovementChooseExtraOldMoveTest::RunTest(const FString& Parameters)
{
	// Task M1-07 (A32's deferred cl_packetdup 2): the move right before the OldMove rides along in the extra slot.
	TArray<FSavedMovePtr> Saved;
	auto Add = [&Saved](float TimeStamp, bool bJumpedHeld = false, bool bBeforeReset = false) -> const FSavedMove_Character*
	{
		TSharedPtr<FSavedMove_Velox> Move = MakeShared<FSavedMove_Velox>();
		Move->Clear();
		Move->TimeStamp = TimeStamp;
		Move->bSavedJumpedHeld = bJumpedHeld;
		Move->bOldTimeStampBeforeReset = bBeforeReset;
		Saved.Add(Move);
		return Move.Get();
	};
	auto MakeAcked = [](float TimeStamp)
	{
		TSharedPtr<FSavedMove_Velox> Move = MakeShared<FSavedMove_Velox>();
		Move->Clear();
		Move->TimeStamp = TimeStamp;
		return FSavedMovePtr(Move);
	};

	{
		// The LatchRun 20261029-141000/r4 layout: ... 1.358427 acked by the server, RPC A (new 1.375095, ground 1/60) and
		// RPC B (pending 1.383429 ground, new 1.391763 = take-off) lost; RPC C carries pending 1.400097, new 1.408430.
		// Before the fix the server got only OldMove 1.391763 (dt 0.0333, take-off 25 ms early: 10.83 cm).
		Saved.Reset();
		const FSavedMovePtr Acked = MakeAcked(1.358427f);
		Add(1.358427f);
		const FSavedMove_Character* A = Add(1.375095f);
		const FSavedMove_Character* P = Add(1.383429f);
		const FSavedMove_Character* N = Add(1.391763f, /*bJumpedHeld (take-off)*/ true);
		const FSavedMove_Character* Pending = Add(1.400097f);
		const FSavedMove_Character* New = Add(1.408430f);
		bool bEvent = false;
		const FSavedMove_Character* Old = FSavedMove_Velox::ChooseOldMove(Saved, Acked, New, Pending, nullptr, &bEvent);
		TestTrue(TEXT("take-off move is the OldMove (event)"), Old == N && bEvent);
		TestTrue(TEXT("its start boundary (the move before it) is the extra old move"),
			FSavedMove_Velox::ChooseExtraOldMove(Saved, Acked, New, Pending, Old) == P);
		// Later RPCs while the take-off is unacked keep both.
		const FSavedMove_Character* Pending2 = Add(1.416764f);
		const FSavedMove_Character* New2 = Add(1.425098f);
		const FSavedMove_Character* Old2 = FSavedMove_Velox::ChooseOldMove(Saved, Acked, New2, Pending2, nullptr);
		TestTrue(TEXT("later RPC: take-off still the OldMove"), Old2 == N);
		TestTrue(TEXT("later RPC: its boundary still the extra"), FSavedMove_Velox::ChooseExtraOldMove(Saved, Acked, New2, Pending2, Old2) == P);
		(void)A;
	}
	{
		// No event: OldMove = previous RPC's NewMove, extra = the previous RPC's pending half (dual) ...
		Saved.Reset();
		const FSavedMovePtr Acked = MakeAcked(1.0f);
		Add(1.0083f);
		const FSavedMove_Character* PrevPending = Add(1.0166f);
		const FSavedMove_Character* PrevNew = Add(1.025f);
		const FSavedMove_Character* Pending = Add(1.0333f);
		const FSavedMove_Character* New = Add(1.0416f);
		const FSavedMove_Character* Old = FSavedMove_Velox::ChooseOldMove(Saved, Acked, New, Pending, nullptr);
		TestTrue(TEXT("dual: OldMove is the previous NewMove"), Old == PrevNew);
		TestTrue(TEXT("dual: extra is the previous pending half"), FSavedMove_Velox::ChooseExtraOldMove(Saved, Acked, New, Pending, Old) == PrevPending);
	}
	{
		// ... or the NewMove of the RPC before (two-RPC burst of single-move RPCs).
		Saved.Reset();
		const FSavedMovePtr Acked = MakeAcked(1.0f);
		const FSavedMove_Character* TwoBack = Add(1.0166f);
		const FSavedMove_Character* Previous = Add(1.0333f);
		const FSavedMove_Character* New = Add(1.05f);
		const FSavedMove_Character* Old = FSavedMove_Velox::ChooseOldMove(Saved, Acked, New, nullptr, nullptr);
		TestTrue(TEXT("single: OldMove is the previous move"), Old == Previous);
		TestTrue(TEXT("single: extra is the move before it"), FSavedMove_Velox::ChooseExtraOldMove(Saved, Acked, New, nullptr, Old) == TwoBack);
	}
	{
		// Acked, reset-flagged, New and Pending are never the extra; nothing older -> none; no OldMove / no ack -> none.
		Saved.Reset();
		const FSavedMovePtr Acked = MakeAcked(1.02f);
		Add(1.01f);
		Add(1.02f);
		const FSavedMove_Character* OnlyOld = Add(1.03f);
		const FSavedMove_Character* Pending = Add(1.04f);
		const FSavedMove_Character* New = Add(1.05f);
		TestTrue(TEXT("acked moves are never the extra"), FSavedMove_Velox::ChooseExtraOldMove(Saved, Acked, New, Pending, OnlyOld) == nullptr);
		TestTrue(TEXT("no OldMove -> no extra"), FSavedMove_Velox::ChooseExtraOldMove(Saved, Acked, New, Pending, nullptr) == nullptr);
		TestTrue(TEXT("no acked move -> no extra"), FSavedMove_Velox::ChooseExtraOldMove(Saved, FSavedMovePtr(), New, Pending, OnlyOld) == nullptr);
		Saved.Reset();
		Add(1.03f, false, /*bBeforeReset*/ true);
		const FSavedMove_Character* Old = Add(1.035f);
		const FSavedMove_Character* New3 = Add(1.05f);
		TestTrue(TEXT("reset-flagged moves are never the extra"), FSavedMove_Velox::ChooseExtraOldMove(Saved, Acked, New3, nullptr, Old) == nullptr);
		// A pending move older than the OldMove is never returned (cannot happen with ChooseOldMove, guarded anyway).
		Saved.Reset();
		const FSavedMove_Character* OddPending = Add(1.03f);
		const FSavedMove_Character* Old4 = Add(1.04f);
		const FSavedMove_Character* New4 = Add(1.05f);
		TestTrue(TEXT("pending never the extra"), FSavedMove_Velox::ChooseExtraOldMove(Saved, Acked, New4, OddPending, Old4) == nullptr);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementExtraOldMoveSerializeTest, "Velox.Movement.SavedMove.ExtraOldMoveSerialize", VeloxSavedMoveTest::Flags)
bool FVeloxMovementExtraOldMoveSerializeTest::RunTest(const FString& Parameters)
{
	// Task M1-07: the extra old move survives the packed-bits round trip after the engine's fields; absent costs one bit.
	UVeloxMovementComponent& Movement = *GetMutableDefault<UVeloxMovementComponent>();
	auto FillMove = [](FCharacterNetworkMoveData& Data, float TimeStamp, uint8 Flags)
	{
		Data.TimeStamp = TimeStamp;
		Data.Acceleration = FVector(800.0, 0.0, 0.0);
		Data.Location = FVector(466.6, 0.0, 72.15);
		Data.ControlRotation = FRotator(0.0, 90.0, 0.0);
		Data.CompressedMoveFlags = Flags;
		Data.MovementMode = MOVE_Falling;
	};
	for (const bool bWithExtra : { true, false })
	{
		FVeloxNetworkMoveDataContainer Sent;
		FillMove(*Sent.GetNewMoveData(), 1.408430f, FSavedMove_Velox::FLAG_JumpHeld);
		Sent.bHasPendingMove = true;
		FillMove(*Sent.GetPendingMoveData(), 1.400097f, FSavedMove_Velox::FLAG_JumpHeld);
		Sent.bHasOldMove = true;
		FillMove(*Sent.GetOldMoveData(), 1.391763f, FSavedMove_Velox::FLAG_JumpHeld | FSavedMove_Character::FLAG_JumpPressed);
		Sent.SetHasExtraOldMove(bWithExtra);
		FillMove(Sent.GetExtraOldMoveStorage(), 1.383429f, FSavedMove_Velox::FLAG_HasForwardInput);

		FBitWriter Writer(0, /*bAllowResize*/ true);
		TestTrue(TEXT("serialise"), Sent.Serialize(Movement, Writer, nullptr));
		// A marker after the container: the reader must end exactly where the writer did.
		uint32 Marker = 0xA5C3u;
		Writer << Marker;

		FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
		FVeloxNetworkMoveDataContainer Received;
		TestTrue(TEXT("deserialise"), Received.Serialize(Movement, Reader, nullptr));
		uint32 ReadMarker = 0;
		Reader << ReadMarker;
		TestEqual(TEXT("stream aligned after the container"), ReadMarker, Marker);
		TestEqual(TEXT("old move kept"), Received.GetOldMoveData()->TimeStamp, 1.391763f);
		TestTrue(TEXT("pending move kept"), Received.bHasPendingMove && Received.GetPendingMoveData()->TimeStamp == 1.400097f);
		const FVeloxNetworkMoveData* Extra = Received.GetExtraOldMoveData();
		if (bWithExtra)
		{
			if (TestNotNull(TEXT("extra old move received"), Extra))
			{
				TestEqual(TEXT("extra time stamp"), Extra->TimeStamp, 1.383429f);
				TestEqual(TEXT("extra flags"), Extra->CompressedMoveFlags, static_cast<uint8>(FSavedMove_Velox::FLAG_HasForwardInput));
				TestTrue(TEXT("extra acceleration"), Extra->Acceleration.Equals(FVector(800.0, 0.0, 0.0), 0.1));
				TestTrue(TEXT("extra is an old move (no client error check on the server)"), Extra->NetworkMoveType == FCharacterNetworkMoveData::ENetworkMoveType::OldMove);
			}
		}
		else
		{
			TestNull(TEXT("no extra old move"), Extra);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementPowerupResponseSerializeTest, "Velox.Movement.SavedMove.PowerupResponseSerialize", VeloxSavedMoveTest::Flags)
bool FVeloxMovementPowerupResponseSerializeTest::RunTest(const FString& Parameters)
{
	// ADR 0004 section 3 (task 03): the powerup end times travel in acks and corrections through the one response
	// serializer (FCharacterMoveResponseDataContainer::Serialize); one bit each while 0.
	UVeloxMovementComponent& Movement = *GetMutableDefault<UVeloxMovementComponent>();
	int32 BitsWithout[2] = { 0, 0 };
	int32 BitsWith[2] = { 0, 0 };
	for (const bool bCorrection : { false, true })
	{
		for (const bool bWithTimes : { false, true })
		{
			FVeloxMoveResponseDataContainer Sent;
			Sent.ClientAdjustment.bAckGoodMove = !bCorrection;
			Sent.ClientAdjustment.TimeStamp = 12.345678f;
			Sent.ClientAdjustment.NewLoc = FVector(100.0, 200.0, 92.15);
			Sent.ClientAdjustment.NewVel = FVector(1040.0, 0.0, 0.0);
			Sent.ClientAdjustment.MovementMode = MOVE_Walking;
			Sent.KnockbackGrace = 0.f;
			if (bWithTimes)
			{
				Sent.PowerupMoveTimes.HasteEnd = 42.123456f;
				Sent.PowerupMoveTimes.QuadEnd = 239.987654f;
			}
			FBitWriter Writer(0, /*bAllowResize*/ true);
			TestTrue(TEXT("serialise"), Sent.Serialize(Movement, Writer, nullptr));
			(bWithTimes ? BitsWith : BitsWithout)[bCorrection ? 1 : 0] = static_cast<int32>(Writer.GetNumBits());
			uint32 Marker = 0x5AC3u;
			Writer << Marker;

			FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
			FVeloxMoveResponseDataContainer Received;
			Received.PowerupMoveTimes.HasteEnd = 1.f; // must be overwritten (also with 0)
			TestTrue(TEXT("deserialise"), Received.Serialize(Movement, Reader, nullptr));
			uint32 ReadMarker = 0;
			Reader << ReadMarker;
			const FString Kind = FString::Printf(TEXT("%s %s"), bCorrection ? TEXT("correction") : TEXT("ack"), bWithTimes ? TEXT("with times") : TEXT("without"));
			TestEqual(Kind + TEXT(": stream aligned"), ReadMarker, Marker);
			TestEqual(Kind + TEXT(": kind kept"), Received.IsCorrection(), bCorrection);
			TestEqual(Kind + TEXT(": HasteEnd exact"), Received.PowerupMoveTimes.HasteEnd, Sent.PowerupMoveTimes.HasteEnd);
			TestEqual(Kind + TEXT(": QuadEnd exact"), Received.PowerupMoveTimes.QuadEnd, Sent.PowerupMoveTimes.QuadEnd);
		}
	}
	TestEqual(TEXT("ack: 64 bits for both end times"), BitsWith[0] - BitsWithout[0], 64);
	TestEqual(TEXT("correction: 64 bits for both end times"), BitsWith[1] - BitsWithout[1], 64);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementMoveShotOldMoveTest, "Velox.Movement.SavedMove.MoveShotOldMove", VeloxSavedMoveTest::Flags)
bool FVeloxMovementMoveShotOldMoveTest::RunTest(const FString& Parameters)
{
	// ADR 0002 A36 with A32/A35: the fire move wins old-move slot 1 while unacked, its predecessor slot 2; the splash move
	// (queued predicted self-knockback, newer) takes slot 1 once it exists.
	TArray<FSavedMovePtr> Saved;
	auto Add = [&Saved](float TimeStamp, bool bShot = false, bool bQueuedPredicted = false) -> const FSavedMove_Character*
	{
		TSharedPtr<FSavedMove_Velox> Move = MakeShared<FSavedMove_Velox>();
		Move->Clear();
		Move->TimeStamp = TimeStamp;
		Move->bSavedHasShot = bShot;
		Move->bSavedQueuedPredictedKnockback = bQueuedPredicted;
		Saved.Add(Move);
		return Move.Get();
	};
	TSharedPtr<FSavedMove_Velox> AckedMove = MakeShared<FSavedMove_Velox>();
	AckedMove->Clear();
	AckedMove->TimeStamp = 1.0f;
	const FSavedMovePtr Acked = AckedMove;

	Add(1.0f);
	const FSavedMove_Character* BeforeFire = Add(1.0083f);
	const FSavedMove_Character* Fire = Add(1.0166f, /*bShot*/ true);
	Add(1.025f);
	const FSavedMove_Character* Ordinary = Add(1.0333f);
	const FSavedMove_Character* New = Add(1.0416f);
	bool bEvent = false;
	const FSavedMove_Character* Old = FSavedMove_Velox::ChooseOldMove(Saved, Acked, New, nullptr, nullptr, &bEvent);
	TestTrue(TEXT("the fire move beats the newer ordinary move for slot 1"), Old == Fire && Old != Ordinary);
	TestTrue(TEXT("slot 1 is an event move"), bEvent);
	TestTrue(TEXT("its predecessor takes slot 2"), FSavedMove_Velox::ChooseExtraOldMove(Saved, Acked, New, nullptr, Old) == BeforeFire);

	// About 55 ms later the predicted self splash was queued at the end of a newer move: that move wins slot 1 (A35).
	const FSavedMove_Character* BeforeSplash = Add(1.05f);
	const FSavedMove_Character* Splash = Add(1.0583f, false, /*bQueuedPredicted*/ true);
	const FSavedMove_Character* New2 = Add(1.0666f);
	const FSavedMove_Character* Old2 = FSavedMove_Velox::ChooseOldMove(Saved, Acked, New2, nullptr, nullptr);
	TestTrue(TEXT("a newer splash move takes slot 1"), Old2 == Splash);
	TestTrue(TEXT("the move before the splash move takes slot 2"), FSavedMove_Velox::ChooseExtraOldMove(Saved, Acked, New2, nullptr, Old2) == BeforeSplash);

	// Once the fire move is acked it is never resent.
	AckedMove->TimeStamp = 1.0166f;
	Saved.Reset();
	Add(1.0166f, true);
	const FSavedMove_Character* Prev = Add(1.025f);
	const FSavedMove_Character* New3 = Add(1.0333f);
	TestTrue(TEXT("acked fire move: the previous move again"), FSavedMove_Velox::ChooseOldMove(Saved, Acked, New3, nullptr, nullptr) == Prev);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMovementMoveShotSerializeTest, "Velox.Movement.SavedMove.MoveShotSerialize", VeloxSavedMoveTest::Flags)
bool FVeloxMovementMoveShotSerializeTest::RunTest(const FString& Parameters)
{
	// ADR 0002 A36: FVeloxNetworkMoveData carries the shot behind one presence bit after the engine's fields; <= 160 bits
	// with a shot; every field survives; every move type (new, pending, old, extra old) carries it.
	UVeloxMovementComponent& Movement = *GetMutableDefault<UVeloxMovementComponent>();
	auto FillMove = [](FCharacterNetworkMoveData& Data, float TimeStamp)
	{
		Data.TimeStamp = TimeStamp;
		Data.Acceleration = FVector(800.0, 0.0, 0.0);
		Data.Location = FVector(466.6, -1203.4, 92.15);
		Data.ControlRotation = FRotator(-90.0, 90.0, 0.0);
		Data.CompressedMoveFlags = FSavedMove_Velox::FLAG_JumpHeld;
		Data.MovementMode = MOVE_Walking;
	};
	// The shot as the fire ability builds it: the RPC payload's quantized values (FVector_NetQuantize100 / Normal).
	FVeloxMoveShot Shot;
	Shot.ShotSeq = 201;
	Shot.WeaponIndex = 15;
	Shot.FireMoveTimeStamp = 1.750139f;
	{
		// Quantize origin and direction once (the RPC's Quantize), so the move-shot round trip must be exact.
		FVeloxMoveShot Raw;
		Raw.Origin = FVector(1834.567, -1203.4321, 152.149);
		Raw.Dir = FVector(0.1234, -0.0567, -0.9907).GetSafeNormal();
		FBitWriter Writer(0, true);
		TestTrue(TEXT("pre-quantize"), Raw.Serialize(Writer));
		FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
		TestTrue(TEXT("pre-quantize read"), Raw.Serialize(Reader));
		Shot.Origin = Raw.Origin;
		Shot.Dir = Raw.Dir;
		TestTrue(TEXT("origin quantized to 1 cm"), FVector::Dist(Shot.Origin, FVector(1834.567, -1203.4321, 152.149)) <= 1.0);
	}

	// Bits of the Velox part alone, per move: the same move serialised by the engine's FCharacterNetworkMoveData.
	auto Bits = [&Movement](FCharacterNetworkMoveData& Data, FCharacterNetworkMoveData::ENetworkMoveType Type)
	{
		FBitWriter Writer(0, true);
		Data.Serialize(Movement, Writer, nullptr, Type);
		return static_cast<int32>(Writer.GetNumBits());
	};
	FCharacterNetworkMoveData EngineMove;
	FillMove(EngineMove, 1.758472f);
	FVeloxNetworkMoveData Without;
	FillMove(Without, 1.758472f);
	FVeloxNetworkMoveData With;
	FillMove(With, 1.758472f);
	With.bHasShot = true;
	With.Shot = Shot;
	const int32 EngineBits = Bits(EngineMove, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
	const int32 WithoutBits = Bits(Without, FCharacterNetworkMoveData::ENetworkMoveType::NewMove) - EngineBits;
	const int32 WithBits = Bits(With, FCharacterNetworkMoveData::ENetworkMoveType::NewMove) - EngineBits;
	AddInfo(FString::Printf(TEXT("Velox part of a move: %d bit(s) without a shot, %d bits with one"), WithoutBits, WithBits));
	TestEqual(TEXT("without a shot: 1 bit"), WithoutBits, 1);
	TestTrue(TEXT("with a shot: <= 160 bits"), WithBits <= 160);
	TestTrue(TEXT("old move type: same cost"), Bits(With, FCharacterNetworkMoveData::ENetworkMoveType::OldMove) - Bits(EngineMove, FCharacterNetworkMoveData::ENetworkMoveType::OldMove) == WithBits);

	// The container round trip: the shot on the new move, the old move and the extra old move; none on the pending one.
	FVeloxNetworkMoveDataContainer Sent;
	FillMove(*Sent.GetNewMoveData(), 1.766805f);
	static_cast<FVeloxNetworkMoveData*>(Sent.GetNewMoveData())->bHasShot = true;
	static_cast<FVeloxNetworkMoveData*>(Sent.GetNewMoveData())->Shot = Shot;
	Sent.bHasPendingMove = true;
	FillMove(*Sent.GetPendingMoveData(), 1.762639f);
	Sent.bHasOldMove = true;
	FillMove(*Sent.GetOldMoveData(), 1.758472f);
	FVeloxMoveShot OldShot = Shot;
	OldShot.ShotSeq = 200;
	OldShot.WeaponIndex = 4;
	static_cast<FVeloxNetworkMoveData*>(Sent.GetOldMoveData())->bHasShot = true;
	static_cast<FVeloxNetworkMoveData*>(Sent.GetOldMoveData())->Shot = OldShot;
	Sent.SetHasExtraOldMove(true);
	FillMove(Sent.GetExtraOldMoveStorage(), 1.750139f);
	FVeloxMoveShot ExtraShot = Shot;
	ExtraShot.ShotSeq = 199;
	ExtraShot.FireMoveTimeStamp = 1.741806f;
	Sent.GetExtraOldMoveStorage().bHasShot = true;
	Sent.GetExtraOldMoveStorage().Shot = ExtraShot;

	FBitWriter Writer(0, true);
	TestTrue(TEXT("serialise"), Sent.Serialize(Movement, Writer, nullptr));
	uint32 Marker = 0xC35Au;
	Writer << Marker;
	FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
	FVeloxNetworkMoveDataContainer Received;
	// Stale values from a previous RPC must be overwritten.
	static_cast<FVeloxNetworkMoveData*>(Received.GetPendingMoveData())->bHasShot = true;
	TestTrue(TEXT("deserialise"), Received.Serialize(Movement, Reader, nullptr));
	uint32 ReadMarker = 0;
	Reader << ReadMarker;
	TestEqual(TEXT("stream aligned after the container"), ReadMarker, Marker);

	const FVeloxNetworkMoveData* NewData = static_cast<const FVeloxNetworkMoveData*>(Received.GetNewMoveData());
	const FVeloxNetworkMoveData* PendingData = static_cast<const FVeloxNetworkMoveData*>(Received.GetPendingMoveData());
	const FVeloxNetworkMoveData* OldData = static_cast<const FVeloxNetworkMoveData*>(Received.GetOldMoveData());
	const FVeloxNetworkMoveData* ExtraData = Received.GetExtraOldMoveData();
	TestTrue(TEXT("new move: shot present"), NewData->bHasShot);
	TestEqual(TEXT("new move: seq"), NewData->Shot.ShotSeq, Shot.ShotSeq);
	TestEqual(TEXT("new move: weapon index 15 (4 bits)"), NewData->Shot.WeaponIndex, Shot.WeaponIndex);
	TestEqual(TEXT("new move: stamp exact"), NewData->Shot.FireMoveTimeStamp, Shot.FireMoveTimeStamp);
	TestTrue(TEXT("new move: quantized origin exact"), FVector(NewData->Shot.Origin) == FVector(Shot.Origin));
	TestTrue(TEXT("new move: quantized dir exact"), FVector(NewData->Shot.Dir) == FVector(Shot.Dir));
	TestTrue(TEXT("new move: whole shot equal"), NewData->Shot == Shot);
	TestFalse(TEXT("pending move: no shot (stale flag overwritten)"), PendingData->bHasShot);
	TestTrue(TEXT("old move: its shot"), OldData->bHasShot && OldData->Shot == OldShot);
	if (TestNotNull(TEXT("extra old move received"), ExtraData))
	{
		TestTrue(TEXT("extra old move: its shot"), ExtraData->bHasShot && ExtraData->Shot == ExtraShot);
	}
	TestTrue(TEXT("AsVeloxMoveData finds the container's own data"), Received.AsVeloxMoveData(Received.GetOldMoveData()) == OldData);
	TestNull(TEXT("AsVeloxMoveData rejects foreign data"), Received.AsVeloxMoveData(&EngineMove));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
