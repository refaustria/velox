// Velox - bot airborne recovery, pad discipline and hazard escape (ADR 0008 sections 3.6.3, 3.6.4 and 3.7, task M7 H5).
//
//   Velox.Bots.HazardRules        automation, world-free: foreign-flight rule, ballistic time to a level, the air-recovery
//                                 wish (on course / correction), the hazard box exit, the pad-flight predicate and what
//                                 TickAirborne wishes on a pad (End, scale 1: the controller zeroes it on hazard maps).
//   velox.bots.FtBotLavaEscape [quit]
//       console on L_HazardTest (-game -nullrhi -UseFixedTimeStep -FPS=60): a bot is put into the lava channel at three
//       spots (centre, north end, south-east corner); each time its feet must be out of the lava (on the ground outside
//       the volume) within 1 s. Health is restored between the spots.
//   velox.bots.FtBotKnockOff [quit] [trials=N]
//       console on L_HazardTest: N (10) seeded trials. The bot stands 45..70 cm inside a platform edge (side, place along
//       the edge, inset, lift and a +-20 degree yaw drawn from FRandomStream(Crc32(1000 + trial))) and is knocked off it with
//       300 cm/s horizontal plus a 300..675 cm/s lift (AddKnockback, override). A trial lands when the bot stands on the
//       platform again, falls when its feet are 150 cm below the platform top (then it is put back before the void).
//       Pass: >= 8 of 10 land (ADR 0008 task H5). Then one pad crossing: from the main floor to the platform centre over
//       Pad_Gap's JUMP_PAD link, report-only (`report=pad`: on_platform, pad-discipline ticks; the fixture's path does not
//       use the link today, see the H5 notes).
//   Both log `VeloxBotRecoveryFt test=... check=... ok=` and `VeloxBotRecoveryFt test=... result=PASS|FAIL`; quit exits
//   0/1. `velox.bots.EdgeAware 0` (A/B) runs the same trials without the ADR 0008 3.6 behaviours.

#include "CoreMinimal.h"

#include "Misc/AutomationTest.h"

