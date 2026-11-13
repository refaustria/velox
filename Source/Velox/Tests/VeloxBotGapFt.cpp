// Velox - FT_BotGapJump (task M7 H11): bots cross JUMP gap links.
//
//   velox.bots.FtBotGapJump [quit] [trials=N] [difficulty=Name] [plain=0|1]
//       console on L_GapTest (-game -nullrhi -UseFixedTimeStep -FPS=60; Tools/editor/create_gap_test_map.py): N (10) seeded
//       crossings per gap (300 and 400 cm, the lanes alternate; per lane the direction alternates, so half the crossings use
//       the both-ways link backwards: take-off 100 cm before the edge, landing 60 cm onto the platform). A crossing starts at
//       rest 150..1100 cm before the take-off edge (y +-450 off the lane centre, so up to ~70 degree side approaches; any yaw; FRandomStream(Crc32(5000 + i))), the
//       goal 300..900 cm beyond the far edge. It counts when the lane's JUMP link reports ok with the bot on the far
//       platform; it fails when the bot falls into the gap (the lava bed 300 cm down, Fornax / Nemus) or after 10 s.
//       Pass: >= 9 of 10 per gap. The map has lava, so the ADR 0008 3.6 behaviours (edge-aware overlay, hop landing guard)
//       are on; with plain=1 (default) the same crossings run a second time with velox.bots.EdgeAware 0 (the Nemus case: a map
//       without hazard volumes), each gap again >= 9 of 10.
//   Logs `VeloxBotGapFt test=FT_BotGapJump trial=... result=...` per crossing (take-off point and speed, the link result),
//   `VeloxBotGapFt test=FT_BotGapJump check=... ok=` and `VeloxBotGapFt test=FT_BotGapJump result=PASS|FAIL`; quit exits 0/1.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Bots/VeloxBotCombat.h"
#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotManeuver.h"
#include "Bots/VeloxBotPathFollowingComponent.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHazardSubsystem.h"
#include "Combat/VeloxHazardVolume.h"
#include "Combat/VeloxHealthSet.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Math/RandomStream.h"
#include "Misc/Crc.h"
#include "Movement/VeloxMovementComponent.h"

namespace VeloxBotGapFt
{
	// L_GapTest coordinates (Tools/editor/create_gap_test_map.py): per lane west platform x -1400..0, gap x 0..G, east
	// platform x G..G+1400, lane y +-600, tops 0.
	struct FLane
	{
		double Gap;
		double Y;
	};
	const FLane Lanes[] = { { 300.0, 0.0 }, { 400.0, 1700.0 } };
	constexpr int32 NumLanes = UE_ARRAY_COUNT(Lanes);
	constexpr double LaneHalfY = 600.0;
	constexpr double RunLen = 1400.0;
	constexpr double StartMinCm = 150.0;
	constexpr double StartMaxCm = 1100.0;
	constexpr double StartHalfY = 450.0;
	constexpr double GoalMinCm = 300.0;
	constexpr double GoalMaxCm = 900.0;
	constexpr double GoalHalfY = 300.0;
	constexpr double FallenBelowCm = -100.0;
	constexpr double SettleSeconds = 0.5;
	constexpr double TrialTimeoutSeconds = 10.0;
	constexpr int32 DefaultTrials = 10;
	constexpr int32 MinOk = 9;
	const FVector LavaProbe(150.0, 0.0, -290.0);

	enum class EPhase : uint8
	{
		Settle,
		Run,
	};

	struct FGapFt
	{
		TWeakObjectPtr<UWorld> World;
		const TCHAR* Name = TEXT("FT_BotGapJump");
		bool bQuit = false;
		bool bPassed = true;
		bool bFinished = false;
		bool bPlainPass = true;
		FString Difficulty = TEXT("Hard");
		FTSTicker::FDelegateHandle TickHandle;
		FDelegateHandle KillHandle;
		FDelegateHandle LinkHandle;
		TWeakObjectPtr<AVeloxBotController> Bot;
		TWeakObjectPtr<AVeloxPlayerState> BotPS;
		int32 Deaths = 0;

