// Velox - FT_BotEdge (ADR 0008 sections 3.6.1, 3.6.2 and 3.7, task M7 H4).
//
//   velox.bots.FtBotEdge [quit] [seconds=N]
//       console, `UnrealEditor-Cmd Velox.uproject L_HazardTest -game -nullrhi -UseFixedTimeStep -FPS=60
//       -ExecCmds="velox.bots.Seed 1, velox.bots.FtBotEdge quit"` (map: Tools/editor/create_hazard_test_map.py).
//       A Hard bot (spawn loadout) is put on the 800 x 800 platform over the void slab and fights the invulnerable strafing
//       dummy (moved to the platform centre) for 60 s. Checks: no death of the bot at all (in particular 0 hazard deaths),
//       the bot fought (target visible most of the time, shots pressed), and the edge-aware overlay ran (probes > 0).
//       Reports the probe / mirror / drop counts, hops refused by the landing guard, and the closest the bot came to a
//       platform edge. With `velox.bots.EdgeAware 0` (A/B) the same run shows what the behaviours prevent.
//   Logs `VeloxBotEdgeFt test=FT_BotEdge check=... ok=` and `VeloxBotEdgeFt test=FT_BotEdge result=PASS|FAIL`; quit
//   exits 0/1.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Bots/VeloxBotCombat.h"
#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotPathFollowingComponent.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxDummyController.h"
#include "Bots/VeloxDummySpawner.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxHazardSubsystem.h"
#include "Combat/VeloxHazardVolume.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Movement/VeloxMovementComponent.h"

namespace VeloxBotEdgeFt
{
	// L_HazardTest coordinates (Tools/editor/create_hazard_test_map.py): platform x 1000..1800, y -400..400, top 0.
	constexpr double PlatformX0 = 1000.0;
	constexpr double PlatformX1 = 1800.0;
	constexpr double PlatformHalfY = 400.0;
	/** The bot starts in the platform's -X/-Y quarter, the dummy strafes along Y through the centre. */
	const FVector BotSpot(1150.0, -250.0, 0.0);
	const FVector DummySpot(1400.0, 0.0, 110.0);
	/** The dummy's strafe stays 150 cm inside the platform's Y edges. */
	constexpr float DummyStrafeHalfWidthCm = 250.f;
	/** A point below the platform edge that must be in the void slab (map sanity). */
	const FVector VoidProbe(1900.0, 0.0, -800.0);

	constexpr double DefaultFightSeconds = 60.0;
	constexpr double SettleSeconds = 0.5;
	/** The bot had the dummy in sight for at least this fraction of the fight (it fought, not idled). */
	constexpr double MinVisibleFraction = 0.5;
	/** Verbose position trace every this many ticks (LogVeloxBots Verbose). */
	constexpr int32 TraceEveryTicks = 6;

	struct FEdgeFt
	{
		TWeakObjectPtr<UWorld> World;
		bool bQuit = false;
		bool bPassed = true;
		bool bFinished = false;
		double FightSeconds = DefaultFightSeconds;
		FTSTicker::FDelegateHandle TickHandle;
		FDelegateHandle KillHandle;

		TWeakObjectPtr<AVeloxBotController> Bot;
		TWeakObjectPtr<AVeloxPlayerState> BotPS;
		double StartTime = -1.0;
		int32 Deaths = 0;
		int32 HazardDeaths = 0;
		int32 Ticks = 0;
		int32 VisibleTicks = 0;
		double MinEdgeCm = TNumericLimits<double>::Max();
		double MaxSpeed = 0.0;

		double Now() const
		{
			const UWorld* W = World.Get();
			return W ? W->GetTimeSeconds() : 0.0;
		}

		void Check(bool bOk, const TCHAR* What, const FString& Detail)
		{
			bPassed &= bOk;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotEdgeFt test=FT_BotEdge check=%s ok=%d %s"), What, bOk ? 1 : 0, *Detail);
		}

		void Finish()
		{
			if (bFinished)
			{
				return;
			}
			bFinished = true;
			if (UWorld* W = World.Get())
			{
				if (AVeloxGameState* GS = W->GetGameState<AVeloxGameState>())
				{
					GS->OnKill.Remove(KillHandle);
				}
			}
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotEdgeFt test=FT_BotEdge result=%s"), bPassed ? TEXT("PASS") : TEXT("FAIL"));
			if (bQuit)
			{
				FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
			}
		}
	};

	TSharedPtr<FEdgeFt> GEdgeFt;

	/** Horizontal distance from Point to the nearest platform edge (negative: beyond it). */
	double EdgeDistance(const FVector& Point)
	{
		return FMath::Min(FMath::Min(Point.X - PlatformX0, PlatformX1 - Point.X), FMath::Min(Point.Y + PlatformHalfY, PlatformHalfY - Point.Y));
	}

