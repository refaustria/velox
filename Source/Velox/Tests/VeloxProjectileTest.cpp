// Velox - projectile tests (ADR 0003 sections 7 and 9, task 05).
//   Velox.Combat.ProjectileSim       world-free: deterministic constant-velocity advance, forward simulation stopping at
//                                    a wall, lifetime end, owner never direct-hit, direct victim excluded from splash,
//                                    Q3 radius points incl. the zero-point edge (B3), three-point LOS, self half damage
//                                    and the self-knockback vector, forward-sim window.
//   Velox.Combat.ProjectileDummy     private game world (authority path, the real ability and projectile): RL direct hit
//                                    on a dummy (100, no splash on the direct victim), RL splash next to a dummy (Q3
//                                    points), PG direct hit, and an offline rocket jump (fire at the feet + jump: apex
//                                    >= 2 x jump height, self damage = points / 2).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Bots/VeloxDummyController.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxDamageMath.h"
#include "Combat/VeloxHealthSet.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/VeloxPlayerState.h"
#include "Movement/VeloxMovementComponent.h"
#include "Weapons/VeloxGameplayAbility_FireProjectile.h"
#include "Weapons/VeloxProjectile.h"
#include "Weapons/VeloxProjectileSim.h"
#include "Weapons/VeloxWeaponCheats.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"
#include "Weapons/VeloxWeaponTable.h"

namespace VeloxProjectileTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	/** Opaque keys for world-free targets (never dereferenced). */
	AActor* Key(uintptr_t Id) { return reinterpret_cast<AActor*>(Id * 64); }

	/** A Velox capsule standing with its bottom on z = 0 at XY. */
	FVeloxHitboxPose Capsule(double X, double Y, float Radius = 37.5f, float HalfHeight = 70.f)
	{
		FVeloxHitboxPose Pose;
		Pose.Shapes.Add(FVeloxHitShape(FVector(X, Y, HalfHeight), Radius, HalfHeight));
		return Pose;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatProjectileSimTest, "Velox.Combat.ProjectileSim", VeloxProjectileTest::Flags)

