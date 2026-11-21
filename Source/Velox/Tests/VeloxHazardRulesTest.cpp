// Velox - hazard kill credit tests (ADR 0008 section 3.2, M7 task H1).
//   Velox.Combat.HazardRules    world-free: the credit window edge (inside / at / outside HazardKillCreditSeconds),
//                               attacker gone, the write rule (self damage, Velox.Damage.Self, hazard and world damage
//                               ignored; health, armor or knockback alone counts), tag hierarchy, settings defaults.
//   Velox.Combat.LastAttacker   the record on a real UVeloxHealthComponent in a private game world with two player
//                               states: an enemy rocket writes it, own rocket / hazard / world damage neither write nor
//                               clear it, window edge with the server clock, a new life clears it, the attacker leaving
//                               (player state destroyed, weak pointer invalid) credits nobody.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHazardRules.h"
#include "Combat/VeloxHealthComponent.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/VeloxPlayerState.h"

namespace VeloxHazardRulesTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FGameplayTagContainer TagsOf(std::initializer_list<FGameplayTag> Tags)
	{
		FGameplayTagContainer Container;
		for (const FGameplayTag& Tag : Tags)
		{
			Container.AddTag(Tag);
		}
		return Container;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxHazardRulesTest, "Velox.Combat.HazardRules", VeloxHazardRulesTest::Flags)

