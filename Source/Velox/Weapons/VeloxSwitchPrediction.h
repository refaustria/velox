// Velox - world-free decisions of the owning client's weapon-switch prediction (ADR 0003 B42, B44). Unit-tested in
// Velox.Combat.SwitchPrediction; applied by UVeloxWeaponInventoryComponent (PredictCurrentIndex, OnRep_CurrentIndex,
// CancelLocalSwitch).

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

namespace VeloxSwitchPrediction
{
	/** Superseded predictions of the current unconfirmed chain kept (oldest evicted). */
	inline constexpr int32 MaxChain = 4;

	/** Earlier predictions of the current unconfirmed chain, oldest first: their echoes are stale. */
	using FChain = TArray<int32, TInlineAllocator<MaxChain>>;

	/**
	 * A new local prediction NewIndex on top of Predicted (the current prediction; bLive = unexpired). Updates Chain and
	 * returns whether NewIndex is a prediction at all (false: no live chain and NewIndex equals the replicated value Auth;
	 * the caller drops the predicted index). A live prediction superseded by a different one joins the chain, also when
	 * NewIndex equals the (possibly stale) replicated value, so a quick switch back is not flipped by that echo.
	 */
	inline bool AdvanceChain(FChain& Chain, int32 Predicted, bool bLive, int32 NewIndex, int32 Auth)
	{
		if (!bLive)
		{
			Chain.Reset();
			return NewIndex != Auth;
		}
		if (Predicted != NewIndex)
		{
			if (Chain.Num() >= MaxChain)
			{
				Chain.RemoveAt(0);
			}
			Chain.Add(Predicted);
		}
		return true;
	}

	/** What the owning client does with a replicated current index. */
	enum class EReplicatedIndex : uint8
	{
		/** No prediction, or the prediction is the replicated value: the replicated value stands, the chain is dropped. */
		Confirm,
		/** The echo of an older step of the live chain: the prediction stands, the chain is trimmed up to that step. */
		EchoOfOlderStep,
		/** A live prediction the replicated value contradicts (not in the chain): a visible correction. */
		CorrectionRep,
		/** An expired prediction the replicated value contradicts: a correction after PredictionTimeout. */
		CorrectionTimeout,
	};

	struct FReplicatedIndexResult
	{
		EReplicatedIndex Kind = EReplicatedIndex::Confirm;
		/** EchoOfOlderStep: chain entries to drop from the front (the echoed step and everything older). */
		int32 TrimCount = 0;
	};

	/** OnRep_CurrentIndex: Predicted (INDEX_NONE = none), bLive = unexpired, Auth = the replicated value, Chain = the chain. */
	inline FReplicatedIndexResult ClassifyReplicatedIndex(int32 Predicted, bool bLive, int32 Auth, TConstArrayView<int32> Chain)
	{
		FReplicatedIndexResult Result;
		if (Predicted == INDEX_NONE || Predicted == Auth)
		{
			return Result;
		}
		if (!bLive)
		{
			Result.Kind = EReplicatedIndex::CorrectionTimeout;
			return Result;
		}
		const int32 At = Chain.Find(Auth);
		if (At != INDEX_NONE)
		{
			Result.Kind = EReplicatedIndex::EchoOfOlderStep;
			Result.TrimCount = At + 1;
			return Result;
		}
		Result.Kind = EReplicatedIndex::CorrectionRep;
		return Result;
	}

	/** What the owning client does when the server refuses one of its switch activations (B44). */
	enum class ELocalRejection : uint8
	{
		/** Not the latest local switch activation: a later one replaced its effect, nothing is rolled back. */
		NotLatest,
		/** A refused retarget whose switch is still in its drop: the previous target is pending again (drop end and switch end stay). */
		RestorePending,
		/** A refused retarget whose drop already landed on its target locally: the previous target is predicted instead. */
		RestoreLanded,
		/** A refused switch (or a retarget whose local result is gone): pending, switch end and prediction are dropped. */
		FullRollback,
	};

	/**
	 * Key = the refused prediction key; LatestKey / LatestTarget / RetargetFrom = the latest local switch activation (its
	 * target, and for a retarget the target it replaced, INDEX_NONE otherwise); Pending = the local pending target;
	 * LivePredicted = the unexpired predicted index (INDEX_NONE if none).
	 */
	inline ELocalRejection ClassifyLocalRejection(int16 Key, int16 LatestKey, int32 LatestTarget, int32 RetargetFrom, int32 Pending, int32 LivePredicted)
	{
		if (Key != LatestKey)
		{
			return ELocalRejection::NotLatest;
		}
		if (RetargetFrom != INDEX_NONE && LatestTarget != INDEX_NONE)
		{
			if (Pending == LatestTarget)
			{
				return ELocalRejection::RestorePending;
			}
			if (Pending == INDEX_NONE && LivePredicted == LatestTarget)
			{
				return ELocalRejection::RestoreLanded;
			}
		}
		return ELocalRejection::FullRollback;
	}
}