	bool Start(const TSharedRef<FEdgeFt>& Ft)
	{
		UWorld* World = Ft->World.Get();
		UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		if (!Bots)
		{
			return false;
		}
		TActorIterator<AVeloxDummySpawner> It(World);
		AVeloxDummySpawner* Spawner = It ? *It : nullptr;
		if (!Spawner)
		{
			Ft->Check(false, TEXT("setup"), TEXT("no AVeloxDummySpawner on the map"));
			return false;
		}
		// Test harness: the platform's dummy strafes through the centre, inside the edges, and cannot be killed (the
		// fight lasts the whole run).
		Spawner->SetActorLocation(DummySpot);
		Spawner->StrafeHalfWidth = DummyStrafeHalfWidthCm;
		Spawner->bInvulnerable = true;
		AVeloxCharacter* Dummy = Spawner->SpawnDummy();

		AVeloxBotController* Bot = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Hard")), TEXT("FtEdgeBot"));
		AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		AVeloxPlayerState* PS = Bot ? Bot->GetPlayerState<AVeloxPlayerState>() : nullptr;
		if (!Dummy || !Pawn || !PS)
		{
			Ft->Check(false, TEXT("setup"), FString::Printf(TEXT("dummy=%s bot=%s"), *GetNameSafe(Dummy), *GetNameSafe(Bot)));
			return false;
		}
		// Test harness: put the bot on the platform at rest (the platform is a nav island; the pad link leads only onto it).
		Pawn->SetActorLocationAndRotation(BotSpot + FVector(0.0, 0.0, Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0), FRotator::ZeroRotator, false,
			nullptr, ETeleportType::TeleportPhysics);
		if (UVeloxMovementComponent* Movement = Pawn->GetVeloxMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
		Ft->Bot = Bot;
		Ft->BotPS = PS;
		Ft->StartTime = Ft->Now();
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotEdgeFt test=FT_BotEdge start bot=%s dummy=%s seconds=%.0f edge_aware=%d"), *Bot->GetBotName(), *Dummy->GetName(),
			Ft->FightSeconds, VeloxBotCombatGlue::IsEdgeAware(World) ? 1 : 0);
		return true;
	}

	bool Tick(const TSharedRef<FEdgeFt>& Ft)
	{
		AVeloxBotController* Bot = Ft->Bot.Get();
		if (!Bot)
		{
			Ft->Check(false, TEXT("bot"), TEXT("bot gone"));
			return false;
		}
		const double Elapsed = Ft->Now() - Ft->StartTime;
		const AVeloxCharacter* Pawn = Cast<AVeloxCharacter>(Bot->GetPawn());
		if (Elapsed >= SettleSeconds && Pawn && !Pawn->IsCorpse() && Ft->Deaths == 0)
		{
			++Ft->Ticks;
			Ft->VisibleTicks += Bot->GetCombatState().HasVisibleTarget() ? 1 : 0;
			const FVector At = Pawn->GetActorLocation();
			if (At.Z > -100.0)
			{
				Ft->MinEdgeCm = FMath::Min(Ft->MinEdgeCm, EdgeDistance(At));
			}
			Ft->MaxSpeed = FMath::Max(Ft->MaxSpeed, static_cast<double>(FVector2D(Pawn->GetVelocity()).Size()));
			if (Ft->Ticks % TraceEveryTicks == 0)
			{
				const UVeloxMovementComponent* Movement = Pawn->GetVeloxMovementComponent();
				UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotEdgeFt trace t=%.2f at=%s vel=%s ground=%d vis=%d edge=%.0f"), Elapsed, *At.ToCompactString(),
					*FVector2D(Pawn->GetVelocity()).ToString(), Movement && Movement->IsOnGround() ? 1 : 0, Bot->GetCombatState().HasVisibleTarget() ? 1 : 0, EdgeDistance(At));
			}
		}
		// The first death ends the run (a respawn lands on the main floor, which is no part of the test).
		if (Elapsed < Ft->FightSeconds && Ft->Deaths == 0)
		{
			return true;
		}

