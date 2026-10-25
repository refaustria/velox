// Velox - spawn point selection (ADR 0003 section 8), world-free so it is unit-tested (Velox.GameMode.SpawnSelection).
//
// Rule (Q3 SelectRandomFurthestSpawnPoint shape, with the distance to the nearest living enemy instead of the death
// point, per ROADMAP): among the free starts, skip those within AvoidRadius of a living enemy while a farther one exists,
// rank by the distance to the nearest enemy and pick uniformly among the farthest half. Without enemies every free start
// is equally likely. Only when every start is occupied does the spawner telefrag: uniform over all starts (Q3 G_KillBox).

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

namespace VeloxSpawn
{
	struct FSelection
	{
		int32 Index = INDEX_NONE;
		/** Every start was occupied: the occupant of Index dies (Velox.Damage.Telefrag). */
		bool bTelefrag = false;
		/** Size of the final candidate pool the random pick was made from. */
		int32 NumCandidates = 0;
	};

	/** Distance from Location to the nearest of Enemies; the largest double without enemies. */
	inline double NearestEnemyDistance(const FVector& Location, TConstArrayView<FVector> Enemies)
	{
		double Best = TNumericLimits<double>::Max();
		for (const FVector& Enemy : Enemies)
		{
			Best = FMath::Min(Best, FVector::Dist(Location, Enemy));
		}
		return Best;
	}

	/** Occupied has one flag per start (a living pawn stands in its capsule volume). */
	inline FSelection Select(TConstArrayView<FVector> Starts, TConstArrayView<bool> Occupied, TConstArrayView<FVector> Enemies, double AvoidRadiusCm, FRandomStream& Random)
	{
		FSelection Result;
		if (Starts.IsEmpty() || Occupied.Num() != Starts.Num())
		{
			return Result;
		}

		TArray<int32, TInlineAllocator<16>> Pool;
		for (int32 Index = 0; Index < Starts.Num(); ++Index)
		{
			if (!Occupied[Index])
			{
				Pool.Add(Index);
			}
		}
		if (Pool.IsEmpty())
		{
			Result.bTelefrag = true;
			Result.NumCandidates = Starts.Num();
			Result.Index = Random.RandHelper(Starts.Num());
			return Result;
		}

		if (!Enemies.IsEmpty())
		{
			TArray<double, TInlineAllocator<16>> Score;
			Score.SetNumUninitialized(Starts.Num());
			bool bAnyOutsideAvoid = false;
			for (const int32 Index : Pool)
			{
				Score[Index] = NearestEnemyDistance(Starts[Index], Enemies);
				bAnyOutsideAvoid |= Score[Index] >= AvoidRadiusCm;
			}
			if (bAnyOutsideAvoid)
			{
				Pool.RemoveAll([&Score, AvoidRadiusCm](int32 Index) { return Score[Index] < AvoidRadiusCm; });
			}
			// Farthest first; ties by index so the order is deterministic for a seed.
			Pool.Sort([&Score](int32 A, int32 B) { return Score[A] > Score[B] || (Score[A] == Score[B] && A < B); });
			Pool.SetNum((Pool.Num() + 1) / 2);
		}

		Result.NumCandidates = Pool.Num();
		Result.Index = Pool[Random.RandHelper(Pool.Num())];
		return Result;
	}
}