#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotManeuver.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxBotsHazardRulesTest, "Velox.Bots.HazardRules", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxBotsHazardRulesTest::RunTest(const FString& Parameters)
{
	// Foreign flight: own hop (jump held at takeoff, no grace, not yet a flight) is not one; everything else is.
	TestFalse(TEXT("own hop"), VeloxBotRules::IsForeignFlight(true, 0.f, false));
	TestTrue(TEXT("walked / knocked off without jump"), VeloxBotRules::IsForeignFlight(false, 0.f, false));
	TestTrue(TEXT("knockback grace"), VeloxBotRules::IsForeignFlight(true, 0.1f, false));
	TestTrue(TEXT("longer than the hop"), VeloxBotRules::IsForeignFlight(true, 0.f, true));

	// Ballistics at g = 2000: back to the own level after 2 Vz / g; a level 100 below from rest after sqrt(2 h / g).
	TestNearlyEqual(TEXT("rise and return"), VeloxBotRules::FlightSecondsToLevel(500.f, 0.f, 2000.f), 0.5f, 1e-4f);
	TestNearlyEqual(TEXT("fall 100 from rest"), VeloxBotRules::FlightSecondsToLevel(0.f, 100.f, 2000.f), FMath::Sqrt(0.1f), 1e-4f);
	TestNearlyEqual(TEXT("falling at own level"), VeloxBotRules::FlightSecondsToLevel(-300.f, 0.f, 2000.f), 0.f, 1e-4f);
	TestNearlyEqual(TEXT("no gravity"), VeloxBotRules::FlightSecondsToLevel(500.f, 0.f, 0.f), 0.f, 1e-6f);

	// Air recovery wish: on course -> none; else the correction landing -> target (not feet -> target).
	TestTrue(TEXT("on course: no wish"), VeloxBotRules::AirRecoveryWish(FVector2D(100.0, 0.0), FVector2D(110.0, 0.0), 20.f).IsZero());
	const FVector2D Back = VeloxBotRules::AirRecoveryWish(FVector2D(120.0, 0.0), FVector2D(-10.0, 0.0), 20.f);
	TestTrue(TEXT("overshoot: wish back"), Back.Equals(FVector2D(-1.0, 0.0), 1e-6));

	// Box exit: a 300 x 1200 channel (half 150 x 600), feet 40 cm west of the centre leave through the west side.
	const FVector2D West = VeloxBotRules::HazardBoxExit(FVector2D(-40.0, 0.0), FVector2D::ZeroVector, FVector2D(1.0, 0.0), 150.f, 600.f, 100.f);
	TestTrue(TEXT("exit west"), West.Equals(FVector2D(-250.0, 0.0), 1e-3));
	// The same box yawed 90 degrees (its X along world Y): feet at world y +500 are near its local +X side.
	const FVector2D North = VeloxBotRules::HazardBoxExit(FVector2D(0.0, 500.0), FVector2D::ZeroVector, FVector2D(0.0, 1.0), 600.f, 150.f, 100.f);
	TestTrue(TEXT("exit along the yawed axis"), North.Equals(FVector2D(0.0, 700.0), 1e-3));

	// Pad discipline: the pad maneuver's flight is the IsPadFlight phase; TickAirborne itself wishes End at scale 1 without
	// jump (the pre-M7 behaviour the controller overrides on hazard maps).
	{
		FVeloxManeuverParams Pad;
		Pad.Type = EVeloxManeuverType::JumpPad;
		FVeloxBotManeuver M;
		M.Begin(Pad, FVector::ZeroVector, FVector(1500.0, 0.0, 0.0), 0.0);
		FVeloxManeuverInput In;
		In.bOnGround = true;
		In.DistToStart = 200.f;
		In.DistToEnd = 1500.f;
		In.Time = 0.0;
		M.Tick(In);
		TestFalse(TEXT("pad approach is no pad flight"), M.IsPadFlight());
		In.bOnGround = false;
		In.bImpulseReceived = true;
		In.DistToStart = 10.f;
		In.Time = 1.0 / 60.0;
		FVeloxManeuverOutput O = M.Tick(In);
		TestTrue(TEXT("launched: pad flight"), M.IsPadFlight());
		In.bImpulseReceived = false;
		In.DistToEnd = 900.f;
		In.Time = 2.0 / 60.0;
		O = M.Tick(In);
		TestTrue(TEXT("pad flight: wish End"), O.WishToward == EVeloxManeuverWish::End);
		TestEqual(TEXT("pad flight: scale 1 (TickAirborne)"), O.WishScale, 1.f);
		TestFalse(TEXT("pad flight: no jump"), O.bJumpHeld);
		In.bOnGround = true;
		In.DistToEnd = 50.f;
		In.Time = 3.0 / 60.0;
		O = M.Tick(In);
		TestTrue(TEXT("pad landing ok"), O.bDone && M.Result == EVeloxManeuverResult::Ok);
		TestFalse(TEXT("after the landing no pad flight"), M.IsPadFlight());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#if !UE_BUILD_SHIPPING

#include "Bots/VeloxBotCombat.h"
#include "Bots/VeloxBotLog.h"
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

namespace VeloxBotRecoveryFt
{
	// L_HazardTest coordinates (Tools/editor/create_hazard_test_map.py).
	constexpr double PlatformX0 = 1000.0;
	constexpr double PlatformX1 = 1800.0;
	constexpr double PlatformHalfY = 400.0;
	constexpr double LavaBedZ = -60.0;
	/** Lava spots: channel x -1500..-1200, y -600..600 (centre, north end, south-east corner). */
	const FVector LavaSpots[] = { FVector(-1350.0, 0.0, LavaBedZ), FVector(-1420.0, 500.0, LavaBedZ), FVector(-1240.0, -540.0, LavaBedZ) };
	const FVector LavaProbe(-1350.0, 0.0, -50.0);
	/** Where the bot waits between the lava spots (main floor, east of the channel). */
	const FVector FloorSpot(-900.0, 0.0, 0.0);

	constexpr double EscapeLimitSeconds = 1.0;
	constexpr double EscapeTimeoutSeconds = 4.0;
	constexpr double SettleSeconds = 0.5;
	constexpr int32 DefaultTrials = 10;
	constexpr int32 MinLanded = 8;
	constexpr double KnockSpeed = 300.0;
	constexpr double FallenBelowCm = -150.0;
	constexpr double TrialTimeoutSeconds = 3.0;
	constexpr double PadTimeoutSeconds = 15.0;
	const FVector PlatformCentre(1400.0, 0.0, 0.0);

	enum class EPhase : uint8
	{
		Settle,
		Run,
	};

	struct FRecoveryFt
	{
		TWeakObjectPtr<UWorld> World;
		const TCHAR* Name = TEXT("");
		bool bKnockOff = false;
		bool bQuit = false;
		bool bPassed = true;
		bool bFinished = false;
		FTSTicker::FDelegateHandle TickHandle;
		FDelegateHandle KillHandle;
		TWeakObjectPtr<AVeloxBotController> Bot;
		TWeakObjectPtr<AVeloxPlayerState> BotPS;
		int32 Deaths = 0;

		int32 Trial = -1;
		int32 NumTrials = 0;
		EPhase Phase = EPhase::Settle;
		double PhaseStart = 0.0;
		bool bSeenAirborne = false;
		int32 Landed = 0;
		int32 EscapedInTime = 0;
		double WorstEscape = 0.0;
		/** Knock-off trial: spot, outward direction and lift. */
		FVector Spot = FVector::ZeroVector;
		FVector2D Out = FVector2D::ZeroVector;
		double Lift = 0.0;
		int32 FlightsBefore = 0;
		/** Knock-off FT, after the trials: one pad crossing from the main floor onto the platform (pad discipline). */
		bool bPadTrial = false;
		int32 PadTicksBefore = 0;

		double Now() const
		{
			const UWorld* W = World.Get();
			return W ? W->GetTimeSeconds() : 0.0;
		}

		void Check(bool bOk, const TCHAR* What, const FString& Detail)
		{
			bPassed &= bOk;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s check=%s ok=%d %s"), Name, What, bOk ? 1 : 0, *Detail);
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
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s result=%s"), Name, bPassed ? TEXT("PASS") : TEXT("FAIL"));
			if (bQuit)
			{
				FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
			}
		}
	};

	TSharedPtr<FRecoveryFt> GRecoveryFt;

	/** Test harness: the bot at rest with its feet at Feet. */
	void Place(AVeloxCharacter& Pawn, const FVector& Feet, float Yaw)
	{
		Pawn.SetActorLocationAndRotation(Feet + FVector(0.0, 0.0, Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0), FRotator(0.f, Yaw, 0.f), false, nullptr,
			ETeleportType::TeleportPhysics);
		if (UVeloxMovementComponent* Movement = Pawn.GetVeloxMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
	}

	/** Test harness: health back to full (GE_Heal; the pickup rule caps it). */
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

	float HealthOf(const AVeloxCharacter& Pawn)
	{
		const AVeloxPlayerState* PS = Pawn.GetPlayerState<AVeloxPlayerState>();
		const UVeloxHealthSet* Health = PS ? PS->GetHealthSet() : nullptr;
		return Health ? Health->GetHealth() : -1.f;
	}

	void BeginKnockTrial(FRecoveryFt& Ft, AVeloxCharacter& Pawn)
	{
		// Hashed: consecutive raw seeds give correlated first draws.
		const uint32 Seed = 1000u + static_cast<uint32>(Ft.Trial);
		FRandomStream Rng(static_cast<int32>(FCrc::MemCrc32(&Seed, sizeof(Seed))));
		const int32 Side = Rng.RandRange(0, 3);
		const double Along = Rng.FRandRange(-250.f, 250.f);
		const double Inset = Rng.FRandRange(45.f, 70.f);
		Ft.Lift = Rng.FRandRange(300.f, 675.f);
		const double YawJitter = FMath::DegreesToRadians(Rng.FRandRange(-20.f, 20.f));
		const double CentreX = 0.5 * (PlatformX0 + PlatformX1);
		FVector2D Normal;
		switch (Side)
		{
		case 0: Ft.Spot = FVector(PlatformX1 - Inset, Along, 0.0); Normal = FVector2D(1.0, 0.0); break;
		case 1: Ft.Spot = FVector(PlatformX0 + Inset, Along, 0.0); Normal = FVector2D(-1.0, 0.0); break;
		case 2: Ft.Spot = FVector(CentreX + Along, PlatformHalfY - Inset, 0.0); Normal = FVector2D(0.0, 1.0); break;
		default: Ft.Spot = FVector(CentreX + Along, -PlatformHalfY + Inset, 0.0); Normal = FVector2D(0.0, -1.0); break;
		}
		Ft.Out = Normal.GetRotated(FMath::RadiansToDegrees(YawJitter));
		Place(Pawn, Ft.Spot, Rng.FRandRange(-180.f, 180.f));
		Heal(Pawn);
		if (AVeloxBotController* Bot = Ft.Bot.Get())
		{
			Bot->SetForcedPointGoal(Ft.Spot);
		}
		Ft.Phase = EPhase::Settle;
		Ft.PhaseStart = Ft.Now();
		Ft.bSeenAirborne = false;
	}

	void BeginLavaTrial(FRecoveryFt& Ft, AVeloxCharacter& Pawn)
	{
		Place(Pawn, FloorSpot, 0.f);
		Heal(Pawn);
		Ft.Phase = EPhase::Settle;
		Ft.PhaseStart = Ft.Now();
	}

	bool Tick(const TSharedRef<FRecoveryFt>& Ft)
	{
		AVeloxBotController* Bot = Ft->Bot.Get();
		AVeloxCharacter* Pawn = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		UVeloxMovementComponent* Movement = Pawn ? Pawn->GetVeloxMovementComponent() : nullptr;
		const UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(Ft->World.Get());
		if (!Bot || !Pawn || !Movement || !Hazards || Pawn->IsCorpse() || Ft->Deaths > 0)
		{
			Ft->Check(false, TEXT("bot"), FString::Printf(TEXT("bot gone or dead deaths=%d trial=%d"), Ft->Deaths, Ft->Trial));
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
			Ft->FlightsBefore = Bot->GetBotState().AirRecoveryFlights;
			if (Ft->bKnockOff)
			{
				const FVector Impulse(Ft->Out.X * KnockSpeed, Ft->Out.Y * KnockSpeed, Ft->Lift);
				Movement->AddKnockback(Impulse, true, EVeloxKnockbackSource::Server);
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d knock at=%s impulse=%s"), Ft->Name, Ft->Trial, *Feet.ToCompactString(), *Impulse.ToCompactString());
			}
			else
			{
				const FVector& Spot = LavaSpots[Ft->Trial];
				Place(*Pawn, Spot, 90.f * Ft->Trial);
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d lava at=%s health=%.0f"), Ft->Name, Ft->Trial, *Spot.ToCompactString(), HealthOf(*Pawn));
			}
			return true;
		}

		if (Ft->bPadTrial)
		{
			const bool bOnPlatform = Movement->IsOnGround() && Feet.Z > -50.0 && Feet.X >= PlatformX0 && Feet.X <= PlatformX1 && FMath::Abs(Feet.Y) <= PlatformHalfY;
			const bool bFell = Feet.Z < FallenBelowCm;
			if (!bOnPlatform && !bFell && Elapsed <= PadTimeoutSeconds)
			{
				return true;
			}
			const int32 PadTicks = Bot->GetBotState().PadDisciplineTicks - Ft->PadTicksBefore;
			const bool bEdgeAware = VeloxBotCombatGlue::IsEdgeAware(Ft->World.Get());
			// Report-only (task H5 finding): on L_HazardTest the path to the platform does not use Pad_Gap's JUMP_PAD link
			// (no VeloxBotLink line, base and H5 alike); the bot runs over the pad, is launched off a link and overshoots
			// the platform. Judged once the fixture's link is fixed.
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s report=pad on_platform=%d fell=%d dt=%.2f at=%s pad_ticks=%d links_ok=%d edge_aware=%d"), Ft->Name,
				bOnPlatform ? 1 : 0, bFell ? 1 : 0, Elapsed, *Feet.ToCompactString(), PadTicks, Bot->GetBotState().LinksOk, bEdgeAware ? 1 : 0);
			return false;
		}
		bool bTrialDone = false;
		if (Ft->bKnockOff)
		{
			const bool bOnGround = Movement->IsOnGround();
			Ft->bSeenAirborne |= !bOnGround;
			const bool bLanded = Ft->bSeenAirborne && bOnGround && Feet.Z > -50.0;
			const bool bFell = Feet.Z < FallenBelowCm;
			if (bLanded || bFell || Elapsed > TrialTimeoutSeconds)
			{
				Ft->Landed += bLanded ? 1 : 0;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s trial=%d result=%s dt=%.2f at=%s spot=%s lift=%.0f steered=%d"), Ft->Name, Ft->Trial,
					bLanded ? TEXT("landed") : bFell ? TEXT("fell") : TEXT("timeout"), Elapsed, *Feet.ToCompactString(), *Ft->Spot.ToCompactString(), Ft->Lift,
					Bot->GetBotState().AirRecoveryFlights - Ft->FlightsBefore);
				bTrialDone = true;
			}
		}
		else
		{
			const bool bInLava = Hazards->IsInHazard(Feet + FVector(0.0, 0.0, 1.0), EVeloxHazardType::Lava);
			const bool bOut = !bInLava && Movement->IsOnGround() && Elapsed > 0.05;
			if (bOut || Elapsed > EscapeTimeoutSeconds)
			{
				const bool bInTime = bOut && Elapsed <= EscapeLimitSeconds;
				Ft->EscapedInTime += bInTime ? 1 : 0;
				Ft->WorstEscape = FMath::Max(Ft->WorstEscape, Elapsed);
				Ft->Check(bInTime, TEXT("escape"), FString::Printf(TEXT("trial=%d out=%d dt=%.2f at=%s health=%.0f"), Ft->Trial, bOut ? 1 : 0, Elapsed,
					*Feet.ToCompactString(), HealthOf(*Pawn)));
				bTrialDone = true;
			}
		}
		if (!bTrialDone)
		{
			return true;
		}
		++Ft->Trial;
		if (Ft->Trial >= Ft->NumTrials)
		{
			if (Ft->bKnockOff)
			{
				Ft->Check(Ft->Landed >= MinLanded, TEXT("landed"), FString::Printf(TEXT("landed=%d of %d (need %d) flights_steered=%d"), Ft->Landed, Ft->NumTrials, MinLanded,
					Bot->GetBotState().AirRecoveryFlights));
				// Pad discipline: the bot walks from the main floor to the platform centre over Pad_Gap's JUMP_PAD link
				// with no move input in the flight and still lands on the platform.
				Ft->bPadTrial = true;
				Place(*Pawn, FloorSpot, 0.f);
				Heal(*Pawn);
				Bot->SetForcedPointGoal(PlatformCentre);
				Ft->Phase = EPhase::Run;
				Ft->PhaseStart = Ft->Now();
				Ft->PadTicksBefore = Bot->GetBotState().PadDisciplineTicks;
				return true;
			}
			else
			{
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s summary escaped=%d of %d worst=%.2f escapes=%d"), Ft->Name, Ft->EscapedInTime, Ft->NumTrials,
					Ft->WorstEscape, Bot->GetBotState().HazardEscapes);
			}
			return false;
		}
		Ft->bKnockOff ? BeginKnockTrial(*Ft, *Pawn) : BeginLavaTrial(*Ft, *Pawn);
		return true;
	}

	void Command(const TArray<FString>& Args, UWorld* World, bool bKnockOff)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		AVeloxGameState* GameState = World ? World->GetGameState<AVeloxGameState>() : nullptr;
		const UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(World);
		if (!Bots || !GameState)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtBot%s: needs the authority world of L_HazardTest (-game)."), bKnockOff ? TEXT("KnockOff") : TEXT("LavaEscape"));
			return;
		}
		TSharedRef<FRecoveryFt> Ft = MakeShared<FRecoveryFt>();
		Ft->World = World;
		Ft->bKnockOff = bKnockOff;
		Ft->Name = bKnockOff ? TEXT("FT_BotKnockOff") : TEXT("FT_BotLavaEscape");
		Ft->NumTrials = bKnockOff ? DefaultTrials : UE_ARRAY_COUNT(LavaSpots);
		for (const FString& Arg : Args)
		{
			FString Value;
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				Ft->bQuit = true;
			}
			else if (bKnockOff && Arg.StartsWith(TEXT("trials="), ESearchCase::IgnoreCase) && Arg.Split(TEXT("="), nullptr, &Value))
			{
				Ft->NumTrials = FMath::Max(1, FCString::Atoi(*Value));
			}
		}
		GRecoveryFt = Ft;
		const bool bLava = Hazards && Hazards->HasHazards() && Hazards->IsInHazard(LavaProbe, EVeloxHazardType::Lava);
		Ft->Check(bLava, TEXT("map_hazards"), FString::Printf(TEXT("volumes=%d"), Hazards ? Hazards->GetVolumes().Num() : 0));
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
		TWeakPtr<FRecoveryFt> Weak = Ft;
		Ft->KillHandle = GameState->OnKill.AddLambda([Weak](const FVeloxKillEvent& Event)
		{
			const TSharedPtr<FRecoveryFt> F = Weak.Pin();
			if (F && F->BotPS.IsValid() && Event.Victim.Get() == F->BotPS.Get())
			{
				++F->Deaths;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s death trial=%d tags=%s"), F->Name, F->Trial, *Event.DamageTags.ToStringSimple());
			}
		});
		Bots->CallWhenNavReady([Weak, Bots]()
		{
			const TSharedPtr<FRecoveryFt> F = Weak.Pin();
			if (!F)
			{
				return;
			}
			AVeloxBotController* Bot = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Hard")), TEXT("FtRecoveryBot"));
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
			F->Trial = 0;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRecoveryFt test=%s start bot=%s trials=%d edge_aware=%d"), F->Name, *Bot->GetBotName(), F->NumTrials,
				VeloxBotCombatGlue::IsEdgeAware(F->World.Get()) ? 1 : 0);
			F->bKnockOff ? BeginKnockTrial(*F, *Pawn) : BeginLavaTrial(*F, *Pawn);
			F->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				const TSharedPtr<FRecoveryFt> Inner = Weak.Pin();
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

	FAutoConsoleCommandWithWorldAndArgs LavaCmd(TEXT("velox.bots.FtBotLavaEscape"),
		TEXT("velox.bots.FtBotLavaEscape [quit]: FT_BotLavaEscape on L_HazardTest (-game -nullrhi -UseFixedTimeStep -FPS=60): a bot put into the lava channel at three "
			 "spots is out within 1 s each time (ADR 0008 3.6.4). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Command(Args, World, false); }));

	FAutoConsoleCommandWithWorldAndArgs KnockCmd(TEXT("velox.bots.FtBotKnockOff"),
		TEXT("velox.bots.FtBotKnockOff [quit] [trials=N]: FT_BotKnockOff on L_HazardTest: a bot knocked off the platform edge with 300 cm/s lands back in >= 8 of 10 "
			 "seeded trials (ADR 0008 3.6.3). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Command(Args, World, true); }));
}

#endif // !UE_BUILD_SHIPPING
