// Velox - game state.

#include "GameModes/VeloxGameState.h"

#include "Combat/VeloxCombatLog.h"
#include "Net/Core/PushModel/PushModel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameState)

AVeloxGameState::AVeloxGameState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	KillFeed.Owner = this;
}

void FVeloxKillFeed::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 /*FinalSize*/)
{
	if (!Owner)
	{
		return;
	}
	// A late joiner gets the whole array in one receive, in array order; deliver oldest first.
	TArray<FVeloxKillFeedEntry> Added;
	Added.Reserve(AddedIndices.Num());
	for (const int32 Index : AddedIndices)
	{
		if (Entries.IsValidIndex(Index))
		{
			Added.Add(Entries[Index]);
		}
	}
	for (const FVeloxKillFeedEntry& Entry : VeloxKillFeed::SortedBySeq(Added))
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxKillFeed client seq=%u killer=%s victim=%s tag=%s suicide=%d t=%.2f server_now=%.2f"), Entry.Seq, *Entry.KillerName, *Entry.VictimName,
			*Entry.DamageTag.ToString(), Entry.bSuicide ? 1 : 0, Entry.ServerTime, Owner->GetServerWorldTimeSeconds());
		Owner->OnKillFeedEntry.Broadcast(Entry);
	}
}

const FVeloxKillFeedEntry& AVeloxGameState::AppendKillFeedEntry(FVeloxKillFeedEntry Entry)
{
	Entry.Seq = NextKillFeedSeq++;
	Entry.ServerTime = static_cast<float>(GetServerWorldTimeSeconds());
	const int32 Index = KillFeed.Entries.Add(MoveTemp(Entry));
	KillFeed.MarkItemDirty(KillFeed.Entries[Index]);
	// Keep the newest KillFeedLength; removal changes indices, so the array itself is dirtied (MarkArrayDirty).
	bool bRemoved = false;
	while (KillFeed.Entries.Num() > GetKillFeedLength())
	{
		KillFeed.Entries.RemoveAt(VeloxKillFeed::OldestIndex(KillFeed.Entries));
		bRemoved = true;
	}
	if (bRemoved)
	{
		KillFeed.MarkArrayDirty();
	}
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, KillFeed, this);
	// The feed matters at once; the AInfo default (10 Hz) would add up to 100 ms.
	ForceNetUpdate();

	// The newest entry is the one just added (highest Seq).
	const FVeloxKillFeedEntry* Stored = nullptr;
	for (const FVeloxKillFeedEntry& Candidate : KillFeed.Entries)
	{
		if (!Stored || VeloxKillFeed::SeqLess(Stored->Seq, Candidate.Seq))
		{
			Stored = &Candidate;
		}
	}
	check(Stored);
	OnKillFeedEntry.Broadcast(*Stored);
	return *Stored;
}

void AVeloxGameState::ClearKillFeed()
{
	if (KillFeed.Entries.IsEmpty())
	{
		return;
	}
	KillFeed.Entries.Reset();
	KillFeed.MarkArrayDirty();
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxGameState, KillFeed, this);
	ForceNetUpdate();
}