		/** Trials per gap and mode; Index runs over NumTrials x NumLanes per mode, the modes one after the other. */
		int32 NumTrials = DefaultTrials;
		int32 Index = 0;
		/** 0 = edge-aware (the map's lava), 1 = velox.bots.EdgeAware 0. */
		int32 Mode = 0;
		int32 OldEdgeAware = 1;
		EPhase Phase = EPhase::Settle;
		double PhaseStart = 0.0;

		// The current crossing.
		int32 Lane = 0;
		bool bEastward = true;
		FVector Start = FVector::ZeroVector;
		FVector Goal = FVector::ZeroVector;
		bool bReported = false;
		EVeloxManeuverResult LinkResult = EVeloxManeuverResult::None;
		int32 LinkReports = 0;
		bool bTookOff = false;
		/** Take-off: distance before the take-off edge (cm, negative = past it) and 2-D speed. */
		double TakeoffBeforeEdge = 0.0;
		double TakeoffSpeed = 0.0;
		/** The feet stood on (or hopped over) the far platform when the link reported. */
		bool bReportOnFar = false;

		int32 Ok[2][NumLanes] = {};
		int32 Done[2][NumLanes] = {};

		double Now() const
		{
			const UWorld* W = World.Get();
			return W ? W->GetTimeSeconds() : 0.0;
		}

		void Check(bool bOk, const TCHAR* What, const FString& Detail)
		{
			bPassed &= bOk;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotGapFt test=%s check=%s ok=%d %s"), Name, What, bOk ? 1 : 0, *Detail);
		}

		void SetEdgeAware(int32 Value)
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.bots.EdgeAware")))
			{
				Var->Set(Value, ECVF_SetByCode);
			}
		}

