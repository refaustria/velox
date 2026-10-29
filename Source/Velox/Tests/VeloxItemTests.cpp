// Velox - item tests (ADR 0004 sections 1, 2, 9, 10; task M3-01).
//   Velox.Items.PickupRules   world-free Q3 rules: the section 2 table (health / armor / ammo caps, weapon ammo top-up,
//                             powerup duration sum), respawn seconds per type and mode, initial delay, weapon-stay, and the
//                             autoswitch rule of the inventory (Q3 cg_autoswitch).
//   Velox.Items.PickupTiming  FVeloxPickupTimer: take at t -> unavailable, exact TimeRemaining, available at exactly
//                             t + 35, initial delay, Reset, ItemRespawnScale.
//   Velox.Items.Content       the DA_Item_* assets of create_item_content.py carry the Q3 table and the native tags.
//   Velox.Items.Pickup        private game world: Health.Medium refused at 100, taken at 50 -> 75 while standing in it,
//                             replicated state + registry delegates, respawn at exactly +35 s and the standing pawn retakes
//                             it, Reset; armor / ammo / weapon grants (weapon ammo top-up +1), mode respawn scale, a powerup
//                             taken (tag, 30 s); client prediction (hide, predicted weapon slot, timeout -> shown).
//   Velox.Items.PowerupStacking  task 02: powerup GE layout, grant / in-place extension (ModifyActiveEffectStartTime) with
//                             the rule's arithmetic, Quad x3 on a dummy, Battle Suit, Regen ticks and decay skip, loss on
//                             GE_SpawnInit, no regen while dead, frozen = no damage (once task 05 is merged), projectile
//                             Quad state and Reset.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Components/CapsuleComponent.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/VeloxPlayerState.h"
#include "Movement/VeloxMovementComponent.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "Items/VeloxPickupRules.h"
#include "Items/VeloxPowerupEffects.h"
#include "Weapons/VeloxProjectile.h"
#include "Weapons/VeloxCombatFeedbackComponent.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"
#include "Weapons/VeloxWeaponTable.h"

namespace VeloxItemTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FVeloxPickupQuery HealthQuery(int32 Quantity, EVeloxHealthCap Cap, float Health)
	{
		FVeloxPickupQuery Q;
		Q.Type = EVeloxItemType::Health;
		Q.Quantity = Quantity;
		Q.HealthCap = Cap;
		Q.Health = Health;
		Q.MaxHealth = 100.f;
		Q.HealthHardCap = 200.f;
		Q.MaxArmor = 200.f;
		return Q;
	}

	FVeloxPickupQuery ArmorQuery(int32 Quantity, float Armor)
	{
		FVeloxPickupQuery Q = HealthQuery(0, EVeloxHealthCap::MaxHealth, 100.f);
		Q.Type = EVeloxItemType::Armor;
		Q.Quantity = Quantity;
		Q.Armor = Armor;
		return Q;
	}

	FVeloxPickupQuery AmmoQuery(EVeloxItemType Type, int32 Quantity, int32 Have, int32 Max = 200)
	{
		FVeloxPickupQuery Q = HealthQuery(0, EVeloxHealthCap::MaxHealth, 100.f);
		Q.Type = Type;
		Q.Quantity = Quantity;
		Q.Ammo = Have;
		Q.MaxAmmo = Max;
		return Q;
	}
}

// ------------------------------------------------------------------------------------------------------ PickupRules

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxItemsPickupRulesTest, "Velox.Items.PickupRules", VeloxItemTest::Flags)