bool FVeloxCombatProjectileSimTest::RunTest(const FString& Parameters)
{
	using namespace VeloxProjectileTest;

	constexpr float Speed = 2250.f;
	constexpr float Radius = 2.f;
	constexpr float Lifetime = 15.f;
	auto NoWorld = [](const FVector&, const FVector&, double&, FVector&) { return false; };
	// A wall facing -X at x = 1000: the sphere centre stops Radius in front of it.
	constexpr double WallX = 1000.0;
	auto Wall = [](const FVector& Start, const FVector& End, double& OutDist, FVector& OutNormal)
	{
		const double StopX = WallX - Radius;
		if (End.X < StopX || Start.X > StopX || FMath::IsNearlyEqual(End.X, Start.X))
		{
			return false;
		}
		OutDist = (End - Start).Size() * (StopX - Start.X) / (End.X - Start.X);
		OutNormal = FVector(-1.0, 0.0, 0.0);
		return true;
	};
	const TArray<FVeloxProjectileTarget> NoTargets;

	// Deterministic, partition-independent constant-velocity advance (no gravity, Q3).
	{
		FVeloxProjectileState A, B;
		A.Velocity = B.Velocity = FVector(Speed, 0.0, 0.0);
		FVeloxProjectileImpact Impact;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			FVeloxProjectileSim::Step(A, 1.f / 120.f, Lifetime, Radius, NoWorld, NoTargets, nullptr, Impact);
		}
		for (int32 Index = 0; Index < 6; ++Index)
		{
			FVeloxProjectileSim::Step(B, 1.f / 60.f, Lifetime, Radius, NoWorld, NoTargets, nullptr, Impact);
		}
		TestTrue(TEXT("advance: 12 x 1/120 == 6 x 1/60"), A.Location.Equals(B.Location, 1e-3));
		TestTrue(TEXT("advance: dead reckoning matches"), A.Location.Equals(FVeloxProjectileSim::PositionAt(FVector::ZeroVector, A.Velocity, 0.1), 1e-3));
		TestEqual(TEXT("advance: no gravity"), A.Location.Z, 0.0);
	}

	// Forward simulation as a sweep: far from the wall nothing happens; within the window the shot stops at the wall.
	{
		FVeloxProjectileState S;
		S.Velocity = FVector(Speed, 0.0, 0.0);
		FVeloxProjectileImpact Impact;
		const float Fwd = FVeloxProjectileSim::ForwardSimSeconds(200.f, 0.1f);
		TestFalse(TEXT("fwd sim: 225 cm from x=0 stays clear of the wall"), FVeloxProjectileSim::Step(S, Fwd, Lifetime, Radius, Wall, NoTargets, nullptr, Impact));
		TestTrue(TEXT("fwd sim: 225 cm advanced"), FMath::IsNearlyEqual(S.Location.X, 225.0, 1e-3));
		S = FVeloxProjectileState();
		S.Location = FVector(900.0, 0.0, 0.0);
		S.Velocity = FVector(Speed, 0.0, 0.0);
		TestTrue(TEXT("fwd sim: hit inside the window"), FVeloxProjectileSim::Step(S, Fwd, Lifetime, Radius, Wall, NoTargets, nullptr, Impact));
		TestTrue(TEXT("fwd sim: world impact"), Impact.bHitWorld && !Impact.Pawn && !Impact.bLifetimeExpired);
		TestTrue(TEXT("fwd sim: stops at the wall minus the radius"), FMath::IsNearlyEqual(Impact.Location.X, WallX - Radius, 1e-3));
		TestTrue(TEXT("fwd sim: age = distance / speed"), FMath::IsNearlyEqual(S.Age, (WallX - Radius - 900.0) / Speed, 1e-6));

		FVector Where;
		FVector Normal;
		bool bHitWorld = false;
		const double Flight = FVeloxProjectileSim::WorldFlight(FVector::ZeroVector, FVector::ForwardVector, Speed, Lifetime, Wall, Where, Normal, bHitWorld);
		TestTrue(TEXT("world flight: wall"), bHitWorld && FMath::IsNearlyEqual(Where.X, WallX - Radius, 1e-3));
		TestTrue(TEXT("world flight: time"), FMath::IsNearlyEqual(Flight, (WallX - Radius) / Speed, 1e-6));
		const double Open = FVeloxProjectileSim::WorldFlight(FVector::ZeroVector, FVector::ForwardVector, Speed, 2.f, NoWorld, Where, Normal, bHitWorld);
		TestTrue(TEXT("world flight: lifetime end"), !bHitWorld && FMath::IsNearlyEqual(Open, 2.0, 1e-6) && FMath::IsNearlyEqual(Where.X, 2.0 * Speed, 1e-3));

		TestEqual(TEXT("fwd window: half the ping"), FVeloxProjectileSim::ForwardSimSeconds(100.f, 0.1f), 0.05f);
		TestEqual(TEXT("fwd window: capped"), FVeloxProjectileSim::ForwardSimSeconds(400.f, 0.1f), 0.1f);
		TestEqual(TEXT("fwd window: local shooter"), FVeloxProjectileSim::ForwardSimSeconds(0.f, 0.1f), 0.f);
	}

	// Lifetime: explode in place.
	{
		FVeloxProjectileState S;
		S.Velocity = FVector(Speed, 0.0, 0.0);
		FVeloxProjectileImpact Impact;
		TestTrue(TEXT("lifetime: ends"), FVeloxProjectileSim::Step(S, 1.f, 0.5f, Radius, NoWorld, NoTargets, nullptr, Impact));
		TestTrue(TEXT("lifetime: in place"), Impact.bLifetimeExpired && FMath::IsNearlyEqual(Impact.Location.X, 0.5 * Speed, 1e-3));
	}

	// Owner never direct-hit; another pawn on the path is hit at the sphere-capsule contact.
	{
		AActor* Owner = Key(1);
		AActor* Victim = Key(2);
		const FVeloxHitboxPose OwnerPose = Capsule(0.0, 0.0);
		const FVeloxHitboxPose VictimPose = Capsule(500.0, 0.0);
		const TArray<FVeloxProjectileTarget> Targets = { { Owner, &OwnerPose }, { Victim, &VictimPose } };
		FVeloxProjectileState S;
		S.Location = FVector(0.0, 0.0, 70.0); // inside the owner's capsule
		S.Velocity = FVector(Speed, 0.0, 0.0);
		FVeloxProjectileImpact Impact;
		TestTrue(TEXT("pawn: hit within 1 s"), FVeloxProjectileSim::Step(S, 1.f, Lifetime, Radius, NoWorld, Targets, Owner, Impact));
		TestTrue(TEXT("pawn: the victim, not the owner"), Impact.Pawn == Victim);
		TestTrue(TEXT("pawn: contact at capsule radius + sphere radius"), FMath::IsNearlyEqual(Impact.Location.X, 500.0 - 37.5 - Radius, 1e-3));
		// Pawn behind the wall: the wall wins.
		const FVeloxHitboxPose Behind = Capsule(1200.0, 0.0);
		const TArray<FVeloxProjectileTarget> BehindTargets = { { Victim, &Behind } };
		S = FVeloxProjectileState();
		S.Location = FVector(0.0, 0.0, 70.0);
		S.Velocity = FVector(Speed, 0.0, 0.0);
		TestTrue(TEXT("wall before pawn: impact"), FVeloxProjectileSim::Step(S, 1.f, Lifetime, Radius, Wall, BehindTargets, Owner, Impact));
		TestTrue(TEXT("wall before pawn: world"), Impact.bHitWorld && Impact.Pawn == nullptr);
	}

	// Splash (Q3 G_RadiusDamage, rocket 100 / 300 cm): points, direct victim excluded, zero-point edge, LOS.
	{
		auto Clear = [](const FVector&, const FVector&) { return true; };
		auto Blocked = [](const FVector&, const FVector&) { return false; };
		AActor* Direct = Key(3);
		AActor* Near = Key(4);
		AActor* Edge = Key(5);
		AActor* Far = Key(6);
		// At the height of the capsules' inner segment (z 37.5..102.5): the distances below are purely horizontal.
		const FVector Origin(0.0, 0.0, 50.0);
		const FVeloxHitboxPose DirectPose = Capsule(0.0, 0.0);
		const FVeloxHitboxPose NearPose = Capsule(37.5 + 150.0, 0.0);    // surface 150 cm away horizontally
		const FVeloxHitboxPose EdgePose = Capsule(37.5 + 299.9, 0.0);    // just inside: 0 points, still a hit (B3)
		const FVeloxHitboxPose FarPose = Capsule(37.5 + 300.5, 0.0);     // outside
		const TArray<FVeloxProjectileTarget> Targets = { { Direct, &DirectPose }, { Near, &NearPose }, { Edge, &EdgePose }, { Far, &FarPose } };
		TArray<FVeloxSplashHit> Hits;
		FVeloxProjectileSim::CollectSplash(Origin, Targets, Direct, 100, 300.f, 60.f, Clear, Hits);
		TestEqual(TEXT("splash: near and edge, not the direct victim, not far"), Hits.Num(), 2);
		const FVeloxSplashHit* NearHit = Hits.FindByPredicate([Near](const FVeloxSplashHit& H) { return H.Actor == Near; });
		const FVeloxSplashHit* EdgeHit = Hits.FindByPredicate([Edge](const FVeloxSplashHit& H) { return H.Actor == Edge; });
		TestTrue(TEXT("splash: direct victim excluded"), !Hits.ContainsByPredicate([Direct](const FVeloxSplashHit& H) { return H.Actor == Direct; }));
		if (TestNotNull(TEXT("splash: near"), NearHit))
		{
			TestEqual(TEXT("splash: near points (int)(100 * (1 - 150/300))"), NearHit->Points, 50);
			TestTrue(TEXT("splash: near dir away from the origin, lifted by the Q3 z offset"), NearHit->Dir.X > 0.0 && NearHit->Dir.Z > 0.0 && FMath::IsNearlyEqual(NearHit->Dir.Size(), 1.0, 1e-6));
		}
		if (TestNotNull(TEXT("splash: edge"), EdgeHit))
		{
			TestEqual(TEXT("splash: edge has 0 points but counts"), EdgeHit->Points, 0);
		}
		FVeloxProjectileSim::CollectSplash(Origin, Targets, nullptr, 100, 300.f, 60.f, Blocked, Hits);
		TestEqual(TEXT("LOS: all three points blocked -> no splash"), Hits.Num(), 0);
		int32 Calls = 0;
		auto OnlyBottom = [&Calls](const FVector&, const FVector& To) { ++Calls; return To.Z < 40.0; };
		FVeloxSplashHit One;
		TestTrue(TEXT("LOS: the bottom point alone is enough"), FVeloxProjectileSim::EvaluateSplash(Origin, Near, NearPose, 100, 300.f, 60.f, OnlyBottom, One));
		TestEqual(TEXT("LOS: centre, top, bottom tried"), Calls, 3);
	}

	// Self: half damage, full knockback (rocket jump); the vector matches the server's GE path.
	{
		FVeloxCombatParams Params;
		FVeloxSplashHit Self;
		Self.Points = 100;
		Self.Dir = FVeloxDamageMath::RadiusKnockbackDir(FVector(0.0, 0.0, 2.0), FVector(0.0, 0.0, 70.0), 60.f);
		FVeloxDamageInput In;
		In.Damage = Self.Points;
		In.bSelf = true;
		const FVeloxDamageResult Result = FVeloxDamageMath::Apply(In, Params);
		TestEqual(TEXT("self: half damage"), Result.HealthLoss, 50);
		TestEqual(TEXT("self: knockback from the raw 100"), Result.KnockbackSpeed, 1250.f);
		const FVector Impulse = FVeloxProjectileSim::SelfKnockback(Self, 1.f, Params, false);
		TestTrue(TEXT("self: 1250 cm/s straight up"), Impulse.Equals(FVector(0.0, 0.0, 1250.0), 1e-3));
		TestTrue(TEXT("self: protected -> none"), FVeloxProjectileSim::SelfKnockback(Self, 1.f, Params, true).IsZero());
		Self.Points = 0;
		TestTrue(TEXT("self: zero points -> no knockback"), FVeloxProjectileSim::SelfKnockback(Self, 1.f, Params, false).IsZero());
	}
	return true;
}

