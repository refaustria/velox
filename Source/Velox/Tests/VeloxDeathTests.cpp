// Velox - death, respawn and readability tests (ADR 0003 sections 8 and 10, task 06).
//   Velox.GameMode.SpawnSelection  world-free: farthest half by nearest-enemy distance, occupied starts, telefrag only when
//                                  every start is occupied, avoid radius, no enemies = uniform, determinism for a seed.
//   Velox.GameMode.DeathFlow       private game world: B8 order (log), death tag, unpossessed, no avatar, collision off,
//                                  hitbox unregistered, second damage of the frame yields nothing, corpse removed after
//                                  CorpseLifetime, death camera distance, respawn with a fresh hitbox and no dead tag.
//   Velox.GameMode.SpawnProtection mechanism (default 0 s = off), protected pawn takes nothing, first shot ends it, expiry.
//   Velox.GameMode.PlayerColors    palette is bright and distinct; the PlayerState colour reaches the pawn's body material.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
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
#include "GameFramework/WorldSettings.h"
#include "GameModes/VeloxPlayerState.h"
#include "GameModes/VeloxSpawnSelection.h"
#include "Materials/MaterialInterface.h"
#include "Misc/OutputDevice.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "Weapons/VeloxGameplayAbility_FireHitscan.h"

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
		FDeathTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxDeathTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
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
				Floor->SetActorScale3D(FVector(40, 40, 1));
			}
			PlayerState = World->SpawnActor<AVeloxPlayerState>();
			Controller = World->SpawnActor<AAIController>();
			if (Controller)
			{
				Controller->PlayerState = PlayerState;
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

#endif // WITH_DEV_AUTOMATION_TESTS
