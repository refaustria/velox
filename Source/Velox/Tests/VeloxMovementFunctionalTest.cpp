// Velox - scripted movement tests (ADR 0002 section 7 FT_MovementDeterminism and the task-02 review checks A9/A12/A13, task 05).
//
// Headless and deterministic: each test builds a private game world (no PIE, no map load), spawns a real AVeloxCharacter
// controlled by an AI controller (locally controlled, authority) and drives it with UVeloxScriptedInputComponent while
// stepping the world with a fixed dt of 1/120 s. The geometry is built from engine cubes with the dimensions of
// Tools/editor/create_movement_test_map.py (corridor, stairs, ramps), with the ramps pitched along +X as the map's design
// intends. L_MovementTest itself is not loaded: it is already loaded in the editor world during automation runs (and
// PIE/real-time ticking is not deterministic); the map is exercised by `velox.move.RunScript` in -game.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Character/VeloxCharacter.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Movement/VeloxMoveDebug.h"
#include "Movement/VeloxMovementComponent.h"
#include "Movement/VeloxMovementSettings.h"
#include "Movement/VeloxScriptedInputComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace VeloxScriptedTest
{
	/** Fixed world step (the net soak runs the client at 120 fps). */
	constexpr float TickDt = 1.f / 120.f;

	/** Seconds the freshly spawned pawn gets to settle onto the ground before a script starts. */
	constexpr float SettleSeconds = 0.5f;

	struct FSample
	{
		float ScriptTime = 0.f;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		bool bOnGround = false;
		bool bLatched = false;
		float Grace = 0.f;
		float Speed2D() const { return static_cast<float>(Velocity.Size2D()); }
	};

	/** A private game world with a character; destroys everything in the destructor. */
	class FTestWorld
	{
	public:
		FTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxScriptedTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay();
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		}

		~FTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::LevelTransition);
				GEngine->ShutdownWorldNetDriver(World);
				World->DestroyWorld(true);
				World->SetPhysicsScene(nullptr);
				GEngine->DestroyWorldContext(World);
				World->RemoveFromRoot();
				World = nullptr;
			}
		}

		bool IsValid() const { return World && Cube; }

		void Tick()
		{
			// The tick manager runs each tick function at most once per engine frame; advance the counter ourselves.
			++GFrameCounter;
			World->Tick(LEVELTICK_All, TickDt);
		}

		/** Solid box, size = full extents in cm (the engine cube is 100 cm). */
		void AddBox(const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Center, Rotation);
			Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Actor->SetActorScale3D(Size / 100.0);
		}

		/** Floor with its top at Z = 0 and the corridor of the movement test map (x 0..5120, y -500..500, 250 high walls). */
		void BuildFloorAndCorridor()
		{
			AddBox(FVector(2700, 1400, -50), FVector(6600, 11200, 100));
			AddBox(FVector(2560, -550, 125), FVector(5320, 100, 250));
			AddBox(FVector(2560, 550, 125), FVector(5320, 100, 250));
			AddBox(FVector(-50, 0, 125), FVector(100, 1000, 250));
			AddBox(FVector(5170, 0, 125), FVector(100, 1000, 250));
		}

		/** 12 steps of 80 cm tread starting at x = 100, riser in cm, centred on lane y (map: y = 4700 / 5300 / 5900). */
		void BuildStairs(float Riser, float Y)
		{
			for (int32 Step = 0; Step < 12; ++Step)
			{
				const float Height = (Step + 1) * Riser;
				AddBox(FVector(100.f + Step * 80.f + 40.f, Y, Height / 2.f), FVector(80, 400, Height));
			}
		}

		/** Ramp of 1600 cm slope length rising along +X from (0, y, 0), then a 400 cm platform (map: y = 1000 + 700 * lane). */
		void BuildRamp(float Degrees, float Y)
		{
			const float Thickness = 20.f;
			const float Rad = FMath::DegreesToRadians(Degrees);
			const float Length = 1600.f;
			const float Run = Length * FMath::Cos(Rad);
			const float Rise = Length * FMath::Sin(Rad);
			AddBox(FVector(Length / 2 * FMath::Cos(Rad) + Thickness / 2 * FMath::Sin(Rad), Y, Length / 2 * FMath::Sin(Rad) - Thickness / 2 * FMath::Cos(Rad)),
				FVector(Length, 600, Thickness), FRotator(Degrees, 0.0, 0.0));
			AddBox(FVector(Run + 200.f, Y, Rise / 2.f), FVector(400, 600, Rise));
		}

		AVeloxCharacter* SpawnPawn(const FVector& Location, float Yaw)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVeloxCharacter* Character = World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), Location, FRotator(0.0, Yaw, 0.0), Params);
			if (Character)
			{
				AAIController* Controller = World->SpawnActor<AAIController>();
				Controller->Possess(Character);
			}
			return Character;
		}

		UWorld* World = nullptr;
		UStaticMesh* Cube = nullptr;
	};

	/** Ticks until the pawn stands on the ground (at most SettleSeconds, then a few more ticks to rest). */
	bool Settle(FTestWorld& TW, const AVeloxCharacter& Character)
	{
		const UVeloxMovementComponent* Movement = Character.GetVeloxMovementComponent();
		for (int32 I = 0; I < FMath::RoundToInt(SettleSeconds / TickDt); ++I)
		{
			TW.Tick();
		}
		return Movement && Movement->IsOnGround();
	}

	struct FRun
	{
		TArray<FSample> Samples;
		FVeloxScriptResult Result;
		bool bStarted = false;
	};

	/**
	 * Loads Json (or the named file when Json is empty), teleports to its start location, settles, runs it to the end.
	 * Settings, if given, replace the pawn's movement settings before the run (same on every call of one test).
	 */
	FRun RunScript(FTestWorld& TW, AVeloxCharacter& Character, const FString& JsonOrFileName, bool bIsFile)
	{
		FRun Run;
		UVeloxScriptedInputComponent* Scripted = Character.FindComponentByClass<UVeloxScriptedInputComponent>();
		UVeloxMovementComponent* Movement = Character.GetVeloxMovementComponent();
		if (!Scripted || !Movement)
		{
			return Run;
		}
		const bool bLoaded = bIsFile ? Scripted->LoadScriptFromFile(JsonOrFileName) : Scripted->LoadScriptFromJson(JsonOrFileName, TEXT("test"));
		if (!bLoaded)
		{
			return Run;
		}
		if (Scripted->NeedsRepositioning())
		{
			Scripted->RepositionToStart();
		}
		if (!Settle(TW, Character) || !Scripted->StartScript())
		{
			return Run;
		}
		Run.bStarted = true;
		const int32 MaxTicks = FMath::CeilToInt((Scripted->GetScript().TotalDuration() + 1.f) / TickDt);
		Scripted->SetDrivenExternally(true);
		for (int32 I = 0; I < MaxTicks && Scripted->IsRunning(); ++I)
		{
			Scripted->StepScript(TickDt);
			TW.Tick();
			FSample Sample;
			Sample.ScriptTime = Scripted->GetScriptTime();
			Sample.Location = Character.GetActorLocation();
			Sample.Velocity = Movement->Velocity;
			Sample.bOnGround = Movement->IsOnGround();
			Sample.bLatched = Movement->IsJumpLatched();
			Sample.Grace = Movement->GetKnockbackGrace();
			Run.Samples.Add(Sample);
		}
		Run.Result = Scripted->GetResult();
		return Run;
	}

	float PeakSpeed2D(const FRun& Run)
	{
		float Peak = 0.f;
		for (const FSample& S : Run.Samples)
		{
			Peak = FMath::Max(Peak, S.Speed2D());
		}
		return Peak;
	}

	FString RowJson(float Duration, float Forward, float Right, bool bJump, const FString& Extra = FString())
	{
		return FString::Printf(TEXT("{\"duration\":%g,\"forward\":%g,\"right\":%g,\"jump\":%s%s}"), Duration, Forward, Right, bJump ? TEXT("true") : TEXT("false"), *Extra);
	}

	/** Default test fixture: no cvar overrides leak in from the environment. */
	struct FNoOverrides
	{
		FNoOverrides() { VeloxMoveDebug::ClearOverrides(); }
	};
}

