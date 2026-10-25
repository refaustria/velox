// Velox - weapon tests (ADR 0003 sections 4, 5, 9; task 04).
//   Velox.Combat.FireRateBucket      token bucket: exact rate, 2x rate, receive jitter (world-free).
//   Velox.Combat.WeaponTable         slot lookup, weapnext/prev cycling; the DA_WeaponTable asset carries the Q3 numbers.
//   Velox.Combat.TargetDataSerialize FGameplayAbilityTargetData_VeloxShot NetSerialize round trip (struct and handle).
//   Velox.Combat.Inventory           loadout, AddWeapon, fire routing to the current weapon, switch drop/raise timing
//                                    and refire wait, through the ASC input path (private game world).
//   Velox.Combat.HitscanDummy        RG / MG / SG / LG FireTest at a dummy (AVeloxDummyController) 10 m away: the
//                                    Q3 damage reaches its health and armor through TraceShot and GE_Damage.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Bots/VeloxDummyController.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxDamageMath.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/VeloxPlayerState.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "Weapons/VeloxCombatFeedbackComponent.h"
#include "Weapons/VeloxFireRateBucket.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxGameplayAbility_FireHitscan.h"
#include "Weapons/VeloxShotTargetData.h"
#include "Weapons/VeloxWeaponCheats.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"
#include "Weapons/VeloxWeaponTable.h"

