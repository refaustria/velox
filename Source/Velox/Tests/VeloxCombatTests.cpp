// Velox - combat foundation tests (ADR 0003 section 9, task 01).
//   Velox.Combat.DamageMath    Q3 G_Damage/CheckArmor/G_RadiusDamage table (world-free).
//   Velox.Combat.Ammo          ammo clamp, cost check, GE_AmmoCost/GE_SpawnInit layout (world-free).
//   Velox.Combat.KnockbackSeq  ShouldReplayPredictedKnockback wrap cases (world-free).
//   Velox.Combat.Effects       the GE plumbing on a real PlayerState ASC in a private game world: spawn init 125,
//                              decay to max, damage split + AddKnockback(Server), ammo, dead/protection tags, respawn
//                              without re-grant, re-grant on changed pawn data.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Character/VeloxCharacter.h"
#include "AbilitySystemGlobals.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAbilitySystemGlobals.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxDamageMath.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthComponent.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "Core/VeloxPawnData.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/VeloxPlayerState.h"
#include "Movement/VeloxMovementComponent.h"
#include "Weapons/VeloxWeaponDefinition.h"

namespace VeloxCombatTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FVeloxDamageResult Hit(int32 Damage, int32 Armor, bool bSelf = false, bool bProtected = false, int32 Pellets = 1)
	{
		FVeloxDamageInput In;
		In.Damage = Damage;
		In.Armor = Armor;
		In.bSelf = bSelf;
		In.bProtected = bProtected;
		return FVeloxDamageMath::ApplyPellets(In, Pellets, FVeloxCombatParams());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatDamageMathTest, "Velox.Combat.DamageMath", VeloxCombatTest::Flags)

