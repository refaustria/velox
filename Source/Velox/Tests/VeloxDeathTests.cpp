// Velox - death, respawn and readability tests (ADR 0003 sections 8 and 10, task 06).
//   Velox.GameMode.SpawnSelection  world-free: farthest half by nearest-enemy distance, occupied starts, telefrag only when
//                                  every start is occupied, avoid radius, no enemies = uniform, determinism for a seed.
//   Velox.GameMode.DeathFlow       private game world: B8 order (log), death tag, unpossessed, no avatar, collision off,
//                                  hitbox unregistered, second damage of the frame yields nothing, corpse removed after
//                                  CorpseLifetime, death camera distance, respawn with a fresh hitbox and no dead tag.
//   Velox.GameMode.SpawnProtection mechanism (default 0 s = off), protected pawn takes nothing, first shot ends it, expiry.
//   Velox.GameMode.DummyRespawn    game world with an AVeloxGameMode (task 06b, B28/B29): a spawner dummy respawns in place as its
//                                  dummy class, same controller, strafe origin at the spawner; invulnerable dummy keeps its
//                                  health and still takes a confirmed hit; kill labels never empty; a corpse dropped above
//                                  another pawn lands on the floor, not on the pawn.
//   Velox.GameMode.PlayerColors    palette is bright and distinct; the PlayerState colour reaches the pawn's body material.
//   Velox.GameMode.SpectateClamp   world-free (ADR 0004 C11): VeloxSpectate::ClampViewPoint inside / outside the expanded box /
//                                  below KillZ + 50 / speed step / Dt <= 0 / NaN.
//   Velox.GameMode.DeathSpectate   game world, local AVeloxPlayerController (C11): FreeSpectate death -> Spectating with an
//                                  AVeloxSpectatorPawn at the death eye, flight speed, early/late respawn request, Corpse
//                                  rule (velox.match.DeathCamera 0), intermission while spectating -> Inactive.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Bots/VeloxDummyController.h"
#include "Bots/VeloxDummySpawner.h"
#include "AbilitySystemComponent.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthComponent.h"
#include "Combat/VeloxHealthSet.h"
#include "Combat/VeloxHitboxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Camera/CameraTypes.h"
#include "Engine/GameInstance.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameModes/VeloxGameMode.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxSpectateMath.h"
#include "GameModes/VeloxSpectatorPawn.h"
#include "HAL/IConsoleManager.h"
#include "UI/VeloxHUDViewModel.h"
#include "UI/VeloxUISettings.h"
#include "GameModes/VeloxPlayerState.h"
#include "GameModes/VeloxSpawnSelection.h"
#include "Materials/MaterialInterface.h"
#include "Misc/OutputDevice.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxGameplayAbility_FireHitscan.h"
#include "Weapons/VeloxWeaponCheats.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"
#include "Weapons/VeloxWeaponTable.h"

namespace VeloxDeathTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	/** Collects log lines containing a marker while in scope. */
	struct FDeathLogCapture : public FOutputDevice
	{
		FString Marker;
		TArray<FString> Lines;
		explicit FDeathLogCapture(const TCHAR* InMarker) : Marker(InMarker) { GLog->AddOutputDevice(this); }
		virtual ~FDeathLogCapture() override { GLog->RemoveOutputDevice(this); }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			if (FCString::Strstr(V, *Marker))
			{
				Lines.Add(V);
			}
		}
	};

	/** Private game world with a floor and an AI controller that owns an AVeloxPlayerState. */
	class FDeathTestWorld
	{
	public:
		/** bWithGameMode: an AVeloxGameMode is the authority game mode (controllers then get their own PlayerState). */
		explicit FDeathTestWorld(bool bWithGameMode = false)
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxDeathTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			if (bWithGameMode)
			{
				GameInstance = NewObject<UGameInstance>(GEngine);
				GameInstance->AddToRoot();
				Context.OwningGameInstance = GameInstance;
				World->SetGameInstance(GameInstance);
				World->GetWorldSettings()->DefaultGameMode = AVeloxGameMode::StaticClass();
				World->SetGameMode(FURL());
			}
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay();

			if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
			{
				AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -50), FRotator::ZeroRotator);
				Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
				Floor->SetActorScale3D(FVector(40, 40, 1));
			}
			if (!bWithGameMode)
			{
				PlayerState = World->SpawnActor<AVeloxPlayerState>();
				Controller = World->SpawnActor<AAIController>();
				if (Controller)
				{
					Controller->PlayerState = PlayerState;
				}
			}
		}

		~FDeathTestWorld()
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
			if (GameInstance)
			{
				GameInstance->RemoveFromRoot();
			}
		}

		AVeloxCharacter* SpawnAndPossess()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVeloxCharacter* Character = World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, Params);
			if (Character && Controller)
			{
				Controller->Possess(Character);
			}
			return Character;
		}

		void Advance(float Seconds)
		{
			const float Dt = 0.05f;
			for (float T = 0.f; T < Seconds - 1e-4f; T += Dt)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Dt);
			}
		}

		UWorld* World = nullptr;
		AVeloxPlayerState* PlayerState = nullptr;
		AAIController* Controller = nullptr;
		UGameInstance* GameInstance = nullptr;
	};

	void Damage(UVeloxAbilitySystemComponent* ASC, int32 Amount)
	{
		FVeloxDamageSpecParams Params;
		Params.Damage = Amount;
		Params.DamageTag = VeloxGameplayTags::Damage_Environment;
		Params.bNoKnockback = true;
		ASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeDamageSpec(ASC, Params).Data.Get());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxSpawnSelectionTest, "Velox.GameMode.SpawnSelection", VeloxDeathTest::Flags)