bool FVeloxHazardRulesTest::RunTest(const FString& Parameters)
{
	using namespace VeloxHazardRules;
	using VeloxHazardRulesTest::TagsOf;

	// Settings defaults (ADR 0008 3.3).
	const UVeloxCombatSettings& Defaults = UVeloxCombatSettings::GetOrDefault(nullptr);
	TestEqual(TEXT("LavaDamagePerTick 25"), Defaults.LavaDamagePerTick, 25);
	TestEqual(TEXT("LavaTickSeconds 0.25"), Defaults.LavaTickSeconds, 0.25f);
	TestEqual(TEXT("HazardKillCreditSeconds 5"), Defaults.HazardKillCreditSeconds, 5.f);
	const float Window = Defaults.HazardKillCreditSeconds;

	// Tags: Lava/Void are children of Hazard, none of them under Environment (the runners' world_kill substring).
	TestTrue(TEXT("Hazard tag valid"), VeloxGameplayTags::Damage_Hazard.GetTag().IsValid());
	TestEqual(TEXT("Hazard tag name"), VeloxGameplayTags::Damage_Hazard.GetTag().ToString(), FString(TEXT("Velox.Damage.Hazard")));
	TestEqual(TEXT("Lava tag name"), VeloxGameplayTags::Damage_Hazard_Lava.GetTag().ToString(), FString(TEXT("Velox.Damage.Hazard.Lava")));
	TestEqual(TEXT("Void tag name"), VeloxGameplayTags::Damage_Hazard_Void.GetTag().ToString(), FString(TEXT("Velox.Damage.Hazard.Void")));
	TestTrue(TEXT("Lava is a hazard"), VeloxGameplayTags::Damage_Hazard_Lava.GetTag().MatchesTag(VeloxGameplayTags::Damage_Hazard));
	TestTrue(TEXT("Void is a hazard"), VeloxGameplayTags::Damage_Hazard_Void.GetTag().MatchesTag(VeloxGameplayTags::Damage_Hazard));
	TestFalse(TEXT("Lava is not environment"), VeloxGameplayTags::Damage_Hazard_Lava.GetTag().MatchesTag(VeloxGameplayTags::Damage_Environment));
	TestFalse(TEXT("tag string has no Damage.Environment"), VeloxGameplayTags::Damage_Hazard_Void.GetTag().ToString().Contains(TEXT("Damage.Environment")));

	// Window edge: strictly younger than the window.
	const double T0 = 100.0;
	TestTrue(TEXT("age 0 credited"), IsCredited(true, true, T0, T0, Window));
	TestTrue(TEXT("age 4.9 credited"), IsCredited(true, true, T0, T0 + 4.9, Window));
	TestTrue(TEXT("age 4.999 credited"), IsCredited(true, true, T0, T0 + 4.999, Window));
	TestFalse(TEXT("age exactly 5 expired"), IsCredited(true, true, T0, T0 + 5.0, Window));
	TestFalse(TEXT("age 5.001 expired"), IsCredited(true, true, T0, T0 + 5.001, Window));
	TestFalse(TEXT("age 6 expired"), IsCredited(true, true, T0, T0 + 6.0, Window));
	TestTrue(TEXT("record from the future counts as age 0"), IsCredited(true, true, T0, T0 - 1.0, Window));
	TestFalse(TEXT("no record"), IsCredited(false, true, T0, T0, Window));
	TestFalse(TEXT("attacker gone"), IsCredited(true, false, T0, T0 + 1.0, Window));
	TestFalse(TEXT("zero window credits nobody"), IsCredited(true, true, T0, T0, 0.f));

	// Write rule.
	const FGameplayTagContainer Rocket = TagsOf({ VeloxGameplayTags::Damage_Rocket });
	TestTrue(TEXT("enemy rocket, health loss"), ShouldRecordAttacker(true, Rocket, 50.f, 0.f, true));
	TestTrue(TEXT("enemy hit, armor loss only"), ShouldRecordAttacker(true, Rocket, 0.f, 10.f, false));
	TestTrue(TEXT("enemy hit, knockback only (protected)"), ShouldRecordAttacker(true, Rocket, 0.f, 0.f, true));
	TestFalse(TEXT("enemy hit without effect"), ShouldRecordAttacker(true, Rocket, 0.f, 0.f, false));
	TestFalse(TEXT("own rocket (instigator is the victim)"), ShouldRecordAttacker(false, Rocket, 50.f, 0.f, true));
	TestFalse(TEXT("Damage.Self tag"), ShouldRecordAttacker(true, TagsOf({ VeloxGameplayTags::Damage_Rocket, VeloxGameplayTags::Damage_Self }), 50.f, 0.f, true));
	TestFalse(TEXT("hazard lava from the credited attacker's ASC"), ShouldRecordAttacker(true, TagsOf({ VeloxGameplayTags::Damage_Hazard_Lava }), 25.f, 0.f, false));
	TestFalse(TEXT("hazard void"), ShouldRecordAttacker(true, TagsOf({ VeloxGameplayTags::Damage_Hazard_Void }), 1000.f, 0.f, false));
	TestFalse(TEXT("hazard parent tag"), ShouldRecordAttacker(true, TagsOf({ VeloxGameplayTags::Damage_Hazard }), 1.f, 0.f, false));
	TestFalse(TEXT("world damage"), ShouldRecordAttacker(true, TagsOf({ VeloxGameplayTags::Damage_Environment }), 1000.f, 0.f, false));
	TestTrue(TEXT("telefrag is an attack"), ShouldRecordAttacker(true, TagsOf({ VeloxGameplayTags::Damage_Telefrag }), 1000.f, 0.f, false));
	TestTrue(TEXT("world-or-hazard: lava"), IsWorldOrHazardDamage(TagsOf({ VeloxGameplayTags::Damage_Hazard_Lava })));
	TestFalse(TEXT("world-or-hazard: rail"), IsWorldOrHazardDamage(TagsOf({ VeloxGameplayTags::Damage_Rail })));
	return true;
}

namespace VeloxHazardRulesTest
{
	/** Private game world with a floor and two AI-controlled players, each with its own AVeloxPlayerState. */
	class FTwoPlayerWorld
	{
	public:
		FTwoPlayerWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxHazardRulesTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
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
			for (int32 i = 0; i < 2; ++i)
			{
				PlayerStates[i] = World->SpawnActor<AVeloxPlayerState>();
				Controllers[i] = World->SpawnActor<AAIController>();
				if (Controllers[i])
				{
					Controllers[i]->PlayerState = PlayerStates[i];
				}
			}
		}