namespace VeloxProjectileTest
{
	/** Private game world with a floor (same construction as the weapon tests' world). */
	class FProjectileWorld
	{
	public:
		FProjectileWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxProjectileTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay();
			if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
			{
				AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -50), FRotator::ZeroRotator);
				Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
				Floor->SetActorScale3D(FVector(60, 60, 1));
			}
		}

		~FProjectileWorld()
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

		AVeloxCharacter* Spawn(const FVector& Location, TSubclassOf<AAIController> ControllerClass, AAIController** OutController = nullptr)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVeloxCharacter* Pawn = World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
			AVeloxPlayerState* PlayerState = World->SpawnActor<AVeloxPlayerState>();
			AAIController* Controller = World->SpawnActor<AAIController>(ControllerClass, FTransform::Identity, Params);
			if (!Pawn || !PlayerState || !Controller)
			{
				return nullptr;
			}
			Controller->PlayerState = PlayerState;
			PlayerState->SetOwner(Controller);
			Controller->Possess(Pawn);
			if (OutController)
			{
				*OutController = Controller;
			}
			return Pawn;
		}

		/** Ticks; OnTick (optional) runs after every tick. */
		void Advance(float Seconds, TFunctionRef<void()> OnTick, float Dt = 1.f / 120.f)
		{
			for (float T = 0.f; T < Seconds - 1e-4f; T += Dt)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Dt);
				OnTick();
			}
		}

		void Advance(float Seconds) { Advance(Seconds, [] {}); }

		UWorld* World = nullptr;
	};

	AVeloxPlayerState* PS(const AVeloxCharacter* Pawn) { return Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr; }

	void ResetDummy(AVeloxCharacter& Dummy, const FVector& Location, float Health)
	{
		UVeloxAbilitySystemComponent* ASC = PS(&Dummy)->GetVeloxAbilitySystemComponent();
		ASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), Health);
		ASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), 0.f);
		Dummy.SetActorLocation(FVector(Location.X, Location.Y, Dummy.GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		Dummy.GetCharacterMovement()->Velocity = FVector::ZeroVector;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatProjectileDummyTest, "Velox.Combat.ProjectileDummy", VeloxProjectileTest::Flags)

bool FVeloxCombatProjectileDummyTest::RunTest(const FString& Parameters)
{
	using namespace VeloxProjectileTest;

	FProjectileWorld W;
	AAIController* DummyController = nullptr;
	AVeloxCharacter* Shooter = W.Spawn(FVector(0, 0, 100), AAIController::StaticClass());
	AVeloxCharacter* Dummy = W.Spawn(FVector(1000, 0, 100), AVeloxDummyController::StaticClass(), &DummyController);
	if (!TestNotNull(TEXT("shooter"), Shooter) || !TestNotNull(TEXT("dummy"), Dummy) || !PS(Shooter) || !PS(Dummy))
	{
		return false;
	}
	if (AVeloxDummyController* Strafer = Cast<AVeloxDummyController>(DummyController))
	{
		Strafer->StrafeSpeedScale = 0.f;
	}
	W.Advance(0.3f);
	TestTrue(TEXT("give all"), VeloxWeaponCheats::GiveAllWeapons(*PS(Shooter)));
	UVeloxWeaponInventoryComponent* Inventory = PS(Shooter)->GetWeaponInventory();
	const UVeloxWeaponTable* Table = Inventory->GetWeaponTable();
	const UVeloxWeaponDefinition* RL = Table->Get(Table->FindBySlot(5));
	const UVeloxWeaponDefinition* PG = Table->Get(Table->FindBySlot(8));
	if (!TestNotNull(TEXT("RL"), RL) || !TestNotNull(TEXT("PG"), PG))
	{
		return false;
	}
	TestTrue(TEXT("RL: projectile fire ability"), RL->FireAbility && RL->FireAbility->IsChildOf(UVeloxGameplayAbility_FireProjectile::StaticClass()));
	TestTrue(TEXT("RL: projectile class"), RL->ProjectileClass && RL->ProjectileClass->IsChildOf(AVeloxProjectile::StaticClass()));
	TestTrue(TEXT("PG: projectile fire ability"), PG->FireAbility && PG->FireAbility->IsChildOf(UVeloxGameplayAbility_FireProjectile::StaticClass()));
	const UVeloxHealthSet* DummyHealth = PS(Dummy)->GetHealthSet();
	const UVeloxHealthSet* ShooterHealth = PS(Shooter)->GetHealthSet();
	const FVeloxProjectileExplosionInfo& Last = AVeloxProjectile::GetLastServerExplosion();
	const FVector DummyHome(1000, 0, 0);

	// 1. RL direct hit: 100 to the dummy, no splash on the direct victim, nothing on the shooter 10 m away.
	ResetDummy(*Dummy, DummyHome, 200.f);
	FString Error;
	int32 Count = Last.Count;
	TestTrue(FString::Printf(TEXT("RL fired (%s)"), *Error), VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("RL"), Dummy, 0.f, Error));
	W.Advance(0.6f);
	TestEqual(TEXT("RL direct: exploded once"), Last.Count, Count + 1);
	TestTrue(TEXT("RL direct: the dummy is the direct victim"), Last.DirectVictim.Get() == Dummy);
	TestEqual(TEXT("RL direct: no splash on the direct victim (or anyone)"), Last.Splash.Num(), 0);
	TestEqual(TEXT("RL direct: 200 - 100"), DummyHealth->GetHealth(), 100.f);

	// 2. RL into the floor 150 cm in front of the dummy: Q3 radius points from the actual explosion origin.
	W.Advance(0.3f); // refire 0.8 s
	ResetDummy(*Dummy, DummyHome, 200.f);
	W.Advance(0.1f);
	const FVector FloorPoint(850.0, 0.0, 0.0);
	if (AController* Controller = Shooter->GetController())
	{
		Controller->SetControlRotation((FloorPoint - Shooter->GetPawnViewLocation()).Rotation());
	}
	Count = Last.Count;
	TestTrue(FString::Printf(TEXT("RL at the floor fired (%s)"), *Error), VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("RL"), nullptr, 0.f, Error));
	W.Advance(0.6f);
	TestEqual(TEXT("RL splash: exploded once"), Last.Count, Count + 1);
	TestTrue(TEXT("RL splash: no direct victim"), !Last.DirectVictim.IsValid());
	const TPair<TWeakObjectPtr<AActor>, int32>* DummySplash = Last.Splash.FindByPredicate([Dummy](const TPair<TWeakObjectPtr<AActor>, int32>& P) { return P.Key.Get() == Dummy; });
	if (TestNotNull(TEXT("RL splash: the dummy is a splash victim"), DummySplash))
	{
		const float Dist = FVeloxDamageMath::ClosestCapsuleDistance(Last.Location, FVector(1000.0, 0.0, Dummy->GetActorLocation().Z), 37.5f, 70.f);
		AddInfo(FString::Printf(TEXT("RL splash at %s, dummy distance %.1f cm, %d points"), *Last.Location.ToCompactString(), Dist, DummySplash->Value));
		TestTrue(TEXT("RL splash: Q3 points in (0, 100)"), DummySplash->Value > 0 && DummySplash->Value < 100);
		TestEqual(TEXT("RL splash: health 200 - points"), DummyHealth->GetHealth(), 200.f - DummySplash->Value);
	}

	// 3. PG direct hit: 20.
	W.Advance(0.3f);
	ResetDummy(*Dummy, DummyHome, 200.f);
	TestTrue(FString::Printf(TEXT("PG fired (%s)"), *Error), VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("PG"), Dummy, 0.f, Error));
	W.Advance(0.4f);
	TestTrue(TEXT("PG direct: the dummy is the direct victim"), Last.DirectVictim.Get() == Dummy);
	TestEqual(TEXT("PG direct: 200 - 20"), DummyHealth->GetHealth(), 180.f);

	// 4. Offline rocket jump: look straight down, jump and fire in the same frame. Owner self splash at the explosion
	// (authority shooter): half damage, full knockback through AddKnockback(Server).
	W.Advance(1.0f);
	UVeloxMovementComponent* Movement = Shooter->GetVeloxMovementComponent();
	UVeloxAbilitySystemComponent* ShooterASC = PS(Shooter)->GetVeloxAbilitySystemComponent();
	ShooterASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), 100.f);
	ShooterASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), 0.f);
	TestTrue(TEXT("rocket jump: standing"), Movement->IsOnGround());
	if (AController* Controller = Shooter->GetController())
	{
		Controller->SetControlRotation(FRotator(-90.f, 0.f, 0.f));
	}
	const double StartZ = Shooter->GetActorLocation().Z;
	double MaxZ = StartZ;
	Count = Last.Count;
	Movement->SetJumpHeld(true);
	Shooter->Jump();
	TestTrue(FString::Printf(TEXT("RL down fired (%s)"), *Error), VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("RL"), nullptr, 0.f, Error));
	W.Advance(2.0f, [&MaxZ, Shooter, Movement]()
	{
		MaxZ = FMath::Max(MaxZ, Shooter->GetActorLocation().Z);
		Movement->SetJumpHeld(false);
	});
	TestEqual(TEXT("rocket jump: exploded once"), Last.Count, Count + 1);
	const TPair<TWeakObjectPtr<AActor>, int32>* SelfSplash = Last.Splash.FindByPredicate([Shooter](const TPair<TWeakObjectPtr<AActor>, int32>& P) { return P.Key.Get() == Shooter; });
	const FVeloxMoveParams& P = Movement->GetSimParams();
	const double JumpHeight = FMath::Square(P.JumpVelocity) / (-2.0 * P.GravityZ);
	const double Apex = MaxZ - StartZ;
	AddInfo(FString::Printf(TEXT("rocket jump apex %.1f cm (jump height %.1f cm, x%.2f), self points %d, health %.0f"), Apex, JumpHeight, Apex / JumpHeight, SelfSplash ? SelfSplash->Value : -1,
		ShooterHealth->GetHealth()));
	if (TestNotNull(TEXT("rocket jump: the shooter is in its own splash"), SelfSplash))
	{
		TestEqual(TEXT("rocket jump: self damage = points / 2"), ShooterHealth->GetHealth(), 100.f - FMath::Max(SelfSplash->Value / 2, 1));
	}
	TestTrue(TEXT("rocket jump: apex >= 2 x jump height"), Apex >= 2.0 * JumpHeight);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