bool FVeloxSpawnSelectionTest::RunTest(const FString& Parameters)
{
	// Four starts on a line, 1000 cm apart.
	const TArray<FVector> Starts = { FVector(0, 0, 0), FVector(1000, 0, 0), FVector(2000, 0, 0), FVector(3000, 0, 0) };
	const TArray<bool> Free = { false, false, false, false };
	constexpr double Avoid = 500.0;

	// An enemy next to start 0: the farthest half is {3, 2}; starts 0 and 1 are never picked.
	{
		const TArray<FVector> Enemies = { FVector(50, 0, 0) };
		TSet<int32> Seen;
		for (int32 Seed = 1; Seed <= 200; ++Seed)
		{
			FRandomStream Random(Seed);
			const VeloxSpawn::FSelection Pick = VeloxSpawn::Select(Starts, Free, Enemies, Avoid, Random);
			TestFalse(TEXT("free start: no telefrag"), Pick.bTelefrag);
			TestTrue(TEXT("farthest half only"), Pick.Index == 2 || Pick.Index == 3);
			TestEqual(TEXT("candidate pool is half the free starts"), Pick.NumCandidates, 2);
			Seen.Add(Pick.Index);
		}
		TestEqual(TEXT("both farthest-half starts occur"), Seen.Num(), 2);
	}

	// Determinism for a seed.
	{
		const TArray<FVector> Enemies = { FVector(1500, 0, 0) };
		FRandomStream A(77);
		FRandomStream B(77);
		for (int32 Step = 0; Step < 20; ++Step)
		{
			TestEqual(TEXT("same seed, same pick"), VeloxSpawn::Select(Starts, Free, Enemies, Avoid, A).Index, VeloxSpawn::Select(Starts, Free, Enemies, Avoid, B).Index);
		}
	}

	// No enemies: uniform over the free starts (occupied start 1 never).
	{
		const TArray<bool> OneTaken = { false, true, false, false };
		TSet<int32> Seen;
		FRandomStream Random(5);
		for (int32 Step = 0; Step < 400; ++Step)
		{
			const VeloxSpawn::FSelection Pick = VeloxSpawn::Select(Starts, OneTaken, TArray<FVector>(), Avoid, Random);
			TestFalse(TEXT("no telefrag while a start is free"), Pick.bTelefrag);
			TestNotEqual(TEXT("occupied start never chosen"), Pick.Index, 1);
			Seen.Add(Pick.Index);
		}
		TestEqual(TEXT("every free start occurs without enemies"), Seen.Num(), 3);
	}

	// Avoid radius: 3 starts, enemy distances 100 / 400 / 3000 (radius 500). Plain "farthest half of 3" would be {2, 1};
	// with the radius only start 2 qualifies.
	{
		const TArray<FVector> Three = { FVector(0, 0, 0), FVector(300, 0, 0), FVector(2900, 0, 0) };
		const TArray<bool> NoneTaken = { false, false, false };
		const TArray<FVector> Enemies = { FVector(-100, 0, 0) };
		for (int32 Seed = 1; Seed <= 50; ++Seed)
		{
			FRandomStream Random(Seed);
			TestEqual(TEXT("only the start outside the avoid radius"), VeloxSpawn::Select(Three, NoneTaken, Enemies, Avoid, Random).Index, 2);
		}
		// Everything inside the radius: fall back to the farthest half instead of failing.
		FRandomStream Random(1);
		const VeloxSpawn::FSelection Fallback = VeloxSpawn::Select(Three, NoneTaken, Enemies, 1.0e6, Random);
		TestTrue(TEXT("all inside the radius: farthest half of what exists"), Fallback.Index == 2 || Fallback.Index == 1);
	}

	// Telefrag only when every start is occupied (one start test).
	{
		FRandomStream Random(3);
		const VeloxSpawn::FSelection One = VeloxSpawn::Select({ FVector::ZeroVector }, { true }, { FVector(10, 0, 0) }, Avoid, Random);
		TestEqual(TEXT("single occupied start is chosen"), One.Index, 0);
		TestTrue(TEXT("single occupied start: telefrag"), One.bTelefrag);
		const VeloxSpawn::FSelection AllTaken = VeloxSpawn::Select(Starts, { true, true, true, true }, { FVector(10, 0, 0) }, Avoid, Random);
		TestTrue(TEXT("all occupied: telefrag"), AllTaken.bTelefrag && AllTaken.Index != INDEX_NONE);
		const VeloxSpawn::FSelection Empty = VeloxSpawn::Select(TArray<FVector>(), TArray<bool>(), TArray<FVector>(), Avoid, Random);
		TestEqual(TEXT("no starts: no pick"), Empty.Index, static_cast<int32>(INDEX_NONE));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxDeathFlowTest, "Velox.GameMode.DeathFlow", VeloxDeathTest::Flags)

bool FVeloxDeathFlowTest::RunTest(const FString& Parameters)
{
	using namespace VeloxDeathTest;

	FDeathTestWorld W;
	if (!TestNotNull(TEXT("player state"), W.PlayerState) || !TestNotNull(TEXT("controller"), W.Controller))
	{
		return false;
	}
	UVeloxAbilitySystemComponent* ASC = W.PlayerState->GetVeloxAbilitySystemComponent();
	const UVeloxHealthSet* Health = W.PlayerState->GetHealthSet();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(W.PlayerState->GetCombatSettings());
	AVeloxCharacter* Pawn = W.SpawnAndPossess();
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}
	UVeloxLagCompensationSubsystem* LagComp = UVeloxLagCompensationSubsystem::Get(W.World);
	TestTrue(TEXT("alive pawn is registered for lag compensation"), LagComp && LagComp->IsRegistered(Pawn));
	TestFalse(TEXT("alive pawn is not a corpse"), Pawn->IsCorpse());
	W.Advance(2.f); // spawn protection (if any) is over

	int32 NumOutOfHealth = 0;
	Health->OnOutOfHealth.AddLambda([&NumOutOfHealth](const FVeloxDamageEvent&) { ++NumOutOfHealth; });
	FDeathLogCapture Log(TEXT("VeloxDeath step="));
	Damage(ASC, 1000);
	// B8: dead tag, ability/input/cue teardown, hitbox and game mode notification are synchronous (no tick yet) ...
	TestEqual(TEXT("health 0"), Health->GetHealth(), 0.f);
	TestTrue(TEXT("corpse"), Pawn->IsCorpse());
	TestTrue(TEXT("dead tag on the player state's ASC"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead));
	TestTrue(TEXT("hitbox unregistered synchronously"), LagComp && !LagComp->IsRegistered(Pawn));
	GLog->FlushThreadedLogs();
	TestEqual(TEXT("three synchronous death steps logged"), Log.Lines.Num(), 3);
	// ... movement, collision and possession follow on the next tick (a lethal rocket jump dies inside PerformMovement).
	TestTrue(TEXT("still possessed before the tick"), W.Controller->GetPawn() == Pawn);
	W.Advance(0.05f);
	TestTrue(TEXT("controller lost the pawn"), W.Controller->GetPawn() == nullptr);
	TestTrue(TEXT("corpse is not the avatar"), ASC->GetAvatarActor() == nullptr);
	TestEqual(TEXT("corpse capsule has no collision"), static_cast<int32>(Pawn->GetCapsuleComponent()->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::NoCollision));
	TestTrue(TEXT("health component death started"), Pawn->GetHealthComponent()->IsDeadOrDying());
	TestEqual(TEXT("health component detached from the ASC"), Pawn->GetHealthComponent()->GetHealth(), 0.f);

	// B8 order, from the log.
	GLog->FlushThreadedLogs();
	TestEqual(TEXT("five death steps logged"), Log.Lines.Num(), 5);
	for (int32 Step = 0; Step < Log.Lines.Num(); ++Step)
	{
		TestTrue(*FString::Printf(TEXT("step %d in order"), Step + 1), Log.Lines[Step].Contains(FString::Printf(TEXT("step=%d "), Step + 1)));
	}
	TestTrue(TEXT("step 1 saw the death tag"), Log.Lines.Num() > 0 && Log.Lines[0].Contains(TEXT("dead=1")));

	// A second damage GE of the same frame: Dead target takes nothing, no second death.
	Damage(ASC, 1000);
	TestEqual(TEXT("no second death"), NumOutOfHealth, 1);
	TestEqual(TEXT("no second death step lines"), Log.Lines.Num(), 5);

	// Death camera: Settings.DeathCamDistanceCm from the corpse's eye, above the floor.
	{
		FMinimalViewInfo View;
		Pawn->CalcCamera(0.f, View);
		const FVector Pivot = Pawn->GetActorLocation() + FVector(0, 0, Pawn->BaseEyeHeight);
		TestTrue(TEXT("death camera distance"), FMath::IsNearlyEqual(FVector::Dist(View.Location, Pivot), Settings.DeathCamDistanceCm, 1.0));
		TestTrue(TEXT("death camera looks down at the body"), View.Rotation.Pitch < 0.0);
	}

	// Corpse removed after CorpseLifetime, not before.
	W.Advance(FMath::Max(Settings.CorpseLifetime - 0.5f, 0.f));
	TestTrue(TEXT("corpse still there before its lifetime"), IsValid(Pawn));
	W.Advance(0.8f);
	TestFalse(TEXT("corpse removed after its lifetime"), IsValid(Pawn));

	// Respawn: fresh pawn, tag cleared, hitbox registered again, loadout health.
	AVeloxCharacter* NewPawn = W.SpawnAndPossess();
	if (!TestNotNull(TEXT("respawned pawn"), NewPawn))
	{
		return false;
	}
	TestFalse(TEXT("dead tag removed by GE_SpawnInit"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead));
	TestEqual(TEXT("respawn health 125"), Health->GetHealth(), 125.f);
	TestTrue(TEXT("new pawn is the avatar"), ASC->GetAvatarActor() == NewPawn);
	TestTrue(TEXT("new pawn registered with a fresh hitbox"), LagComp && LagComp->IsRegistered(NewPawn));
	TestFalse(TEXT("new pawn is no corpse"), NewPawn->IsCorpse());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxSpawnProtectionTest, "Velox.GameMode.SpawnProtection", VeloxDeathTest::Flags)

