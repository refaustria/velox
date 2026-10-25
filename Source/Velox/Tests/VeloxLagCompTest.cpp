// Velox - lag compensation tests (ADR 0003 sections 6 and 9, task 03). World-free.
//   Velox.Combat.LagCompRewind  synthetic history x = 100 t at 60 Hz for 1 s: pose at a world time and at a pawn stamp
//                               (interpolated within 1e-3), clamping to oldest / newest / MaxRewind, hint-by-stamp for
//                               the hinted pawn only with fall-back, pawn at rest, ray hits the rewound capsule and
//                               misses the current one.
//   Velox.Combat.LagCompHits    ray collection: nearest wins, world blocks, rail penetration count, pellets merge per
//                               pawn, hits sorted by distance with ray indices remapped.
//   Velox.Combat.LagCompTrace   private game world: hitboxes register on BeginPlay, the subsystem samples per tick,
//                               TraceShot hits a moving pawn at the rewound place and misses it at the current one,
//                               velox.combat.LagComp 0, world blocks on the Weapon channel, shooter excluded, origin
//                               snap, unregister on EndPlay.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/VeloxCharacter.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Net/VeloxLagCompensationSubsystem.h"

#include <limits>

namespace VeloxLagCompTest
{
	/** Collects log lines containing a marker while in scope. */
	struct FLogCapture : public FOutputDevice
	{
		FString Marker;
		TArray<FString> Lines;
		explicit FLogCapture(const TCHAR* InMarker) : Marker(InMarker) { GLog->AddOutputDevice(this); }
		virtual ~FLogCapture() override { GLog->RemoveOutputDevice(this); }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			if (FCString::Strstr(V, *Marker))
			{
				Lines.Add(V);
			}
		}
	};

	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr double Tol = 1e-3;
	constexpr double Rate = 60.0;
	/** The stamp domain is offset from world time (accumulated client move time of a remote player). */
	constexpr double StampOffset = 3.0;

	FVeloxHitboxPose PoseAt(double X, double HalfHeight = 70.0)
	{
		FVeloxHitboxPose Pose;
		Pose.Shapes.Emplace(FVector(X, 0, 0), 37.5f, static_cast<float>(HalfHeight));
		return Pose;
	}

	/** x = 100 t, half height 70 -> 50 over the second; samples t = i / 60, i = 0..60, into a ring of 60. */
	void MakeMovingHistory(FVeloxHitboxHistory& History)
	{
		History.Init(60);
		for (int32 I = 0; I <= 60; ++I)
		{
			const double T = I / Rate;
			History.Push(T, static_cast<float>(StampOffset + T), PoseAt(100.0 * T, 70.0 - 20.0 * T));
		}
	}

	double X(const FVeloxHitboxPose& Pose) { return Pose.Shapes.Num() > 0 ? Pose.Shapes[0].Centre.X : -1e9; }
	double HH(const FVeloxHitboxPose& Pose) { return Pose.Shapes.Num() > 0 ? Pose.Shapes[0].HalfHeight : -1e9; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatLagCompRewindTest, "Velox.Combat.LagCompRewind", VeloxLagCompTest::Flags)

