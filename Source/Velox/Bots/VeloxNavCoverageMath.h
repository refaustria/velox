// Velox - world-free clustering core of the nav-coverage lint (ADR 0006 section 6; task M5-03). The world part
// (VeloxNavCoverage.cpp) samples walkable floor on a Grid cm lattice and marks each sample covered or not; this file
// groups the uncovered samples into 4-connected clusters and measures them. Unit test: Velox.Bots.NavCoverageClusters.
//
// Lattice: cell (X, Y) spans [X * Grid, (X + 1) * Grid) on each axis, its sample sits at the cell centre. Several
// samples may share one (X, Y) on different floors (a platform over the floor); two samples are neighbours when their
// cells share an edge and their heights differ by at most MaxNeighbourDZ, so a floor and the platform above it never
// merge into one cluster.

#pragma once

#include "CoreMinimal.h"

/** One lattice sample on walkable floor. */
struct FVeloxNavCoverageCell
{
	int32 X = 0;
	int32 Y = 0;
	/** Sample height (cm). */
	float Z = 0.f;
	/** The nav mesh covers this sample (projection within AgentRadius horizontally and StepHeight vertically). */
	bool bCovered = false;
	/** Index of the owning surface (caller's table: component / actor); INDEX_NONE if unknown. */
	int32 Owner = INDEX_NONE;
};

/** A 4-connected group of uncovered cells. */
struct FVeloxNavCoverageCluster
{
	int32 NumCells = 0;
	/** NumCells * Grid^2 in m^2. */
	double AreaM2 = 0.0;
	/** XY: centre of the cells' bounding rectangle; Z: mean sample height (cm). */
	FVector Center = FVector::ZeroVector;
	/** Full size of the cells' bounding rectangle (cm). */
	FVector2D Extent = FVector2D::ZeroVector;
	/** The most frequent Owner among the cells (lowest index on a tie). */
	int32 Owner = INDEX_NONE;
	/**
	 * A covered cell is a neighbour (same rule as within the cluster): a walking pawn or bot can step from the nav mesh
	 * into this cluster (D41's / D17's class). False: an isolated surface reached only by a jump, a pad or knockback
	 * (box tops, wall tops, outside ledges). Report-only classification; the gate counts every cluster (ADR 0006 section 6).
	 */
	bool bWalkIn = false;
	/**
	 * Edge strip (ADR 0006 E0.2 (e)): one cell wide along its whole length and every cell has a covered 4-neighbour.
	 * Recast erodes an off-lattice ledge by about four cells, which leaves such a strip 70-76 cm from nav, inside the bots'
	 * projection radius: not a defect. Report-only kind=edge; the gate does not count it.
	 */
	bool bEdge = false;
};

namespace VeloxNavCoverageMath
{
	/** Lattice index of a world coordinate (cell [i * Grid, (i + 1) * Grid)). */
	inline int32 CellIndex(double WorldCm, double GridCm)
	{
		return FMath::FloorToInt32(WorldCm / GridCm);
	}

	/** World coordinate of a cell centre. */
	inline double CellCenter(int32 Index, double GridCm)
	{
		return (static_cast<double>(Index) + 0.5) * GridCm;
	}

	/** Area of NumCells cells in m^2. */
	inline double CellsToM2(int32 NumCells, double GridCm)
	{
		return static_cast<double>(NumCells) * GridCm * GridCm / 10000.0;
	}