// ---------------------------------------------------------------------------------------------------------------------
// FT_MovementDeterminism

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxFTMovementDeterminism, "Velox.Functional.FT_MovementDeterminism",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxFTMovementDeterminism::RunTest(const FString& Parameters)
{
	using namespace VeloxScriptedTest;
	FNoOverrides NoOverrides;

	// The same script, twice, each in a fresh world from the same spawn state: every sample must be bit-identical.
	TArray<FRun> Runs;
	float GroundSpeed = 0.f;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		FTestWorld TW;
		if (!TestTrue(TEXT("test world created"), TW.IsValid()))
		{
			return false;
		}
		TW.BuildFloorAndCorridor();
		AVeloxCharacter* Character = TW.SpawnPawn(FVector(40, 0, 100), 0.f);
		if (!TestNotNull(TEXT("character spawned"), Character))
		{
			return false;
		}
		const UVeloxMovementComponent* Movement = Character->GetVeloxMovementComponent();
		TestTrue(TEXT("movement settings applied"), Movement && Movement->GetMovementSettings() != nullptr);
		GroundSpeed = Movement ? Movement->GetSimParams().MaxGroundSpeed : 0.f;
		Runs.Add(RunScript(TW, *Character, TEXT("StrafeRun"), true));
		if (!TestTrue(FString::Printf(TEXT("pass %d: StrafeRun started"), Pass), Runs.Last().bStarted))
		{
			return false;
		}
	}

	const FRun& A = Runs[0];
	const FRun& B = Runs[1];
	TestEqual(TEXT("same number of ticks"), A.Samples.Num(), B.Samples.Num());
	int32 NumDifferent = 0;
	for (int32 I = 0; I < FMath::Min(A.Samples.Num(), B.Samples.Num()); ++I)
	{
		if (A.Samples[I].Location != B.Samples[I].Location || A.Samples[I].Velocity != B.Samples[I].Velocity)
		{
			if (NumDifferent++ == 0)
			{
				AddError(FString::Printf(TEXT("first divergence at tick %d: %s / %s vs %s / %s"), I, *A.Samples[I].Location.ToString(), *A.Samples[I].Velocity.ToString(),
					*B.Samples[I].Location.ToString(), *B.Samples[I].Velocity.ToString()));
			}
		}
	}
	TestEqual(TEXT("identical state on every tick"), NumDifferent, 0);
	if (A.Samples.Num() > 0 && B.Samples.Num() > 0)
	{
		TestTrue(TEXT("identical end position"), A.Samples.Last().Location == B.Samples.Last().Location);
		TestTrue(TEXT("identical end velocity"), A.Samples.Last().Velocity == B.Samples.Last().Velocity);
	}

	// Strafe jumping works in the real component. The eventual target is 1.4 x ground speed (ADR 0002 section 7); the
	// check here is only that there is a gain, the measured number goes to the log.
	const float Peak = PeakSpeed2D(A);
	if (A.Samples.Num() > 0)
	{
		AddInfo(FString::Printf(TEXT("first %s / %s ground %d, last %s / %s ground %d, result %s"), *A.Samples[0].Location.ToString(), *A.Samples[0].Velocity.ToString(), A.Samples[0].bOnGround,
			*A.Samples.Last().Location.ToString(), *A.Samples.Last().Velocity.ToString(), A.Samples.Last().bOnGround, *A.Result.Reason));
	}
	AddInfo(FString::Printf(TEXT("StrafeRun peak speed %.0f cm/s = %.2f x ground speed %.0f (%d ticks)"), Peak, GroundSpeed > 0.f ? Peak / GroundSpeed : 0.f, GroundSpeed, A.Samples.Num()));
	TestTrue(TEXT("StrafeRun gains speed beyond the ground speed"), Peak > GroundSpeed * 1.05f);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stairs (A13)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxScriptedStairsTest, "Velox.Movement.Scripted.Stairs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxScriptedStairsTest::RunTest(const FString& Parameters)
{
	using namespace VeloxScriptedTest;
	FNoOverrides NoOverrides;

	struct FFlight { float Riser; float Y; };
	for (const FFlight& Flight : { FFlight{ 20.f, 4700.f }, FFlight{ 40.f, 5300.f }, FFlight{ 60.f, 5900.f } })
	{
		FTestWorld TW;
		if (!TestTrue(TEXT("test world created"), TW.IsValid()))
		{
			return false;
		}
		TW.BuildFloorAndCorridor();
		TW.BuildStairs(Flight.Riser, Flight.Y);
		AVeloxCharacter* Character = TW.SpawnPawn(FVector(-300, Flight.Y, 100), 0.f);
		if (!TestNotNull(TEXT("character spawned"), Character))
		{
			return false;
		}
		const FString Json = FString::Printf(TEXT("{\"startYaw\":0,\"startLocation\":[-300,%g,100],\"rows\":[%s]}"), Flight.Y, *RowJson(2.2f, 1.f, 0.f, false));
		const FRun Run = RunScript(TW, *Character, Json, false);
		const FString Ctx = FString::Printf(TEXT("riser %.0f: "), Flight.Riser);
		if (!TestTrue(Ctx + TEXT("script ran"), Run.bStarted && Run.Samples.Num() > 0))
		{
			continue;
		}

		// Reference speed on the flat before the first step (x in 40..90), then the speed while climbing (x 120..1050): the
		// mean over the flight (the metric: every step-up costs a one-tick dip that the next accelerate refills) and the
		// slowest sample (information).
		float SpeedBefore = 0.f;
		float SlowestOnStairs = TNumericLimits<float>::Max();
		double SumOnStairs = 0.0;
		int32 NumOnStairs = 0;
		for (const FSample& S : Run.Samples)
		{
			if (S.Location.X > 40.f && S.Location.X < 90.f)
			{
				SpeedBefore = S.Speed2D();
			}
			if (S.Location.X > 120.f && S.Location.X < 1050.f)
			{
				SlowestOnStairs = FMath::Min(SlowestOnStairs, S.Speed2D());
				SumOnStairs += S.Speed2D();
				++NumOnStairs;
			}
		}
		const float MeanOnStairs = NumOnStairs > 0 ? static_cast<float>(SumOnStairs / NumOnStairs) : 0.f;

		if (Flight.Riser <= 40.f)
		{
			TestTrue(Ctx + TEXT("ran at full speed before the stairs"), SpeedBefore >= 790.f);
			AddInfo(FString::Printf(TEXT("%.0f cm risers: %.1f cm/s before, mean on the flight %.1f (%.1f %%), slowest sample %.1f (%.1f %%)"), Flight.Riser, SpeedBefore, MeanOnStairs,
				100.f * MeanOnStairs / FMath::Max(SpeedBefore, 1.f), SlowestOnStairs, 100.f * SlowestOnStairs / FMath::Max(SpeedBefore, 1.f)));
			TestTrue(Ctx + TEXT("keeps at least 98 % of the speed (mean over the flight)"), MeanOnStairs >= 0.98f * SpeedBefore);
			TestTrue(Ctx + TEXT("reaches the top of the flight"), Run.Samples.Last().Location.X > 1000.f);
		}
		else
		{
			const FSample& End = Run.Samples.Last();
			TestTrue(Ctx + TEXT("is stopped by the first step"), End.Location.X < 100.f && End.Speed2D() < 1.f);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Wall slide (A13)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxScriptedWallSlideTest, "Velox.Movement.Scripted.WallSlide",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxScriptedWallSlideTest::RunTest(const FString& Parameters)
{
	using namespace VeloxScriptedTest;
	FNoOverrides NoOverrides;

	FTestWorld TW;
	if (!TestTrue(TEXT("test world created"), TW.IsValid()))
	{
		return false;
	}
	TW.BuildFloorAndCorridor();
	AVeloxCharacter* Character = TW.SpawnPawn(FVector(500, 300, 100), 45.f);
	if (!TestNotNull(TEXT("character spawned"), Character))
	{
		return false;
	}
	const auto& Sim = Character->GetVeloxMovementComponent()->GetSimParams();
	const float GroundSpeed = Sim.MaxGroundSpeed;
	const FString Json = FString::Printf(TEXT("{\"startYaw\":45,\"startLocation\":[500,300,100],\"rows\":[%s]}"), *RowJson(1.5f, 1.f, 0.f, false));
	const FRun Run = RunScript(TW, *Character, Json, false);
	if (!TestTrue(TEXT("script ran"), Run.bStarted && Run.Samples.Num() > 0))
	{
		return false;
	}

	const FSample& End = Run.Samples.Last();
	// ADR 0002 A23: steady state of the Q3 ground model against a wall at 45 degrees is a*W*cos45/f, bounded by the
	// accelerate clamp W/cos45 (about 942.8 cm/s for the defaults).
	const float Cos45 = FMath::Cos(FMath::DegreesToRadians(45.f));
	const float Expected = FMath::Min(Sim.GroundAcceleration * GroundSpeed * Cos45 / Sim.GroundFriction, GroundSpeed / Cos45);
	const float Measured = End.Speed2D();
	AddInfo(FString::Printf(TEXT("45 degree wall slide: measured %.1f cm/s, expected %.1f cm/s (ADR 0002 A23), deviation %+.2f %%, y %.1f"), Measured, Expected, 100.f * (Measured - Expected) / Expected, End.Location.Y));
	TestTrue(TEXT("pressed against the wall (y near 500 - radius)"), End.Location.Y > 440.f && End.Location.Y < 465.f);
	TestTrue(TEXT("velocity is along the wall (clipped, not just blocked)"), FMath::Abs(End.Velocity.Y) < 1.f);
	TestTrue(TEXT("faster than ground speed (Q3 wall-hug)"), Measured > GroundSpeed);
	TestTrue(TEXT("within -10 %/+1 % of the A23 steady state"), Measured >= 0.90f * Expected && Measured <= 1.01f * Expected);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ramp exit (A9)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxScriptedRampExitTest, "Velox.Movement.Scripted.RampExit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxScriptedRampExitTest::RunTest(const FString& Parameters)
{
	using namespace VeloxScriptedTest;
	FNoOverrides NoOverrides;

	struct FRamp { float Degrees; float Y; };
	for (const FRamp& Ramp : { FRamp{ 20.f, 1700.f }, FRamp{ 30.f, 2400.f } })
	{
		FTestWorld TW;
		if (!TestTrue(TEXT("test world created"), TW.IsValid()))
		{
			return false;
		}
		TW.BuildFloorAndCorridor();
		TW.BuildRamp(Ramp.Degrees, Ramp.Y);
		AVeloxCharacter* Character = TW.SpawnPawn(FVector(-300, Ramp.Y, 100), 0.f);
		if (!TestNotNull(TEXT("character spawned"), Character))
		{
			return false;
		}
		const float GroundSpeed = Character->GetVeloxMovementComponent()->GetSimParams().MaxGroundSpeed;
		const FString Json = FString::Printf(TEXT("{\"startYaw\":0,\"startLocation\":[-300,%g,100],\"rows\":[%s]}"), Ramp.Y, *RowJson(3.5f, 1.f, 0.f, false));
		const FRun Run = RunScript(TW, *Character, Json, false);
		const FString Ctx = FString::Printf(TEXT("%.0f deg ramp: "), Ramp.Degrees);
		if (!TestTrue(Ctx + TEXT("script ran"), Run.bStarted && Run.Samples.Num() > 0))
		{
			continue;
		}

		const float Rad = FMath::DegreesToRadians(Ramp.Degrees);
		const float Run_ = 1600.f * FMath::Cos(Rad);
		const float Rise = 1600.f * FMath::Sin(Rad);

		// First airborne sample after the pawn has climbed most of the ramp: the exit.
		const FSample* Exit = nullptr;
		for (const FSample& S : Run.Samples)
		{
			if (S.Location.X > 0.6f * Run_ && !S.bOnGround)
			{
				Exit = &S;
				break;
			}
		}
		if (!TestNotNull(Ctx + TEXT("leaves the ground at the top"), Exit))
		{
			continue;
		}
		const float ExpectedVz = GroundSpeed * FMath::Tan(Rad);
		AddInfo(FString::Printf(TEXT("%s exit at x %.0f, Vz %.1f (expected %.1f), speed %.1f"), *Ctx, Exit->Location.X, Exit->Velocity.Z, ExpectedVz, Exit->Speed2D()));
		TestTrue(Ctx + TEXT("exits near the ramp top"), FMath::Abs(Exit->Location.X - Run_) < 150.f);
		TestTrue(Ctx + TEXT("Vz = speed * tan(angle) +-10 %"), FMath::IsNearlyEqual(Exit->Velocity.Z, ExpectedVz, 0.1f * ExpectedVz));

		// Lands on the 400 cm platform (standing height Rise + 72) after the exit; later it runs off the far end.
		bool bLandedOnPlatform = false;
		for (const FSample& S : Run.Samples)
		{
			bLandedOnPlatform |= S.bOnGround && S.Location.X > Run_ + 10.f && S.Location.X < Run_ + 400.f && FMath::Abs(S.Location.Z - (Rise + 72.f)) < 5.f;
		}
		TestTrue(Ctx + TEXT("lands on the platform"), bLandedOnPlatform);
	}

	// Running across the 10 degree ramp sideways never leaves the ground.
	{
		FTestWorld TW;
		if (!TestTrue(TEXT("test world created"), TW.IsValid()))
		{
			return false;
		}
		TW.BuildFloorAndCorridor();
		TW.BuildRamp(10.f, 1000.f);
		const float RampZ = 800.f * FMath::Tan(FMath::DegreesToRadians(10.f));
		AVeloxCharacter* Character = TW.SpawnPawn(FVector(800, 1000, RampZ + 75.f), 90.f);
		if (!TestNotNull(TEXT("character spawned"), Character))
		{
			return false;
		}
		const FString Json = FString::Printf(TEXT("{\"startYaw\":90,\"startLocation\":[800,1000,%g],\"rows\":[%s]}"), RampZ + 75.f, *RowJson(0.3f, 1.f, 0.f, false));
		const FRun Run = RunScript(TW, *Character, Json, false);
		if (TestTrue(TEXT("sideways: script ran"), Run.bStarted && Run.Samples.Num() > 0))
		{
			int32 NumAirborne = 0;
			for (const FSample& S : Run.Samples)
			{
				NumAirborne += S.bOnGround ? 0 : 1;
			}
			TestEqual(TEXT("sideways on the 10 degree ramp: airborne samples"), NumAirborne, 0);
			TestTrue(TEXT("sideways on the 10 degree ramp: actually moved"), Run.Samples.Last().Location.Y > 1100.f);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Jump latch (A12), VQ3 and auto-hop

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxScriptedJumpLatchTest, "Velox.Movement.Scripted.JumpLatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxScriptedJumpLatchTest::RunTest(const FString& Parameters)
{
	using namespace VeloxScriptedTest;
	FNoOverrides NoOverrides;

	for (const bool bAutoHop : { false, true })
	{
		FTestWorld TW;
		if (!TestTrue(TEXT("test world created"), TW.IsValid()))
		{
			return false;
		}
		TW.BuildFloorAndCorridor();
		AVeloxCharacter* Character = TW.SpawnPawn(FVector(40, 0, 100), 0.f);
		if (!TestNotNull(TEXT("character spawned"), Character))
		{
			return false;
		}
		// Same asset values as the default settings, only the auto-hop flag differs (VQ3 = off).
		TStrongObjectPtr<UVeloxMovementSettings> Settings(NewObject<UVeloxMovementSettings>(GetTransientPackage()));
		Settings->bAutoHop = bAutoHop;
		UVeloxMovementComponent* Movement = Character->GetVeloxMovementComponent();
		Movement->SetMovementSettings(Settings.Get());
		const float JumpVelocity = Movement->GetSimParams().JumpVelocity;
		const float Gravity = -Movement->GetSimParams().GravityZ;

		// LatchRun.json: take-off, release, press again mid-air and hold (with a mid-air knockback), through two landings.
		const FRun Run = RunScript(TW, *Character, TEXT("LatchRun"), true);
		const FString Ctx = bAutoHop ? TEXT("auto-hop: ") : TEXT("VQ3: ");
		if (!TestTrue(Ctx + TEXT("script ran"), Run.bStarted && Run.Samples.Num() > 0))
		{
			continue;
		}

		// A landing jump is a sample where the pawn was descending in the previous sample and rises now without ever
		// being reported on the ground (the jump happens in the landing move).
		TArray<int32> LandingJumps;
		TArray<int32> GroundedLandings;
		for (int32 I = 1; I < Run.Samples.Num(); ++I)
		{
			const FSample& Prev = Run.Samples[I - 1];
			const FSample& Cur = Run.Samples[I];
			if (Prev.Velocity.Z < -1.f && !Prev.bOnGround && Cur.Velocity.Z > 0.5f * JumpVelocity)
			{
				LandingJumps.Add(I);
			}
			else if (Prev.Velocity.Z < -1.f && !Prev.bOnGround && Cur.bOnGround)
			{
				GroundedLandings.Add(I);
			}
		}
		AddInfo(FString::Printf(TEXT("%slanding jumps %d, grounded landings %d"), *Ctx, LandingJumps.Num(), GroundedLandings.Num()));

		if (!TestTrue(Ctx + TEXT("the pre-pressed landing jumps"), LandingJumps.Num() >= 1))
		{
			continue;
		}
		const int32 First = LandingJumps[0];
		const FSample& Before = Run.Samples[First - 1];
		const FSample& After = Run.Samples[First];
		TestTrue(Ctx + TEXT("Vz after the landing move is JumpVelocity - g * remainder"), After.Velocity.Z <= JumpVelocity && After.Velocity.Z > JumpVelocity - 2.f * Gravity * TickDt);
		TestTrue(Ctx + TEXT("XY speed unchanged (+-1 %) by the landing jump"), FMath::IsNearlyEqual(After.Speed2D(), Before.Speed2D(), 0.01f * Before.Speed2D()));

		if (!bAutoHop)
		{
			// Jump held continuously through the next landing: the latch is set by the landing jump, so no jump.
			TestEqual(Ctx + TEXT("exactly one landing jump"), LandingJumps.Num(), 1);
			if (TestTrue(Ctx + TEXT("second landing happens on the ground"), GroundedLandings.Num() >= 1))
			{
				const int32 Land = GroundedLandings.Last();
				bool bStayedDown = true;
				for (int32 I = Land; I < FMath::Min(Land + 36, Run.Samples.Num()); ++I)
				{
					bStayedDown &= Run.Samples[I].bOnGround && Run.Samples[I].Velocity.Z < 1.f;
				}
				TestTrue(Ctx + TEXT("no jump for 0.3 s while the button stays held"), bStayedDown);
				TestTrue(Ctx + TEXT("latch is set while held through the landing"), Run.Samples[Land].bLatched);
			}
		}
		else
		{
			TestTrue(Ctx + TEXT("holding re-jumps on every landing"), LandingJumps.Num() >= 2);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Knockback script

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxScriptedKnockbackTest, "Velox.Movement.Scripted.Knockback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxScriptedKnockbackTest::RunTest(const FString& Parameters)
{
	using namespace VeloxScriptedTest;
	FNoOverrides NoOverrides;

	FTestWorld TW;
	if (!TestTrue(TEXT("test world created"), TW.IsValid()))
	{
		return false;
	}
	TW.BuildFloorAndCorridor();
	AVeloxCharacter* Character = TW.SpawnPawn(FVector(40, 0, 100), 0.f);
	if (!TestNotNull(TEXT("character spawned"), Character))
	{
		return false;
	}
	const FRun Run = RunScript(TW, *Character, TEXT("KnockbackRun"), true);
	if (!TestTrue(TEXT("script ran"), Run.bStarted && Run.Samples.Num() > 0))
	{
		return false;
	}

	// KnockbackRun: stand 1 s, then (1250, 0, 600).
	float SpeedAfter = 0.f;
	float PeakGrace = 0.f;
	bool bLeftGround = false;
	for (const FSample& S : Run.Samples)
	{
		PeakGrace = FMath::Max(PeakGrace, S.Grace);
		bLeftGround |= !S.bOnGround;
		if (S.ScriptTime >= 1.15f && SpeedAfter == 0.f)
		{
			SpeedAfter = S.Speed2D();
		}
	}
	TestTrue(TEXT("knocked into the air"), bLeftGround);
	TestTrue(TEXT("grace > 0.1 s after a 1250 cm/s knockback"), PeakGrace > 0.1f);
	TestTrue(TEXT("0.15 s after the knockback XY speed >= 0.9 x 1250 (no ground friction)"), SpeedAfter >= 0.9f * 1250.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