bool FVeloxCombatLagCompRewindTest::RunTest(const FString& Parameters)
{
	using namespace VeloxLagCompTest;
	FVeloxHitboxHistory H;
	MakeMovingHistory(H);
	FVeloxHitboxPose Pose;
	double PoseTime = 0.0;

	// Ring: 61 pushes into 60 slots drop t = 0.
	TestEqual(TEXT("ring holds capacity"), H.Num(), 60);
	TestNearlyEqual(TEXT("oldest is t = 1/60"), H.Get(0).WorldTime, 1.0 / Rate, Tol);
	TestNearlyEqual(TEXT("newest is t = 1"), H.Newest()->WorldTime, 1.0, Tol);

	// World time, between samples.
	TestTrue(TEXT("pose at world time"), H.PoseAtWorldTime(0.5 + 0.5 / Rate, nullptr, Pose, PoseTime));
	TestNearlyEqual(TEXT("x interpolated at t = 0.5083"), X(Pose), 100.0 * (0.5 + 0.5 / Rate), Tol);
	TestNearlyEqual(TEXT("half height interpolated"), HH(Pose), 70.0 - 20.0 * (0.5 + 0.5 / Rate), Tol);
	TestNearlyEqual(TEXT("pose time = query"), PoseTime, 0.5 + 0.5 / Rate, 1e-9);

	// Clamping to the ends.
	H.PoseAtWorldTime(-1.0, nullptr, Pose, PoseTime);
	TestNearlyEqual(TEXT("older than window -> oldest"), X(Pose), 100.0 / Rate, Tol);
	TestNearlyEqual(TEXT("oldest pose time"), PoseTime, 1.0 / Rate, Tol);
	H.PoseAtWorldTime(2.0, nullptr, Pose, PoseTime);
	TestNearlyEqual(TEXT("newer than latest -> latest"), X(Pose), 100.0, Tol);
	FVeloxHitboxSample Live;
	Live.WorldTime = 1.01;
	Live.PawnStamp = static_cast<float>(StampOffset + 1.01);
	Live.Pose = PoseAt(101.0, 49.8);
	H.PoseAtWorldTime(1.005, &Live, Pose, PoseTime);
	TestNearlyEqual(TEXT("between latest sample and live"), X(Pose), 100.5, Tol);
	H.PoseAtWorldTime(5.0, &Live, Pose, PoseTime);
	TestNearlyEqual(TEXT("newer than latest -> current (live)"), X(Pose), 101.0, Tol);

	// Stamp domain.
	TestTrue(TEXT("pose at stamp"), H.PoseAtStamp(static_cast<float>(StampOffset + 0.25 + 0.5 / Rate), -1e9, nullptr, Pose, PoseTime));
	TestNearlyEqual(TEXT("x at stamp (t = 0.2583)"), X(Pose), 100.0 * (0.25 + 0.5 / Rate), Tol);
	TestNearlyEqual(TEXT("world time of stamp pose"), PoseTime, 0.25 + 0.5 / Rate, 1e-4);
	H.PoseAtStamp(static_cast<float>(StampOffset + 30.0 / Rate), -1e9, nullptr, Pose, PoseTime);
	TestNearlyEqual(TEXT("stamp matching a sample picks it"), X(Pose), 50.0, Tol);
	TestFalse(TEXT("stamp older than history fails"), H.PoseAtStamp(static_cast<float>(StampOffset - 1.0), -1e9, nullptr, Pose, PoseTime));
	TestFalse(TEXT("stamp pose older than MinWorldTime fails"), H.PoseAtStamp(static_cast<float>(StampOffset + 0.3), 0.75, nullptr, Pose, PoseTime));
	TestTrue(TEXT("stamp ahead of live"), H.PoseAtStamp(static_cast<float>(StampOffset + 2.0), 0.75, &Live, Pose, PoseTime));
	TestNearlyEqual(TEXT("stamp ahead -> current (live)"), X(Pose), 101.0, Tol);

	// Shot pose selection (what TraceShot does per pawn). Now = 1, live = the newest sample.
	{
		const FVeloxHitboxSample Current = *H.Newest();
		VeloxLagComp::FPoseQuery Q;
		Q.Now = 1.0;
		Q.MaxRewindSeconds = 0.25f;
		Q.ViewServerTime = 0.9;
		TestEqual(TEXT("unhinted: by world time"), VeloxLagComp::SelectPose(H, &Current, Q, Pose, PoseTime), EVeloxRewindMode::WorldTime);
		TestNearlyEqual(TEXT("unhinted x at 0.9"), X(Pose), 90.0, Tol);

		Q.ViewServerTime = 0.5;
		VeloxLagComp::SelectPose(H, &Current, Q, Pose, PoseTime);
		TestNearlyEqual(TEXT("rewind clamped to MaxRewind (t = 0.75)"), X(Pose), 75.0, Tol);
		TestNearlyEqual(TEXT("clamped pose time"), PoseTime, 0.75, 1e-9);
		Q.ViewServerTime = 1.5;
		VeloxLagComp::SelectPose(H, &Current, Q, Pose, PoseTime);
		TestNearlyEqual(TEXT("future view time clamped to now"), X(Pose), 100.0, Tol);

		// Hint: the hinted pawn by its stamp (t = 0.8), everyone else by world time (0.9).
		Q.ViewServerTime = 0.9;
		Q.bHinted = true;
		Q.HintStamp = static_cast<float>(StampOffset + 0.8);
		TestEqual(TEXT("hinted: by stamp"), VeloxLagComp::SelectPose(H, &Current, Q, Pose, PoseTime), EVeloxRewindMode::Stamp);
		TestNearlyEqual(TEXT("hinted x at stamp 0.8"), X(Pose), 80.0, Tol);
		TestNearlyEqual(TEXT("hinted pose time"), PoseTime, 0.8, 1e-4);
		Q.bHinted = false;
		VeloxLagComp::SelectPose(H, &Current, Q, Pose, PoseTime);
		TestNearlyEqual(TEXT("same query unhinted ignores the stamp"), X(Pose), 90.0, Tol);

		// A stamp beyond MaxRewind falls back to world time.
		Q.bHinted = true;
		Q.HintStamp = static_cast<float>(StampOffset + 0.6);
		TestEqual(TEXT("stale hint falls back"), VeloxLagComp::SelectPose(H, &Current, Q, Pose, PoseTime), EVeloxRewindMode::WorldTime);
		TestNearlyEqual(TEXT("fallback x at 0.9"), X(Pose), 90.0, Tol);

		// velox.combat.LagComp 0.
		Q.bCompensate = false;
		TestEqual(TEXT("compensation off: current"), VeloxLagComp::SelectPose(H, &Current, Q, Pose, PoseTime), EVeloxRewindMode::Current);
		TestNearlyEqual(TEXT("compensation off x"), X(Pose), 100.0, Tol);

		// ADR 0001 "picks the right historical pose": a ray 30 cm in front of the rewound centre (x = 75) hits it and
		// misses the current capsule (x = 100, 55 cm away, radius 37.5).
		Q.bCompensate = true;
		Q.bHinted = false;
		Q.ViewServerTime = 0.75;
		VeloxLagComp::SelectPose(H, &Current, Q, Pose, PoseTime);
		const FVector Origin(45, -1000, 0);
		const FVector Dir(0, 1, 0);
		double Dist;
		int32 Shape;
		TestTrue(TEXT("ray hits the rewound capsule"), FVeloxHitMath::RayPose(Origin, Dir, 5000.0, Pose, Dist, Shape));
		TestNearlyEqual(TEXT("entry distance"), Dist, 1000.0 - FMath::Sqrt(37.5 * 37.5 - 30.0 * 30.0), Tol);
		TestFalse(TEXT("ray misses the current capsule"), FVeloxHitMath::RayPose(Origin, Dir, 5000.0, Current.Pose, Dist, Shape));
	}

	// Pawn at rest, then moving: the engine stamps only transform changes, so a run of equal stamps resolves to the
	// newest sample of the run (recent enough for MaxRewind while the pawn stands).
	{
		FVeloxHitboxHistory R;
		R.Init(60);
		for (int32 I = 0; I < 60; ++I)
		{
			const double T = I / Rate;
			const double Moving = FMath::Max(I - 29, 0) / Rate;
			R.Push(T, static_cast<float>(2.0 + Moving), PoseAt(100.0 * Moving));
		}
		TestTrue(TEXT("rest stamp resolves"), R.PoseAtStamp(2.0f, -1e9, nullptr, Pose, PoseTime));
		TestNearlyEqual(TEXT("rest stamp -> newest sample of the run"), PoseTime, 29.0 / Rate, 1e-4);
		TestNearlyEqual(TEXT("rest pose x"), X(Pose), 0.0, Tol);

		FVeloxHitboxHistory S;
		S.Init(60);
		for (int32 I = 0; I < 60; ++I)
		{
			S.Push(I / Rate, 2.0f, PoseAt(0.0));
		}
		TestTrue(TEXT("standing pawn: old stamp within MaxRewind"), S.PoseAtStamp(2.0f, 59.0 / Rate - 0.25, nullptr, Pose, PoseTime));
		TestNearlyEqual(TEXT("standing pawn pose time is the newest"), PoseTime, 59.0 / Rate, 1e-9);
	}

	// Teleport discontinuity (B16): x = 100 t, except a 1000 cm jump between t = 0.5 and 0.5 + 1/60; a 40 cm step
	// at t = 0.8 is ordinary movement and stays interpolated.
	{
		constexpr float Teleport = 150.f;
		FVeloxHitboxHistory D;
		D.Init(60);
		for (int32 I = 0; I < 60; ++I)
		{
			const double T = I / Rate;
			double Px = 100.0 * T;
			if (T > 0.5 + 0.5 / Rate)
			{
				Px += 1000.0;
			}
			if (T > 0.8 + 0.5 / Rate)
			{
				Px += 40.0;
			}
			D.Push(T, static_cast<float>(StampOffset + T), PoseAt(Px), Teleport);
		}
		const double TJump = 30.0 / Rate; // last pre-jump sample
		TestFalse(TEXT("ordinary sample is no discontinuity"), D.Get(10).bDiscontinuity);
		TestTrue(TEXT("jump sample flagged"), D.Get(31).bDiscontinuity);
		const double Pre = X(D.Get(30).Pose);
		const double Post = X(D.Get(31).Pose);
		TestTrue(TEXT("pre-jump sample is the jump's predecessor"), FMath::IsNearlyEqual(D.Get(30).WorldTime, TJump, 1e-9));
		TestTrue(TEXT("pose at world time before the midpoint -> pre-jump pose"), D.PoseAtWorldTime(TJump + 0.4 / Rate, nullptr, Pose, PoseTime));
		TestNearlyEqual(TEXT("pre-jump pose, not a lerp"), X(Pose), Pre, Tol);
		D.PoseAtWorldTime(TJump + 0.6 / Rate, nullptr, Pose, PoseTime);
		TestNearlyEqual(TEXT("post-jump pose, not a lerp"), X(Pose), Post, Tol);
		TestTrue(TEXT("pose at stamp before the midpoint"), D.PoseAtStamp(static_cast<float>(StampOffset + TJump + 0.4 / Rate), -1e9, nullptr, Pose, PoseTime));
		TestNearlyEqual(TEXT("pre-jump pose by stamp"), X(Pose), Pre, Tol);
		D.PoseAtStamp(static_cast<float>(StampOffset + TJump + 0.6 / Rate), -1e9, nullptr, Pose, PoseTime);
		TestNearlyEqual(TEXT("post-jump pose by stamp"), X(Pose), Post, Tol);
		TestNearlyEqual(TEXT("stamp pose time is the nearer sample's"), PoseTime, D.Get(31).WorldTime, 1e-4);

		// A 40 cm step (index 49: t = 0.8167 + ...) is flagged by nobody and interpolated linearly.
		int32 Step = INDEX_NONE;
		for (int32 I = 1; I < D.Num(); ++I)
		{
			if (I != 31 && X(D.Get(I).Pose) - X(D.Get(I - 1).Pose) > 30.0)
			{
				Step = I;
			}
		}
		if (TestTrue(TEXT("found the 40 cm step"), Step != INDEX_NONE))
		{
			TestFalse(TEXT("40 cm step is no discontinuity"), D.Get(Step).bDiscontinuity);
			const double T0 = D.Get(Step - 1).WorldTime;
			D.PoseAtWorldTime(T0 + 0.5 / Rate, nullptr, Pose, PoseTime);
			TestNearlyEqual(TEXT("40 cm step interpolated"), X(Pose), 0.5 * (X(D.Get(Step - 1).Pose) + X(D.Get(Step).Pose)), Tol);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatLagCompHitsTest, "Velox.Combat.LagCompHits", VeloxLagCompTest::Flags)

bool FVeloxCombatLagCompHitsTest::RunTest(const FString& Parameters)
{
	using namespace VeloxLagCompTest;
	// Distinct actor identities; CollectRayHits never dereferences them.
	AActor* A = GetMutableDefault<AActor>();
	AActor* B = GetMutableDefault<APawn>();
	AActor* C = GetMutableDefault<ACharacter>();
	const FVeloxHitboxPose PA = PoseAt(500), PB = PoseAt(1000), PC = PoseAt(1500);
	const TArray<VeloxLagComp::FCandidate> Candidates = {{C, &PC}, {A, &PA}, {B, &PB}};
	const FVector Origin(0, 0, 0);
	const FVector Dir(1, 0, 0);

	auto Run = [&](double WorldDist, bool bPen, int32 MaxPen, FVeloxShotResult& R) {
		R = FVeloxShotResult();
		FVeloxShotRay& Ray = R.Rays.AddDefaulted_GetRef();
		Ray.End = Origin + Dir * WorldDist;
		Ray.WorldDist = WorldDist;
		Ray.bHitWorld = true;
		VeloxLagComp::CollectRayHits(Origin, Dir, WorldDist, Candidates, bPen, MaxPen, R, Ray);
		VeloxLagComp::SortHits(R);
	};

	FVeloxShotResult R;
	Run(20000, false, 1, R);
	TestEqual(TEXT("nearest only"), R.Hits.Num(), 1);
	TestTrue(TEXT("nearest is A"), R.Hits.Num() == 1 && R.Hits[0].Pawn == A);
	TestNearlyEqual(TEXT("entry distance"), R.Hits.Num() ? R.Hits[0].Dist : -1.0, 462.5, Tol);
	TestNearlyEqual(TEXT("ray stops in the pawn"), R.Rays[0].End.X, 462.5, Tol);
	TestFalse(TEXT("ray not world-stopped"), R.Rays[0].bHitWorld);

	Run(20000, true, 2, R);
	TestEqual(TEXT("rail: MaxPenetrations pawns"), R.Hits.Num(), 2);
	TestTrue(TEXT("rail: A then B"), R.Hits.Num() == 2 && R.Hits[0].Pawn == A && R.Hits[1].Pawn == B);
	TestNearlyEqual(TEXT("rail ray continues to world"), R.Rays[0].End.X, 20000.0, Tol);

	Run(800, true, 4, R);
	TestEqual(TEXT("world at 800 blocks B and C"), R.Hits.Num(), 1);
	Run(400, true, 4, R);
	TestEqual(TEXT("world at 400 blocks all"), R.Hits.Num(), 0);

	// Pellets: three rays, the far pawn C takes two, the near pawn A (off the axis) one; hits come out nearest first.
	{
		FVeloxShotResult P;
		const FVeloxHitboxPose PFar = PoseAt(1500);
		FVeloxHitboxPose PNearOff = PoseAt(500);
		PNearOff.Shapes[0].Centre.Y = 200.0; // only the second pellet reaches the near pawn
		const TArray<VeloxLagComp::FCandidate> Two = {{C, &PFar}, {A, &PNearOff}};
		FVeloxShotRay& Ray1 = P.Rays.AddDefaulted_GetRef();
		VeloxLagComp::CollectRayHits(Origin, Dir, 20000, Two, false, 1, P, Ray1); // far pawn C
		FVeloxShotRay& Ray2 = P.Rays.AddDefaulted_GetRef();
		const FVector Dir2 = FVector(500, 200, 0).GetSafeNormal();
		VeloxLagComp::CollectRayHits(Origin, Dir2, 20000, Two, false, 1, P, Ray2); // near pawn A
		FVeloxShotRay& Ray3 = P.Rays.AddDefaulted_GetRef();
		VeloxLagComp::CollectRayHits(Origin + FVector(0, 10, 0), Dir, 20000, Two, false, 1, P, Ray3); // C again
		VeloxLagComp::SortHits(P);
		TestEqual(TEXT("pellets: two pawns"), P.Hits.Num(), 2);
		if (P.Hits.Num() == 2)
		{
			TestTrue(TEXT("sorted: near A first"), P.Hits[0].Pawn == A && P.Hits[1].Pawn == C);
			TestEqual(TEXT("C took two pellets"), P.Hits[1].PelletHits, 2);
			TestEqual(TEXT("A took one pellet"), P.Hits[0].PelletHits, 1);
			TestTrue(TEXT("ray indices remapped"), P.Rays[0].HitIndices.Num() == 1 && P.Rays[0].HitIndices[0] == 1 && P.Rays[1].HitIndices[0] == 0 && P.Rays[2].HitIndices[0] == 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatLagCompTraceTest, "Velox.Combat.LagCompTrace", VeloxLagCompTest::Flags)

bool FVeloxCombatLagCompTraceTest::RunTest(const FString& Parameters)
{
	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxLagCompTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::LevelTransition);
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	UVeloxLagCompensationSubsystem* LagComp = UVeloxLagCompensationSubsystem::Get(World);
	if (!TestNotNull(TEXT("subsystem exists in a standalone game world"), LagComp))
	{
		return false;
	}

	// Unpossessed characters do not simulate movement: the test teleports the target and the history records it.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVeloxCharacter* Shooter = World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), FVector(0, -1000, 100), FRotator::ZeroRotator, Params);
	AVeloxCharacter* Target = World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("shooter"), Shooter) || !TestNotNull(TEXT("target"), Target))
	{
		return false;
	}
	TestTrue(TEXT("target registered on BeginPlay"), LagComp->IsRegistered(Target));
	TestTrue(TEXT("shooter registered on BeginPlay"), LagComp->IsRegistered(Shooter));

	// 600 cm/s along X at 60 Hz for 0.5 s: x = 10 per frame.
	const float Dt = 1.f / 60.f;
	double TimeAtX200 = -1.0;
	for (int32 Frame = 1; Frame <= 30; ++Frame)
	{
		Target->SetActorLocation(FVector(10.0 * Frame, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Dt);
		if (Frame == 20)
		{
			TimeAtX200 = World->GetTimeSeconds();
		}
	}

	FVeloxHitboxPose Pose;
	TestTrue(TEXT("RewindPoseFor by world time"), LagComp->RewindPoseFor(Target, TimeAtX200, false, Pose));
	TestNearlyEqual(TEXT("rewound x = 200"), Pose.Shapes.Num() ? Pose.Shapes[0].Centre.X : -1.0, 200.0, 1e-2);

	FVeloxShotRequest Request;
	Request.Shooter = Shooter;
	Request.Origin = FVector(200, -1000, 100);
	Request.Dirs.Add(FVector(0, 1, 0));
	Request.bValidateOrigin = false;
	Request.ViewServerTime = TimeAtX200;
	Request.ShotSeq = 1;
	FVeloxShotResult Result;
	LagComp->TraceShot(Request, Result);
	TestEqual(TEXT("rewound shot hits the target"), Result.Hits.Num(), 1);
	TestTrue(TEXT("hit pawn is the target"), Result.Hits.Num() == 1 && Result.Hits[0].Pawn == Target);
	TestNearlyEqual(TEXT("rewind_ms = 10 frames"), static_cast<double>(Result.RewindMs), 10.0 * 1000.0 / 60.0, 1.0);

	Request.ViewServerTime = World->GetTimeSeconds();
	Request.ShotSeq = 2;
	LagComp->TraceShot(Request, Result);
	TestEqual(TEXT("unrewound shot misses (target at x = 300)"), Result.Hits.Num(), 0);

	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.combat.LagComp"));
		if (CVar)
		{
			CVar->Set(0, ECVF_SetByCode);
			Request.ViewServerTime = TimeAtX200;
			Request.ShotSeq = 3;
			LagComp->TraceShot(Request, Result);
			TestEqual(TEXT("velox.combat.LagComp 0: current poses, miss"), Result.Hits.Num(), 0);
			CVar->Set(1, ECVF_SetByCode);
		}
	}

	// The shooter never hits himself: a ray from inside his own capsule.
	Request.Origin = FVector(0, -1000, 100);
	Request.Dirs[0] = FVector(1, 0, 0);
	Request.ShotSeq = 4;
	LagComp->TraceShot(Request, Result);
	TestEqual(TEXT("shooter excluded"), Result.Hits.Num(), 0);

	// Origin farther than MaxOriginErrorCm from the server eye snaps to it.
	Request.bValidateOrigin = true;
	Request.Origin = Shooter->GetPawnViewLocation() + FVector(100, 0, 0);
	Request.ShotSeq = 5;
	LagComp->TraceShot(Request, Result);
	TestTrue(TEXT("origin snapped"), Result.bOriginSnapped);
	TestTrue(TEXT("snapped to the eye"), Result.Origin.Equals(Shooter->GetPawnViewLocation(), 1e-3));
	Request.Origin = Shooter->GetPawnViewLocation() + FVector(50, 0, 0);
	LagComp->TraceShot(Request, Result);
	TestFalse(TEXT("origin within 75 cm kept"), Result.bOriginSnapped);

	// NaN origin and direction (B15 / defect 4), and the view_ms field of the log line (B17).
	{
		const double NaN = std::numeric_limits<double>::quiet_NaN();
		VeloxLagCompTest::FLogCapture Capture(TEXT("VeloxShot seq="));
		FVeloxShotRequest NanRequest = Request;
		NanRequest.Origin = FVector(NaN, 0, 0);
		NanRequest.ShotSeq = 20;
		LagComp->TraceShot(NanRequest, Result);
		TestTrue(TEXT("NaN origin snapped"), Result.bOriginSnapped);
		TestTrue(TEXT("NaN origin snapped to the eye"), Result.Origin.Equals(Shooter->GetPawnViewLocation(), 1e-3));
		TestTrue(TEXT("NaN origin reason"), Result.OriginSnapReason && FCString::Strcmp(Result.OriginSnapReason, TEXT("nan")) == 0);
		TestFalse(TEXT("snapped origin is finite"), Result.Origin.ContainsNaN());

		NanRequest = Request;
		NanRequest.Dirs.Reset();
		NanRequest.Dirs.Add(FVector(0, NaN, 0));
		NanRequest.Dirs.Add(FVector(0, 1, 0));
		NanRequest.ShotSeq = 21;
		LagComp->TraceShot(NanRequest, Result);
		TestEqual(TEXT("NaN direction skipped, the valid ray traced"), Result.Rays.Num(), 1);

		NanRequest = Request;
		NanRequest.ViewServerTime = NaN;
		NanRequest.ShotSeq = 22;
		LagComp->TraceShot(NanRequest, Result);
		TestTrue(TEXT("NaN view time yields a finite view_ms"), FMath::IsFinite(Result.ViewMs) && FMath::IsFinite(Result.RewindMs));

		GLog->FlushThreadedLogs();
		TestTrue(TEXT("a VeloxShot line was logged"), Capture.Lines.Num() > 0);
		bool bAllHaveViewMs = Capture.Lines.Num() > 0;
		for (const FString& Line : Capture.Lines)
		{
			bAllHaveViewMs &= Line.Contains(TEXT("view_ms="));
		}
		TestTrue(TEXT("VeloxShot line contains view_ms="), bAllHaveViewMs);
	}

	// World geometry blocks on the Weapon channel (pawn capsules do not).
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>(FVector(200, -500, 100), FRotator::ZeroRotator);
		Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Request.bValidateOrigin = false;
		Request.Origin = FVector(200, -1000, 100);
		Request.Dirs[0] = FVector(0, 1, 0);
		Request.ViewServerTime = TimeAtX200;
		Request.ShotSeq = 6;
		LagComp->TraceShot(Request, Result);
		TestEqual(TEXT("wall blocks the rewound hit"), Result.Hits.Num(), 0);
		TestTrue(TEXT("ray stopped by the world"), Result.Rays.Num() == 1 && Result.Rays[0].bHitWorld && Result.Rays[0].WorldDist < 500.0);

		// B15: a client origin within tolerance but behind a thin wall between it and the server eye snaps.
		AStaticMeshActor* Thin = World->SpawnActor<AStaticMeshActor>(Shooter->GetPawnViewLocation() + FVector(0, -25, 0), FRotator::ZeroRotator);
		Thin->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Thin->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Thin->SetActorScale3D(FVector(4.0, 0.1, 4.0)); // 10 cm thick, 400 cm wide
		Request.bValidateOrigin = true;
		Request.Origin = Shooter->GetPawnViewLocation() + FVector(0, -50, 0);
		Request.ShotSeq = 7;
		LagComp->TraceShot(Request, Result);
		TestTrue(TEXT("origin behind a wall snapped"), Result.bOriginSnapped);
		TestTrue(TEXT("occluded reason"), Result.OriginSnapReason && FCString::Strcmp(Result.OriginSnapReason, TEXT("occluded")) == 0);
		TestTrue(TEXT("occluded snap -> eye"), Result.Origin.Equals(Shooter->GetPawnViewLocation(), 1e-3));
		Request.Origin = Shooter->GetPawnViewLocation() + FVector(50, 0, 0);
		Request.ShotSeq = 8;
		LagComp->TraceShot(Request, Result);
		TestFalse(TEXT("unobstructed origin 50 cm off is kept"), Result.bOriginSnapped);
		Thin->Destroy();
	}

	Target->Destroy();
	TestFalse(TEXT("unregistered on EndPlay"), LagComp->IsRegistered(Target));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