		~FTwoPlayerWorld()
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

		AVeloxCharacter* SpawnAndPossess(int32 Index)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVeloxCharacter* Character = World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), FVector(Index * 400.f, 0, 100), FRotator::ZeroRotator, Params);
			if (Character && Controllers[Index])
			{
				Controllers[Index]->Possess(Character);
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
		AVeloxPlayerState* PlayerStates[2] = { nullptr, nullptr };
		AAIController* Controllers[2] = { nullptr, nullptr };
	};

	/** Applies a damage spec built on SourceASC to TargetASC (the server path every weapon and hazard uses). */
	void Deal(UVeloxAbilitySystemComponent* SourceASC, UVeloxAbilitySystemComponent* TargetASC, int32 Damage, const FGameplayTag& DamageTag, bool bSelf, bool bNoKnockback)
	{
		FVeloxDamageSpecParams Params;
		Params.Damage = Damage;
		Params.DamageTag = DamageTag;
		Params.Direction = FVector(1, 0, 1);
		Params.bSelf = bSelf;
		Params.bNoArmor = bNoKnockback;
		Params.bNoKnockback = bNoKnockback;
		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(SourceASC, Params);
		if (Spec.Data.IsValid())
		{
			SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxLastAttackerTest, "Velox.Combat.LastAttacker", VeloxHazardRulesTest::Flags)

bool FVeloxLastAttackerTest::RunTest(const FString& Parameters)
{
	using namespace VeloxHazardRulesTest;

	FTwoPlayerWorld W;
	AVeloxPlayerState* VictimPS = W.PlayerStates[0];
	AVeloxPlayerState* AttackerPS = W.PlayerStates[1];
	if (!TestNotNull(TEXT("victim player state"), VictimPS) || !TestNotNull(TEXT("attacker player state"), AttackerPS))
	{
		return false;
	}
	AVeloxCharacter* Victim = W.SpawnAndPossess(0);
	AVeloxCharacter* Attacker = W.SpawnAndPossess(1);
	if (!TestNotNull(TEXT("victim pawn"), Victim) || !TestNotNull(TEXT("attacker pawn"), Attacker))
	{
		return false;
	}
	UVeloxAbilitySystemComponent* VictimASC = VictimPS->GetVeloxAbilitySystemComponent();
	UVeloxAbilitySystemComponent* AttackerASC = AttackerPS->GetVeloxAbilitySystemComponent();
	UVeloxHealthComponent* Health = Victim->GetHealthComponent();
	if (!TestNotNull(TEXT("victim ASC"), VictimASC) || !TestNotNull(TEXT("attacker ASC"), AttackerASC) || !TestNotNull(TEXT("health component"), Health))
	{
		return false;
	}
	const float Window = UVeloxCombatSettings::GetOrDefault(VictimPS->GetCombatSettings()).HazardKillCreditSeconds;
	W.Advance(0.5f);

	// Fresh life: no record.
	TestFalse(TEXT("fresh life: no record"), Health->GetLastAttacker().HasRecord());
	TestNull(TEXT("fresh life: nobody credited"), Health->GetCreditedAttacker(W.World->GetTimeSeconds(), Window));

	// Own rocket on a fresh life writes nothing.
	Deal(VictimASC, VictimASC, 20, VeloxGameplayTags::Damage_Rocket, true, false);
	TestFalse(TEXT("own rocket: no record"), Health->GetLastAttacker().HasRecord());

	// Enemy rocket writes the record with the server clock.
	const double HitTime = W.World->GetTimeSeconds();
	Deal(AttackerASC, VictimASC, 20, VeloxGameplayTags::Damage_Rocket, false, false);
	const FVeloxLastAttacker& Record = Health->GetLastAttacker();
	TestTrue(TEXT("enemy rocket: record"), Record.HasRecord());
	TestTrue(TEXT("enemy rocket: attacker"), Record.Attacker.Get() == AttackerPS);
	TestEqual(TEXT("enemy rocket: server time"), Record.ServerTime, HitTime);
	TestTrue(TEXT("enemy rocket: damage tag"), Record.DamageTag == VeloxGameplayTags::Damage_Rocket);

	// Window edge with the server clock.
	TestTrue(TEXT("credited at +4.9 s"), Health->GetCreditedAttacker(HitTime + 4.9, Window) == AttackerPS);
	TestNull(TEXT("expired at +5.0 s"), Health->GetCreditedAttacker(HitTime + Window, Window));
	TestNull(TEXT("expired at +5.1 s"), Health->GetCreditedAttacker(HitTime + 5.1, Window));

	// Own rocket, hazard and world damage neither write nor clear it.
	W.Advance(0.2f);
	Deal(VictimASC, VictimASC, 20, VeloxGameplayTags::Damage_Rocket, true, false);
	TestTrue(TEXT("own rocket: attacker kept"), Health->GetLastAttacker().Attacker.Get() == AttackerPS);
	TestEqual(TEXT("own rocket: time kept"), Health->GetLastAttacker().ServerTime, HitTime);
	Deal(AttackerASC, VictimASC, 10, VeloxGameplayTags::Damage_Hazard_Lava, false, true);
	TestEqual(TEXT("lava from the attacker's ASC: time kept"), Health->GetLastAttacker().ServerTime, HitTime);
	Deal(VictimASC, VictimASC, 10, VeloxGameplayTags::Damage_Hazard_Lava, false, true);
	TestTrue(TEXT("uncredited lava: attacker kept"), Health->GetLastAttacker().Attacker.Get() == AttackerPS);
	Deal(VictimASC, VictimASC, 1, VeloxGameplayTags::Damage_Environment, false, true);
	TestEqual(TEXT("world damage: time kept"), Health->GetLastAttacker().ServerTime, HitTime);
	TestTrue(TEXT("still credited inside the window"), Health->GetCreditedAttacker(W.World->GetTimeSeconds(), Window) == AttackerPS);

	// A later enemy hit refreshes the time.
	W.Advance(1.0f);
	const double SecondHit = W.World->GetTimeSeconds();
	Deal(AttackerASC, VictimASC, 10, VeloxGameplayTags::Damage_MachineGun, false, false);
	TestEqual(TEXT("refresh: time"), Health->GetLastAttacker().ServerTime, SecondHit);
	TestTrue(TEXT("refresh: tag"), Health->GetLastAttacker().DamageTag == VeloxGameplayTags::Damage_MachineGun);
	TestTrue(TEXT("refresh: credited at first hit + 5.5 s"), Health->GetCreditedAttacker(HitTime + 5.5, Window) == AttackerPS);

	// Rebinding (new life) clears it.
	Health->UninitializeFromAbilitySystem();
	TestFalse(TEXT("unbind clears"), Health->GetLastAttacker().HasRecord());
	Health->InitializeWithAbilitySystem(VictimASC);
	TestFalse(TEXT("rebind: no record"), Health->GetLastAttacker().HasRecord());

	// Attacker gone: the player state is destroyed, the weak pointer goes invalid, nobody is credited.
	const double ThirdHit = W.World->GetTimeSeconds();
	Deal(AttackerASC, VictimASC, 10, VeloxGameplayTags::Damage_Rail, false, false);
	TestTrue(TEXT("before leave: credited"), Health->GetCreditedAttacker(ThirdHit + 1.0, Window) == AttackerPS);
	W.Controllers[1]->UnPossess();
	Attacker->Destroy();
	AttackerPS->Destroy();
	TestFalse(TEXT("after leave: weak pointer invalid"), Health->GetLastAttacker().Attacker.IsValid());
	TestNull(TEXT("after leave: nobody credited"), Health->GetCreditedAttacker(ThirdHit + 1.0, Window));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