bool FVeloxSpawnProtectionTest::RunTest(const FString& Parameters)
{
	using namespace VeloxDeathTest;

	FDeathTestWorld W;
	if (!TestNotNull(TEXT("player state"), W.PlayerState) || !TestNotNull(TEXT("controller"), W.Controller))
	{
		return false;
	}
	UVeloxAbilitySystemComponent* ASC = W.PlayerState->GetVeloxAbilitySystemComponent();
	const UVeloxHealthSet* Health = W.PlayerState->GetHealthSet();
	AVeloxCharacter* Pawn = W.SpawnAndPossess();
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}

	// Default 0 s: the possession applied nothing.
	TestFalse(TEXT("default: no spawn protection"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected));

	UVeloxCombatSettings* Settings = NewObject<UVeloxCombatSettings>(GetTransientPackage());
	Settings->SpawnProtectionSeconds = 1.0f;
	W.PlayerState->ApplySpawnProtection(*Settings);
	TestTrue(TEXT("protected"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected));
	const float HealthBefore = Health->GetHealth();
	Damage(ASC, 50);
	TestEqual(TEXT("protected: no health loss"), Health->GetHealth(), HealthBefore);

	// The first shot ends it (bSpawnProtectionEndsOnFire).
	ASC->AbilityActivatedCallbacks.Broadcast(GetMutableDefault<UVeloxGameplayAbility_FireHitscan>());
	TestFalse(TEXT("fire ended the protection"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected));
	Damage(ASC, 10);
	TestTrue(TEXT("unprotected: health drops"), Health->GetHealth() < HealthBefore);

	// Expiry by duration.
	W.PlayerState->ApplySpawnProtection(*Settings);
	TestTrue(TEXT("protected again"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected));
	W.Advance(1.2f);
	TestFalse(TEXT("protection expired"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxDummyRespawnTest, "Velox.GameMode.DummyRespawn", VeloxDeathTest::Flags)

bool FVeloxDummyRespawnTest::RunTest(const FString& Parameters)
{
	using namespace VeloxDeathTest;

	FDeathTestWorld W(true);
	AVeloxGameMode* GameMode = W.World->GetAuthGameMode<AVeloxGameMode>();
	if (!TestNotNull(TEXT("Velox game mode"), GameMode))
	{
		return false;
	}
	// The default pawn is a different class: a dummy that comes back as AVeloxCharacter proves the fixed respawn class.
	GameMode->DefaultPawnClass = ADefaultPawn::StaticClass();

	const auto MakeSpawner = [&W](const FVector& Location, bool bInvulnerable)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		AVeloxDummySpawner* Spawner = W.World->SpawnActor<AVeloxDummySpawner>(Location, FRotator::ZeroRotator, Params);
		Spawner->bAutoSpawn = false;
		Spawner->bInvulnerable = bInvulnerable;
		Spawner->FinishSpawning(FTransform(Location));
		return Spawner;
	};
	constexpr float StandZ = VeloxCharacterGeometry::CapsuleHalfHeight;

	// 1. A killed dummy comes back in place as its dummy class under the same controller.
	const FVector SpawnerLocation(500.0, 200.0, StandZ);
	AVeloxDummySpawner* Spawner = MakeSpawner(SpawnerLocation, false);
	AVeloxCharacter* Dummy = Spawner->SpawnDummy();
	if (!TestNotNull(TEXT("dummy spawned"), Dummy))
	{
		return false;
	}
	AVeloxDummyController* DummyController = Cast<AVeloxDummyController>(Dummy->GetController());
	AVeloxPlayerState* DummyPS = DummyController ? DummyController->GetPlayerState<AVeloxPlayerState>() : nullptr;
	if (!TestNotNull(TEXT("dummy controller"), DummyController) || !TestNotNull(TEXT("dummy player state (from the game mode)"), DummyPS))
	{
		return false;
	}
	TestTrue(TEXT("controller remembers a fixed respawn"), DummyController->HasFixedRespawn());
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(DummyPS->GetCombatSettings());
	W.Advance(0.5f);

	FDeathLogCapture KillLog(TEXT("VeloxKill "));
	FDeathLogCapture RespawnLog(TEXT("(fixed)"));
	Damage(DummyPS->GetVeloxAbilitySystemComponent(), 1000);
	TestTrue(TEXT("dummy died"), Dummy->IsCorpse());
	// Step tick by tick and look at the new pawn the moment it exists (it strafes away afterwards).
	AVeloxCharacter* Reborn = nullptr;
	float Waited = 0.f;
	for (; Waited < Settings.MinRespawnDelay + 0.1f && !Reborn; Waited += 0.05f)
	{
		W.Advance(0.05f);
		Reborn = Cast<AVeloxCharacter>(DummyController->GetPawn());
	}
	if (!TestNotNull(TEXT("dummy respawned within MinRespawnDelay + 0.1 s"), Reborn))
	{
		return false;
	}
	TestTrue(TEXT("a new pawn"), Reborn != Dummy);
	TestTrue(TEXT("respawned as the dummy class, not the game mode default"), Reborn->GetClass() == Spawner->DummyClass.Get());
	// The pawn already strafed for the rest of the spawning tick, so only the strafe axis may differ (by < one tick).
	TestTrue(*FString::Printf(TEXT("respawned at the spawner transform (X within 1 cm, Z within the floor skin, Y within one tick of strafing): %s"), *Reborn->GetActorLocation().ToCompactString()),
		FMath::Abs(Reborn->GetActorLocation().X - SpawnerLocation.X) <= 1.0 && FMath::Abs(Reborn->GetActorLocation().Z - SpawnerLocation.Z) <= 5.0
			&& FMath::Abs(Reborn->GetActorLocation().Y - SpawnerLocation.Y) <= DummyController->StrafeHalfWidth * 0.1);
	TestTrue(TEXT("strafe origin is the spawner location"), DummyController->GetStrafeOrigin().Equals(SpawnerLocation, 1.0));
	TestTrue(TEXT("same controller owns the pawn"), Reborn->GetController() == DummyController);
	TestFalse(TEXT("reborn dummy is alive"), Reborn->IsCorpse());
	W.Advance(0.3f);
	TestTrue(TEXT("reborn dummy strafes (moves along Y)"), FMath::Abs(Reborn->GetActorLocation().Y - SpawnerLocation.Y) > 1.0);
	GLog->FlushThreadedLogs();
	TestEqual(TEXT("one fixed-respawn log line"), RespawnLog.Lines.Num(), 1);
	TestTrue(TEXT("the log names the spawner location exactly"), RespawnLog.Lines.Num() == 1 && RespawnLog.Lines[0].Contains(SpawnerLocation.ToCompactString()));

	// Kill labels (B29): AI PlayerStates have no name, the label falls back to the object name; null is "-".
	TestTrue(TEXT("AI label is not empty"), !AVeloxPlayerState::VeloxPlayerLabel(DummyPS).IsEmpty());
	TestEqual(TEXT("null label"), AVeloxPlayerState::VeloxPlayerLabel(nullptr), FString(TEXT("-")));
	TestEqual(TEXT("AI label is the object name"), AVeloxPlayerState::VeloxPlayerLabel(DummyPS), DummyPS->GetName());
	TestEqual(TEXT("one kill logged"), KillLog.Lines.Num(), 1);
	if (KillLog.Lines.Num() == 1)
	{
		TestTrue(TEXT("kill line names the victim by its label"), KillLog.Lines[0].Contains(FString::Printf(TEXT("victim=%s "), *DummyPS->GetName())));
	}

	// 2. An invulnerable dummy takes no damage, stays registered and still yields a hit on a hitscan shot.
	AVeloxDummySpawner* TargetSpawner = MakeSpawner(FVector(1500.0, 0.0, StandZ), true);
	AVeloxCharacter* Target = TargetSpawner->SpawnDummy();
	AVeloxDummyController* TargetController = Target ? Cast<AVeloxDummyController>(Target->GetController()) : nullptr;
	AVeloxPlayerState* TargetPS = TargetController ? TargetController->GetPlayerState<AVeloxPlayerState>() : nullptr;
	if (!TestNotNull(TEXT("invulnerable dummy"), TargetPS))
	{
		return false;
	}
	TargetController->StrafeSpeedScale = 0.f; // hold still for the shot
	UVeloxAbilitySystemComponent* TargetASC = TargetPS->GetVeloxAbilitySystemComponent();
	TestTrue(TEXT("invulnerable dummy is spawn protected"), TargetASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected));
	W.Advance(0.5f);
	const float HealthBefore = TargetPS->GetHealthSet()->GetHealth();
	Damage(TargetASC, 1000);
	TestEqual(TEXT("invulnerable: no health loss from 1000 damage"), TargetPS->GetHealthSet()->GetHealth(), HealthBefore);
	TestFalse(TEXT("invulnerable: not a corpse"), Target->IsCorpse());
	UVeloxLagCompensationSubsystem* LagComp = UVeloxLagCompensationSubsystem::Get(W.World);
	W.Advance(Settings.MinRespawnDelay + 0.2f);
	TestTrue(TEXT("invulnerable dummy stays registered"), LagComp && LagComp->IsRegistered(Target));
	TestTrue(TEXT("invulnerable dummy keeps its pawn"), TargetController->GetPawn() == Target);

	// Shooter: an AI pawn with all weapons, aimed at the target by the test shot.
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AVeloxCharacter* Shooter = W.World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), FVector(1500.0, -800.0, StandZ), FRotator::ZeroRotator, Params);
		AAIController* ShooterController = W.World->SpawnActor<AAIController>(AAIController::StaticClass(), FTransform::Identity, Params);
		AVeloxPlayerState* ShooterPS = W.World->SpawnActor<AVeloxPlayerState>();
		if (!TestNotNull(TEXT("shooter"), Shooter) || !TestNotNull(TEXT("shooter controller"), ShooterController) || !TestNotNull(TEXT("shooter player state"), ShooterPS))
		{
			return false;
		}
		ShooterController->PlayerState = ShooterPS;
		ShooterPS->SetOwner(ShooterController);
		ShooterController->Possess(Shooter);
		W.Advance(0.2f);
		TestTrue(TEXT("give all"), VeloxWeaponCheats::GiveAllWeapons(*ShooterPS));
		FString Error;
		const float HealthBeforeShot = TargetPS->GetHealthSet()->GetHealth();
		const bool bFired = VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("RG"), Target, 0.f, Error);
		TestTrue(*FString::Printf(TEXT("railgun test shot fired (%s)"), *Error), bFired);
		UVeloxWeaponInventoryComponent* Inventory = ShooterPS->GetWeaponInventory();
		const int32 Index = Inventory->GetWeaponTable()->FindBySlot(7);
		const FGameplayAbilitySpec* Spec = Inventory->FindFireSpec(Index);
		const UVeloxGameplayAbility_Fire* Fire = Spec ? Cast<UVeloxGameplayAbility_Fire>(Spec->GetPrimaryInstance()) : nullptr;
		if (TestNotNull(TEXT("rail fire instance"), Fire))
		{
			TestTrue(TEXT("protected target still yields a hit (confirm is sent)"), Fire->GetLastServerShot().NumHits > 0);
			TestEqual(TEXT("protected target takes 0 damage"), Fire->GetLastServerShot().TotalDamage, 0);
			TestFalse(TEXT("shot not rejected"), Fire->GetLastServerShot().bRejected);
		}
		// (Health above the maximum decays on its own, so compare with the value right before the shot.)
		TestTrue(TEXT("target health not reduced by the rail"), TargetPS->GetHealthSet()->GetHealth() >= HealthBeforeShot - 1.f);
	}

	// 3. A corpse dropped from the air lands on the floor, not on a pawn standing below it.
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Column(-1500.0, 0.0, 0.0);
		AVeloxCharacter* Below = W.World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), Column + FVector(0, 0, StandZ), FRotator::ZeroRotator, Params);
		AAIController* AboveController = W.World->SpawnActor<AAIController>(AAIController::StaticClass(), FTransform::Identity, Params);
		AVeloxPlayerState* AbovePS = W.World->SpawnActor<AVeloxPlayerState>();
		AVeloxCharacter* Above = W.World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), Column + FVector(0, 0, 3.f * StandZ + 150.f), FRotator::ZeroRotator, Params);
		if (!TestNotNull(TEXT("pawn below"), Below) || !TestNotNull(TEXT("pawn above"), Above) || !TestNotNull(TEXT("controller"), AboveController) || !TestNotNull(TEXT("player state"), AbovePS))
		{
			return false;
		}
		AboveController->PlayerState = AbovePS;
		AbovePS->SetOwner(AboveController);
		AboveController->Possess(Above);
		W.Advance(0.1f); // airborne now
		Damage(AbovePS->GetVeloxAbilitySystemComponent(), 1000);
		W.Advance(0.05f); // CompleteDeath: floor drop
		TestTrue(TEXT("above is a corpse"), Above->IsCorpse());
		TestTrue(TEXT("corpse ended on the floor, not on the pawn below"), Above->GetActorLocation().Z < 2.f * StandZ - 1.f);
		TestTrue(*FString::Printf(TEXT("corpse rests at standing height on the floor (Z=%.1f)"), Above->GetActorLocation().Z), FMath::Abs(Above->GetActorLocation().Z - StandZ) <= 6.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxPlayerColorsTest, "Velox.GameMode.PlayerColors", VeloxDeathTest::Flags)

