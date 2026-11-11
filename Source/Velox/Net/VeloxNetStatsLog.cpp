// Velox - non-shipping server network/frame statistics line (ADR 0007 section 5.6, M6 task 10).
//   velox.net.StatsLog 0|1   with 1, a server world (dedicated / listen) logs every 10 s on LogVeloxNet:
//   (a client process logs `VeloxNetStats role=client ... fps=`.)
//   VeloxNetStats conns= pawns= window= frames= cpu_ms_mean= cpu_ms_p50= cpu_ms_p99= cpu_ms_max= wall_ms_mean= wall_ms_p99=
//                 out_bps_total= out_bps_per_conn=[..] in_bps_total= in_bps_per_conn=[..] out_inst_per_conn=[..]
//   cpu_ms_*: game-thread CPU time per frame (clock_gettime THREAD_CPUTIME between the begin/end frame delegates), the
//   work without the MaxFPS sleep; wall_ms_*: interval between frame starts. *_bps_*: UNetConnection Out/InTotalBytes
//   deltas over the window; out_inst_per_conn: the engine's own 1 s OutBytesPerSecond at the sample. Reads only, no game effect.

#include "Algo/MaxElement.h"
#include "Containers/Ticker.h"
#include "Core/VeloxLog.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"

#if !UE_BUILD_SHIPPING
#if PLATFORM_UNIX
#include <time.h>
#endif

namespace VeloxNetStatsPrivate
{
	int32 StatsLog = 0;
	FAutoConsoleVariableRef CVarStatsLog(TEXT("velox.net.StatsLog"), StatsLog,
		TEXT("Server: 1 logs a VeloxNetStats line (connections, per-connection bytes/s, frame times) every 10 s (task m6-online/10)."), ECVF_Default);

	double ThreadCpuSeconds()
	{
#if PLATFORM_UNIX
		timespec Ts;
		if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &Ts) == 0)
		{
			return static_cast<double>(Ts.tv_sec) + static_cast<double>(Ts.tv_nsec) * 1e-9;
		}
#endif
		return FPlatformTime::Seconds();
	}

	struct FState
	{
		TArray<float> CpuMs;
		TArray<float> WallMs;
		double BeginCpu = 0.0;
		double LastBeginWall = 0.0;
		double WindowStart = 0.0;
		TMap<TWeakObjectPtr<UNetConnection>, TPair<int32, int32>> Prev;   // Out/InTotalBytes at the last sample
		FTSTicker::FDelegateHandle Ticker;

		static float Pct(TArray<float>& A, double P)
		{
			if (A.IsEmpty()) { return 0.f; }
			A.Sort();
			return A[FMath::Clamp(FMath::CeilToInt(P * A.Num()) - 1, 0, A.Num() - 1)];
		}
		static float Mean(const TArray<float>& A)
		{
			double S = 0.0;
			for (float V : A) { S += V; }
			return A.IsEmpty() ? 0.f : static_cast<float>(S / A.Num());
		}

		void Begin()
		{
			if (!StatsLog) { return; }
			const double Now = FPlatformTime::Seconds();
			if (LastBeginWall > 0.0) { WallMs.Add(static_cast<float>((Now - LastBeginWall) * 1000.0)); }
			LastBeginWall = Now;
			BeginCpu = ThreadCpuSeconds();
		}
		void End()
		{
			if (!StatsLog || BeginCpu <= 0.0) { return; }
			CpuMs.Add(static_cast<float>((ThreadCpuSeconds() - BeginCpu) * 1000.0));
		}

		bool Sample(float)
		{
			if (!StatsLog || !GEngine) { return true; }
			UWorld* Server = nullptr;
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				UWorld* W = Ctx.World();
				if (W && W->GetNetMode() != NM_Client && W->GetNetMode() != NM_Standalone && W->GetNetDriver()) { Server = W; break; }
			}
			const double Now = FPlatformTime::Seconds();
			const double Window = FMath::Max(Now - WindowStart, 0.001);
			if (!Server)
			{
				// A client process: its own frame rate (task 10 confirms the clients stay at <= 60 fps).
				if (WindowStart > 0.0 && !CpuMs.IsEmpty())
				{
					UE_LOG(LogVeloxNet, Log, TEXT("VeloxNetStats role=client window=%.2f frames=%d fps=%.2f cpu_ms_mean=%.3f wall_ms_p99=%.3f"), Window, CpuMs.Num(), CpuMs.Num() / Window, Mean(CpuMs), Pct(WallMs, 0.99));
				}
				WindowStart = Now; CpuMs.Reset(); WallMs.Reset(); return true;
			}
			if (WindowStart <= 0.0) { WindowStart = Now; return true; }
			int32 Pawns = 0;
			for (TActorIterator<APawn> It(Server); It; ++It) { ++Pawns; }
			FString OutPer, InPer, Inst;
			double OutTot = 0.0, InTot = 0.0;
			int32 Conns = 0;
			for (UNetConnection* C : Server->GetNetDriver()->ClientConnections)
			{
				if (!C) { continue; }
				++Conns;
				const TPair<int32, int32> P = Prev.FindRef(C);
				const double O = static_cast<double>(static_cast<uint32>(C->OutTotalBytes) - static_cast<uint32>(P.Key)) / Window;
				const double I = static_cast<double>(static_cast<uint32>(C->InTotalBytes) - static_cast<uint32>(P.Value)) / Window;
				Prev.Add(C, TPair<int32, int32>(C->OutTotalBytes, C->InTotalBytes));
				OutTot += O; InTot += I;
				OutPer += FString::Printf(TEXT("%s%.0f"), OutPer.IsEmpty() ? TEXT("") : TEXT(","), O);
				InPer += FString::Printf(TEXT("%s%.0f"), InPer.IsEmpty() ? TEXT("") : TEXT(","), I);
				Inst += FString::Printf(TEXT("%s%d"), Inst.IsEmpty() ? TEXT("") : TEXT(","), C->OutBytesPerSecond);
			}
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxNetStats conns=%d pawns=%d window=%.2f frames=%d cpu_ms_mean=%.3f cpu_ms_p50=%.3f cpu_ms_p99=%.3f cpu_ms_max=%.3f wall_ms_mean=%.3f wall_ms_p99=%.3f out_bps_total=%.0f out_bps_per_conn=[%s] in_bps_total=%.0f in_bps_per_conn=[%s] out_inst_per_conn=[%s]"),
				Conns, Pawns, Window, CpuMs.Num(), Mean(CpuMs), Pct(CpuMs, 0.5), Pct(CpuMs, 0.99), CpuMs.IsEmpty() ? 0.f : *Algo::MaxElement(CpuMs),
				Mean(WallMs), Pct(WallMs, 0.99), OutTot, *OutPer, InTot, *InPer, *Inst);
			CpuMs.Reset(); WallMs.Reset(); WindowStart = Now;
			return true;
		}
	};

	FState& State() { static FState S; return S; }

	struct FRegistrar
	{
		FRegistrar()
		{
			FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
			{
				FState& S = State();
				FCoreDelegates::OnBeginFrame.AddLambda([]() { State().Begin(); });
				FCoreDelegates::OnEndFrame.AddLambda([]() { State().End(); });
				S.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(&S, &FState::Sample), 10.0f);
			});
		}
	};
	FRegistrar GRegistrar;
}
#endif
