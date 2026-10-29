// Velox - replicated kill feed (ADR 0004 section 6, task 06).
// A FastArraySerializer of the last AVeloxGameState::KillFeedLength frags. Names are snapshotted, so entries stay valid
// after a PlayerState leaves. Late joiners receive the whole array as adds, which is how they get the earlier kills.

#pragma once

#include "CoreMinimal.h"
#include "Core/VeloxGameplayTags.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "VeloxKillFeed.generated.h"

class AVeloxGameState;

/** One frag as shown in the feed. */
USTRUCT()
struct VELOX_API FVeloxKillFeedEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Server counter, wraps at 65536; compare with VeloxKillFeed::SeqLess. */
	UPROPERTY()
	uint16 Seq = 0;

	UPROPERTY()
	FString KillerName;

	UPROPERTY()
	FString VictimName;

	/** AVeloxPlayerState palette index (AVeloxPlayerState::NoPlayerColor = unassigned). */
	UPROPERTY()
	uint8 KillerColor = 255;

	UPROPERTY()
	uint8 VictimColor = 255;

	/** Velox.Damage.<Weapon> (the cause), Velox.Damage.Self only when nothing more specific exists. */
	UPROPERTY()
	FGameplayTag DamageTag;

	/** Killer == victim, or no killer. KillerName then equals VictimName. */
	UPROPERTY()
	bool bSuicide = false;

	/** Server world time (GetServerWorldTimeSeconds) of the frag; the HUD expires entries on it. */
	UPROPERTY()
	float ServerTime = 0.f;
};

/** Client: one entry arrived (a new frag or, for a late joiner, one of the earlier ones). Server: the frag just happened. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnVeloxKillFeedEntry, const FVeloxKillFeedEntry& /*Entry*/);

USTRUCT()
struct VELOX_API FVeloxKillFeed : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVeloxKillFeedEntry> Entries;

	/** Set by AVeloxGameState (not replicated). */
	AVeloxGameState* Owner = nullptr;

	/** Array-level callback (UE 5.8 FastArraySerializer.h: called once per receive with every index added, in array order). */
	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FVeloxKillFeedEntry, FVeloxKillFeed>(Entries, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FVeloxKillFeed> : public TStructOpsTypeTraitsBase2<FVeloxKillFeed>
{
	enum { WithNetDeltaSerializer = true };
};

/** World-free helpers (unit-tested). */
namespace VeloxKillFeed
{
	/** Wrap-aware A older than B (uint16 counters; valid while fewer than 32768 entries apart). */
	inline bool SeqLess(uint16 A, uint16 B)
	{
		return static_cast<int16>(static_cast<uint16>(A - B)) < 0;
	}

	/** Entries oldest first. The client array is not in order after removals (the engine removes with swap). */
	inline TArray<FVeloxKillFeedEntry> SortedBySeq(const TArray<FVeloxKillFeedEntry>& Entries)
	{
		TArray<FVeloxKillFeedEntry> Sorted(Entries);
		Sorted.StableSort([](const FVeloxKillFeedEntry& A, const FVeloxKillFeedEntry& B) { return SeqLess(A.Seq, B.Seq); });
		return Sorted;
	}

	/** Index of the oldest entry, INDEX_NONE when empty. */
	inline int32 OldestIndex(const TArray<FVeloxKillFeedEntry>& Entries)
	{
		int32 Oldest = INDEX_NONE;
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			if (Oldest == INDEX_NONE || SeqLess(Entries[Index].Seq, Entries[Oldest].Seq))
			{
				Oldest = Index;
			}
		}
		return Oldest;
	}

	/** The weapon/cause tag of a kill: the first damage tag that is not Velox.Damage.Self, else Self, else invalid. */
	inline FGameplayTag PickDamageTag(const FGameplayTagContainer& DamageTags)
	{
		FGameplayTag Fallback;
		for (const FGameplayTag& Tag : DamageTags)
		{
			if (Tag == VeloxGameplayTags::Damage_Self.GetTag())
			{
				Fallback = Tag;
			}
			else
			{
				return Tag;
			}
		}
		return Fallback;
	}
}
