// Velox - per-player anomaly counters (ADR 0007 section 5.4, task m6-online/09).

#include "Net/VeloxAnomalyStats.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAnomalyStats)

void FVeloxPlayerAnomalyStats::Note(EVeloxAnomaly Kind, int32 Count, FName Reason)
{
	switch (Kind)
	{
	case EVeloxAnomaly::Shot: Shots += Count; break;
	case EVeloxAnomaly::Switch: Switches += Count; break;
	case EVeloxAnomaly::OriginSnap: OriginSnaps += Count; break;
	case EVeloxAnomaly::TimeClamp: TimeClamps += Count; break;
	case EVeloxAnomaly::LateRetarget: LateRetargets += Count; break;
	case EVeloxAnomaly::FreshRetarget: FreshRetargets += Count; break;
	case EVeloxAnomaly::MoveShotOrphan: MoveShotOrphans += Count; break;
	case EVeloxAnomaly::RpcRefused: RpcRefused += Count; break;
	case EVeloxAnomaly::Discrepancy: Discrepancies += Count; break;
	case EVeloxAnomaly::Reject:
	{
		static const FName Unknown(TEXT("unknown"));
		const FName Tag = Reason.IsNone() ? Unknown : Reason;
		for (TPair<FName, int32>& Entry : Rejects)
		{
			if (Entry.Key == Tag)
			{
				Entry.Value += Count;
				return;
			}
		}
		Rejects.Emplace(Tag, Count);
		break;
	}
	}
}

int32 FVeloxPlayerAnomalyStats::GetRejects() const
{
	int32 Sum = 0;
	for (const TPair<FName, int32>& Entry : Rejects)
	{
		Sum += Entry.Value;
	}
	return Sum;
}

int32 FVeloxPlayerAnomalyStats::GetRejects(FName Reason) const
{
	for (const TPair<FName, int32>& Entry : Rejects)
	{
		if (Entry.Key == Reason)
		{
			return Entry.Value;
		}
	}
	return 0;
}

int32 FVeloxPlayerAnomalyStats::GetShotAttempts() const
{
	return Shots + GetRejects();
}

bool FVeloxPlayerAnomalyStats::HasAnyCounter() const
{
	return Shots || Switches || GetRejects() || OriginSnaps || TimeClamps || LateRetargets || FreshRetargets || MoveShotOrphans || RpcRefused || Discrepancies;
}

bool FVeloxPlayerAnomalyStats::SameCounters(const FVeloxPlayerAnomalyStats& Other) const
{
	if (Shots != Other.Shots || Switches != Other.Switches || OriginSnaps != Other.OriginSnaps || TimeClamps != Other.TimeClamps || LateRetargets != Other.LateRetargets
		|| FreshRetargets != Other.FreshRetargets || MoveShotOrphans != Other.MoveShotOrphans || RpcRefused != Other.RpcRefused || Discrepancies != Other.Discrepancies
		|| Rejects.Num() != Other.Rejects.Num())
	{
		return false;
	}
	for (const TPair<FName, int32>& Entry : Rejects)
	{
		if (Other.GetRejects(Entry.Key) != Entry.Value)
		{
			return false;
		}
	}
	return true;
}

VeloxAnomaly::FRates VeloxAnomaly::Rates(const FVeloxPlayerAnomalyStats& Stats)
{
	FRates Out;
	Out.LateShare = static_cast<float>(Stats.LateRetargets) / static_cast<float>(FMath::Max(Stats.Switches, 1));
	// Review 0007-09 F2: rejects / attempts (accepted + rejected), so the share lies in [0,1] and an all-rejected client
	// reaches the threshold check.
	Out.RejectShare = static_cast<float>(Stats.GetRejects()) / static_cast<float>(FMath::Max(Stats.GetShotAttempts(), 1));
	return Out;
}

TArray<FName> VeloxAnomaly::Exceeds(const FVeloxPlayerAnomalyStats& Stats, const FVeloxAnomalyThresholds& T)
{
	TArray<FName> Out;
	const FRates R = Rates(Stats);
	if (T.MaxLateShare > 0.f && Stats.Switches >= T.MinSwitchesForLateShare && R.LateShare > T.MaxLateShare)
	{
		Out.Add(TEXT("late_share"));
	}
	if (T.MaxRejectShare > 0.f && Stats.GetShotAttempts() >= T.MinShotsForRejectShare && R.RejectShare > T.MaxRejectShare)
	{
		Out.Add(TEXT("reject_share"));
	}
	const auto Count = [&Out](int32 Value, int32 Max, const TCHAR* Name)
	{
		if (Max > 0 && Value > Max)
		{
			Out.Add(Name);
		}
	};
	Count(Stats.OriginSnaps, T.MaxOriginSnaps, TEXT("snaps"));
	Count(Stats.TimeClamps, T.MaxTimeClamps, TEXT("clamps"));
	Count(Stats.MoveShotOrphans, T.MaxMoveShotOrphans, TEXT("orphans"));
	Count(Stats.RpcRefused, T.MaxRpcRefused, TEXT("rpc_refused"));
	Count(Stats.Discrepancies, T.MaxDiscrepancies, TEXT("discrepancies"));
	return Out;
}

FString VeloxAnomaly::FormatLine(const FVeloxPlayerAnomalyStats& S, const FString& Player, int32 Id, const FVeloxPlayerAnomalyStats& P, const TCHAR* At,
	const TArray<FName>& Exceeded)
{
	FString Line;
	Line.Reserve(320);
	Line.Appendf(TEXT("VeloxAnomaly at=%s pid=%d"), At, Id);
	const auto Field = [&Line](const TCHAR* Key, int32 Value, int32 Before)
	{
		Line.Appendf(TEXT(" %s=%d(+%d)"), Key, Value, Value - Before);
	};
	Field(TEXT("shots"), S.Shots, P.Shots);
	Field(TEXT("switches"), S.Switches, P.Switches);
	Line += TEXT(" rejects{");
	for (int32 I = 0; I < S.Rejects.Num(); ++I)
	{
		const TPair<FName, int32>& Entry = S.Rejects[I];
		Line.Appendf(TEXT("%s%s:%d(+%d)"), I ? TEXT(",") : TEXT(""), *Entry.Key.ToString(), Entry.Value, Entry.Value - P.GetRejects(Entry.Key));
	}
	Line += TEXT("}");
	Field(TEXT("snaps"), S.OriginSnaps, P.OriginSnaps);
	Field(TEXT("clamps"), S.TimeClamps, P.TimeClamps);
	Field(TEXT("late_retargets"), S.LateRetargets, P.LateRetargets);
	Field(TEXT("fresh_retargets"), S.FreshRetargets, P.FreshRetargets);
	Field(TEXT("orphans"), S.MoveShotOrphans, P.MoveShotOrphans);
	Field(TEXT("rpc_refused"), S.RpcRefused, P.RpcRefused);
	Field(TEXT("discrepancies"), S.Discrepancies, P.Discrepancies);
	const FRates R = Rates(S);
	Line.Appendf(TEXT(" late_share=%.2f reject_share=%.2f rtt_ms=%d"), R.LateShare, R.RejectShare, FMath::RoundToInt32(S.RttMs));
	if (Exceeded.Num() > 0)
	{
		Line += TEXT(" exceeds=");
		for (int32 I = 0; I < Exceeded.Num(); ++I)
		{
			Line.Appendf(TEXT("%s%s"), I ? TEXT(",") : TEXT(""), *Exceeded[I].ToString());
		}
	}
	Line.Appendf(TEXT(" player=%s"), *Player);
	return Line;
}