	/**
	 * Groups the uncovered cells into 4-connected clusters (neighbour: shared edge and |dZ| <= MaxNeighbourDZ), drops
	 * clusters below MinClusterM2 and returns the rest sorted by area, largest first (ties: lower centre X, then Y).
	 */
	inline TArray<FVeloxNavCoverageCluster> BuildClusters(const TArray<FVeloxNavCoverageCell>& Cells, double GridCm, double MaxNeighbourDZ, double MinClusterM2)
	{
		TArray<FVeloxNavCoverageCluster> Clusters;
		if (GridCm <= 0.0)
		{
			return Clusters;
		}

		// (X, Y) -> indices of all cells in that column (covered ones only mark a cluster as walk-in).
		TMap<FIntPoint, TArray<int32, TInlineAllocator<2>>> Columns;
		for (int32 i = 0; i < Cells.Num(); ++i)
		{
			Columns.FindOrAdd(FIntPoint(Cells[i].X, Cells[i].Y)).Add(i);
		}

		static const FIntPoint Offsets[4] = {FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)};
		TBitArray<> Visited(false, Cells.Num());
		TArray<int32> Stack;
		TArray<int32> Members;
		TMap<int32, int32> OwnerCounts;
		for (int32 Seed = 0; Seed < Cells.Num(); ++Seed)
		{
			if (Cells[Seed].bCovered || Visited[Seed])
			{
				continue;
			}
			Members.Reset();
			Stack.Reset();
			Stack.Add(Seed);
			Visited[Seed] = true;
			bool bWalkIn = false;
			int32 NumWithCoveredNeighbour = 0;
			while (Stack.Num())
			{
				const int32 Index = Stack.Pop(EAllowShrinking::No);
				Members.Add(Index);
				const FVeloxNavCoverageCell& Cell = Cells[Index];
				bool bCellHasCovered = false;
				for (const FIntPoint& Offset : Offsets)
				{
					const TArray<int32, TInlineAllocator<2>>* Column = Columns.Find(FIntPoint(Cell.X + Offset.X, Cell.Y + Offset.Y));
					if (!Column)
					{
						continue;
					}
					for (const int32 Other : *Column)
					{
						if (FMath::Abs(static_cast<double>(Cells[Other].Z) - Cell.Z) > MaxNeighbourDZ)
						{
							continue;
						}
						if (Cells[Other].bCovered)
						{
							bWalkIn = true;
							bCellHasCovered = true;
						}
						else if (!Visited[Other])
						{
							Visited[Other] = true;
							Stack.Add(Other);
						}
					}
				}
				NumWithCoveredNeighbour += bCellHasCovered ? 1 : 0;
			}

			FVeloxNavCoverageCluster Cluster;
			Cluster.bWalkIn = bWalkIn;
			Cluster.NumCells = Members.Num();
			Cluster.AreaM2 = CellsToM2(Cluster.NumCells, GridCm);
			if (Cluster.AreaM2 + KINDA_SMALL_NUMBER < MinClusterM2)
			{
				continue;
			}
			FIntPoint Min(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
			FIntPoint Max(TNumericLimits<int32>::Lowest(), TNumericLimits<int32>::Lowest());
			double SumZ = 0.0;
			OwnerCounts.Reset();
			for (const int32 Index : Members)
			{
				const FVeloxNavCoverageCell& Cell = Cells[Index];
				Min = FIntPoint(FMath::Min(Min.X, Cell.X), FMath::Min(Min.Y, Cell.Y));
				Max = FIntPoint(FMath::Max(Max.X, Cell.X), FMath::Max(Max.Y, Cell.Y));
				SumZ += Cell.Z;
				++OwnerCounts.FindOrAdd(Cell.Owner);
			}
			int32 BestCount = 0;
			for (const TPair<int32, int32>& Pair : OwnerCounts)
			{
				if (Pair.Value > BestCount || (Pair.Value == BestCount && Pair.Key < Cluster.Owner))
				{
					BestCount = Pair.Value;
					Cluster.Owner = Pair.Key;
				}
			}
			Cluster.Extent = FVector2D((Max.X - Min.X + 1) * GridCm, (Max.Y - Min.Y + 1) * GridCm);
			Cluster.bEdge = FMath::Min(Max.X - Min.X, Max.Y - Min.Y) == 0 && NumWithCoveredNeighbour == Members.Num();
			Cluster.Center = FVector(Min.X * GridCm + Cluster.Extent.X * 0.5, Min.Y * GridCm + Cluster.Extent.Y * 0.5, SumZ / Members.Num());
			Clusters.Add(Cluster);
		}

		Clusters.Sort([](const FVeloxNavCoverageCluster& A, const FVeloxNavCoverageCluster& B)
		{
			if (A.NumCells != B.NumCells)
			{
				return A.NumCells > B.NumCells;
			}
			if (A.Center.X != B.Center.X)
			{
				return A.Center.X < B.Center.X;
			}
			return A.Center.Y < B.Center.Y;
		});
		return Clusters;
	}
}