bool FVeloxItemsPickupRulesTest::RunTest(const FString& Parameters)
{
	using namespace VeloxItemTest;
	using namespace VeloxPickupRules;

	struct FRow { const TCHAR* Name; FVeloxPickupQuery Query; bool bGrab; int32 Amount; };
	const FRow Rows[] = {
		{ TEXT("health 5 at 199 -> 200"), HealthQuery(5, EVeloxHealthCap::HardCap, 199.f), true, 1 },
		{ TEXT("health 5 at 200 refused"), HealthQuery(5, EVeloxHealthCap::HardCap, 200.f), false, 0 },
		{ TEXT("health 5 at 100 -> 105 (stacks past max)"), HealthQuery(5, EVeloxHealthCap::HardCap, 100.f), true, 5 },
		{ TEXT("health 25 at 100 refused"), HealthQuery(25, EVeloxHealthCap::MaxHealth, 100.f), false, 0 },
		{ TEXT("health 25 at 90 -> 100"), HealthQuery(25, EVeloxHealthCap::MaxHealth, 90.f), true, 10 },
		{ TEXT("health 25 at 50 -> 75"), HealthQuery(25, EVeloxHealthCap::MaxHealth, 50.f), true, 25 },
		{ TEXT("health 50 at 125 refused (above max)"), HealthQuery(50, EVeloxHealthCap::MaxHealth, 125.f), false, 0 },
		{ TEXT("mega at 150 -> 200"), HealthQuery(100, EVeloxHealthCap::HardCap, 150.f), true, 50 },
		{ TEXT("mega at 50 -> 150"), HealthQuery(100, EVeloxHealthCap::HardCap, 50.f), true, 100 },
		{ TEXT("shard at 199 -> 200"), ArmorQuery(5, 199.f), true, 1 },
		{ TEXT("YA at 150 -> 200"), ArmorQuery(50, 150.f), true, 50 },
		{ TEXT("YA at 180 -> 200"), ArmorQuery(50, 180.f), true, 20 },
		{ TEXT("RA at 200 refused"), ArmorQuery(100, 200.f), false, 0 },
		{ TEXT("RA at 0 -> 100"), ArmorQuery(100, 0.f), true, 100 },
		{ TEXT("ammo at 200 refused"), AmmoQuery(EVeloxItemType::Ammo, 10, 200), false, 0 },
		{ TEXT("ammo 50 at 180 -> 200"), AmmoQuery(EVeloxItemType::Ammo, 50, 180), true, 20 },
		{ TEXT("ammo 10 at 5 -> 15"), AmmoQuery(EVeloxItemType::Ammo, 10, 5), true, 10 },
		{ TEXT("weapon ammo top-up 40 - 10 = 30"), AmmoQuery(EVeloxItemType::Weapon, 40, 10), true, 30 },
		{ TEXT("weapon already 60 -> +1"), AmmoQuery(EVeloxItemType::Weapon, 40, 60), true, 1 },
		{ TEXT("weapon exactly 40 -> +1"), AmmoQuery(EVeloxItemType::Weapon, 40, 40), true, 1 },
		{ TEXT("weapon at the ammo cap: grabbed, +0"), AmmoQuery(EVeloxItemType::Weapon, 10, 200), true, 0 },
	};
	for (const FRow& Row : Rows)
	{
		const FVeloxPickupGain Result = Gain(Row.Query);
		TestEqual(FString::Printf(TEXT("%s: CanGrab"), Row.Name), CanGrab(Row.Query), Row.bGrab);
		TestEqual(FString::Printf(TEXT("%s: grab"), Row.Name), Result.bGrab, Row.bGrab);
		TestEqual(FString::Printf(TEXT("%s: amount"), Row.Name), Result.Amount, Row.Amount);
	}

	// Powerups: always grabbable, the item's seconds add to what is left (Q3 Pickup_Powerup), no cap.
	FVeloxPickupQuery Powerup;
	Powerup.Type = EVeloxItemType::Powerup;
	Powerup.PowerupSeconds = 30.f;
	Powerup.PowerupRemaining = 10.f;
	TestTrue(TEXT("powerup grabbable"), CanGrab(Powerup));
	TestEqual(TEXT("powerup 30 + remaining 10 -> 40"), Gain(Powerup).PowerupSeconds, 40.f);
	Powerup.PowerupRemaining = 0.f;
	TestEqual(TEXT("powerup fresh -> 30"), Gain(Powerup).PowerupSeconds, 30.f);
	Powerup.PowerupRemaining = 55.f;
	TestEqual(TEXT("powerup no cap: 55 + 30 -> 85"), Gain(Powerup).PowerupSeconds, 85.f);

	// Respawn seconds and mode rules.
	FVeloxItemModeRules Rules;
	TestEqual(TEXT("health respawn 35 (scale 1)"), RespawnSecondsFor(EVeloxItemType::Health, 35.f, Rules), 35.0);
	TestEqual(TEXT("weapon respawn from the mode (5)"), RespawnSecondsFor(EVeloxItemType::Weapon, 35.f, Rules), 5.0);
	Rules.ItemRespawnScale = 0.5f;
	Rules.WeaponRespawnSeconds = 30.f;
	TestEqual(TEXT("armor 25 x 0.5 = 12.5"), RespawnSecondsFor(EVeloxItemType::Armor, 25.f, Rules), 12.5);
	TestEqual(TEXT("weapons ignore the item scale (30)"), RespawnSecondsFor(EVeloxItemType::Weapon, 5.f, Rules), 30.0);
	TestFalse(TEXT("no weapon-stay by default"), StaysAfterTake(EVeloxItemType::Weapon, FVeloxItemModeRules()));
	Rules.bWeaponStay = true;
	TestTrue(TEXT("weapon-stay keeps weapons"), StaysAfterTake(EVeloxItemType::Weapon, Rules));
	TestFalse(TEXT("weapon-stay keeps only weapons"), StaysAfterTake(EVeloxItemType::Ammo, Rules));
	TestEqual(TEXT("initial delay min"), InitialDelayFor(FFloatInterval(30.f, 60.f), 0.f), 30.0);
	TestEqual(TEXT("initial delay max"), InitialDelayFor(FFloatInterval(30.f, 60.f), 1.f), 60.0);
	TestEqual(TEXT("initial delay mid"), InitialDelayFor(FFloatInterval(30.f, 60.f), 0.5f), 45.0);
	TestEqual(TEXT("initial delay 0-0"), InitialDelayFor(FFloatInterval(0.f, 0.f), 0.7f), 0.0);

	// Autoswitch (Q3 cg_autoswitch, owning client): one new weapon, setting on, holding MG/gauntlet, not the current one.
	using Inv = UVeloxWeaponInventoryComponent;
	constexpr uint16 MG = 1 << 0, SG = 1 << 1, RL = 1 << 2, RG = 1 << 4;
	TestEqual(TEXT("autoswitch: MG + RG pickup -> RG"), Inv::SelectAutoSwitchTarget(true, true, 0, MG, MG | RG), 4);
	TestEqual(TEXT("autoswitch: setting off"), Inv::SelectAutoSwitchTarget(false, true, 0, MG, MG | RG), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("autoswitch: holding RL"), Inv::SelectAutoSwitchTarget(true, false, 2, MG | RL, MG | RL | RG), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("autoswitch: already owned (ammo only)"), Inv::SelectAutoSwitchTarget(true, true, 0, MG | RG, MG | RG), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("autoswitch: GiveAllWeapons (several new) never"), Inv::SelectAutoSwitchTarget(true, true, 0, MG, MG | SG | RL | RG), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("autoswitch: first replication from an empty mask never"), Inv::SelectAutoSwitchTarget(true, true, INDEX_NONE, 0, MG), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("autoswitch: the new weapon is already current"), Inv::SelectAutoSwitchTarget(true, true, 4, MG, MG | RG), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("autoswitch: respawn loadout shrink never"), Inv::SelectAutoSwitchTarget(true, true, 0, MG | RG, MG), static_cast<int32>(INDEX_NONE));
	TestTrue(TEXT("autoswitch from no weapon"), Inv::AllowsAutoSwitchFrom(nullptr));
	TestTrue(TEXT("slot input tag 7"), Inv::SlotInputTag(7) == VeloxGameplayTags::InputTag_Weapon_Slot7.GetTag());
	TestFalse(TEXT("slot input tag 0 invalid"), Inv::SlotInputTag(0).IsValid());
	return true;
}

// ----------------------------------------------------------------------------------------------------- PickupTiming

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxItemsPickupTimingTest, "Velox.Items.PickupTiming", VeloxItemTest::Flags)