bool FVeloxPlayerColorsTest::RunTest(const FString& Parameters)
{
	using namespace VeloxDeathTest;

	// Readability is a non-negotiable: bright, saturated, distinct.
	const UVeloxCombatSettings& Defaults = *GetDefault<UVeloxCombatSettings>();
	TestTrue(TEXT("at least eight colours"), Defaults.PlayerColorPalette.Num() >= 8);
	for (int32 A = 0; A < Defaults.PlayerColorPalette.Num(); ++A)
	{
		const FColor C = Defaults.PlayerColorPalette[A];
		const int32 Max = FMath::Max3<int32>(C.R, C.G, C.B);
		const int32 Min = FMath::Min3<int32>(C.R, C.G, C.B);
		TestTrue(*FString::Printf(TEXT("colour %d is bright"), A), Max >= 200);
		TestTrue(*FString::Printf(TEXT("colour %d is saturated"), A), Max - Min >= 150);
		for (int32 B = A + 1; B < Defaults.PlayerColorPalette.Num(); ++B)
		{
			TestTrue(*FString::Printf(TEXT("colours %d and %d differ"), A, B), Defaults.PlayerColorPalette[A] != Defaults.PlayerColorPalette[B]);
		}
	}

	FDeathTestWorld W;
	if (!TestNotNull(TEXT("player state"), W.PlayerState))
	{
		return false;
	}
	TestFalse(TEXT("no colour before the game mode assigns one"), W.PlayerState->HasPlayerColor());
	AVeloxCharacter* Pawn = W.SpawnAndPossess();
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}
	W.PlayerState->SetPlayerColorIndex(3);
	TestTrue(TEXT("colour assigned"), W.PlayerState->HasPlayerColor());
	TestEqual(TEXT("palette entry"), W.PlayerState->GetPlayerColor(), UVeloxCombatSettings::GetOrDefault(W.PlayerState->GetCombatSettings()).PlayerColorPalette[3]);

	// The body material exists once the bootstrap script ran; then the colour must arrive on the pawn.
	if (LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_PlayerBody.M_PlayerBody")))
	{
		const FLinearColor Expected = FLinearColor::FromSRGBColor(W.PlayerState->GetPlayerColor());
		const FLinearColor Body = Pawn->GetBodyColor();
		TestTrue(TEXT("body colour = player colour"), FMath::IsNearlyEqual(Body.R, Expected.R, 1e-3f) && FMath::IsNearlyEqual(Body.G, Expected.G, 1e-3f) && FMath::IsNearlyEqual(Body.B, Expected.B, 1e-3f));
	}
	else
	{
		AddInfo(TEXT("M_PlayerBody not found (run Tools/editor/create_bootstrap_content.py): body colour check skipped"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxSpectateClampTest, "Velox.GameMode.SpectateClamp", VeloxDeathTest::Flags)

bool FVeloxSpectateClampTest::RunTest(const FString& Parameters)
{
	using namespace VeloxSpectate;
	const FBox Arena(FVector(-1000, -1000, 0), FVector(1000, 1000, 500));
	const FBox Bounds = Arena.ExpandBy(300.0);
	const double MinZ = -200.0 + KillZMarginCm;
	constexpr double Step = 1200.0 * 3.0;   // DeathSpectateSpeedCmS x DeathSpectateSyncSpeedScale
	const TOptional<FVector> NoLast;

	// Inside, no previous point: unchanged.
	{
		const FClampResult R = ClampViewPoint(FVector(100, 200, 300), NoLast, 0.25, Bounds, MinZ, Step);
		TestFalse(TEXT("inside: not clamped"), R.WasClamped());
		TestTrue(TEXT("inside: point unchanged"), R.Point.Equals(FVector(100, 200, 300)));
		TestEqual(TEXT("inside: distance 0"), R.DistCm, 0.0);
		TestEqual(TEXT("reason none"), ReasonsToString(R.Reasons), FString(TEXT("none")));
	}
	// Outside the expanded box: nearest point on it.
	{
		const FClampResult R = ClampViewPoint(FVector(5000, 0, 250), NoLast, 0.25, Bounds, MinZ, Step);
		TestTrue(TEXT("outside: bounds clamp"), (R.Reasons & ClampBounds) != 0);
		TestTrue(*FString::Printf(TEXT("outside: nearest box point (%s)"), *R.Point.ToCompactString()), R.Point.Equals(FVector(1300, 0, 250), 0.01));
		TestTrue(TEXT("outside: distance logged"), FMath::IsNearlyEqual(R.DistCm, 3700.0, 0.01));
		TestEqual(TEXT("reason bounds"), ReasonsToString(R.Reasons), FString(TEXT("bounds")));
		// Inside the margin (beyond the nav box, inside the expanded one): accepted.
		TestFalse(TEXT("inside the margin: accepted"), ClampViewPoint(FVector(1200, 0, 250), NoLast, 0.25, Bounds, MinZ, Step).WasClamped());
	}
	// Below KillZ + 50: raised (unbounded box, so only the KillZ rule acts).
	{
		const FClampResult R = ClampViewPoint(FVector(0, 0, -1000), NoLast, 0.25, FBox(ForceInit), MinZ, Step);
		TestTrue(TEXT("killz: clamp"), R.Reasons == ClampKillZ);
		TestEqual(TEXT("killz: raised to KillZ + 50"), R.Point.Z, MinZ);
		TestEqual(TEXT("reason killz"), ReasonsToString(R.Reasons), FString(TEXT("killz")));
		TestFalse(TEXT("unbounded box: far point accepted"), ClampViewPoint(FVector(1e6, -1e6, 0), NoLast, 0.25, FBox(ForceInit), MinZ, Step).WasClamped());
	}
	// Speed step: a step above the cap is shortened along the request.
	{
		const TOptional<FVector> Last(FVector(0, 0, 100));
		const double Dt = 0.25;
		const FClampResult Ok = ClampViewPoint(FVector(800, 0, 100), Last, Dt, Bounds, MinZ, Step);
		TestFalse(TEXT("speed: a step within the cap is accepted"), Ok.WasClamped());
		const FClampResult R = ClampViewPoint(FVector(1100, 600, 100), Last, Dt, Bounds, MinZ, Step);
		TestTrue(TEXT("speed: clamp"), R.Reasons == ClampSpeed);
		const FVector Dir = (FVector(1100, 600, 100) - FVector(0, 0, 100)).GetSafeNormal();
		TestTrue(TEXT("speed: step length = cap x dt"), FMath::IsNearlyEqual(FVector::Dist(R.Point, Last.GetValue()), Step * Dt, 0.01));
		TestTrue(TEXT("speed: along the request"), (R.Point - Last.GetValue()).GetSafeNormal().Equals(Dir, 1e-6));
		TestEqual(TEXT("reason speed"), ReasonsToString(R.Reasons), FString(TEXT("speed")));
		// Dt <= 0 (the caller floors it; guard anyway): no step, no NaN.
		const FClampResult Zero = ClampViewPoint(FVector(500, 0, 100), Last, 0.0, Bounds, MinZ, Step);
		TestTrue(TEXT("dt 0: stays at the last accepted point"), Zero.Point.Equals(Last.GetValue()) && !Zero.Point.ContainsNaN());
		const FClampResult Negative = ClampViewPoint(FVector(500, 0, 100), Last, -1.0, Bounds, MinZ, Step);
		TestTrue(TEXT("dt < 0: stays at the last accepted point"), Negative.Point.Equals(Last.GetValue()) && Negative.Reasons == ClampSpeed);
		TestFalse(TEXT("dt 0: an unchanged point is no clamp"), ClampViewPoint(Last.GetValue(), Last, 0.0, Bounds, MinZ, Step).WasClamped());
		// Speed and bounds together.
		const FClampResult Both = ClampViewPoint(FVector(1e5, 0, 100), TOptional<FVector>(FVector(1200, 0, 100)), 1.0, Bounds, MinZ, Step);
		TestTrue(TEXT("speed+bounds"), Both.Reasons == (ClampSpeed | ClampBounds) && Both.Point.Equals(FVector(1300, 0, 100), 0.01));
		TestEqual(TEXT("reason speed+bounds"), ReasonsToString(Both.Reasons), FString(TEXT("speed+bounds")));
	}
	// A non-finite request falls back to the last accepted point.
	{
		const FClampResult R = ClampViewPoint(FVector(NAN, 0, 0), TOptional<FVector>(FVector(10, 20, 30)), 0.25, Bounds, MinZ, Step);
		TestTrue(TEXT("NaN: last accepted point"), R.Point.Equals(FVector(10, 20, 30)) && R.WasClamped());
	}
	return true;
}

namespace VeloxDeathTest
{
	/** Game world with an AVeloxGameMode, a player start and a local AVeloxPlayerController that owns a live pawn. */
	struct FSpectateWorld : public FDeathTestWorld
	{
		FSpectateWorld() : FDeathTestWorld(true)
		{
			GameMode = World->GetAuthGameMode<AVeloxGameMode>();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, Params);
			PC = World->SpawnActor<AVeloxPlayerController>(AVeloxPlayerController::StaticClass(), FTransform::Identity, Params);
			if (PC)
			{
				// No ULocalPlayer and no net driver: mark it local, as AGameModeBase does for the listen host.
				PC->SetAsLocalPlayerController();
			}
			PS = PC ? PC->GetPlayerState<AVeloxPlayerState>() : nullptr;
		}

		AVeloxCharacter* Respawn()
		{
			if (GameMode && PC && !PC->GetPawn())
			{
				GameMode->RestartPlayer(PC);
			}
			return PC ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
		}

		AVeloxGameMode* GameMode = nullptr;
		AVeloxPlayerController* PC = nullptr;
		AVeloxPlayerState* PS = nullptr;
	};

	struct FDeathCameraCVarScope
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.match.DeathCamera"));
		explicit FDeathCameraCVarScope(int32 Value) { if (CVar) { CVar->Set(Value, ECVF_SetByCode); } }
		~FDeathCameraCVarScope() { if (CVar) { CVar->Set(-1, ECVF_SetByCode); } }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxDeathSpectateTest, "Velox.GameMode.DeathSpectate", VeloxDeathTest::Flags)

bool FVeloxDeathSpectateTest::RunTest(const FString& Parameters)
{
	using namespace VeloxDeathTest;

	FSpectateWorld W;
	if (!TestNotNull(TEXT("game mode"), W.GameMode) || !TestNotNull(TEXT("player controller"), W.PC) || !TestNotNull(TEXT("player state (game mode)"), W.PS))
	{
		return false;
	}
	TestTrue(TEXT("spectator class is the Velox spectator pawn"), W.World->GetGameState() && W.World->GetGameState()->SpectatorClass == AVeloxSpectatorPawn::StaticClass());
	TestEqual(TEXT("rule without a definition: FreeSpectate (q16 default)"), static_cast<int32>(W.GameMode->GetDeathCamera()), static_cast<int32>(EVeloxDeathCamera::FreeSpectate));
	AVeloxCharacter* Pawn = W.Respawn();
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(W.PS->GetCombatSettings());
	UVeloxAbilitySystemComponent* ASC = W.PS->GetVeloxAbilitySystemComponent();
	W.Advance(2.f);

	// 1. Death: spectating, no pawn, a spectator pawn at the death eye with the death view.
	const FRotator DeathRotation(-10.0, 45.0, 0.0);
	W.PC->SetControlRotation(DeathRotation);
	const FVector Eye = Pawn->GetPawnViewLocation();
	FDeathLogCapture StepLog(TEXT("VeloxDeath step=6"));
	FDeathLogCapture SpectateLog(TEXT("VeloxSpectate "));
	Damage(ASC, 1000);
	W.Advance(0.05f);
	TestTrue(TEXT("spectating after CompleteDeath"), W.PC->IsInState(NAME_Spectating));
	TestTrue(TEXT("death spectate flag"), W.PC->IsDeathSpectating());
	TestTrue(TEXT("no pawn while spectating"), W.PC->GetPawn() == nullptr);
	AVeloxSpectatorPawn* Spectator = Cast<AVeloxSpectatorPawn>(W.PC->GetSpectatorPawn());
	if (!TestNotNull(TEXT("an AVeloxSpectatorPawn (local controller)"), Spectator))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("spectator within 10 cm of the death eye (%s vs %s)"), *Spectator->GetActorLocation().ToCompactString(), *Eye.ToCompactString()),
		FVector::Dist(Spectator->GetActorLocation(), Eye) <= 10.0);
	TestTrue(TEXT("control rotation = rotation at death"), W.PC->GetControlRotation().Equals(DeathRotation, 0.01));
	TestTrue(TEXT("view target = spectator pawn"), W.PC->GetViewTarget() == Spectator);
	TestTrue(TEXT("pawn-or-spectator is the spectator"), W.PC->GetPawnOrSpectator() == Spectator);
	TestTrue(TEXT("the corpse stays"), IsValid(Pawn) && Pawn->IsCorpse());
	GLog->FlushThreadedLogs();
	TestTrue(TEXT("step 6 logged spectate=free"), StepLog.Lines.Num() == 1 && StepLog.Lines[0].Contains(TEXT("spectate=free")));
	TestTrue(TEXT("client begin logged"), SpectateLog.Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("VeloxSpectate begin at=")); }));
	{
		// HUD: the free-look prompt until MinRespawnDelay (C11; UVeloxUISettings::DeathSpectatePrompt).
		UVeloxHUDViewModel* ViewModel = NewObject<UVeloxHUDViewModel>(GetTransientPackage());
		ViewModel->UpdateFromGame(*W.PC);
		TestTrue(TEXT("HUD death overlay"), ViewModel->bShowDeath);
		TestEqual(TEXT("HUD prompt = DeathSpectatePrompt"), ViewModel->DeathPrompt, UVeloxUISettings::Get().DeathSpectatePrompt);
		TestFalse(TEXT("HUD: dead is no spectator"), ViewModel->bSpectator);
		ViewModel->Unbind();
	}

	// 2. Free fly at <= DeathSpectateSpeedCmS.
	const TWeakObjectPtr<AVeloxSpectatorPawn> WeakSpectator(Spectator);
	float PeakSpeed = 0.f;
	for (int32 Tick = 0; Tick < 20; ++Tick)
	{
		Spectator->AddMovementInput(FVector::ForwardVector, 1.f);
		W.Advance(0.05f);
		PeakSpeed = FMath::Max(PeakSpeed, static_cast<float>(Spectator->GetVelocity().Size()));
	}
	const float FinalSpeed = static_cast<float>(Spectator->GetVelocity().Size());
	TestTrue(*FString::Printf(TEXT("peak speed %.1f <= 1.01 x DeathSpectateSpeedCmS"), PeakSpeed), PeakSpeed <= Settings.DeathSpectateSpeedCmS * 1.01f);
	TestTrue(*FString::Printf(TEXT("speed after 1 s %.1f >= 0.9 x DeathSpectateSpeedCmS"), FinalSpeed), FinalSpeed >= Settings.DeathSpectateSpeedCmS * 0.9f);

	// 3. A respawn request before MinRespawnDelay is ignored; after it the player is back with GE_SpawnInit.
	TestTrue(TEXT("test reached the request before MinRespawnDelay"), 1.05f < Settings.MinRespawnDelay);
	W.PC->ServerRequestRespawn();
	TestTrue(TEXT("early request: still spectating"), W.PC->IsInState(NAME_Spectating) && W.PC->GetPawn() == nullptr);
	W.Advance(Settings.MinRespawnDelay - 1.f);
	W.PC->ServerRequestRespawn();
	AVeloxCharacter* NewPawn = Cast<AVeloxCharacter>(W.PC->GetPawn());
	if (!TestNotNull(TEXT("requested respawn after MinRespawnDelay"), NewPawn))
	{
		return false;
	}
	TestTrue(TEXT("a new pawn"), NewPawn != Pawn);
	TestTrue(TEXT("playing"), W.PC->IsInState(NAME_Playing));
	TestFalse(TEXT("death spectate over"), W.PC->IsDeathSpectating());
	TestTrue(TEXT("spectator pawn destroyed"), !WeakSpectator.IsValid() && W.PC->GetSpectatorPawn() == nullptr);
	TestFalse(TEXT("GE_SpawnInit: dead tag removed"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead));
	TestEqual(TEXT("GE_SpawnInit: spawn health"), W.PS->GetHealthSet()->GetHealth(), 125.f);
	TestTrue(TEXT("view target = the new pawn"), W.PC->GetViewTarget() == NewPawn);
	GLog->FlushThreadedLogs();
	TestTrue(TEXT("end_server reason=respawn logged"), SpectateLog.Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("end_server")) && L.Contains(TEXT("reason=respawn")); }));
	TestTrue(TEXT("end reason=respawn logged"), SpectateLog.Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("VeloxSpectate end reason=respawn")); }));

	// 4. Rule Corpse: today's view of the corpse, no spectating state.
	W.Advance(2.f);
	{
		FDeathCameraCVarScope Corpse(0);
		Damage(ASC, 1000);
		W.Advance(0.05f);
		TestTrue(TEXT("corpse rule: view target = corpse"), W.PC->GetViewTarget() == NewPawn);
		TestFalse(TEXT("corpse rule: not spectating"), W.PC->IsInState(NAME_Spectating));
		TestTrue(TEXT("corpse rule: no spectator pawn"), W.PC->GetSpectatorPawn() == nullptr);
		GLog->FlushThreadedLogs();
		TestTrue(TEXT("step 6 logged spectate=corpse"), StepLog.Lines.Num() == 2 && StepLog.Lines[1].Contains(TEXT("spectate=corpse")));
		W.Advance(Settings.MinRespawnDelay + 0.1f);
		W.PC->ServerRequestRespawn();
	}
	AVeloxCharacter* ThirdPawn = Cast<AVeloxCharacter>(W.PC->GetPawn());
	if (!TestNotNull(TEXT("respawn after the corpse view"), ThirdPawn))
	{
		return false;
	}

	// 5. Intermission while spectating: Inactive, the spectator pawn gone.
	W.Advance(2.f);
	Damage(ASC, 1000);
	W.Advance(0.05f);
	TestTrue(TEXT("spectating again"), W.PC->IsInState(NAME_Spectating) && W.PC->GetSpectatorPawn() != nullptr);
	const TWeakObjectPtr<ASpectatorPawn> SecondSpectator(W.PC->GetSpectatorPawn());
	W.PC->EnterIntermission(nullptr);
	TestTrue(TEXT("intermission: inactive"), W.PC->IsInState(NAME_Inactive));
	TestTrue(TEXT("intermission: spectator pawn destroyed"), !SecondSpectator.IsValid() && W.PC->GetSpectatorPawn() == nullptr);
	TestFalse(TEXT("intermission: death spectate over"), W.PC->IsDeathSpectating());
	GLog->FlushThreadedLogs();
	TestTrue(TEXT("end reason=intermission logged"), SpectateLog.Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("VeloxSpectate end reason=intermission")); }));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