namespace VeloxWeaponTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	const UVeloxWeaponTable* LoadTable()
	{
		return LoadObject<UVeloxWeaponTable>(nullptr, TEXT("/Game/Weapons/DA_WeaponTable.DA_WeaponTable"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatFireRateBucketTest, "Velox.Combat.FireRateBucket", VeloxWeaponTest::Flags)

bool FVeloxCombatFireRateBucketTest::RunTest(const FString& Parameters)
{
	// Exact rate: LG 0.05 s for 60 s, MG 0.1 s, rail 1.5 s; none rejected.
	for (const float Interval : { 0.05f, 0.1f, 1.5f })
	{
		FVeloxFireRateBucket Bucket;
		int32 Accepted = 0;
		const int32 Shots = 1200;
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			Accepted += Bucket.TryConsume(100.0 + Index * static_cast<double>(Interval), Interval, 3.f) ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("exact rate %.2f s: all accepted"), Interval), Accepted, Shots);
	}

	// Twice the rate: the burst of 3, then every other shot -> half rejected; long-term rate exactly 1/Interval.
	{
		FVeloxFireRateBucket Bucket;
		const float Interval = 0.05f;
		int32 Accepted = 0;
		const int32 Shots = 400;
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			Accepted += Bucket.TryConsume(Index * 0.025, Interval, 3.f) ? 1 : 0;
		}
		const int32 Expected = 3 + FMath::FloorToInt32((Shots - 1) * 0.025 / Interval);
		TestTrue(FString::Printf(TEXT("2x rate: %d accepted of %d (expected %d +- 1)"), Accepted, Shots, Expected), FMath::Abs(Accepted - Expected) <= 1);
	}

	// Receive jitter: sent every 50 ms, received with up to 40 ms of extra delay (spikes and clumps): all accepted.
	{
		FVeloxFireRateBucket Bucket;
		FRandomStream Stream(1234);
		int32 Accepted = 0;
		const int32 Shots = 2000;
		double LastReceive = 0.0;
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			const double Receive = FMath::Max(Index * 0.05 + Stream.FRandRange(0.0, 0.04), LastReceive);
			LastReceive = Receive;
			Accepted += Bucket.TryConsume(Receive, 0.05f, 3.f) ? 1 : 0;
		}
		TestEqual(TEXT("40 ms jitter at 50 ms: all accepted"), Accepted, Shots);
	}

	// A burst after a pause: capacity shots pass at once, the next is rejected (all over-rate shots are).
	{
		FVeloxFireRateBucket Bucket;
		TestTrue(TEXT("first shot"), Bucket.TryConsume(10.0, 1.0f, 3.f));
		TestTrue(TEXT("burst 2"), Bucket.TryConsume(10.0, 1.0f, 3.f));
		TestTrue(TEXT("burst 3"), Bucket.TryConsume(10.0, 1.0f, 3.f));
		TestFalse(TEXT("over the burst"), Bucket.TryConsume(10.0, 1.0f, 3.f));
		TestFalse(TEXT("still over at +0.5 s"), Bucket.TryConsume(10.5, 1.0f, 3.f));
		TestTrue(TEXT("refilled at +1 s"), Bucket.TryConsume(11.0, 1.0f, 3.f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatWeaponTableTest, "Velox.Combat.WeaponTable", VeloxWeaponTest::Flags)

bool FVeloxCombatWeaponTableTest::RunTest(const FString& Parameters)
{
	// Cycling (Q3 weapnext/weapprev) over owned bits, wrapping.
	TestEqual(TEXT("next from 0 over {0,2,4}"), UVeloxWeaponTable::CycleOwned(0b10101, 6, 0, +1), 2);
	TestEqual(TEXT("next from 4 wraps"), UVeloxWeaponTable::CycleOwned(0b10101, 6, 4, +1), 0);
	TestEqual(TEXT("prev from 0 wraps"), UVeloxWeaponTable::CycleOwned(0b10101, 6, 0, -1), 4);
	TestEqual(TEXT("only the current owned: itself"), UVeloxWeaponTable::CycleOwned(0b00100, 6, 2, +1), 2);
	TestEqual(TEXT("nothing owned"), UVeloxWeaponTable::CycleOwned(0, 6, 0, +1), INDEX_NONE);
	TestEqual(TEXT("no current: next is the first"), UVeloxWeaponTable::CycleOwned(0b10100, 6, INDEX_NONE, +1), 2);
	TestEqual(TEXT("bits beyond the table ignored"), UVeloxWeaponTable::CycleOwned(0b1000000, 6, 0, +1), INDEX_NONE);

	const UVeloxWeaponTable* Table = VeloxWeaponTest::LoadTable();
	if (!TestNotNull(TEXT("DA_WeaponTable (bootstrap script)"), Table))
	{
		return false;
	}
	TestEqual(TEXT("six weapons"), Table->Num(), 6);
	struct FRow { const TCHAR* Name; int32 Slot; float Interval; int32 Damage; int32 Pellets; float RangeCm; bool bHitscan; };
	using namespace VeloxGameplayTags;
	const FRow Rows[] = {
		{ TEXT("MachineGun"), 2, 0.1f, 7, 1, 20480.f, true },
		{ TEXT("Shotgun"), 3, 1.0f, 10, 11, 20480.f, true },
		{ TEXT("RocketLauncher"), 5, 0.8f, 100, 1, 20480.f, false },
		{ TEXT("LightningGun"), 6, 0.05f, 8, 1, 1920.f, true },
		{ TEXT("Railgun"), 7, 1.5f, 100, 1, 20480.f, true },
		{ TEXT("PlasmaGun"), 8, 0.1f, 20, 1, 20480.f, false },
	};
	int32 LastSlot = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rows) && Index < Table->Num(); ++Index)
	{
		const FRow& Row = Rows[Index];
		const UVeloxWeaponDefinition* Def = Table->Get(Index);
		if (!TestNotNull(FString::Printf(TEXT("%s definition"), Row.Name), Def))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s tag"), Row.Name), Def->WeaponTag.ToString().EndsWith(Row.Name));
		TestEqual(FString::Printf(TEXT("%s slot"), Row.Name), Def->Slot, Row.Slot);
		TestTrue(FString::Printf(TEXT("%s in slot order"), Row.Name), Def->Slot > LastSlot);
		LastSlot = Def->Slot;
		TestEqual(FString::Printf(TEXT("%s interval"), Row.Name), Def->FireInterval, Row.Interval, 1e-5f);
		TestEqual(FString::Printf(TEXT("%s damage"), Row.Name), Def->Damage, Row.Damage);
		TestEqual(FString::Printf(TEXT("%s pellets"), Row.Name), Def->PelletCount, Row.Pellets);
		TestEqual(FString::Printf(TEXT("%s range"), Row.Name), Def->RangeCm, Row.RangeCm, 0.01f);
		TestEqual(FString::Printf(TEXT("%s hitscan ability"), Row.Name), Def->FireAbility.Get() == UVeloxGameplayAbility_FireHitscan::StaticClass(), Row.bHitscan);
		TestTrue(FString::Printf(TEXT("%s damage/ammo/cue tags"), Row.Name), Def->DamageTag.IsValid() && Def->AmmoTag.IsValid() && Def->FireCueTag.IsValid() && Def->ImpactCueTag.IsValid());
		TestEqual(FString::Printf(TEXT("%s slot lookup"), Row.Name), Table->FindBySlot(Row.Slot), Index);
		TestEqual(FString::Printf(TEXT("%s index"), Row.Name), Table->IndexOf(Def), Index);
	}
	if (Table->Num() == 6)
	{
		TestEqual(TEXT("MG spread atan(200/8192)"), Table->Get(0)->SpreadAngleDeg, 1.3987f, 1e-3f);
		TestEqual(TEXT("SG spread atan(700/8192)"), Table->Get(1)->SpreadAngleDeg, 4.8838f, 1e-3f);
		TestTrue(TEXT("RG penetrates up to 4"), Table->Get(4)->bPenetratesPawns && Table->Get(4)->MaxPenetrations == 4);
		TestTrue(TEXT("LG beam cue"), Table->Get(3)->bBeam && Table->Get(3)->BeamCueTag == GameplayCue_Weapon_LightningGun_Beam);
		TestEqual(TEXT("RL splash 100 / 300 cm, 2250 cm/s"), FVector(Table->Get(2)->SplashDamage, Table->Get(2)->SplashRadiusCm, Table->Get(2)->ProjectileSpeed), FVector(100, 300, 2250));
		TestEqual(TEXT("PG splash 15 / 50 cm, 5000 cm/s"), FVector(Table->Get(5)->SplashDamage, Table->Get(5)->SplashRadiusCm, Table->Get(5)->ProjectileSpeed), FVector(15, 50, 5000));
		TestEqual(TEXT("slot 1 is empty"), Table->FindBySlot(1), INDEX_NONE);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatTargetDataSerializeTest, "Velox.Combat.TargetDataSerialize", VeloxWeaponTest::Flags)

bool FVeloxCombatTargetDataSerializeTest::RunTest(const FString& Parameters)
{
	FGameplayAbilityTargetData_VeloxShot In;
	In.Origin = FVector(1234.567, -89.012, 155.0);
	In.Dir = FVector(0.6, 0.64, -0.48);
	In.ViewServerTime = 1834.21875f;
	In.ShotSeq = 251;
	In.HintTargetStamp = 77.123456f;

	// Struct NetSerialize without a package map: the hint cannot be named and is dropped (one bit).
	FBitWriter Writer(1024, true);
	bool bOk = false;
	In.NetSerialize(Writer, nullptr, bOk);
	TestTrue(TEXT("write ok"), bOk && !Writer.IsError());
	TestTrue(FString::Printf(TEXT("compact: %lld bits"), Writer.GetNumBits()), Writer.GetNumBits() <= 200);

	FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
	FGameplayAbilityTargetData_VeloxShot Out;
	Out.NetSerialize(Reader, nullptr, bOk);
	TestTrue(TEXT("read ok"), bOk && !Reader.IsError());
	TestTrue(TEXT("origin to 1/100 cm"), Out.Origin.Equals(In.Origin, 0.01));
	TestTrue(TEXT("dir to 1e-4"), Out.Dir.Equals(In.Dir, 1e-4));
	TestEqual(TEXT("view time exact float"), Out.ViewServerTime, In.ViewServerTime);
	TestEqual(TEXT("shot seq"), static_cast<int32>(Out.ShotSeq), 251);
	TestFalse(TEXT("no hint"), Out.bHasHint);

	// The hint bit with a stamp round-trips exactly as a float (B12): checked on the stamp field directly.
	{
		FBitWriter StampWriter(64, true);
		float Stamp = In.HintTargetStamp;
		StampWriter << Stamp;
		FBitReader StampReader(StampWriter.GetData(), StampWriter.GetNumBits());
		float StampOut = 0.f;
		StampReader << StampOut;
		TestEqual(TEXT("stamp exact float"), StampOut, In.HintTargetStamp);
	}

	// Through the handle (script-struct cache of UAbilitySystemGlobals): the type survives.
	FGameplayAbilityTargetDataHandle HandleIn(new FGameplayAbilityTargetData_VeloxShot(In));
	FBitWriter HandleWriter(2048, true);
	HandleIn.NetSerialize(HandleWriter, nullptr, bOk);
	FBitReader HandleReader(HandleWriter.GetData(), HandleWriter.GetNumBits());
	FGameplayAbilityTargetDataHandle HandleOut;
	HandleOut.NetSerialize(HandleReader, nullptr, bOk);
	const FGameplayAbilityTargetData* Raw = HandleOut.Get(0);
	if (TestTrue(TEXT("handle carries one VeloxShot"), HandleOut.Num() == 1 && Raw && Raw->GetScriptStruct() == FGameplayAbilityTargetData_VeloxShot::StaticStruct()))
	{
		const FGameplayAbilityTargetData_VeloxShot* Shot = static_cast<const FGameplayAbilityTargetData_VeloxShot*>(Raw);
		TestEqual(TEXT("handle: shot seq"), static_cast<int32>(Shot->ShotSeq), 251);
		TestTrue(TEXT("handle: origin"), Shot->Origin.Equals(In.Origin, 0.01));
	}
	return true;
}

namespace VeloxWeaponTest
{
	/** Private game world with a floor; combatants are pawns possessed by controllers that own an AVeloxPlayerState. */
	class FWeaponWorld
	{
	public:
		FWeaponWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxWeaponTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
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

		~FWeaponWorld()
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

		/** Pawn at Location possessed by a new controller of ControllerClass with its own Velox PlayerState. */
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
			// No game mode in a private world: the controller gets its PlayerState by hand before possession.
			Controller->PlayerState = PlayerState;
			PlayerState->SetOwner(Controller);
			Controller->Possess(Pawn);
			if (OutController)
			{
				*OutController = Controller;
			}
			return Pawn;
		}

		void Advance(float Seconds, float Dt = 1.f / 60.f)
		{
			for (float T = 0.f; T < Seconds - 1e-4f; T += Dt)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Dt);
			}
		}

		UWorld* World = nullptr;
	};

	AVeloxPlayerState* PS(const AVeloxCharacter* Pawn) { return Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatInventoryTest, "Velox.Combat.Inventory", VeloxWeaponTest::Flags)