		void Finish()
		{
			if (bFinished)
			{
				return;
			}
			bFinished = true;
			SetEdgeAware(OldEdgeAware);
			if (UWorld* W = World.Get())
			{
				if (AVeloxGameState* GS = W->GetGameState<AVeloxGameState>())
				{
					GS->OnKill.Remove(KillHandle);
				}
			}
			if (AVeloxBotController* B = Bot.Get())
			{
				B->OnLinkFinished.Remove(LinkHandle);
			}
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotGapFt test=%s result=%s"), Name, bPassed ? TEXT("PASS") : TEXT("FAIL"));
			if (bQuit)
			{
				FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
			}
		}
	};

	TSharedPtr<FGapFt> GGapFt;

	void Place(AVeloxCharacter& Pawn, const FVector& Feet, float Yaw)
	{
		Pawn.SetActorLocationAndRotation(Feet + FVector(0.0, 0.0, Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0), FRotator(0.f, Yaw, 0.f), false, nullptr,
			ETeleportType::TeleportPhysics);
		if (UVeloxMovementComponent* Movement = Pawn.GetVeloxMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
	}

	void Heal(AVeloxCharacter& Pawn)
	{
		const AVeloxPlayerState* PS = Pawn.GetPlayerState<AVeloxPlayerState>();
		UVeloxAbilitySystemComponent* ASC = Pawn.GetVeloxAbilitySystemComponent();
		const UVeloxHealthSet* Health = PS ? PS->GetHealthSet() : nullptr;
		if (!ASC || !Health)
		{
			return;
		}
		const int32 Missing = FMath::RoundToInt32(100.f - Health->GetHealth());
		if (Missing > 0)
		{
			const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeHealSpec(ASC, Missing);
			if (Spec.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			}
		}
	}

	/** X of the take-off edge (west edge 0 eastward, east edge G westward) and the direction of travel (+1 / -1). */
	double TakeoffEdgeX(const FGapFt& Ft) { return Ft.bEastward ? 0.0 : Lanes[Ft.Lane].Gap; }
	double Dir(const FGapFt& Ft) { return Ft.bEastward ? 1.0 : -1.0; }

	void BeginTrial(FGapFt& Ft, AVeloxCharacter& Pawn)
	{
		const int32 PerMode = Ft.NumTrials * NumLanes;
		const int32 InMode = Ft.Index % PerMode;
		Ft.Lane = InMode % NumLanes;
		Ft.bEastward = ((InMode / NumLanes) % 2) == 0;
		const uint32 Seed = 5000u + static_cast<uint32>(Ft.Index);
		FRandomStream Rng(static_cast<int32>(FCrc::MemCrc32(&Seed, sizeof(Seed))));
		const FLane& L = Lanes[Ft.Lane];
		const double EdgeX = TakeoffEdgeX(Ft);
		const double FarEdgeX = Ft.bEastward ? L.Gap : 0.0;
		const double D = Dir(Ft);
		Ft.Start = FVector(EdgeX - D * Rng.FRandRange(StartMinCm, StartMaxCm), L.Y + Rng.FRandRange(-StartHalfY, StartHalfY), 0.0);
		Ft.Goal = FVector(FarEdgeX + D * Rng.FRandRange(GoalMinCm, GoalMaxCm), L.Y + Rng.FRandRange(-GoalHalfY, GoalHalfY), 0.0);
		Place(Pawn, Ft.Start, Rng.FRandRange(-180.f, 180.f));
		Heal(Pawn);
		if (AVeloxBotController* Bot = Ft.Bot.Get())
		{
			Bot->SetForcedPointGoal(Ft.Start);
		}
		Ft.bReported = false;
		Ft.LinkResult = EVeloxManeuverResult::None;
		Ft.LinkReports = 0;
		Ft.bTookOff = false;
		Ft.TakeoffBeforeEdge = 0.0;
		Ft.TakeoffSpeed = 0.0;
		Ft.bReportOnFar = false;
		Ft.Phase = EPhase::Settle;
		Ft.PhaseStart = Ft.Now();
	}

	void FinishMode(FGapFt& Ft)
	{
		for (int32 L = 0; L < NumLanes; ++L)
		{
			Ft.Check(Ft.Ok[Ft.Mode][L] >= MinOk, Ft.Mode == 0 ? TEXT("gap_edge_aware") : TEXT("gap_plain"),
				FString::Printf(TEXT("gap=%.0f ok=%d of %d (need %d) edge_aware=%d"), Lanes[L].Gap, Ft.Ok[Ft.Mode][L], Ft.Done[Ft.Mode][L], MinOk, Ft.Mode == 0 ? 1 : 0));
		}
	}

	bool Tick(const TSharedRef<FGapFt>& Ft)
	{
		AVeloxBotController* Bot = Ft->Bot.Get();
		AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		UVeloxMovementComponent* Movement = Pawn ? Pawn->GetVeloxMovementComponent() : nullptr;
		if (!Bot || !Pawn || !Movement || Pawn->IsCorpse() || Ft->Deaths > 0)
		{
			Ft->Check(false, TEXT("bot"), FString::Printf(TEXT("bot gone or dead deaths=%d index=%d"), Ft->Deaths, Ft->Index));
			return false;
		}
		const double Elapsed = Ft->Now() - Ft->PhaseStart;
		const FVector Feet = Movement->GetActorFeetLocation();
		if (Ft->Phase == EPhase::Settle)
		{
			if (Elapsed < SettleSeconds)
			{
				return true;
			}
			Ft->Phase = EPhase::Run;
			Ft->PhaseStart = Ft->Now();
			Bot->SetForcedPointGoal(Ft->Goal);
			return true;
		}

		const FLane& L = Lanes[Ft->Lane];
		const double D = Dir(*Ft);
		const double EdgeX = TakeoffEdgeX(*Ft);
		if (!Ft->bTookOff && !Movement->IsOnGround() && (Feet.X - EdgeX) * D > -250.0 && FMath::Abs(Feet.Y - L.Y) <= LaneHalfY + 50.0)
		{
			const UVeloxBotPathFollowingComponent* Follow = Bot->GetBotPathFollowing();
			if (Follow && Follow->IsOnLink() && Follow->GetManeuver().Phase == EVeloxManeuverPhase::Airborne)
			{
				Ft->bTookOff = true;
				Ft->TakeoffBeforeEdge = (EdgeX - Feet.X) * D;
				Ft->TakeoffSpeed = Movement->GetSpeed2D();
			}
		}
		// Judged at the link's first report (the feet on the far platform then): a Hard bot bunny-hops on after the landing
		// (auto-hop: never on the ground at a ticker tick) and, without the hazard behaviours, may hop off the far end later.
		const bool bFell = Feet.Z < FallenBelowCm;
		if (!Ft->bReported && !bFell && Elapsed <= TrialTimeoutSeconds)
		{
			return true;
		}
		const bool bOnFar = Ft->bReported && Ft->bReportOnFar;
		const bool bOk = bOnFar && Ft->LinkResult == EVeloxManeuverResult::Ok;
		Ft->Ok[Ft->Mode][Ft->Lane] += bOk ? 1 : 0;
		++Ft->Done[Ft->Mode][Ft->Lane];
		UE_LOG(LogVeloxBots, Log,
			TEXT("VeloxBotGapFt test=%s trial=%d mode=%s gap=%.0f dir=%s result=%s link=%s reports=%d on_far=%d fell=%d dt=%.2f takeoff_before_edge=%.0f takeoff_speed=%.0f at=%s start=%s goal=%s"),
			Ft->Name, Ft->Index, Ft->Mode == 0 ? TEXT("edge_aware") : TEXT("plain"), L.Gap, Ft->bEastward ? TEXT("east") : TEXT("west"), bOk ? TEXT("ok") : TEXT("failed"),
			Ft->bReported ? LexToString(Ft->LinkResult) : TEXT("none"), Ft->LinkReports, bOnFar ? 1 : 0, bFell ? 1 : 0, Elapsed, Ft->bTookOff ? Ft->TakeoffBeforeEdge : -1.0,
			Ft->TakeoffSpeed, *Feet.ToCompactString(), *Ft->Start.ToCompactString(), *Ft->Goal.ToCompactString());

		++Ft->Index;
		const int32 PerMode = Ft->NumTrials * NumLanes;
		if (Ft->Index % PerMode == 0)
		{
			FinishMode(*Ft);
			if (Ft->Mode == 1 || !Ft->bPlainPass)
			{
				return false;
			}
			Ft->Mode = 1;
			Ft->SetEdgeAware(0);
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotGapFt test=%s plain pass (velox.bots.EdgeAware 0) edge_aware=%d"), Ft->Name,
				VeloxBotCombatGlue::IsEdgeAware(Ft->World.Get()) ? 1 : 0);
		}
		BeginTrial(*Ft, *Pawn);
		return true;
	}

	void Command(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		AVeloxGameState* GameState = World ? World->GetGameState<AVeloxGameState>() : nullptr;
		const UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(World);
		if (!Bots || !GameState)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtBotGapJump: needs the authority world of L_GapTest (-game)."));
			return;
		}
		TSharedRef<FGapFt> Ft = MakeShared<FGapFt>();
		Ft->World = World;
		for (const FString& Arg : Args)
		{
			FString Value;
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				Ft->bQuit = true;
			}
			else if (Arg.StartsWith(TEXT("difficulty="), ESearchCase::IgnoreCase) && Arg.Split(TEXT("="), nullptr, &Value))
			{
				Ft->Difficulty = Value;
			}
			else if (Arg.StartsWith(TEXT("trials="), ESearchCase::IgnoreCase) && Arg.Split(TEXT("="), nullptr, &Value))
			{
				Ft->NumTrials = FMath::Max(1, FCString::Atoi(*Value));
			}
			else if (Arg.StartsWith(TEXT("plain="), ESearchCase::IgnoreCase) && Arg.Split(TEXT("="), nullptr, &Value))
			{
				Ft->bPlainPass = FCString::Atoi(*Value) != 0;
			}
		}
		if (const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.bots.EdgeAware")))
		{
			Ft->OldEdgeAware = Var->GetInt();
		}
		GGapFt = Ft;
		const bool bLava = Hazards && Hazards->HasHazards() && Hazards->IsInHazard(LavaProbe, EVeloxHazardType::Lava);
		Ft->Check(bLava, TEXT("map"), FString::Printf(TEXT("lava_in_gap=%d volumes=%d (L_GapTest)"), bLava ? 1 : 0, Hazards ? Hazards->GetVolumes().Num() : 0));
		if (!bLava)
		{
			Ft->Finish();
			return;
		}
		for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
		{
			// The -game local player's pawn is no part of the test.
			if (Cast<APlayerController>(It->GetController()))
			{
				It->Destroy();
			}
		}
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("velox.match.Phase Live"));
		}
		TWeakPtr<FGapFt> Weak = Ft;
		Ft->KillHandle = GameState->OnKill.AddLambda([Weak](const FVeloxKillEvent& Event)
		{
			const TSharedPtr<FGapFt> F = Weak.Pin();
			if (F && F->BotPS.IsValid() && Event.Victim.Get() == F->BotPS.Get())
			{
				++F->Deaths;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotGapFt test=%s death index=%d tags=%s"), F->Name, F->Index, *Event.DamageTags.ToStringSimple());
			}
		});
		Bots->CallWhenNavReady([Weak, Bots]()
		{
			const TSharedPtr<FGapFt> F = Weak.Pin();
			if (!F)
			{
				return;
			}
			AVeloxBotController* Bot = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(F->Difficulty), TEXT("FtGapBot"));
			AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
			AVeloxPlayerState* PS = Bot ? Bot->GetPlayerState<AVeloxPlayerState>() : nullptr;
			if (!Pawn || !PS)
			{
				F->Check(false, TEXT("setup"), FString::Printf(TEXT("bot=%s"), *GetNameSafe(Bot)));
				F->Finish();
				return;
			}
			F->Bot = Bot;
			F->BotPS = PS;
			F->LinkHandle = Bot->OnLinkFinished.AddLambda([Weak](AVeloxBotController* Reporter, const FVeloxBotLinkReport& Report)
			{
				const TSharedPtr<FGapFt> G = Weak.Pin();
				if (!G || G->Phase != EPhase::Run || Report.Type != EVeloxManeuverType::Jump)
				{
					return;
				}
				const double LaneY = Lanes[G->Lane].Y;
				if (FMath::Abs(Report.From.Y - LaneY) > LaneHalfY || FMath::Abs(Report.To.Y - LaneY) > LaneHalfY)
				{
					return;
				}
				++G->LinkReports;
				// The first verdict of the crossing counts (a miss followed by a retry from the pit is still a miss).
				if (!G->bReported)
				{
					G->bReported = true;
					G->LinkResult = Report.Result;
					const AVeloxCharacter* P = Reporter ? Cast<AVeloxCharacter>(Reporter->GetPawn()) : nullptr;
					const UVeloxMovementComponent* M = P ? P->GetVeloxMovementComponent() : nullptr;
					if (M)
					{
						const FVector Feet = M->GetActorFeetLocation();
						const double FarEdgeX = G->bEastward ? Lanes[G->Lane].Gap : 0.0;
						const double Past = (Feet.X - FarEdgeX) * Dir(*G);
						G->bReportOnFar = Feet.Z > -50.0 && Past >= 0.0 && Past <= RunLen && FMath::Abs(Feet.Y - LaneY) <= LaneHalfY;
					}
				}
			});
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotGapFt test=%s start bot=%s difficulty=%s trials=%d per gap and mode plain=%d edge_aware=%d"), F->Name, *Bot->GetBotName(),
				*F->Difficulty, F->NumTrials, F->bPlainPass ? 1 : 0, VeloxBotCombatGlue::IsEdgeAware(F->World.Get()) ? 1 : 0);
			BeginTrial(*F, *Pawn);
			F->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				const TSharedPtr<FGapFt> Inner = Weak.Pin();
				if (!Inner || Inner->bFinished)
				{
					return false;
				}
				if (!Tick(Inner.ToSharedRef()))
				{
					Inner->Finish();
					return false;
				}
				return true;
			}));
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GapCmd(TEXT("velox.bots.FtBotGapJump"),
		TEXT("velox.bots.FtBotGapJump [quit] [trials=N] [difficulty=Name] [plain=0|1]: FT_BotGapJump on L_GapTest (-game -nullrhi -UseFixedTimeStep -FPS=60): a bot "
			 "crosses the 300 and the 400 cm JUMP gap link in >= 9 of 10 seeded crossings each (both directions), with the hazard behaviours on and (plain=1) off "
			 "(task M7 H11). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Command(Args, World); }));
}

#endif // !UE_BUILD_SHIPPING