		const FVeloxBotCombatState& Combat = Bot->GetCombatState();
		const UVeloxBotPathFollowingComponent* Follow = Bot->GetBotPathFollowing();
		const int32 HopRefused = Follow ? Follow->GetHopLandingBlockedTicks() : 0;
		const double VisibleFraction = Ft->Ticks > 0 ? static_cast<double>(Ft->VisibleTicks) / Ft->Ticks : 0.0;
		Ft->Check(Ft->HazardDeaths == 0, TEXT("hazard_deaths"), FString::Printf(TEXT("hazard_deaths=%d"), Ft->HazardDeaths));
		Ft->Check(Ft->Deaths == 0, TEXT("deaths"), FString::Printf(TEXT("deaths=%d"), Ft->Deaths));
		Ft->Check(VisibleFraction >= MinVisibleFraction && Combat.ShotsPressed > 0, TEXT("fought"),
			FString::Printf(TEXT("visible=%.2f shots=%d"), VisibleFraction, Combat.ShotsPressed));
		const bool bEdgeAware = VeloxBotCombatGlue::IsEdgeAware(Ft->World.Get());
		Ft->Check(!bEdgeAware || Combat.EdgeProbeCount > 0, TEXT("edge_probes"), FString::Printf(TEXT("probes=%d edge_aware=%d"), Combat.EdgeProbeCount, bEdgeAware ? 1 : 0));
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotEdgeFt test=FT_BotEdge summary seconds=%.1f probes=%d mirror=%d drop=%d hop_refused=%d min_edge=%.0f max_speed=%.0f dodges=%d"),
			Elapsed, Combat.EdgeProbeCount, Combat.EdgeMirrorCount, Combat.EdgeDropCount, HopRefused,
			Ft->MinEdgeCm == TNumericLimits<double>::Max() ? -1.0 : Ft->MinEdgeCm, Ft->MaxSpeed, Combat.DodgeCount);
		return false;
	}

	void Command(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		AVeloxGameState* GameState = World ? World->GetGameState<AVeloxGameState>() : nullptr;
		const UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(World);
		if (!Bots || !GameState)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtBotEdge: needs the authority world of L_HazardTest (-game)."));
			return;
		}
		TSharedRef<FEdgeFt> Ft = MakeShared<FEdgeFt>();
		Ft->World = World;
		for (const FString& Arg : Args)
		{
			FString Value;
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				Ft->bQuit = true;
			}
			else if (Arg.Split(TEXT("="), nullptr, &Value) && Arg.StartsWith(TEXT("seconds="), ESearchCase::IgnoreCase))
			{
				Ft->FightSeconds = FMath::Max(1.0, FCString::Atod(*Value));
			}
		}
		GEdgeFt = Ft;
		const bool bVoid = Hazards && Hazards->HasHazards() && Hazards->IsInHazard(VoidProbe, EVeloxHazardType::Void);
		Ft->Check(bVoid, TEXT("map_hazards"), FString::Printf(TEXT("volumes=%d"), Hazards ? Hazards->GetVolumes().Num() : 0));
		if (!bVoid)
		{
			Ft->Finish();
			return;
		}
		if (IConsoleVariable* Peaceful = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.bots.Peaceful")))
		{
			Peaceful->Set(0, ECVF_SetByCode);
		}
		for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
		{
			// The -game local player's pawn stands on a PlayerStart on the main floor and is no part of the test.
			if (Cast<APlayerController>(It->GetController()))
			{
				It->Destroy();
			}
		}
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("velox.match.Phase Live"));
		}
		TWeakPtr<FEdgeFt> Weak = Ft;
		Ft->KillHandle = GameState->OnKill.AddLambda([Weak](const FVeloxKillEvent& Event)
		{
			const TSharedPtr<FEdgeFt> F = Weak.Pin();
			if (F && F->BotPS.IsValid() && Event.Victim.Get() == F->BotPS.Get())
			{
				++F->Deaths;
				const bool bHazard = Event.DamageTags.HasTag(VeloxGameplayTags::Damage_Hazard);
				F->HazardDeaths += bHazard ? 1 : 0;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotEdgeFt test=FT_BotEdge death t=%.2f hazard=%d tags=%s"), F->Now() - F->StartTime, bHazard ? 1 : 0,
					*Event.DamageTags.ToStringSimple());
			}
		});
		Bots->CallWhenNavReady([Weak]()
		{
			const TSharedPtr<FEdgeFt> F = Weak.Pin();
			if (!F)
			{
				return;
			}
			if (!Start(F.ToSharedRef()))
			{
				F->Finish();
				return;
			}
			F->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				const TSharedPtr<FEdgeFt> Inner = Weak.Pin();
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

	FAutoConsoleCommandWithWorldAndArgs Cmd(TEXT("velox.bots.FtBotEdge"),
		TEXT("velox.bots.FtBotEdge [quit] [seconds=N]: FT_BotEdge on L_HazardTest (-game -nullrhi -UseFixedTimeStep -FPS=60): a Hard bot fights the dummy on the "
			 "void platform for 60 s with 0 hazard deaths (ADR 0008 3.6.1/2). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));
}

#endif // !UE_BUILD_SHIPPING