bool FVeloxItemsPickupTimingTest::RunTest(const FString& Parameters)
{
	using namespace VeloxPickupRules;

	FVeloxPickupTimer Timer;
	TestTrue(TEXT("fresh: available"), Timer.IsAvailable(0.0));
	TestEqual(TEXT("fresh: nothing remaining"), Timer.TimeRemaining(12.0), 0.0);

	const double T = 123.456;
	Timer.Take(T, 35.0);
	TestFalse(TEXT("take at t -> unavailable at t"), Timer.IsAvailable(T));
	TestEqual(TEXT("TimeRemaining at t = 35"), Timer.TimeRemaining(T), 35.0);
	TestEqual(TEXT("TimeRemaining at t + 10 = 25 (exact)"), Timer.TimeRemaining(T + 10.0), 35.0 - 10.0);
	TestFalse(TEXT("unavailable just before t + 35"), Timer.IsAvailable(T + 35.0 - 1e-6));
	TestTrue(TEXT("available at exactly t + 35"), Timer.IsAvailable(T + 35.0));
	TestEqual(TEXT("respawn instant exact"), Timer.RespawnTime, T + 35.0);
	TestEqual(TEXT("TimeRemaining 0 once available"), Timer.TimeRemaining(T + 40.0), 0.0);
	Timer.MarkRespawned();
	TestTrue(TEXT("respawned"), Timer.IsAvailable(T + 35.0) && Timer.RespawnTime == 0.0);

	// Initial delay (powerups 30-60 s): unavailable until the drawn instant.
	const double Start = 2.0;
	const double Delay = InitialDelayFor(FFloatInterval(30.f, 60.f), 0.25f);
	Timer.ScheduleInitial(Start, Delay);
	TestEqual(TEXT("initial delay 37.5"), Delay, 37.5);
	TestFalse(TEXT("initial delay honoured"), Timer.IsAvailable(Start + Delay - 0.01));
	TestTrue(TEXT("available after the initial delay"), Timer.IsAvailable(Start + Delay));
	Timer.ScheduleInitial(Start, 0.0);
	TestTrue(TEXT("no initial delay -> available"), Timer.IsAvailable(Start));

	// Reset: available again whatever the state.
	Timer.Take(50.0, 120.0);
	Timer.Reset();
	TestTrue(TEXT("Reset -> available"), Timer.IsAvailable(50.0) && Timer.TimeRemaining(50.0) == 0.0);

	// ItemRespawnScale through the respawn rule.
	FVeloxItemModeRules Rules;
	Rules.ItemRespawnScale = 2.f;
	Timer.Take(10.0, RespawnSecondsFor(EVeloxItemType::Health, 35.f, Rules));
	TestEqual(TEXT("ItemRespawnScale 2 -> back at t + 70"), Timer.RespawnTime, 80.0);
	TestFalse(TEXT("ItemRespawnScale: still down at t + 35"), Timer.IsAvailable(45.0));

	// Zero respawn: a take leaves it available (weapon-stay uses no take at all).
	Timer.Take(10.0, 0.0);
	TestTrue(TEXT("respawn 0 -> available"), Timer.IsAvailable(10.0));
	return true;
}

// ---------------------------------------------------------------------------------------------------------- Content

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxItemsContentTest, "Velox.Items.Content", VeloxItemTest::Flags)

bool FVeloxItemsContentTest::RunTest(const FString& Parameters)
{
	using namespace VeloxGameplayTags;
	struct FRow { const TCHAR* Name; FGameplayTag Tag; EVeloxItemType Type; int32 Quantity; float Respawn; bool bMajor; };
	const FRow Rows[] = {
		{ TEXT("Health.Small"), Item_Health_Small, EVeloxItemType::Health, 5, 35.f, false },
		{ TEXT("Health.Medium"), Item_Health_Medium, EVeloxItemType::Health, 25, 35.f, false },
		{ TEXT("Health.Large"), Item_Health_Large, EVeloxItemType::Health, 50, 35.f, false },
		{ TEXT("Health.Mega"), Item_Health_Mega, EVeloxItemType::Health, 100, 35.f, true },
		{ TEXT("Armor.Shard"), Item_Armor_Shard, EVeloxItemType::Armor, 5, 25.f, false },
		{ TEXT("Armor.Yellow"), Item_Armor_Yellow, EVeloxItemType::Armor, 50, 25.f, true },
		{ TEXT("Armor.Red"), Item_Armor_Red, EVeloxItemType::Armor, 100, 25.f, true },
		{ TEXT("Ammo.Bullets"), Item_Ammo_Bullets, EVeloxItemType::Ammo, 50, 40.f, false },
		{ TEXT("Ammo.Shells"), Item_Ammo_Shells, EVeloxItemType::Ammo, 10, 40.f, false },
		{ TEXT("Ammo.Rockets"), Item_Ammo_Rockets, EVeloxItemType::Ammo, 5, 40.f, false },
		{ TEXT("Ammo.Cells"), Item_Ammo_Cells, EVeloxItemType::Ammo, 30, 40.f, false },
		{ TEXT("Ammo.Lightning"), Item_Ammo_Lightning, EVeloxItemType::Ammo, 60, 40.f, false },
		{ TEXT("Ammo.Slugs"), Item_Ammo_Slugs, EVeloxItemType::Ammo, 10, 40.f, false },
		{ TEXT("Weapon.MachineGun"), Item_Weapon_MachineGun, EVeloxItemType::Weapon, 40, 5.f, false },
		{ TEXT("Weapon.Shotgun"), Item_Weapon_Shotgun, EVeloxItemType::Weapon, 10, 5.f, false },
		{ TEXT("Weapon.RocketLauncher"), Item_Weapon_RocketLauncher, EVeloxItemType::Weapon, 10, 5.f, false },
		{ TEXT("Weapon.LightningGun"), Item_Weapon_LightningGun, EVeloxItemType::Weapon, 100, 5.f, false },
		{ TEXT("Weapon.Railgun"), Item_Weapon_Railgun, EVeloxItemType::Weapon, 10, 5.f, false },
		{ TEXT("Weapon.PlasmaGun"), Item_Weapon_PlasmaGun, EVeloxItemType::Weapon, 50, 5.f, false },
		{ TEXT("Powerup.Quad"), Item_Powerup_Quad, EVeloxItemType::Powerup, 0, 120.f, true },
		{ TEXT("Powerup.Haste"), Item_Powerup_Haste, EVeloxItemType::Powerup, 0, 120.f, true },
		{ TEXT("Powerup.Regen"), Item_Powerup_Regen, EVeloxItemType::Powerup, 0, 120.f, true },
		{ TEXT("Powerup.BattleSuit"), Item_Powerup_BattleSuit, EVeloxItemType::Powerup, 0, 120.f, true },
	};
	for (const FRow& Row : Rows)
	{
		const UVeloxItemDefinition* Def = AVeloxPickup::LoadDefinitionByName(Row.Name);
		if (!TestNotNull(FString::Printf(TEXT("%s: asset (run Tools/editor/create_item_content.py)"), Row.Name), Def))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: tag %s"), Row.Name, *Def->ItemTag.ToString()), Def->ItemTag == Row.Tag);
		TestEqual(FString::Printf(TEXT("%s: type"), Row.Name), static_cast<int32>(Def->Type), static_cast<int32>(Row.Type));
		TestEqual(FString::Printf(TEXT("%s: quantity"), Row.Name), Def->GetEffectiveQuantity(), Row.Quantity);
		TestEqual(FString::Printf(TEXT("%s: respawn"), Row.Name), Def->RespawnSeconds, Row.Respawn);
		TestEqual(FString::Printf(TEXT("%s: major"), Row.Name), Def->bMajorItem, Row.bMajor);
		if (Row.Type == EVeloxItemType::Weapon)
		{
			TestTrue(FString::Printf(TEXT("%s: weapon definition"), Row.Name), Def->Weapon != nullptr);
		}
		if (Row.Type == EVeloxItemType::Ammo)
		{
			TestTrue(FString::Printf(TEXT("%s: ammo tag"), Row.Name), Def->AmmoTag.IsValid());
		}
		if (Row.Type == EVeloxItemType::Powerup)
		{
			TestEqual(FString::Printf(TEXT("%s: 30 s"), Row.Name), Def->PowerupSeconds, 30.f);
			TestTrue(FString::Printf(TEXT("%s: initial delay 30-60"), Row.Name), Def->InitialSpawnDelay.Min == 30.f && Def->InitialSpawnDelay.Max == 60.f);
			TestTrue(FString::Printf(TEXT("%s: global sound"), Row.Name), Def->bGlobalPickupSound);
			const UVeloxGE_Powerup* Effect = Def->PowerupEffect ? Def->PowerupEffect->GetDefaultObject<UVeloxGE_Powerup>() : nullptr;
			TestTrue(FString::Printf(TEXT("%s: powerup effect grants Velox.Powerup.%s"), Row.Name, *FString(Row.Name).RightChop(8)),
				Effect && Effect->GetPowerupTag().ToString() == FString(TEXT("Velox.")) + Row.Name);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s: no initial delay"), Row.Name), Def->InitialSpawnDelay.Min == 0.f && Def->InitialSpawnDelay.Max == 0.f);
		}
	}
	const UVeloxItemDefinition* Mega = AVeloxPickup::LoadDefinitionByName(TEXT("Health.Mega"));
	const UVeloxItemDefinition* Medium = AVeloxPickup::LoadDefinitionByName(TEXT("Velox.Item.Health.Medium"));
	TestTrue(TEXT("Mega and Small stack to the hard cap, Medium to max health"), Mega && Medium && Mega->HealthCap == EVeloxHealthCap::HardCap && Medium->HealthCap == EVeloxHealthCap::MaxHealth);
	TestNull(TEXT("unknown name"), AVeloxPickup::LoadDefinitionByName(TEXT("Health.Huge")));
	return true;
}