bool FVeloxCombatInventoryTest::RunTest(const FString& Parameters)
{
	using namespace VeloxWeaponTest;
	using namespace VeloxGameplayTags;

	FWeaponWorld W;
	AVeloxCharacter* Pawn = W.Spawn(FVector(0, 0, 100), AAIController::StaticClass());
	AVeloxPlayerState* PlayerState = PS(Pawn);
	if (!TestNotNull(TEXT("pawn"), Pawn) || !TestNotNull(TEXT("player state"), PlayerState))
	{
		return false;
	}
	UVeloxWeaponInventoryComponent* Inventory = PlayerState->GetWeaponInventory();
	UVeloxAbilitySystemComponent* ASC = PlayerState->GetVeloxAbilitySystemComponent();
	const UVeloxWeaponTable* Table = Inventory ? Inventory->GetWeaponTable() : nullptr;
	if (!TestNotNull(TEXT("inventory"), Inventory) || !TestNotNull(TEXT("weapon table from the combat settings"), Table) || Table->Num() != 6)
	{
		return false;
	}
	const int32 MG = Table->FindBySlot(2);
	const int32 RG = Table->FindBySlot(7);
	const int32 RL = Table->FindBySlot(5);

	// Spawn: the loadout (MG, selected) with 100 bullets from GE_SpawnInit; one fire spec routed by InputTag.Fire, one switch spec.
	TestEqual(TEXT("loadout: MG only"), static_cast<int32>(Inventory->GetOwnedMask()), 1 << MG);
	TestEqual(TEXT("loadout: MG selected"), Inventory->GetCurrentIndex(), MG);
	TestEqual(TEXT("loadout: 100 bullets"), PlayerState->GetAmmoSet()->GetBullets(), 100.f);
	FGameplayAbilitySpec* MGSpec = Inventory->FindFireSpec(MG);
	if (!TestNotNull(TEXT("MG fire spec"), MGSpec))
	{
		return false;
	}
	TestTrue(TEXT("MG spec: SourceObject = definition, InputTag.Fire"), MGSpec->SourceObject.Get() == Table->Get(MG) && MGSpec->GetDynamicSpecSourceTags().HasTagExact(InputTag_Fire));
	const int32 NumSpecsAtSpawn = ASC->GetActivatableAbilities().Num();

	// AddWeapon grants once and adds ammo; routing passes only the current weapon's fire spec.
	TestTrue(TEXT("add RG"), Inventory->AddWeapon(Table->Get(RG), 10));
	TestTrue(TEXT("add RG again (ammo only)"), Inventory->AddWeapon(Table->Get(RG), 5));
	TestTrue(TEXT("owns RG"), Inventory->OwnsIndex(RG));
	TestEqual(TEXT("one spec more"), ASC->GetActivatableAbilities().Num(), NumSpecsAtSpawn + 1);
	TestEqual(TEXT("slugs 15"), PlayerState->GetAmmoSet()->GetSlugs(), 15.f);
	TestTrue(TEXT("add RL (no fire ability yet)"), Inventory->AddWeapon(Table->Get(RL), 5));
	TestNull(TEXT("RL has no fire spec until task 05"), Inventory->FindFireSpec(RL));
	MGSpec = Inventory->FindFireSpec(MG);
	FGameplayAbilitySpec* RGSpec = Inventory->FindFireSpec(RG);
	if (!TestNotNull(TEXT("RG fire spec"), RGSpec) || !MGSpec)
	{
		return false;
	}
	TestTrue(TEXT("route: MG (current)"), ASC->CanRouteInputToSpec(*MGSpec));
	TestFalse(TEXT("route: not RG"), ASC->CanRouteInputToSpec(*RGSpec));
	TestEqual(TEXT("select slot 7 -> RG"), Inventory->ResolveSelectInput(InputTag_Weapon_Slot7), RG);
	TestEqual(TEXT("select slot 3 (not owned) -> none"), Inventory->ResolveSelectInput(InputTag_Weapon_Slot3), INDEX_NONE);
	TestEqual(TEXT("next from MG -> RL (slot order)"), Inventory->ResolveSelectInput(InputTag_Weapon_Next), RL);
	TestEqual(TEXT("prev from MG -> RG (wraps)"), Inventory->ResolveSelectInput(InputTag_Weapon_Prev), RG);

	// Fire the MG through the input path (WhileInputActive), then switch to RG by slot key: refire wait 0.1 s, drop
	// 0.2 s (index changes), raise 0.25 s (fire blocked by Velox.State.WeaponSwitching).
	W.Advance(0.1f);
	const float BulletsBefore = PlayerState->GetAmmoSet()->GetBullets();
	ASC->AbilityInputTagPressed(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	TestEqual(TEXT("MG fired through input: one bullet"), PlayerState->GetAmmoSet()->GetBullets(), BulletsBefore - 1.f);
	const UVeloxGameplayAbility_Fire* MGFire = Cast<UVeloxGameplayAbility_Fire>(MGSpec->GetPrimaryInstance());
	const double T0 = W.World->GetTimeSeconds();
	TestTrue(TEXT("MG next fire time = now + 0.1"), MGFire && FMath::IsNearlyEqual(MGFire->GetNextFireTime(), T0 + 0.1, 1e-3));

	ASC->AbilityInputTagPressed(InputTag_Weapon_Slot7);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Weapon_Slot7);
	TestTrue(TEXT("switching tag at once"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));
	TestEqual(TEXT("still MG during refire wait + drop"), Inventory->GetCurrentIndex(), MG);
	W.Advance(0.25f); // t = 0.25 < 0.1 + 0.2
	TestEqual(TEXT("still MG at 0.25 s (refire 0.1 + drop 0.2)"), Inventory->GetCurrentIndex(), MG);
	W.Advance(0.1f); // t = 0.35
	TestEqual(TEXT("RG after refire + drop"), Inventory->GetCurrentIndex(), RG);
	TestTrue(TEXT("raising: still switching"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));
	ASC->AbilityInputTagPressed(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	TestEqual(TEXT("no rail during the raise"), PlayerState->GetAmmoSet()->GetSlugs(), 15.f);
	W.Advance(0.25f); // t = 0.6 > 0.55
	TestFalse(TEXT("raise done"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));
	ASC->ProcessAbilityInput(1.f / 60.f, false); // fire still held: WhileInputActive re-try
	TestEqual(TEXT("held fire shoots the rail after the raise"), PlayerState->GetAmmoSet()->GetSlugs(), 14.f);
	ASC->AbilityInputTagReleased(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);

	// Respawn semantics: the loadout again, RG/RL taken away.
	Inventory->ResetToLoadout();
	TestEqual(TEXT("reset: MG only"), static_cast<int32>(Inventory->GetOwnedMask()), 1 << MG);
	TestNull(TEXT("reset: RG spec removed"), Inventory->FindFireSpec(RG));
	TestNotNull(TEXT("reset: MG spec kept"), Inventory->FindFireSpec(MG));
	TestEqual(TEXT("reset: MG selected"), Inventory->GetCurrentIndex(), MG);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatHitscanDummyTest, "Velox.Combat.HitscanDummy", VeloxWeaponTest::Flags)

bool FVeloxCombatHitscanDummyTest::RunTest(const FString& Parameters)
{
	using namespace VeloxWeaponTest;

	FWeaponWorld W;
	AAIController* DummyController = nullptr;
	AVeloxCharacter* Shooter = W.Spawn(FVector(0, 0, 100), AAIController::StaticClass());
	AVeloxCharacter* Dummy = W.Spawn(FVector(1000, 0, 100), AVeloxDummyController::StaticClass(), &DummyController);
	if (!TestNotNull(TEXT("shooter"), Shooter) || !TestNotNull(TEXT("dummy"), Dummy) || !PS(Shooter) || !PS(Dummy))
	{
		return false;
	}
	if (AVeloxDummyController* Strafer = Cast<AVeloxDummyController>(DummyController))
	{
		Strafer->StrafeSpeedScale = 0.f; // a standing dummy: the numbers, not the aim, are under test
	}
	W.Advance(0.2f); // land, record lag-comp samples
	UVeloxLagCompensationSubsystem* LagComp = UVeloxLagCompensationSubsystem::Get(W.World);
	TestTrue(TEXT("both pawns registered for lag compensation"), LagComp && LagComp->IsRegistered(Shooter) && LagComp->IsRegistered(Dummy));
	TestTrue(TEXT("give all"), VeloxWeaponCheats::GiveAllWeapons(*PS(Shooter)));

	UVeloxAbilitySystemComponent* DummyASC = PS(Dummy)->GetVeloxAbilitySystemComponent();
	const UVeloxHealthSet* DummyHealth = PS(Dummy)->GetHealthSet();
	UVeloxWeaponInventoryComponent* Inventory = PS(Shooter)->GetWeaponInventory();
	const UVeloxWeaponTable* Table = Inventory->GetWeaponTable();

	struct FCase { const TCHAR* Weapon; int32 Slot; };
	const FCase Cases[] = { { TEXT("RG"), 7 }, { TEXT("MG"), 2 }, { TEXT("SG"), 3 }, { TEXT("LG"), 6 } };
	for (const FCase& Case : Cases)
	{
		// Let the previous knockback play out, then reset the dummy: 100 health / 100 armor, back in place, at rest.
		W.Advance(0.1f);
		DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), 100.f);
		DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), 100.f);
		Dummy->SetActorLocation(FVector(1000, 0, Dummy->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		Dummy->GetCharacterMovement()->Velocity = FVector::ZeroVector;

		const int32 Index = Table->FindBySlot(Case.Slot);
		const UVeloxWeaponDefinition* Def = Table->Get(Index);
		FString Error;
		const bool bFired = VeloxWeaponCheats::FireTestShot(*Shooter, Case.Weapon, Dummy, 0.f, Error);
		if (!TestTrue(FString::Printf(TEXT("%s fired (%s)"), Case.Weapon, *Error), bFired))
		{
			continue;
		}
		const FGameplayAbilitySpec* Spec = Inventory->FindFireSpec(Index);
		const UVeloxGameplayAbility_Fire* Fire = Spec ? Cast<UVeloxGameplayAbility_Fire>(Spec->GetPrimaryInstance()) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s fire instance"), Case.Weapon), Fire))
		{
			continue;
		}
		const FVeloxServerShotInfo& Shot = Fire->GetLastServerShot();
		TestFalse(FString::Printf(TEXT("%s not rejected"), Case.Weapon), Shot.bRejected);
		TestEqual(FString::Printf(TEXT("%s hit the dummy"), Case.Weapon), Shot.NumHits, 1);
		TestTrue(FString::Printf(TEXT("%s pellets hit: %d"), Case.Weapon, Shot.PelletHits), Shot.PelletHits >= 1 && Shot.PelletHits <= Def->PelletCount);

		FVeloxDamageInput In;
		In.Damage = Def->Damage;
		In.Armor = 100;
		const FVeloxDamageResult Expected = FVeloxDamageMath::ApplyPellets(In, FMath::Max(Shot.PelletHits, 1), PS(Shooter)->GetCombatSettings()->BuildCombatParams());
		TestEqual(FString::Printf(TEXT("%s health 100 - %d"), Case.Weapon, Expected.HealthLoss), DummyHealth->GetHealth(), 100.f - Expected.HealthLoss);
		TestEqual(FString::Printf(TEXT("%s armor 100 - %d"), Case.Weapon, Expected.ArmorLoss), DummyHealth->GetArmor(), 100.f - Expected.ArmorLoss);
		TestEqual(FString::Printf(TEXT("%s total damage reported"), Case.Weapon), Shot.TotalDamage, Expected.HealthLoss + Expected.ArmorLoss);
		if (Case.Slot == 7)
		{
			TestEqual(TEXT("rail: Q3 34/66 vs 100 armor"), FVector2D(Expected.HealthLoss, Expected.ArmorLoss), FVector2D(34, 66));
		}
	}

	// The LG (last case) keeps its looping beam cue alive on the server; with no further shot it stops after 2.5 intervals.
	UVeloxAbilitySystemComponent* ShooterASC = PS(Shooter)->GetVeloxAbilitySystemComponent();
	TestTrue(TEXT("LG beam cue active after the shot"), ShooterASC->IsGameplayCueActive(VeloxGameplayTags::GameplayCue_Weapon_LightningGun_Beam));
	TestTrue(TEXT("beam reached the pawn's feedback component"), Shooter->GetCombatFeedbackComponent() && Shooter->GetCombatFeedbackComponent()->IsBeamActive());
	W.Advance(0.2f);
	TestFalse(TEXT("LG beam cue removed after release"), ShooterASC->IsGameplayCueActive(VeloxGameplayTags::GameplayCue_Weapon_LightningGun_Beam));
	TestFalse(TEXT("feedback beam off"), Shooter->GetCombatFeedbackComponent() && Shooter->GetCombatFeedbackComponent()->IsBeamActive());

	// Same weapon again within its interval: the shooter's own NextFireTime refuses (one rail per 1.5 s).
	FString Error;
	TestFalse(TEXT("rail refire blocked within 1.5 s"), VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("RG"), Dummy, 0.f, Error));

	// LG range 1920 cm: a dummy at 25 m is out of reach.
	W.Advance(1.6f);
	DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), 100.f);
	Dummy->SetActorLocation(FVector(2500, 0, Dummy->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
	Dummy->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	TestTrue(TEXT("LG fired at 25 m"), VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("LG"), Dummy, 0.f, Error));
	TestEqual(TEXT("LG out of range: no damage"), DummyHealth->GetHealth(), 100.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