bool FVeloxCombatDamageMathTest::RunTest(const FString& Parameters)
{
	using namespace VeloxCombatTest;

	// The params defaults are the settings defaults (one source of truth for the ADR table).
	const FVeloxCombatParams FromSettings = GetDefault<UVeloxCombatSettings>()->BuildCombatParams();
	TestEqual(TEXT("ArmorProtectionPercent default"), FromSettings.ArmorProtectionPercent, FVeloxCombatParams().ArmorProtectionPercent);
	TestEqual(TEXT("ArmorProtectionPercent is Q3 0.66"), FromSettings.ArmorProtectionPercent, 66);
	TestEqual(TEXT("KnockbackSpeedPerDamage default"), FromSettings.KnockbackSpeedPerDamage, 12.5f);
	TestEqual(TEXT("MaxKnockbackDamage default"), FromSettings.MaxKnockbackDamage, 200);

	struct FRow { int32 Damage; int32 Armor; int32 Health; int32 ArmorLoss; };
	const FRow Table[] = {
		{ 100, 0, 100, 0 },   // no armor
		{ 100, 50, 50, 50 },  // armor runs out
		{ 100, 100, 34, 66 }, // Q3 rail figure: ceil(0.66 x 100) = 66 (ADR 0003 B2)
		{ 50, 100, 17, 33 },  // self rocket after halving: ceil(33.0) = 33
		{ 200, 200, 68, 132 },
		{ 110, 100, 37, 73 }, // ceil(72.6) = 73
		{ 10, 100, 3, 7 },    // ceil(6.6) = 7
		{ 7, 100, 2, 5 },     // MG: ceil(4.62) = 5
		{ 3, 100, 1, 2 },     // ceil(1.98) = 2
		{ 1, 100, 0, 1 },     // Q3 ceil: a 1-point hit is fully absorbed
		{ 0, 100, 0, 1 },     // zero-point radius hit: Q3 minimum 1 damage (B3), absorbed by armor
	};
	for (const FRow& Row : Table)
	{
		const FVeloxDamageResult R = Hit(Row.Damage, Row.Armor);
		TestEqual(FString::Printf(TEXT("%d vs %d armor: health loss"), Row.Damage, Row.Armor), R.HealthLoss, Row.Health);
		TestEqual(FString::Printf(TEXT("%d vs %d armor: armor loss"), Row.Damage, Row.Armor), R.ArmorLoss, Row.ArmorLoss);
	}

	// Knockback from the raw damage, before the self halving: 100 -> 1250 cm/s (ADR 0002 A7 grace 0.2 s).
	TestEqual(TEXT("100 dmg knockback"), Hit(100, 0).KnockbackSpeed, 1250.f);
	const FVeloxDamageResult Self = Hit(100, 0, true);
	TestEqual(TEXT("self 100: health -50"), Self.HealthLoss, 50);
	TestEqual(TEXT("self 100: knockback from raw damage"), Self.KnockbackSpeed, 1250.f);
	TestEqual(TEXT("self 1: at least 1 point"), Hit(1, 0, true).HealthLoss, 1);
	TestEqual(TEXT("zero-point hit: no knockback"), Hit(0, 100).KnockbackSpeed, 0.f);
	TestEqual(TEXT("zero-point hit, no armor: 1 health"), Hit(0, 0).HealthLoss, 1);
	TestEqual(TEXT("self 100 vs 100 armor: health 17"), Hit(100, 100, true).HealthLoss, 17);
	TestEqual(TEXT("self 100 vs 100 armor: armor 33"), Hit(100, 100, true).ArmorLoss, 33);
	{
		FVeloxDamageInput Negative;
		Negative.Damage = -5;
		const FVeloxDamageResult R = FVeloxDamageMath::Apply(Negative, FVeloxCombatParams());
		TestTrue(TEXT("negative damage rejected"), R.HealthLoss == 0 && R.ArmorLoss == 0 && R.KnockbackSpeed == 0.f);
	}
	TestEqual(TEXT("self 15 vs 100 armor (plasma splash): 7 -> 2 health / 5 armor"), Hit(15, 100, true).HealthLoss, 2);

	// Cap.
	TestEqual(TEXT("knockback capped at 200 damage"), Hit(300, 0).KnockbackSpeed, 2500.f);
	TestEqual(TEXT("cap does not limit damage"), Hit(300, 0).HealthLoss, 300);

	// Spawn protection.
	const FVeloxDamageResult Protected = Hit(100, 50, false, true);
	TestEqual(TEXT("protected: health"), Protected.HealthLoss, 0);
	TestEqual(TEXT("protected: armor"), Protected.ArmorLoss, 0);
	TestEqual(TEXT("protected: knockback"), Protected.KnockbackSpeed, 0.f);

	// No armor / no knockback flags and the per-weapon scale.
	{
		FVeloxDamageInput In;
		In.Damage = 100;
		In.Armor = 100;
		In.bNoArmor = true;
		In.bNoKnockback = true;
		const FVeloxDamageResult R = FVeloxDamageMath::Apply(In, FVeloxCombatParams());
		TestEqual(TEXT("no-armor: full health loss"), R.HealthLoss, 100);
		TestEqual(TEXT("no-knockback"), R.KnockbackSpeed, 0.f);
		In.bNoKnockback = false;
		In.KnockbackScale = 0.5f;
		TestEqual(TEXT("knockback scale"), FVeloxDamageMath::Apply(In, FVeloxCombatParams()).KnockbackSpeed, 625.f);
	}

	// Per-pellet rounding: 11 x 10 against 100 armor costs 77 armor (23 left), 100 at once 66.
	const FVeloxDamageResult Pellets = Hit(10, 100, false, false, 11);
	TestEqual(TEXT("11 pellets x 10: armor loss"), Pellets.ArmorLoss, 77);
	TestEqual(TEXT("11 pellets x 10: health loss"), Pellets.HealthLoss, 33);
	TestEqual(TEXT("11 pellets x 10: knockback adds up"), Pellets.KnockbackSpeed, 11.f * 125.f);
	TestEqual(TEXT("one 110 hit: armor loss"), Hit(110, 100).ArmorLoss, 73);
	TestEqual(TEXT("one 100 hit: armor loss"), Hit(100, 100).ArmorLoss, 66);
	// Armor running out mid-volley.
	const FVeloxDamageResult Drain = Hit(10, 10, false, false, 3);
	TestEqual(TEXT("armor runs out mid-volley: armor loss"), Drain.ArmorLoss, 10);
	TestEqual(TEXT("armor runs out mid-volley: health loss"), Drain.HealthLoss, 20);

	// Radius points: (int)(D * (1 - d/R)), 0 at/outside R.
	TestEqual(TEXT("radius at 0"), FVeloxDamageMath::RadiusPoints(100, 0.f, 300.f), 100);
	TestEqual(TEXT("radius at R/2"), FVeloxDamageMath::RadiusPoints(100, 150.f, 300.f), 50);
	TestEqual(TEXT("radius truncates"), FVeloxDamageMath::RadiusPoints(100, 100.f, 300.f), 66);
	TestEqual(TEXT("radius at R"), FVeloxDamageMath::RadiusPoints(100, 300.f, 300.f), 0);
	TestEqual(TEXT("radius beyond R"), FVeloxDamageMath::RadiusPoints(100, 400.f, 300.f), 0);
	TestEqual(TEXT("radius zero radius"), FVeloxDamageMath::RadiusPoints(100, 0.f, 0.f), 0);

	// Capsule distance and radius direction.
	TestEqual(TEXT("capsule: beside"), FVeloxDamageMath::ClosestCapsuleDistance(FVector(137.5, 0, 0), FVector::ZeroVector, 37.5f, 70.f), 100.f, 1e-3f);
	TestEqual(TEXT("capsule: below"), FVeloxDamageMath::ClosestCapsuleDistance(FVector(0, 0, -170), FVector::ZeroVector, 37.5f, 70.f), 100.f, 1e-3f);
	TestEqual(TEXT("capsule: inside"), FVeloxDamageMath::ClosestCapsuleDistance(FVector(10, 0, 20), FVector::ZeroVector, 37.5f, 70.f), 0.f);
	const FVector Dir = FVeloxDamageMath::RadiusKnockbackDir(FVector(0, 0, 0), FVector(100, 0, 0), 60.f);
	TestTrue(TEXT("radius dir lifted"), Dir.Equals(FVector(100, 0, 60).GetSafeNormal(), 1e-4));
	TestTrue(TEXT("radius dir degenerate"), FVeloxDamageMath::RadiusKnockbackDir(FVector(0, 0, -60), FVector::ZeroVector, -60.f).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatAmmoTest, "Velox.Combat.Ammo", VeloxCombatTest::Flags)

bool FVeloxCombatAmmoTest::RunTest(const FString& Parameters)
{
	const UVeloxCombatSettings& Settings = *GetDefault<UVeloxCombatSettings>();
	const TArray<TPair<FGameplayTag, FGameplayAttribute>>& Types = UVeloxAmmoSet::GetAmmoTypes();
	TestEqual(TEXT("six ammo types"), Types.Num(), 6);
	TestEqual(TEXT("settings list six ammo rows"), Settings.AmmoTypes.Num(), 6);
	for (const TPair<FGameplayTag, FGameplayAttribute>& Type : Types)
	{
		TestTrue(FString::Printf(TEXT("%s valid"), *Type.Key.ToString()), Type.Key.IsValid() && Type.Value.IsValid());
		TestTrue(FString::Printf(TEXT("%s maps to its attribute"), *Type.Key.ToString()), UVeloxAmmoSet::GetAmmoAttribute(Type.Key, Settings) == Type.Value);
		const FVeloxAmmoTypeInfo* Info = Settings.FindAmmoType(Type.Value);
		TestTrue(FString::Printf(TEXT("%s cap 200"), *Type.Key.ToString()), Info && Info->MaxAmount == 200);
	}
	// ADR 0003 B5: spawn ammo is the sum of the spawn loadout's ammo per AmmoTag (the class default loadout is empty).
	TestEqual(TEXT("class default loadout: no spawn ammo"), Settings.GetSpawnAmmo(VeloxGameplayTags::Ammo_Bullets), 0);
	{
		UVeloxCombatSettings* Custom = NewObject<UVeloxCombatSettings>(GetTransientPackage());
		UVeloxWeaponDefinition* MG = NewObject<UVeloxWeaponDefinition>(GetTransientPackage());
		MG->AmmoTag = VeloxGameplayTags::Ammo_Bullets;
		UVeloxWeaponDefinition* RG = NewObject<UVeloxWeaponDefinition>(GetTransientPackage());
		RG->AmmoTag = VeloxGameplayTags::Ammo_Slugs;
		Custom->SpawnLoadout = { { MG, 100 }, { RG, 10 }, { MG, 20 } };
		TestEqual(TEXT("loadout: bullets summed"), Custom->GetSpawnAmmo(VeloxGameplayTags::Ammo_Bullets), 120);
		TestEqual(TEXT("loadout: slugs"), Custom->GetSpawnAmmo(VeloxGameplayTags::Ammo_Slugs), 10);
		TestEqual(TEXT("loadout: no cells"), Custom->GetSpawnAmmo(VeloxGameplayTags::Ammo_Cells), 0);
	}
	if (const UVeloxCombatSettings* Asset = LoadObject<UVeloxCombatSettings>(nullptr, TEXT("/Game/Data/DA_CombatSettings_Default.DA_CombatSettings_Default")))
	{
		TestEqual(TEXT("DA_CombatSettings_Default: Q3 spawn with 100 bullets"), Asset->GetSpawnAmmo(VeloxGameplayTags::Ammo_Bullets), 100);
	}
	TestFalse(TEXT("unknown tag has no attribute"), UVeloxAmmoSet::GetAmmoAttribute(VeloxGameplayTags::Damage_Rail, Settings).IsValid());

	// Cost check (CheckCost of the fire abilities): fails below AmmoPerShot.
	TestTrue(TEXT("1 bullet pays 1"), UVeloxAmmoSet::CanAfford(1.f, 1));
	TestFalse(TEXT("0 bullets do not pay 1"), UVeloxAmmoSet::CanAfford(0.f, 1));
	TestFalse(TEXT("1 cell does not pay 2"), UVeloxAmmoSet::CanAfford(1.f, 2));
	TestTrue(TEXT("free weapons always fire"), UVeloxAmmoSet::CanAfford(0.f, 0));

	// Clamp at [0, max], integers.
	const FGameplayAttribute Bullets = UVeloxAmmoSet::GetBulletsAttribute();
	TestEqual(TEXT("clamp at max"), UVeloxAmmoSet::ClampAmmo(Bullets, 250.f, Settings), 200.f);
	TestEqual(TEXT("clamp at 0"), UVeloxAmmoSet::ClampAmmo(Bullets, -3.f, Settings), 0.f);
	TestEqual(TEXT("integral"), UVeloxAmmoSet::ClampAmmo(Bullets, 41.6f, Settings), 42.f);
	TestEqual(TEXT("non-ammo attribute passes"), UVeloxAmmoSet::ClampAmmo(UVeloxHealthSet::GetHealthAttribute(), 250.f, Settings), 250.f);

	// GE layout: GE_AmmoCost has one additive SetByCaller modifier per ammo type, keyed by the ammo tag.
	const UGameplayEffect* Cost = GetDefault<UVeloxGE_AmmoCost>();
	TestEqual(TEXT("GE_AmmoCost modifiers"), Cost->Modifiers.Num(), 6);
	for (int32 Index = 0; Index < FMath::Min(Cost->Modifiers.Num(), Types.Num()); ++Index)
	{
		const FGameplayModifierInfo& Mod = Cost->Modifiers[Index];
		TestTrue(FString::Printf(TEXT("GE_AmmoCost[%d] attribute"), Index), Mod.Attribute == Types[Index].Value);
		TestEqual(FString::Printf(TEXT("GE_AmmoCost[%d] additive"), Index), static_cast<int32>(Mod.ModifierOp), static_cast<int32>(EGameplayModOp::Additive));
		TestTrue(FString::Printf(TEXT("GE_AmmoCost[%d] SetByCaller tag"), Index), Mod.ModifierMagnitude.GetSetByCallerFloat().DataTag == Types[Index].Key);
	}
	TestEqual(TEXT("GE_SpawnInit modifiers (4 health/armor + 6 ammo)"), GetDefault<UVeloxGE_SpawnInit>()->Modifiers.Num(), 10);
	TestTrue(TEXT("GE_Dead grants Velox.State.Dead"), GetDefault<UVeloxGE_Dead>()->GetGrantedTags().HasTagExact(VeloxGameplayTags::State_Dead));
	TestTrue(TEXT("GE_WeaponSwitching grants the switching tag"), GetDefault<UVeloxGE_WeaponSwitching>()->GetGrantedTags().HasTagExact(VeloxGameplayTags::State_WeaponSwitching));
	TestTrue(TEXT("GE_SpawnProtection grants the protection tag"), GetDefault<UVeloxGE_SpawnProtection>()->GetGrantedTags().HasTagExact(VeloxGameplayTags::State_SpawnProtected));
	TestEqual(TEXT("GE_HealthDecay period 1 s"), GetDefault<UVeloxGE_HealthDecay>()->Period.GetValue(), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatKnockbackSeqTest, "Velox.Combat.KnockbackSeq", VeloxCombatTest::Flags)

bool FVeloxCombatKnockbackSeqTest::RunTest(const FString& Parameters)
{
	struct FCase { uint8 Seq; uint8 Confirmed; bool bReplay; const TCHAR* Name; };
	const FCase Cases[] = {
		{ 5, 4, true, TEXT("newer than confirmed") },
		{ 4, 4, false, TEXT("equal: resolved") },
		{ 3, 4, false, TEXT("older: resolved") },
		{ 0, 255, true, TEXT("wrap: 0 after 255") },
		{ 2, 250, true, TEXT("wrap: 2 after 250") },
		{ 255, 0, false, TEXT("wrap: 255 before 0") },
		{ 127, 0, true, TEXT("edge: +127 is newer") },
		{ 128, 0, false, TEXT("edge: +128 counts as older") },
		{ 10, 200, true, TEXT("wrap: 10 after 200 (+66)") },
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(Case.Name, UVeloxMovementComponent::ShouldReplayPredictedKnockback(Case.Seq, Case.Confirmed), Case.bReplay);
	}

	// The PrepMoveFor rule on recorded moves (task 05): replay iff unresolved, nothing for an empty record.
	FVeloxPredictedKnockback Saved;
	Saved.Add(FVector(0.0, 0.0, 1100.0), 9);
	TestTrue(TEXT("replay: unresolved shot re-applies its impulse"), FSavedMove_Velox::PredictedKnockbackForReplay(Saved, 8) == Saved);
	TestFalse(TEXT("replay: resolved shot (confirmed == seq) applies nothing"), FSavedMove_Velox::PredictedKnockbackForReplay(Saved, 9).IsPending());
	TestFalse(TEXT("replay: resolved by a newer confirmation"), FSavedMove_Velox::PredictedKnockbackForReplay(Saved, 12).IsPending());
	TestFalse(TEXT("replay: an empty record stays empty"), FSavedMove_Velox::PredictedKnockbackForReplay(FVeloxPredictedKnockback(), 0).IsPending());

	// Two predictions in one move: impulses add, the sequence is the newer one (wrap-aware).
	FVeloxPredictedKnockback Two;
	Two.Add(FVector(0.0, 0.0, 100.0), 254);
	Two.Add(FVector(10.0, 0.0, 0.0), 1);
	TestEqual(TEXT("accumulate: impulses add"), Two.Velocity, FVector(10.0, 0.0, 100.0));
	TestEqual(TEXT("accumulate: newer seq across the wrap"), static_cast<int32>(Two.Seq), 1);
	Two.Add(FVector::ZeroVector, 250);
	TestEqual(TEXT("accumulate: an older seq does not replace"), static_cast<int32>(Two.Seq), 1);

	// Server bookkeeping helpers.
	TestEqual(TEXT("newer seq: plain"), static_cast<int32>(UVeloxMovementComponent::NewerShotSeq(4, 5)), 5);
	TestEqual(TEXT("newer seq: older ignored"), static_cast<int32>(UVeloxMovementComponent::NewerShotSeq(5, 4)), 5);
	TestEqual(TEXT("newer seq: wrap"), static_cast<int32>(UVeloxMovementComponent::NewerShotSeq(250, 3)), 3);

	// Move-timed events: due at or after their time stamp, and across a time stamp reset (240 s period).
	constexpr float ResetPeriod = 240.f;
	TestTrue(TEXT("due: same stamp"), UVeloxMovementComponent::IsMoveTimeDue(10.f, 10.f, ResetPeriod));
	TestTrue(TEXT("due: later move"), UVeloxMovementComponent::IsMoveTimeDue(10.f, 10.01f, ResetPeriod));
	TestFalse(TEXT("not due: earlier move"), UVeloxMovementComponent::IsMoveTimeDue(10.f, 9.99f, ResetPeriod));
	TestTrue(TEXT("due: the stamps were reset after scheduling"), UVeloxMovementComponent::IsMoveTimeDue(239.9f, 0.05f, ResetPeriod));
	return true;
}

namespace VeloxCombatTest
{
	/** Private game world with a floor; a pawn possessed by an AI controller that owns an AVeloxPlayerState. */
	class FCombatWorld
	{
	public:
		FCombatWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxCombatTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
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

		~FCombatWorld()
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

		/** Advances world time (timers, periodic effects) in 1/20 s steps. */
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

	int32 NumActiveEffects(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass)
	{
		FGameplayEffectQuery Query;
		Query.EffectDefinition = EffectClass;
		return ASC->GetActiveEffects(Query).Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatEffectsTest, "Velox.Combat.Effects", VeloxCombatTest::Flags)

bool FVeloxCombatEffectsTest::RunTest(const FString& Parameters)
{
	using namespace VeloxCombatTest;

	FCombatWorld W;
	if (!TestNotNull(TEXT("player state"), W.PlayerState) || !TestNotNull(TEXT("controller"), W.Controller))
	{
		return false;
	}
	UVeloxAbilitySystemComponent* ASC = W.PlayerState->GetVeloxAbilitySystemComponent();
	const UVeloxHealthSet* Health = W.PlayerState->GetHealthSet();
	const UVeloxAmmoSet* Ammo = W.PlayerState->GetAmmoSet();
	TestTrue(TEXT("ASC found the health set"), ASC && ASC->GetSet<UVeloxHealthSet>() == Health);
	TestTrue(TEXT("ASC found the ammo set"), ASC && ASC->GetSet<UVeloxAmmoSet>() == Ammo);
	TestTrue(TEXT("Velox ability system globals"), UAbilitySystemGlobals::Get().IsA<UVeloxAbilitySystemGlobals>());
	TestNotNull(TEXT("effect context is the Velox context"), FVeloxGameplayEffectContext::Get(ASC->MakeEffectContext()));

	// Possession: grant once + GE_SpawnInit.
	AVeloxCharacter* Pawn = W.SpawnAndPossess();
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}
	TestTrue(TEXT("pawn is the avatar"), ASC->GetAvatarActor() == Pawn);
	TestTrue(TEXT("default ability sets granted"), W.PlayerState->HasGrantedDefaultAbilitySets());
	TestEqual(TEXT("spawn health 125"), Health->GetHealth(), 125.f);
	TestEqual(TEXT("max health 100"), Health->GetMaxHealth(), 100.f);
	TestEqual(TEXT("spawn armor 0"), Health->GetArmor(), 0.f);
	TestEqual(TEXT("max armor 200"), Health->GetMaxArmor(), 200.f);
	TestEqual(TEXT("spawn bullets 100"), Ammo->GetBullets(), 100.f);
	TestEqual(TEXT("one decay effect"), NumActiveEffects(ASC, UVeloxGE_HealthDecay::StaticClass()), 1);
	const UVeloxHealthComponent* HealthComponent = Pawn->GetHealthComponent();
	TestTrue(TEXT("health component bound"), HealthComponent && HealthComponent->GetHealth() == 125.f);

	// Decay 1/s towards max health (Q3), not on application.
	W.Advance(0.5f);
	TestEqual(TEXT("no decay at application"), Health->GetHealth(), 125.f);
	W.Advance(2.6f);
	TestEqual(TEXT("3 decay ticks after 3.1 s"), Health->GetHealth(), 122.f);

	// Damage 50 vs 100 armor (armor not above max health, so it does not decay): Q3 save ceil(0.66 x 50) = 33 (ADR 0003 B2).
	ASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), 100.f);
	FVeloxDamageSpecParams Params;
	Params.Damage = 50;
	Params.DamageTag = VeloxGameplayTags::Damage_Environment;
	Params.Direction = FVector(0, 0, 2);
	ASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeDamageSpec(ASC, Params).Data.Get());
	TestEqual(TEXT("50 vs 100 armor: health"), Health->GetHealth(), 122.f - 17.f);
	TestEqual(TEXT("50 vs 100 armor: armor"), Health->GetArmor(), 67.f);
	TestEqual(TEXT("metas zeroed"), Health->GetIncomingDamage() + Health->GetIncomingArmorDamage() + Health->GetIncomingKnockback(), 0.f);
	const UVeloxMovementComponent* Movement = Pawn->GetVeloxMovementComponent();
	TestTrue(TEXT("knockback through AddKnockback: 625 cm/s up"), Movement && Movement->GetPendingKnockback().Velocity.Equals(FVector(0, 0, 625), 0.5));

	// Self damage halves health loss, keeps full knockback.
	W.Advance(0.1f);
	ASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), 0.f);
	ASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), 100.f);
	Params.Damage = 100;
	Params.bSelf = true;
	ASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeDamageSpec(ASC, Params).Data.Get());
	TestEqual(TEXT("self 100: health 50"), Health->GetHealth(), 50.f);
	TestTrue(TEXT("self 100: 1250 cm/s"), Movement && Movement->GetPendingKnockback().Velocity.Equals(FVector(0, 0, 1250), 0.5));
	Params.bSelf = false;

	// Ammo cost and clamp.
	ASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeAmmoCostSpec(ASC, VeloxGameplayTags::Ammo_Bullets, 1).Data.Get());
	TestEqual(TEXT("ammo cost 1"), Ammo->GetBullets(), 99.f);
	TestEqual(TEXT("other ammo untouched"), Ammo->GetSlugs(), 0.f);
	ASC->SetNumericAttributeBase(UVeloxAmmoSet::GetBulletsAttribute(), 500.f);
	TestEqual(TEXT("ammo clamp 200"), Ammo->GetBullets(), 200.f);

	// Spawn protection: no loss.
	ASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeDurationSpec(ASC, UVeloxGE_SpawnProtection::StaticClass(), 2.f).Data.Get());
	TestTrue(TEXT("protected tag"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected));
	ASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeDamageSpec(ASC, Params).Data.Get());
	TestEqual(TEXT("protected: no health loss"), Health->GetHealth(), 50.f);

	// Weapon switching tag for its duration.
	ASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeDurationSpec(ASC, UVeloxGE_WeaponSwitching::StaticClass(), 0.45f).Data.Get());
	TestTrue(TEXT("switching tag"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_WeaponSwitching));
	W.Advance(0.6f);
	TestFalse(TEXT("switching tag expired"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_WeaponSwitching));

	// Death: health 0 -> out of health once; dead targets take nothing.
	int32 NumOutOfHealth = 0;
	Health->OnOutOfHealth.AddLambda([&NumOutOfHealth](const FVeloxDamageEvent&) { ++NumOutOfHealth; });
	W.Advance(2.f);
	TestFalse(TEXT("protection expired"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected));
	Params.Damage = 200;
	ASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeDamageSpec(ASC, Params).Data.Get());
	TestEqual(TEXT("killed: health 0"), Health->GetHealth(), 0.f);
	TestEqual(TEXT("out of health once"), NumOutOfHealth, 1);
	ASC->ApplyGameplayEffectToSelf(GetDefault<UVeloxGE_Dead>(), 1.f, ASC->MakeEffectContext());
	TestTrue(TEXT("dead tag"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead));
	ASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeDamageSpec(ASC, Params).Data.Get());
	TestEqual(TEXT("out of health not repeated"), NumOutOfHealth, 1);

	// Respawn: new pawn, actor info re-init, GE_SpawnInit clears Dead and restarts one decay; nothing re-granted.
	const int32 NumAbilities = ASC->GetActivatableAbilities().Num();
	W.Controller->UnPossess();
	TestTrue(TEXT("unpossessed: no avatar"), ASC->GetAvatarActor() == nullptr);
	Pawn->Destroy();
	AVeloxCharacter* NewPawn = W.SpawnAndPossess();
	if (!TestNotNull(TEXT("respawned pawn"), NewPawn))
	{
		return false;
	}
	TestTrue(TEXT("new pawn is the avatar"), ASC->GetAvatarActor() == NewPawn);
	TestFalse(TEXT("dead tag removed"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead));
	TestEqual(TEXT("respawn health 125"), Health->GetHealth(), 125.f);
	TestEqual(TEXT("respawn bullets 100"), Ammo->GetBullets(), 100.f);
	TestEqual(TEXT("still one decay effect"), NumActiveEffects(ASC, UVeloxGE_HealthDecay::StaticClass()), 1);
	TestEqual(TEXT("abilities not re-granted"), ASC->GetActivatableAbilities().Num(), NumAbilities);

	// ADR 0003 B9: same pawn data is a no-op; a different one takes the old grant back and re-grants.
	TestFalse(TEXT("same pawn data: no re-grant"), W.PlayerState->GrantDefaultAbilitySets(NewPawn->GetPawnData()));
	UVeloxPawnData* OtherPawnData = NewObject<UVeloxPawnData>(GetTransientPackage());
	TestTrue(TEXT("changed pawn data: re-granted"), W.PlayerState->GrantDefaultAbilitySets(OtherPawnData));
	TestTrue(TEXT("changed pawn data: still granted"), W.PlayerState->HasGrantedDefaultAbilitySets());
	TestTrue(TEXT("changed pawn data (no sets): old abilities taken back"), ASC->GetActivatableAbilities().Num() <= NumAbilities);
	TestFalse(TEXT("changed pawn data: second call no-op"), W.PlayerState->GrantDefaultAbilitySets(OtherPawnData));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