// ----------------------------------------------------------------------------------------------------------- Pickup

namespace VeloxItemTest
{
	/** Private game world with a floor and one pawn (AI controller, own Velox PlayerState). */
	class FItemTestWorld
	{
	public:
		FItemTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxItemTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
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

		~FItemTestWorld()
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

		AVeloxCharacter* SpawnPawn(const FVector& Location)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVeloxCharacter* Pawn = World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
			AVeloxPlayerState* PlayerState = World->SpawnActor<AVeloxPlayerState>();
			AAIController* Controller = World->SpawnActor<AAIController>(AAIController::StaticClass(), FTransform::Identity, Params);
			if (!Pawn || !PlayerState || !Controller)
			{
				return nullptr;
			}
			Controller->PlayerState = PlayerState;
			PlayerState->SetOwner(Controller);
			Controller->Possess(Pawn);
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

		/** 60 Hz frames, then one partial frame, so the world time ends at Target (>= Target by float rounding). */
		void AdvanceTo(double Target)
		{
			constexpr float Dt = 1.f / 60.f;
			while (World->GetTimeSeconds() + Dt < Target)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Dt);
			}
			const double Rest = Target - World->GetTimeSeconds();
			if (Rest > 0.0)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, static_cast<float>(Rest + 1e-6));
			}
		}

		UWorld* World = nullptr;
	};

	const UVeloxHealthSet* HealthSetOf(const AVeloxCharacter* Pawn)
	{
		const AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
		return PS ? PS->GetHealthSet() : nullptr;
	}

	/** Server-side: health to exactly Target (environment damage down, GE_Heal up). */
	void SetHealth(AVeloxCharacter& Pawn, float Target)
	{
		UVeloxAbilitySystemComponent* ASC = Pawn.GetVeloxAbilitySystemComponent();
		const int32 Delta = FMath::RoundToInt32(Target - HealthSetOf(&Pawn)->GetHealth());
		FGameplayEffectSpecHandle Spec;
		if (Delta > 0)
		{
			Spec = VeloxEffects::MakeHealSpec(ASC, Delta);
		}
		else if (Delta < 0)
		{
			FVeloxDamageSpecParams Params;
			Params.Damage = -Delta;
			Params.DamageTag = VeloxGameplayTags::Damage_Environment;
			Params.bNoKnockback = true;
			Params.bNoArmor = true;
			Spec = VeloxEffects::MakeDamageSpec(ASC, Params);
		}
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}

	FVector FeetOf(const AVeloxCharacter& Pawn)
	{
		return Pawn.GetActorLocation() - FVector(0.0, 0.0, Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	/** Teleports Pawn to stand on FloorPoint (overlaps update at once). */
	void MoveTo(AVeloxCharacter& Pawn, const FVector& FloorPoint)
	{
		Pawn.SetActorLocation(FloorPoint + FVector(0.0, 0.0, Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxItemsPickupTest, "Velox.Items.Pickup", VeloxItemTest::Flags)

bool FVeloxItemsPickupTest::RunTest(const FString& Parameters)
{
	using namespace VeloxItemTest;

	const UVeloxItemDefinition* Medium = AVeloxPickup::LoadDefinitionByName(TEXT("Health.Medium"));
	const UVeloxItemDefinition* RedArmor = AVeloxPickup::LoadDefinitionByName(TEXT("Armor.Red"));
	const UVeloxItemDefinition* Slugs = AVeloxPickup::LoadDefinitionByName(TEXT("Ammo.Slugs"));
	const UVeloxItemDefinition* Railgun = AVeloxPickup::LoadDefinitionByName(TEXT("Weapon.Railgun"));
	const UVeloxItemDefinition* Shotgun = AVeloxPickup::LoadDefinitionByName(TEXT("Weapon.Shotgun"));
	const UVeloxItemDefinition* Quad = AVeloxPickup::LoadDefinitionByName(TEXT("Powerup.Quad"));
	if (!TestNotNull(TEXT("item assets"), Medium) || !RedArmor || !Slugs || !Railgun || !Shotgun || !Quad)
	{
		return false;
	}

	FItemTestWorld W;
	AVeloxCharacter* Pawn = W.SpawnPawn(FVector(0, 0, 100));
	AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
	UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(W.World);
	if (!TestNotNull(TEXT("pawn"), Pawn) || !TestNotNull(TEXT("player state"), PS) || !TestNotNull(TEXT("registry"), Registry))
	{
		return false;
	}
	W.Advance(1.0f);
	UVeloxAbilitySystemComponent* ASC = PS->GetVeloxAbilitySystemComponent();
	const UVeloxHealthSet* Health = PS->GetHealthSet();
	int32 TakenEvents = 0;
	int32 RespawnEvents = 0;
	APlayerState* LastTaker = nullptr;
	Registry->OnPickupTaken.AddLambda([&](AVeloxPickup*, APlayerState* Taker) { ++TakenEvents; LastTaker = Taker; });
	Registry->OnPickupRespawned.AddLambda([&](AVeloxPickup*) { ++RespawnEvents; });

	// --- Health.Medium under the pawn: refused at 100.
	SetHealth(*Pawn, 100.f);
	TestEqual(TEXT("health set to 100"), Health->GetHealth(), 100.f);
	AVeloxPickup* Pickup = AVeloxPickup::SpawnPickup(W.World, Medium, FeetOf(*Pawn));
	if (!TestNotNull(TEXT("pickup spawned"), Pickup))
	{
		return false;
	}
	W.Advance(0.1f);
	TestTrue(TEXT("registered"), Registry->GetPickups().Contains(Pickup));
	TestTrue(TEXT("FindByTag(Velox.Item.Health) finds it"), Registry->FindByTag(VeloxGameplayTags::Item_Health_Medium.GetTag().RequestDirectParent()).Contains(Pickup));
	TestFalse(TEXT("not a major item"), Registry->GetMajorPickups().Contains(Pickup));
	TestTrue(TEXT("refused at 100: available"), Pickup->IsAvailable() && Pickup->IsPlaceholderVisible());
	TestEqual(TEXT("refused at 100: no take"), static_cast<int32>(Pickup->GetTakeCount()), 0);
	TestEqual(TEXT("refused at 100: health unchanged"), Health->GetHealth(), 100.f);

	// --- 50 damage while standing in it: taken at once (touch evaluated every frame), 50 -> 75.
	SetHealth(*Pawn, 50.f);
	W.Advance(2.f / 60.f);
	const double TakeTime = W.World->GetTimeSeconds();
	TestEqual(TEXT("taken at 50: take count"), static_cast<int32>(Pickup->GetTakeCount()), 1);
	TestEqual(TEXT("taken at 50: 75 health"), Health->GetHealth(), 75.f);
	TestTrue(TEXT("taken: unavailable and hidden"), !Pickup->IsAvailable() && !Pickup->IsPlaceholderVisible());
	TestTrue(TEXT("taken: LastTakenBy"), Pickup->GetLastTakenBy() == PS);
	TestTrue(TEXT("taken: one registry event with the taker"), TakenEvents == 1 && LastTaker == PS);
	const double RespawnAt = Pickup->GetRespawnServerTime();
	TestTrue(TEXT("RespawnServerTime = take time + 35"), FMath::IsNearlyEqual(RespawnAt, TakeTime + 35.0, 2.0 / 60.0 + 1e-3));
	TestTrue(TEXT("GetTimeUntilRespawn ~ 35"), FMath::IsNearlyEqual(Pickup->GetTimeUntilRespawn(), 35.0, 2.0 / 60.0 + 1e-3));

	// --- Respawn at exactly +35 s; the pawn still standing in it (75 < 100) retakes it at once.
	W.AdvanceTo(RespawnAt - 0.05);
	TestEqual(TEXT("still down just before the respawn"), static_cast<int32>(Pickup->GetTakeCount()), 1);
	TestFalse(TEXT("still unavailable just before the respawn"), Pickup->IsAvailable());
	W.AdvanceTo(RespawnAt + 2.0 / 60.0);
	TestEqual(TEXT("respawn event"), RespawnEvents, 1);
	TestEqual(TEXT("standing pawn retakes it"), static_cast<int32>(Pickup->GetTakeCount()), 2);
	TestEqual(TEXT("retake: 75 -> 100"), Health->GetHealth(), 100.f);

	// --- Reset (ResetLevel): available again, no taker; the pawn at 100 is refused.
	Pickup->Reset();
	TestTrue(TEXT("Reset: available, no taker"), Pickup->IsAvailable() && Pickup->GetLastTakenBy() == nullptr);
	TestEqual(TEXT("Reset: respawn event"), RespawnEvents, 2);
	TestEqual(TEXT("Reset: refused at 100"), static_cast<int32>(Pickup->GetTakeCount()), 2);

	// --- Red armor 0 -> 100 with the mode's ItemRespawnScale 0.5 (25 s -> 12.5 s).
	FVeloxItemModeRules Rules;
	Rules.ItemRespawnScale = 0.5f;
	Registry->SetModeRules(Rules);
	AVeloxPickup* Armor = AVeloxPickup::SpawnPickup(W.World, RedArmor, FVector(600, 0, 0));
	W.Advance(0.1f);
	TestTrue(TEXT("RA: major item"), Registry->GetMajorPickups().Contains(Armor));
	const float ArmorBefore = Health->GetArmor();
	MoveTo(*Pawn, FVector(600, 0, 0));
	W.Advance(2.f / 60.f);
	TestEqual(TEXT("RA: armor +100"), Health->GetArmor(), ArmorBefore + 100.f);
	TestTrue(TEXT("RA: respawn scaled to 12.5 s"), Armor && FMath::IsNearlyEqual(Armor->GetTimeUntilRespawn(), 12.5, 3.0 / 60.0));
	Registry->SetModeRules(FVeloxItemModeRules());

	// --- Slugs +10, then the railgun: owned, fire spec granted, ammo top-up 10 -> +1 (Q3: already >= the pickup amount).
	UVeloxWeaponInventoryComponent* Inventory = PS->GetWeaponInventory();
	const int32 RG = Inventory ? Inventory->IndexOf(Railgun->Weapon) : INDEX_NONE;
	if (!TestTrue(TEXT("RG in the weapon table"), RG != INDEX_NONE))
	{
		return false;
	}
	const float SlugsBefore = PS->GetAmmoSet()->GetSlugs();
	AVeloxPickup::SpawnPickup(W.World, Slugs, FVector(-600, 0, 0));
	W.Advance(0.1f);
	MoveTo(*Pawn, FVector(-600, 0, 0));
	W.Advance(2.f / 60.f);
	TestEqual(TEXT("slugs +10"), PS->GetAmmoSet()->GetSlugs(), FMath::Min(SlugsBefore + 10.f, 200.f));
	const bool bOwnedRG = Inventory->OwnsIndex(RG);
	const float SlugsBeforeWeapon = PS->GetAmmoSet()->GetSlugs();
	AVeloxPickup* RailPickup = AVeloxPickup::SpawnPickup(W.World, Railgun, FVector(0, 600, 0));
	W.Advance(0.1f);
	MoveTo(*Pawn, FVector(0, 600, 0));
	W.Advance(2.f / 60.f);
	TestTrue(TEXT("railgun owned after the pickup"), Inventory->OwnsIndex(RG) && Inventory->FindFireSpec(RG) != nullptr);
	const float ExpectedSlugs = SlugsBeforeWeapon + static_cast<float>(VeloxPickupRules::WeaponAmmoGain(FMath::RoundToInt32(SlugsBeforeWeapon), Railgun->GetEffectiveQuantity(), 200));
	TestEqual(TEXT("railgun ammo top-up (Q3 Pickup_Weapon)"), PS->GetAmmoSet()->GetSlugs(), ExpectedSlugs);
	TestTrue(TEXT("weapon respawn from the mode: 5 s"), RailPickup && FMath::IsNearlyEqual(RailPickup->GetTimeUntilRespawn(), 5.0, 3.0 / 60.0));
	AddInfo(FString::Printf(TEXT("RG owned before: %d, slugs %.0f -> %.0f"), bOwnedRG ? 1 : 0, SlugsBeforeWeapon, PS->GetAmmoSet()->GetSlugs()));

	// --- Quad (task 02): after its 30-60 s initial delay, taken -> Velox.Powerup.Quad for 30 s (Velox.Items.PowerupStacking
	// covers the extension, the x3 damage and the loss on respawn).
	AVeloxPickup* QuadPickup = AVeloxPickup::SpawnPickup(W.World, Quad, FVector(0, -600, 0));
	W.Advance(0.1f);
	TestTrue(TEXT("Quad: initial delay 30-60 s"), QuadPickup && QuadPickup->GetTimeUntilRespawn() >= 29.9 && QuadPickup->GetTimeUntilRespawn() <= 60.0);
	TestTrue(TEXT("Quad: effect is UVeloxGE_Powerup_Quad"), Quad->PowerupEffect == UVeloxGE_Powerup_Quad::StaticClass());
	if (QuadPickup)
	{
		W.AdvanceTo(W.World->GetTimeSeconds() + QuadPickup->GetTimeUntilRespawn() + 0.1);
		MoveTo(*Pawn, FVector(0, -600, 0));
		W.Advance(2.f / 60.f);
		TestEqual(TEXT("Quad: taken"), static_cast<int32>(QuadPickup->GetTakeCount()), 1);
		TestTrue(TEXT("Quad: tag"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::Powerup_Quad));
		TestEqual(TEXT("Quad: 30 s"), VeloxPowerups::GetRemainingSeconds(*ASC, UVeloxGE_Powerup_Quad::StaticClass()), 30.f, 0.1f);
		TestTrue(TEXT("Quad: respawn 120 s"), FMath::IsNearlyEqual(QuadPickup->GetTimeUntilRespawn(), 120.0, 3.0 / 60.0));
	}

	// --- Client prediction (roles switched to a client's): hide + predicted weapon slot, no server answer -> shown again.
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PS->GetCombatSettings());
	const int32 SG = Inventory->IndexOf(Shotgun->Weapon);
	AVeloxPickup* ShotgunPickup = AVeloxPickup::SpawnPickup(W.World, Shotgun, FVector(900, 900, 0));
	W.Advance(0.1f);
	if (!TestNotNull(TEXT("shotgun pickup"), ShotgunPickup) || !TestFalse(TEXT("SG not owned yet"), Inventory->OwnsIndex(SG)))
	{
		return false;
	}
	ShotgunPickup->SetRole(ROLE_SimulatedProxy);
	PS->SetRole(ROLE_SimulatedProxy);
	MoveTo(*Pawn, FVector(900, 900, 0));
	W.Advance(1.f / 60.f);
	TestTrue(TEXT("predict: hidden, take pending"), ShotgunPickup->IsPredictedTaken() && !ShotgunPickup->IsPlaceholderVisible());
	TestTrue(TEXT("predict: replicated state untouched"), ShotgunPickup->IsAvailable() && ShotgunPickup->GetTakeCount() == 0);
	TestTrue(TEXT("predict: SG slot owned locally"), Inventory->OwnsIndex(SG) && (Inventory->GetOwnedMask() & (1u << SG)) == 0);
	TestEqual(TEXT("predict: slot 3 resolves to SG"), Inventory->ResolveSelectInput(VeloxGameplayTags::InputTag_Weapon_Slot3), SG);
	TestFalse(TEXT("predict: idempotent (a replay re-fires the overlap)"), ShotgunPickup->TryPredictTake(Pawn));
	W.Advance(Settings.PickupPredictionTimeout + 0.1f);
	TestTrue(TEXT("timeout: shown again"), !ShotgunPickup->IsPredictedTaken() && ShotgunPickup->IsPlaceholderVisible());
	TestFalse(TEXT("timeout: predicted SG slot dropped"), Inventory->OwnsIndex(SG));
	// Predicted bits also expire on their own.
	Inventory->PredictOwned(SG, W.World->GetTimeSeconds() + 0.2);
	TestTrue(TEXT("PredictOwned: owned"), Inventory->OwnsIndex(SG));
	W.Advance(0.3f);
	TestFalse(TEXT("PredictOwned: expired"), Inventory->OwnsIndex(SG));
	ShotgunPickup->SetRole(ROLE_Authority);
	PS->SetRole(ROLE_Authority);
	Inventory->PredictOwned(SG, W.World->GetTimeSeconds() + 10.0);
	TestFalse(TEXT("PredictOwned is ignored on the server"), Inventory->OwnsIndex(SG));
	return true;
}

// ------------------------------------------------------------------------------------------------- PowerupStacking

namespace VeloxItemTest
{
	int32 NumPowerupEffects(const UAbilitySystemComponent& ASC)
	{
		return ASC.GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(VeloxGameplayTags::Powerup))).Num();
	}

	/** GE_Damage from Source onto Target (direction up, so knockback is visible). */
	void ApplyTestDamage(UAbilitySystemComponent& Source, UAbilitySystemComponent& Target, int32 Damage, float DamageScale, bool bRadius)
	{
		FVeloxDamageSpecParams Params;
		Params.Damage = Damage;
		Params.DamageScale = DamageScale;
		Params.DamageTag = VeloxGameplayTags::Damage_Environment;
		Params.Direction = FVector(0, 0, 1);
		Params.bRadius = bRadius;
		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(&Source, Params);
		if (Spec.IsValid())
		{
			Source.ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), &Target);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxItemsPowerupStackingTest, "Velox.Items.PowerupStacking", VeloxItemTest::Flags)

bool FVeloxItemsPowerupStackingTest::RunTest(const FString& Parameters)
{
	using namespace VeloxItemTest;

	const UVeloxItemDefinition* QuadItem = AVeloxPickup::LoadDefinitionByName(TEXT("Powerup.Quad"));
	if (!TestNotNull(TEXT("Quad item asset"), QuadItem))
	{
		return false;
	}
	FItemTestWorld W;
	AVeloxCharacter* Pawn = W.SpawnPawn(FVector(0, 0, 100));
	AVeloxCharacter* Dummy = W.SpawnPawn(FVector(-900, -900, 100));
	AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
	AVeloxPlayerState* DummyPS = Dummy ? Dummy->GetPlayerState<AVeloxPlayerState>() : nullptr;
	if (!TestNotNull(TEXT("pawn"), PS) || !TestNotNull(TEXT("dummy"), DummyPS))
	{
		return false;
	}
	W.Advance(0.5f);
	UVeloxAbilitySystemComponent* ASC = PS->GetVeloxAbilitySystemComponent();
	UVeloxAbilitySystemComponent* DummyASC = DummyPS->GetVeloxAbilitySystemComponent();
	const UVeloxHealthSet* DummyHealth = DummyPS->GetHealthSet();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PS->GetCombatSettings());
	const TSubclassOf<UVeloxGE_Powerup> QuadClass = UVeloxGE_Powerup_Quad::StaticClass();

	// --- Class layout: HasDuration from SetByCaller.Duration, grants its tag, looping cue, Regen periodic 1 s.
	const UVeloxGE_Powerup* QuadCDO = GetDefault<UVeloxGE_Powerup_Quad>();
	TestTrue(TEXT("Quad GE: HasDuration"), QuadCDO->DurationPolicy == EGameplayEffectDurationType::HasDuration);
	TestTrue(TEXT("Quad GE: grants Velox.Powerup.Quad"), QuadCDO->GetGrantedTags().HasTagExact(VeloxGameplayTags::Powerup_Quad));
	TestTrue(TEXT("Quad GE: looping cue"), QuadCDO->GameplayCues.Num() == 1 && QuadCDO->GameplayCues[0].GameplayCueTags.HasTagExact(VeloxPowerupTags::GameplayCue_Powerup_Quad));
	const UVeloxGE_Powerup* RegenCDO = GetDefault<UVeloxGE_Powerup_Regen>();
	TestTrue(TEXT("Regen GE: period 1 s, not on application"), RegenCDO->Period.GetValueAtLevel(1.f) == 1.f && !RegenCDO->bExecutePeriodicEffectOnApplication);
	TestTrue(TEXT("Suit / Haste tags"), GetDefault<UVeloxGE_Powerup_BattleSuit>()->GetPowerupTag() == VeloxGameplayTags::Powerup_BattleSuit
		&& GetDefault<UVeloxGE_Powerup_Haste>()->GetPowerupTag() == VeloxGameplayTags::Powerup_Haste);

	// --- First grant: applied, 30 s, tag and cue (tint on the pawn's feedback component).
	TestTrue(TEXT("grant: applied"), VeloxPowerups::GrantOrExtend(*ASC, QuadClass, 30.f) == EVeloxPowerupGrant::Applied);
	TestTrue(TEXT("grant: tag"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::Powerup_Quad));
	TestEqual(TEXT("grant: 30 s"), VeloxPowerups::GetRemainingSeconds(*ASC, QuadClass), 30.f, 0.01f);
	const UVeloxCombatFeedbackComponent* Feedback = Pawn->FindComponentByClass<UVeloxCombatFeedbackComponent>();
	TestTrue(TEXT("grant: looping cue reached the pawn (tint)"), Feedback && Feedback->GetActivePowerups().HasTagExact(VeloxGameplayTags::Powerup_Quad));
	TestTrue(TEXT("grant: blue tint"), Feedback && Feedback->GetPowerupTint().Equals(Settings.PowerupTints.FindRef(VeloxGameplayTags::Powerup_Quad)));
	const TArray<FActiveGameplayEffectHandle> Handles = ASC->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(VeloxGameplayTags::Powerup_Quad)));

	// --- Quad x3 on a dummy (outgoing scale from the shooter's tag, MakeDamageSpec): MG 7 -> 21, vs no armor.
	DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), 0.f);
	SetHealth(*Dummy, 100.f);
	const float Scale = VeloxPowerups::GetOutgoingDamageScale(ASC, Settings);
	TestEqual(TEXT("quad: outgoing scale 3"), Scale, 3.f);
	TestEqual(TEXT("dummy: no quad, scale 1"), VeloxPowerups::GetOutgoingDamageScale(DummyASC, Settings), 1.f);
	ApplyTestDamage(*ASC, *DummyASC, 7, Scale, false);
	TestEqual(TEXT("quad MG on the dummy: 100 -> 79"), DummyHealth->GetHealth(), 79.f);

	// --- Extension arithmetic: 10 s later 20 s remain; a second Quad (rule: 20 + 30) extends in place to 50 s.
	W.Advance(10.f);
	const float Remaining = VeloxPowerups::GetRemainingSeconds(*ASC, QuadClass);
	TestEqual(TEXT("10 s later: 20 s left"), Remaining, 20.f, 0.05f);
	FVeloxPickupQuery Query;
	Query.Type = EVeloxItemType::Powerup;
	Query.PowerupSeconds = QuadItem->PowerupSeconds;
	Query.PowerupRemaining = Remaining;
	const FVeloxPickupGain Gain = VeloxPickupRules::Gain(Query);
	TestEqual(TEXT("rule: remaining + 30"), Gain.PowerupSeconds, Remaining + 30.f, 1e-3f);
	TestTrue(TEXT("second grant: extended in place (ModifyActiveEffectStartTime)"), VeloxPowerups::GrantOrExtend(*ASC, QuadClass, Gain.PowerupSeconds) == EVeloxPowerupGrant::Extended);
	TestEqual(TEXT("extended: ~50 s"), VeloxPowerups::GetRemainingSeconds(*ASC, QuadClass), Gain.PowerupSeconds, 0.01f);
	const TArray<FActiveGameplayEffectHandle> HandlesAfter = ASC->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(VeloxGameplayTags::Powerup_Quad)));
	TestTrue(TEXT("extended: same single effect (no removal)"), Handles.Num() == 1 && HandlesAfter.Num() == 1 && Handles[0] == HandlesAfter[0]);
	TestTrue(TEXT("extended: cue not restarted"), Feedback && Feedback->GetActivePowerups().HasTagExact(VeloxGameplayTags::Powerup_Quad));
	W.Advance(Gain.PowerupSeconds - 0.2f);
	TestTrue(TEXT("still active just before the extended end"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::Powerup_Quad));
	W.Advance(0.4f);
	TestFalse(TEXT("expired at the extended end"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::Powerup_Quad));
	TestTrue(TEXT("expired: tint off"), Feedback && !Feedback->GetActivePowerups().HasTagExact(VeloxGameplayTags::Powerup_Quad) && Feedback->GetPowerupTint().A == 0.f);

	// --- Battle Suit on the dummy: radius 100 -> no loss (knockback 1250 stays), direct 100 -> 50.
	TestTrue(TEXT("suit: applied"), VeloxPowerups::GrantOrExtend(*DummyASC, UVeloxGE_Powerup_BattleSuit::StaticClass(), 30.f) == EVeloxPowerupGrant::Applied);
	SetHealth(*Dummy, 100.f);
	const UVeloxMovementComponent* DummyMovement = Dummy->GetVeloxMovementComponent();
	ApplyTestDamage(*ASC, *DummyASC, 100, 1.f, true);
	TestEqual(TEXT("suit radius 100: no health loss"), DummyHealth->GetHealth(), 100.f);
	TestTrue(TEXT("suit radius 100: knockback 1250 cm/s"), DummyMovement && DummyMovement->GetPendingKnockback().Velocity.Equals(FVector(0, 0, 1250), 0.5));
	ApplyTestDamage(*ASC, *DummyASC, 100, 1.f, false);
	TestEqual(TEXT("suit direct 100: -50"), DummyHealth->GetHealth(), 50.f);

	// --- Regen on the dummy: 50 -> 65 -> 80 per second (not on application); over max no decay, +5.
	TestTrue(TEXT("regen: applied"), VeloxPowerups::GrantOrExtend(*DummyASC, UVeloxGE_Powerup_Regen::StaticClass(), 30.f) == EVeloxPowerupGrant::Applied);
	TestEqual(TEXT("regen: not on application"), DummyHealth->GetHealth(), 50.f);
	W.Advance(1.05f);
	TestEqual(TEXT("regen: 50 -> 65 after 1 s"), DummyHealth->GetHealth(), 65.f);
	W.Advance(1.f);
	TestEqual(TEXT("regen: 65 -> 80 after 2 s"), DummyHealth->GetHealth(), 80.f);
	SetHealth(*Dummy, 150.f);
	W.Advance(1.f);
	TestEqual(TEXT("regen over max: 150 -> 155, no decay"), DummyHealth->GetHealth(), 155.f);

	// --- Lost on death: GE_SpawnInit (respawn) removes every powerup of the dummy (suit and regen), nothing else needed.
	TestEqual(TEXT("dummy holds two powerups"), NumPowerupEffects(*DummyASC), 2);
	DummyASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeSpawnInitSpec(DummyASC, Settings).Data.Get());
	TestEqual(TEXT("spawn init: no powerup effects"), NumPowerupEffects(*DummyASC), 0);
	TestFalse(TEXT("spawn init: no Velox.Powerup tag"), DummyASC->HasMatchingGameplayTag(VeloxGameplayTags::Powerup));
	TestEqual(TEXT("spawn init: health 125"), DummyHealth->GetHealth(), 125.f);

	// --- A dead target regenerates nothing (Q3 runs the timer actions for living clients only).
	TestTrue(TEXT("regen again"), VeloxPowerups::GrantOrExtend(*DummyASC, UVeloxGE_Powerup_Regen::StaticClass(), 30.f) == EVeloxPowerupGrant::Applied);
	SetHealth(*Dummy, 20.f);
	DummyASC->ApplyGameplayEffectToSelf(GetDefault<UVeloxGE_Dead>(), 1.f, DummyASC->MakeEffectContext());
	W.Advance(1.05f);
	TestEqual(TEXT("dead: no regen tick"), DummyHealth->GetHealth(), 20.f);

	// --- Match frozen (task 05's tag): treated like dead by the damage execution; skipped while 05 is not merged.
	const FGameplayTag Frozen = VeloxEffects::GetMatchFrozenTag();
	if (Frozen.IsValid())
	{
		ASC->AddLooseGameplayTag(Frozen);
		const float Before = PS->GetHealthSet()->GetHealth();
		ApplyTestDamage(*DummyASC, *ASC, 50, 1.f, false);
		TestEqual(TEXT("frozen: no damage"), PS->GetHealthSet()->GetHealth(), Before);
		ASC->RemoveLooseGameplayTag(Frozen);
	}
	else
	{
		AddInfo(TEXT("Velox.State.MatchFrozen not registered (task 05 not merged): frozen check skipped"));
	}

	// --- Projectiles carry the shot's Quad state (replicated, initial only) and die on ResetLevel (Reset).
	UVeloxWeaponDefinition* Rocket = NewObject<UVeloxWeaponDefinition>(GetTransientPackage());
	Rocket->Damage = 100;
	Rocket->SplashDamage = 100;
	Rocket->SplashRadiusCm = 300.f;
	Rocket->ProjectileSpeed = 100.f;
	Rocket->ProjectileLifetime = 10.f;
	FVeloxProjectileLaunch Launch;
	Launch.Definition = Rocket;
	Launch.Shooter = Pawn;
	Launch.SourceASC = ASC;
	Launch.Origin = FVector(0, 0, 2000);
	Launch.Dir = FVector::UpVector;
	Launch.bQuad = true;
	AVeloxProjectile* Projectile = AVeloxProjectile::SpawnServerProjectile(*W.World, nullptr, Launch);
	if (TestNotNull(TEXT("projectile"), Projectile))
	{
		TestTrue(TEXT("projectile: quad captured"), Projectile->IsQuad());
		TestEqual(TEXT("projectile: quad splash 100 -> 300"), AVeloxProjectile::ScaledShotDamage(Rocket->SplashDamage, Projectile->IsQuad(), ASC), 300);
		TestEqual(TEXT("projectile: no quad, 100"), AVeloxProjectile::ScaledShotDamage(Rocket->SplashDamage, false, ASC), 100);
		Projectile->Reset();
		TestFalse(TEXT("projectile: destroyed by Reset (ResetLevel)"), IsValid(Projectile));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
