// Velox - hazard volume tests (ADR 0008 sections 3.3 and 3.7, task M7 H2).
//
//   Velox.Combat.HazardVolume   automation, private game world: UVeloxHazardSubsystem registration, HasHazards,
//                               IsInHazard / FindHazardAt by type, unregister on destroy; type names; overlap settings.
//   velox.combat.FtHazard       FT_Hazard (console, `UnrealEditor-Cmd Velox.uproject L_HazardTest -game -nullrhi
//                               -UseFixedTimeStep -FPS=60 -ExecCmds="velox.bots.Seed 1, velox.combat.FtHazard quit"`;
//                               map: Tools/editor/create_hazard_test_map.py). Two frozen bots, attacker and victim, fresh
//                               per case; placement, the attacker's hits and the rockets are the test harness:
//     1 lava_self        victim (armor 50) put into the lava channel: every hit is LavaDamagePerTick, armor and velocity
//                        untouched, hits LavaTickSeconds apart, death at (ceil(H0 / 25) - 1) x 0.25 s after the first hit;
//                        suicide (score -1), feed tag Velox.Damage.Hazard.Lava.
//     2 void_rocket      the attacker's rocket knocks the victim (armor 100) off the platform edge into the void slab:
//                        frag credited (attacker kills +1, score +1), tag Velox.Damage.Hazard.Void.
//     3 void_walk_off    victim steps off the platform edge (harness: placed beyond it): suicide.
//     4 void_expired     attacker hits the victim (MG 10), 6 s pass (> HazardKillCreditSeconds 5), victim steps off:
//                        suicide, the record exists but is no longer credited.
//     5 void_own_rocket  the victim's own rocket at its feet (self damage, no record) knocks it off the edge (if it does
//                        not within 1 s, the harness places it beyond the edge): suicide.
//     6 lava_quad        attacker hits the victim (MG 10) and takes Quad, victim (armor 50) put into lava: every hit is
//                        still LavaDamagePerTick (no Quad scaling), armor untouched, frag credited to the attacker.
//   Logs `VeloxHazardFt test=FT_Hazard check=... ok=` and `VeloxHazardFt test=FT_Hazard result=PASS|FAIL`; quit exits 0/1.

#include "Misc/AutomationTest.h"

#include "Combat/VeloxHazardSubsystem.h"
#include "Combat/VeloxHazardVolume.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxHazardVolumeTest, "Velox.Combat.HazardVolume", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxHazardVolumeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("type name lava"), FString(AVeloxHazardVolume::TypeName(EVeloxHazardType::Lava)), FString(TEXT("lava")));
	TestEqual(TEXT("type name void"), FString(AVeloxHazardVolume::TypeName(EVeloxHazardType::Void)), FString(TEXT("void")));

	const AVeloxHazardVolume* CDO = GetDefault<AVeloxHazardVolume>();
	TestFalse(TEXT("volume does not replicate"), CDO->GetIsReplicated());
	TestTrue(TEXT("volume loads on clients"), CDO->bNetLoadOnClient);
	const UBoxComponent* CDOBox = CDO->GetBox();
	if (TestNotNull(TEXT("box component"), CDOBox))
	{
		TestEqual(TEXT("box overlaps pawns"), CDOBox->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
		TestEqual(TEXT("box ignores visibility traces"), CDOBox->GetCollisionResponseToChannel(ECC_Visibility), ECR_Ignore);
		TestEqual(TEXT("box ignores world dynamic"), CDOBox->GetCollisionResponseToChannel(ECC_WorldDynamic), ECR_Ignore);
		TestEqual(TEXT("box is query only"), CDOBox->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		TestFalse(TEXT("box does not affect navigation"), CDOBox->CanEverAffectNavigation());
		TestFalse(TEXT("overlap events off by default (enabled on the server in PostInitializeComponents)"), CDOBox->GetGenerateOverlapEvents());
	}

	if (!GEngine)
	{
		return true;
	}
	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxHazardVolumeTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("world"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay(); // HasBegunPlay: spawned actors get BeginPlay (no game mode here)

	UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(World);
	if (TestNotNull(TEXT("subsystem"), Hazards))
	{
		TestFalse(TEXT("empty world: no hazards"), Hazards->HasHazards());

		FActorSpawnParameters SpawnParams;
		SpawnParams.bDeferConstruction = true;
		AVeloxHazardVolume* Lava = World->SpawnActor<AVeloxHazardVolume>(AVeloxHazardVolume::StaticClass(), FTransform(FVector(0.0, 0.0, -50.0)), SpawnParams);
		AVeloxHazardVolume* Void = World->SpawnActor<AVeloxHazardVolume>(AVeloxHazardVolume::StaticClass(), FTransform(FVector(1000.0, 0.0, -900.0)), SpawnParams);
		if (TestNotNull(TEXT("lava spawned"), Lava) && TestNotNull(TEXT("void spawned"), Void))
		{
			Lava->GetBox()->SetBoxExtent(FVector(150.0, 600.0, 10.0), false);
			Void->GetBox()->SetBoxExtent(FVector(500.0, 500.0, 400.0), false);
			// Type is protected: set through reflection as the generator does (veloxmap.hazard()).
			if (FEnumProperty* TypeProp = CastField<FEnumProperty>(AVeloxHazardVolume::StaticClass()->FindPropertyByName(TEXT("Type"))))
			{
				TypeProp->GetUnderlyingProperty()->SetIntPropertyValue(TypeProp->ContainerPtrToValuePtr<void>(Void), static_cast<int64>(EVeloxHazardType::Void));
			}
			Lava->FinishSpawning(FTransform(FVector(0.0, 0.0, -50.0)));
			Void->FinishSpawning(FTransform(FVector(1000.0, 0.0, -900.0)));

			TestEqual(TEXT("lava type"), Lava->GetHazardType(), EVeloxHazardType::Lava);
			TestEqual(TEXT("void type"), Void->GetHazardType(), EVeloxHazardType::Void);
			TestTrue(TEXT("two volumes: has hazards"), Hazards->HasHazards());
			TestEqual(TEXT("two volumes registered"), Hazards->GetVolumes().Num(), 2);
			TestTrue(TEXT("server world: overlap events on"), Lava->GetBox()->GetGenerateOverlapEvents());
			TestTrue(TEXT("point in lava"), Hazards->IsInHazard(FVector(100.0, -500.0, -45.0)));
			TestTrue(TEXT("point in lava, typed"), Hazards->IsInHazard(FVector(100.0, -500.0, -45.0), EVeloxHazardType::Lava));
			TestFalse(TEXT("point in lava is not void"), Hazards->IsInHazard(FVector(100.0, -500.0, -45.0), EVeloxHazardType::Void));
			TestFalse(TEXT("above the lava surface (-40)"), Hazards->IsInHazard(FVector(100.0, -500.0, -35.0)));
			TestFalse(TEXT("beside the channel"), Hazards->IsInHazard(FVector(160.0, 0.0, -50.0)));
			TestTrue(TEXT("FindHazardAt void"), Hazards->FindHazardAt(FVector(1400.0, 300.0, -1200.0)) == Void);
			TestTrue(TEXT("point in void, typed"), Hazards->IsInHazard(FVector(1400.0, 300.0, -1200.0), EVeloxHazardType::Void));
			TestNull(TEXT("FindHazardAt open air"), Hazards->FindHazardAt(FVector(0.0, 0.0, 500.0)));

			Void->Destroy();
			TestEqual(TEXT("destroyed volume unregistered"), Hazards->GetVolumes().Num(), 1);
			Lava->Destroy();
			TestFalse(TEXT("all destroyed: no hazards"), Hazards->HasHazards());
		}
	}

	World->DestroyWorld(true);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// --------------------------------------------------------------------------------------------------------- FT_Hazard

#if !UE_BUILD_SHIPPING

#include "AbilitySystemComponent.h"
#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxDummyController.h"
#include "Bots/VeloxDummySpawner.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthComponent.h"
#include "Combat/VeloxHealthSet.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/VeloxGameplayTags.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Items/VeloxPowerupEffects.h"
#include "Movement/VeloxMovementComponent.h"
#include "Weapons/VeloxProjectile.h"
#include "Weapons/VeloxWeaponCheats.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

namespace VeloxHazardFt
{
	// L_HazardTest coordinates (Tools/editor/create_hazard_test_map.py).
	const FVector AttackerSpot(-2000.0, 0.0, 0.0);
	const FVector VictimRestSpot(-800.0, 300.0, 0.0);
	const FVector LavaSpot(-1350.0, 0.0, -60.0);
	/** 60 cm inside the platform's +X edge (x 1800). */
	const FVector EdgeSpot(1740.0, 0.0, 0.0);
	/** 100 cm beyond the platform's +X edge: nothing below but the void slab. */
	const FVector OffEdgeSpot(1900.0, 0.0, 0.0);
	constexpr double PlatformEdgeX = 1800.0;

	constexpr double SettleSeconds = 0.5;
	constexpr double ObserveTimeoutSeconds = 8.0;
	constexpr double NextCaseDelaySeconds = 0.5;
	constexpr double CreditExpireWaitSeconds = 6.0;
	constexpr double OwnRocketWaitSeconds = 1.0;
	constexpr double LavaAfterHitDelaySeconds = 0.3;
	constexpr double RocketDistanceCm = 400.0;
	constexpr int32 HitDamage = 10;
	constexpr float LavaArmor = 50.f;
	constexpr float RocketArmor = 100.f;
	/** Death / tick time tolerance: one 60 Hz frame plus slack. */
	constexpr double TimeToleranceSeconds = 1.0 / 60.0 + 0.004;
	constexpr float QuadSeconds = 30.f;

	enum class ECase : uint8 { LavaSelf, VoidRocket, VoidWalkOff, VoidExpired, VoidOwnRocket, LavaQuad, Count };

	const TCHAR* CaseName(ECase Case)
	{
		switch (Case)
		{
		case ECase::LavaSelf: return TEXT("lava_self");
		case ECase::VoidRocket: return TEXT("void_rocket");
		case ECase::VoidWalkOff: return TEXT("void_walk_off");
		case ECase::VoidExpired: return TEXT("void_expired");
		case ECase::VoidOwnRocket: return TEXT("void_own_rocket");
		case ECase::LavaQuad: return TEXT("lava_quad");
		default: return TEXT("?");
		}
	}

	bool IsLava(ECase Case) { return Case == ECase::LavaSelf || Case == ECase::LavaQuad; }
	bool IsCredited(ECase Case) { return Case == ECase::VoidRocket || Case == ECase::LavaQuad; }

	struct FHit
	{
		double Time = 0.0;
		FVeloxDamageEvent Event;
	};

	struct FHazardFt
	{
		TWeakObjectPtr<UWorld> World;
		bool bQuit = false;
		bool bPassed = true;
		bool bFinished = false;
		FTSTicker::FDelegateHandle TickHandle;
		FDelegateHandle KillHandle;

		int32 CaseIndex = 0;
		/** 0 settle, 1.. case steps, 99 observe (wait for the death), 100 next-case delay. */
		int32 Phase = 0;
		double PhaseStart = 0.0;
		TWeakObjectPtr<AVeloxBotController> Attacker;
		TWeakObjectPtr<AVeloxBotController> Victim;
		TWeakObjectPtr<AVeloxPlayerState> AttackerPS;
		TWeakObjectPtr<AVeloxPlayerState> VictimPS;
		TWeakObjectPtr<UVeloxHealthComponent> VictimHealth;
		FDelegateHandle DamageHandle;

		// Per-case observations.
		TArray<FHit> Hits;
		bool bKilled = false;
		FVeloxKillEvent Kill;
		float ArmorBefore = 0.f;
		int32 AttackerKills0 = 0;
		float AttackerScore0 = 0.f;
		float VictimScore0 = 0.f;
		int32 VictimDeaths0 = 0;
		bool bRecordAtHit = false;
		bool bCreditedAtStep = false;
		bool bPushedOff = false;

		ECase Case() const { return static_cast<ECase>(CaseIndex); }

		void Check(bool bOk, const TCHAR* What, const FString& Detail)
		{
			bPassed &= bOk;
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxHazardFt test=FT_Hazard case=%s check=%s ok=%d %s"), CaseName(Case()), What, bOk ? 1 : 0, *Detail);
		}

		double Now() const
		{
			const UWorld* W = World.Get();
			return W ? W->GetTimeSeconds() : 0.0;
		}

		void UnbindVictim()
		{
			if (UVeloxHealthComponent* Health = VictimHealth.Get())
			{
				Health->OnDamaged.Remove(DamageHandle);
			}
			DamageHandle.Reset();
			VictimHealth.Reset();
		}

		void Finish()
		{
			if (bFinished)
			{
				return;
			}
			bFinished = true;
			UnbindVictim();
			if (UWorld* W = World.Get())
			{
				if (AVeloxGameState* GS = W->GetGameState<AVeloxGameState>())
				{
					GS->OnKill.Remove(KillHandle);
				}
			}
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxHazardFt test=FT_Hazard result=%s"), bPassed ? TEXT("PASS") : TEXT("FAIL"));
			if (bQuit)
			{
				FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
			}
		}
	};

	TSharedPtr<FHazardFt> GHazardFt;

	void Place(AVeloxCharacter& Pawn, const FVector& FloorPoint)
	{
		// Test harness: put the pawn on the floor point at rest (the bot never does this itself).
		Pawn.SetActorLocationAndRotation(FloorPoint + FVector(0.0, 0.0, Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0), FRotator::ZeroRotator, false, nullptr,
			ETeleportType::TeleportPhysics);
		if (UVeloxMovementComponent* Movement = Pawn.GetVeloxMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
	}

	AVeloxCharacter* PawnOf(const TWeakObjectPtr<AVeloxBotController>& Bot)
	{
		return Bot.IsValid() ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
	}

	const UVeloxWeaponDefinition* FindRocketLauncher(const AVeloxPlayerState& PS)
	{
		const UVeloxWeaponInventoryComponent* Inventory = PS.GetWeaponInventory();
		for (int32 i = 0; Inventory && i < Inventory->NumWeapons(); ++i)
		{
			const UVeloxWeaponDefinition* Definition = Inventory->GetDefinition(i);
			if (Definition && Definition->WeaponTag == VeloxGameplayTags::Weapon_RocketLauncher)
			{
				return Definition;
			}
		}
		return nullptr;
	}

	void GiveArmor(AVeloxCharacter& Pawn, float Armor)
	{
		UVeloxAbilitySystemComponent* ASC = Pawn.GetVeloxAbilitySystemComponent();
		const FGameplayEffectSpecHandle Spec = ASC ? VeloxEffects::MakeArmorGainSpec(ASC, FMath::RoundToInt(Armor)) : FGameplayEffectSpecHandle();
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}

	/** The attacker's machine-gun hit on the victim (writes the last-attacker record). */
	bool AttackerHit(FHazardFt& Ft)
	{
		AVeloxCharacter* AttackerPawn = PawnOf(Ft.Attacker);
		AVeloxCharacter* VictimPawn = PawnOf(Ft.Victim);
		UVeloxAbilitySystemComponent* AttackerASC = AttackerPawn ? AttackerPawn->GetVeloxAbilitySystemComponent() : nullptr;
		UVeloxAbilitySystemComponent* VictimASC = VictimPawn ? VictimPawn->GetVeloxAbilitySystemComponent() : nullptr;
		if (!AttackerASC || !VictimASC)
		{
			Ft.Check(false, TEXT("attacker_hit"), TEXT("no ability system"));
			return false;
		}
		FVeloxDamageSpecParams Params;
		Params.Damage = HitDamage;
		Params.DamageTag = VeloxGameplayTags::Damage_MachineGun;
		Params.HitLocation = VictimPawn->GetActorLocation();
		Params.bNoKnockback = true;
		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(AttackerASC, Params);
		if (!Spec.IsValid())
		{
			Ft.Check(false, TEXT("attacker_hit"), TEXT("no spec"));
			return false;
		}
		AttackerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), VictimASC);
		const UVeloxHealthComponent* Health = VictimPawn->GetHealthComponent();
		Ft.bRecordAtHit = Health && Health->GetLastAttacker().HasRecord() && Health->GetLastAttacker().Attacker.Get() == Ft.AttackerPS.Get();
		Ft.Check(Ft.bRecordAtHit, TEXT("record_written"), FString::Printf(TEXT("attacker=%s"), Health ? *GetNameSafe(Health->GetLastAttacker().Attacker.Get()) : TEXT("-")));
		return true;
	}

	bool SpawnRocket(FHazardFt& Ft, AVeloxCharacter& Shooter, const FVector& Origin, const FVector& Dir)
	{
		UWorld* World = Ft.World.Get();
		const AVeloxPlayerState* PS = Shooter.GetPlayerState<AVeloxPlayerState>();
		const UVeloxWeaponDefinition* Rocket = PS ? FindRocketLauncher(*PS) : nullptr;
		if (!World || !Rocket)
		{
			Ft.Check(false, TEXT("rocket_definition"), FString::Printf(TEXT("shooter=%s has no rocket launcher"), *Shooter.GetName()));
			return false;
		}
		FVeloxProjectileLaunch Launch;
		Launch.Definition = Rocket;
		Launch.Shooter = &Shooter;
		Launch.SourceASC = Shooter.GetVeloxAbilitySystemComponent();
		Launch.Origin = Origin;
		Launch.Dir = Dir.GetSafeNormal();
		if (!AVeloxProjectile::SpawnServerProjectile(*World, Rocket->ProjectileClass, Launch))
		{
			Ft.Check(false, TEXT("rocket_spawn"), TEXT("no projectile"));
			return false;
		}
		return true;
	}

	bool StartCase(const TSharedRef<FHazardFt>& Ft)
	{
		UWorld* World = Ft->World.Get();
		UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		if (!Bots)
		{
			return false;
		}
		Ft->UnbindVictim();
		if (AVeloxBotController* Old = Ft->Attacker.Get())
		{
			Bots->KickBot(Old->GetBotName());
		}
		if (AVeloxBotController* Old = Ft->Victim.Get())
		{
			Bots->KickBot(Old->GetBotName());
		}
		AVeloxBotController* Attacker = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Easy")), FString::Printf(TEXT("FtHazAttacker%d"), Ft->CaseIndex + 1));
		AVeloxBotController* Victim = Bots->AddBot(UVeloxBotSubsystem::LoadDifficulty(TEXT("Easy")), FString::Printf(TEXT("FtHazVictim%d"), Ft->CaseIndex + 1));
		AVeloxCharacter* AttackerPawn = Attacker ? Cast<AVeloxCharacter>(Attacker->GetPawn()) : nullptr;
		AVeloxCharacter* VictimPawn = Victim ? Cast<AVeloxCharacter>(Victim->GetPawn()) : nullptr;
		AVeloxPlayerState* AttackerPS = Attacker ? Attacker->GetPlayerState<AVeloxPlayerState>() : nullptr;
		AVeloxPlayerState* VictimPS = Victim ? Victim->GetPlayerState<AVeloxPlayerState>() : nullptr;
		UVeloxHealthComponent* Health = VictimPawn ? VictimPawn->GetHealthComponent() : nullptr;
		if (!AttackerPawn || !VictimPawn || !AttackerPS || !VictimPS || !Health || !VeloxWeaponCheats::GiveAllWeapons(*AttackerPS) || !VeloxWeaponCheats::GiveAllWeapons(*VictimPS))
		{
			Ft->Check(false, TEXT("setup"), FString::Printf(TEXT("attacker=%s victim=%s"), *GetNameSafe(Attacker), *GetNameSafe(Victim)));
			return false;
		}
		Attacker->SetFrozen(true);
		Victim->SetFrozen(true);
		Ft->Attacker = Attacker;
		Ft->Victim = Victim;
		Ft->AttackerPS = AttackerPS;
		Ft->VictimPS = VictimPS;
		Ft->VictimHealth = Health;
		TWeakPtr<FHazardFt> Weak = Ft;
		Ft->DamageHandle = Health->OnDamaged.AddLambda([Weak](const FVeloxDamageEvent& Event)
		{
			if (const TSharedPtr<FHazardFt> F = Weak.Pin())
			{
				F->Hits.Add({ F->Now(), Event });
			}
		});
		Place(*AttackerPawn, AttackerSpot);
		Place(*VictimPawn, IsLava(Ft->Case()) ? VictimRestSpot : EdgeSpot);
		Ft->Hits.Reset();
		Ft->bKilled = false;
		Ft->Kill = FVeloxKillEvent();
		Ft->AttackerKills0 = AttackerPS->GetKills();
		Ft->AttackerScore0 = AttackerPS->GetScore();
		Ft->VictimScore0 = VictimPS->GetScore();
		Ft->VictimDeaths0 = VictimPS->GetDeaths();
		Ft->bRecordAtHit = false;
		Ft->bCreditedAtStep = false;
		Ft->bPushedOff = false;
		Ft->Phase = 0;
		Ft->PhaseStart = Ft->Now();
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxHazardFt test=FT_Hazard case=%s start attacker=%s victim=%s"), CaseName(Ft->Case()), *AVeloxPlayerState::VeloxPlayerLabel(AttackerPS),
			*AVeloxPlayerState::VeloxPlayerLabel(VictimPS));
		return true;
	}

	void SetPhase(FHazardFt& Ft, int32 Phase)
	{
		Ft.Phase = Phase;
		Ft.PhaseStart = Ft.Now();
	}

	/** Puts the victim into the case's hazard (lava channel or off the edge) and starts observing. */
	void EnterHazard(FHazardFt& Ft, AVeloxCharacter& VictimPawn)
	{
		Ft.Hits.Reset();
		Ft.ArmorBefore = VictimPawn.GetHealthComponent() ? VictimPawn.GetHealthComponent()->GetArmor() : 0.f;
		Place(VictimPawn, IsLava(Ft.Case()) ? LavaSpot : OffEdgeSpot);
		SetPhase(Ft, 99);
	}

	void Evaluate(FHazardFt& Ft)
	{
		const ECase Case = Ft.Case();
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Ft.VictimPS.IsValid() ? Ft.VictimPS->GetCombatSettings() : nullptr);
		const FGameplayTag ExpectedTag = IsLava(Case) ? VeloxGameplayTags::Damage_Hazard_Lava : VeloxGameplayTags::Damage_Hazard_Void;
		AVeloxPlayerState* AttackerPS = Ft.AttackerPS.Get();
		AVeloxPlayerState* VictimPS = Ft.VictimPS.Get();
		Ft.Check(Ft.bKilled, TEXT("death"), FString::Printf(TEXT("killed=%d"), Ft.bKilled ? 1 : 0));
		if (!Ft.bKilled || !AttackerPS || !VictimPS)
		{
			return;
		}
		Ft.Check(Ft.Kill.DamageTags.HasTagExact(ExpectedTag), TEXT("kill_tag"), FString::Printf(TEXT("tags=[%s] expected=%s"), *Ft.Kill.DamageTags.ToStringSimple(), *ExpectedTag.ToString()));
		const bool bCredited = IsCredited(Case);
		const AVeloxPlayerState* ExpectedKiller = bCredited ? AttackerPS : VictimPS;
		Ft.Check(Ft.Kill.Killer.Get() == ExpectedKiller, TEXT("killer"), FString::Printf(TEXT("killer=%s expected=%s"), *AVeloxPlayerState::VeloxPlayerLabel(Ft.Kill.Killer.Get()),
			*AVeloxPlayerState::VeloxPlayerLabel(ExpectedKiller)));
		const int32 KillsDelta = AttackerPS->GetKills() - Ft.AttackerKills0;
		const float AttackerScoreDelta = AttackerPS->GetScore() - Ft.AttackerScore0;
		const float VictimScoreDelta = VictimPS->GetScore() - Ft.VictimScore0;
		const int32 DeathsDelta = VictimPS->GetDeaths() - Ft.VictimDeaths0;
		const bool bScoreOk = DeathsDelta == 1 && (bCredited ? (KillsDelta == 1 && AttackerScoreDelta == 1.f && VictimScoreDelta == 0.f)
			: (KillsDelta == 0 && AttackerScoreDelta == 0.f && VictimScoreDelta == -1.f));
		Ft.Check(bScoreOk, bCredited ? TEXT("frag_credited") : TEXT("suicide"), FString::Printf(TEXT("attacker_kills%+d attacker_score%+.0f victim_score%+.0f victim_deaths%+d"),
			KillsDelta, AttackerScoreDelta, VictimScoreDelta, DeathsDelta));

		// Hazard hits observed by the victim's health component.
		TArray<const FHit*> HazardHits;
		for (const FHit& Hit : Ft.Hits)
		{
			if (Hit.Event.DamageTags.HasTag(VeloxGameplayTags::Damage_Hazard))
			{
				HazardHits.Add(&Hit);
			}
		}
		Ft.Check(HazardHits.Num() > 0 && HazardHits.Last()->Event.DamageTags.HasTagExact(ExpectedTag), TEXT("hazard_hits"), FString::Printf(TEXT("n=%d"), HazardHits.Num()));
		if (HazardHits.Num() == 0)
		{
			return;
		}
		bool bNoArmorNoKnock = true;
		bool bInstigatorOk = true;
		for (const FHit* Hit : HazardHits)
		{
			bNoArmorNoKnock &= Hit->Event.ArmorLoss == 0.f && Hit->Event.Knockback.IsNearlyZero() && Hit->Event.NewArmor == Ft.ArmorBefore;
			bInstigatorOk &= Hit->Event.Instigator.Get() == ExpectedKiller;
		}
		Ft.Check(bNoArmorNoKnock, TEXT("no_armor_no_knockback"), FString::Printf(TEXT("armor_before=%.0f armor_last=%.0f"), Ft.ArmorBefore, HazardHits.Last()->Event.NewArmor));
		Ft.Check(bInstigatorOk, TEXT("instigator"), FString::Printf(TEXT("first=%s"), *GetNameSafe(HazardHits[0]->Event.Instigator.Get())));

		if (IsLava(Case))
		{
			bool bAmountOk = true;
			bool bSpacingOk = true;
			double WorstSpacing = 0.0;
			for (int32 i = 0; i < HazardHits.Num(); ++i)
			{
				bAmountOk &= HazardHits[i]->Event.HealthLoss == static_cast<float>(Settings.LavaDamagePerTick);
				if (i > 0)
				{
					const double Spacing = HazardHits[i]->Time - HazardHits[i - 1]->Time;
					bSpacingOk &= FMath::Abs(Spacing - Settings.LavaTickSeconds) <= TimeToleranceSeconds;
					WorstSpacing = FMath::Max(WorstSpacing, FMath::Abs(Spacing - Settings.LavaTickSeconds));
				}
			}
			Ft.Check(bAmountOk, Case == ECase::LavaQuad ? TEXT("lava_not_quad_scaled") : TEXT("lava_per_tick"), FString::Printf(TEXT("hits=%d per_tick=%d first_loss=%.0f last_loss=%.0f"),
				HazardHits.Num(), Settings.LavaDamagePerTick, HazardHits[0]->Event.HealthLoss, HazardHits.Last()->Event.HealthLoss));
			Ft.Check(bSpacingOk, TEXT("lava_tick_spacing"), FString::Printf(TEXT("tick=%.3f worst_error=%.4f"), Settings.LavaTickSeconds, WorstSpacing));
			const float H0 = HazardHits[0]->Event.OldHealth;
			const int32 ExpectedHits = FMath::CeilToInt(H0 / static_cast<float>(Settings.LavaDamagePerTick));
			const double ExpectedDeath = (ExpectedHits - 1) * Settings.LavaTickSeconds;
			const double DeathAfter = Ft.Kill.Time - HazardHits[0]->Time;
			Ft.Check(HazardHits.Num() == ExpectedHits && FMath::Abs(DeathAfter - ExpectedDeath) <= TimeToleranceSeconds, TEXT("lava_death_time"),
				FString::Printf(TEXT("h0=%.0f hits=%d expected_hits=%d death_after=%.3f expected=%.3f"), H0, HazardHits.Num(), ExpectedHits, DeathAfter, ExpectedDeath));
		}
		else
		{
			Ft.Check(HazardHits.Num() == 1 && HazardHits[0]->Event.HealthLoss == static_cast<float>(Settings.EnvironmentKillDamage), TEXT("void_one_kill_hit"),
				FString::Printf(TEXT("hits=%d loss=%.0f"), HazardHits.Num(), HazardHits[0]->Event.HealthLoss));
		}
		if (Case == ECase::LavaQuad)
		{
			const AVeloxCharacter* AttackerPawn = PawnOf(Ft.Attacker);
			Ft.Check(AttackerPawn && VeloxPowerups::HasQuad(AttackerPawn->GetVeloxAbilitySystemComponent()), TEXT("attacker_has_quad"), TEXT(""));
		}
	}

	/** One FT frame. Returns false when the run is over (finished or failed). */
	bool Tick(const TSharedRef<FHazardFt>& Ft)
	{
		FHazardFt& F = *Ft;
		const double T = F.Now();
		const double InPhase = T - F.PhaseStart;
		if (F.Phase == 100)
		{
			if (InPhase < NextCaseDelaySeconds)
			{
				return true;
			}
			++F.CaseIndex;
			if (F.CaseIndex >= static_cast<int32>(ECase::Count))
			{
				return false;
			}
			return StartCase(Ft);
		}
		if (F.Phase == 99)
		{
			if (F.bKilled)
			{
				Evaluate(F);
				SetPhase(F, 100);
				return true;
			}
			if (InPhase > ObserveTimeoutSeconds)
			{
				const AVeloxCharacter* VictimPawn = PawnOf(F.Victim);
				F.Check(false, TEXT("death"), FString::Printf(TEXT("timeout at=%s"), VictimPawn ? *VictimPawn->GetActorLocation().ToCompactString() : TEXT("-")));
				SetPhase(F, 100);
			}
			return true;
		}

		AVeloxCharacter* VictimPawn = PawnOf(F.Victim);
		AVeloxCharacter* AttackerPawn = PawnOf(F.Attacker);
		if (!VictimPawn || !AttackerPawn || VictimPawn->IsCorpse())
		{
			F.Check(false, TEXT("alive"), TEXT("attacker or victim pawn gone before the case acted"));
			SetPhase(F, 100);
			return true;
		}
		if (F.Phase == 0)
		{
			if (InPhase < SettleSeconds)
			{
				return true;
			}
			switch (F.Case())
			{
			case ECase::LavaSelf:
				GiveArmor(*VictimPawn, LavaArmor);
				EnterHazard(F, *VictimPawn);
				return true;
			case ECase::VoidRocket:
			{
				GiveArmor(*VictimPawn, RocketArmor);
				F.Hits.Reset();
				F.ArmorBefore = 0.f;
				const FVector Target = VictimPawn->GetActorLocation();
				if (!SpawnRocket(F, *AttackerPawn, Target - FVector(RocketDistanceCm, 0.0, 0.0), FVector::ForwardVector))
				{
					SetPhase(F, 100);
					return true;
				}
				SetPhase(F, 1);
				return true;
			}
			case ECase::VoidWalkOff:
				EnterHazard(F, *VictimPawn);
				return true;
			case ECase::VoidExpired:
				if (!AttackerHit(F))
				{
					SetPhase(F, 100);
					return true;
				}
				SetPhase(F, 1);
				return true;
			case ECase::VoidOwnRocket:
			{
				const FVector Feet = VictimPawn->GetActorLocation() - FVector(0.0, 0.0, VictimPawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
				if (!SpawnRocket(F, *VictimPawn, Feet + FVector(-70.0, 0.0, 50.0), -FVector::UpVector))
				{
					SetPhase(F, 100);
					return true;
				}
				SetPhase(F, 1);
				return true;
			}
			case ECase::LavaQuad:
			{
				if (!AttackerHit(F))
				{
					SetPhase(F, 100);
					return true;
				}
				UVeloxAbilitySystemComponent* AttackerASC = AttackerPawn->GetVeloxAbilitySystemComponent();
				if (AttackerASC)
				{
					VeloxPowerups::GrantOrExtend(*AttackerASC, UVeloxGE_Powerup_Quad::StaticClass(), QuadSeconds);
				}
				F.Check(AttackerASC && VeloxPowerups::HasQuad(AttackerASC), TEXT("quad_granted"), TEXT(""));
				GiveArmor(*VictimPawn, LavaArmor);
				SetPhase(F, 1);
				return true;
			}
			default:
				return false;
			}
		}

		// Phase 1: case-specific follow-up.
		const UVeloxHealthComponent* Health = VictimPawn->GetHealthComponent();
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(F.VictimPS.IsValid() ? F.VictimPS->GetCombatSettings() : nullptr);
		switch (F.Case())
		{
		case ECase::VoidRocket:
		{
			// The rocket hit (health or armor loss from Velox.Damage.Rocket) must have written the record; the fall is observed in phase 99.
			const bool bRocketHit = F.Hits.ContainsByPredicate([](const FHit& H) { return H.Event.DamageTags.HasTag(VeloxGameplayTags::Damage_Rocket); });
			if (bRocketHit)
			{
				F.bRecordAtHit = Health && Health->GetLastAttacker().Attacker.Get() == F.AttackerPS.Get();
				F.Check(F.bRecordAtHit, TEXT("record_written"), FString::Printf(TEXT("attacker=%s"), Health ? *GetNameSafe(Health->GetLastAttacker().Attacker.Get()) : TEXT("-")));
				F.ArmorBefore = Health ? Health->GetArmor() : 0.f;
				SetPhase(F, 99);
				// The hits list keeps the rocket hit; the evaluation looks at hazard-tagged hits only.
				F.PhaseStart = T;
			}
			else if (InPhase > 2.0)
			{
				F.Check(false, TEXT("rocket_hit"), TEXT("no Velox.Damage.Rocket hit on the victim within 2 s"));
				SetPhase(F, 100);
			}
			return true;
		}
		case ECase::VoidExpired:
			if (InPhase >= CreditExpireWaitSeconds)
			{
				F.bCreditedAtStep = Health && Health->GetCreditedAttacker(T, Settings.HazardKillCreditSeconds) != nullptr;
				F.Check(Health && Health->GetLastAttacker().HasRecord() && !F.bCreditedAtStep, TEXT("record_expired"),
					FString::Printf(TEXT("age=%.2f window=%.1f credited=%d"), Health ? T - Health->GetLastAttacker().ServerTime : -1.0, Settings.HazardKillCreditSeconds, F.bCreditedAtStep ? 1 : 0));
				EnterHazard(F, *VictimPawn);
			}
			return true;
		case ECase::VoidOwnRocket:
		{
			const bool bSelfHit = F.Hits.ContainsByPredicate([](const FHit& H) { return H.Event.DamageTags.HasTag(VeloxGameplayTags::Damage_Self); });
			// Knocked off: below the platform top or beyond its +X edge (the void slab is under both).
			const FVector At = VictimPawn->GetActorLocation();
			const bool bOffPlatform = At.Z < -100.0 || At.X > PlatformEdgeX;
			if (InPhase < OwnRocketWaitSeconds && !(bSelfHit && bOffPlatform))
			{
				return true;
			}
			F.Check(bSelfHit, TEXT("self_hit"), FString::Printf(TEXT("hits=%d"), F.Hits.Num()));
			F.Check(Health && !Health->GetLastAttacker().HasRecord(), TEXT("self_no_record"), TEXT(""));
			const bool bAlreadyFalling = bOffPlatform;
			F.bPushedOff = bAlreadyFalling;
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxHazardFt test=FT_Hazard case=%s knocked_off_by_own_rocket=%d at=%s"), CaseName(F.Case()), bAlreadyFalling ? 1 : 0,
				*VictimPawn->GetActorLocation().ToCompactString());
			if (bAlreadyFalling)
			{
				F.ArmorBefore = Health ? Health->GetArmor() : 0.f;
				SetPhase(F, 99);
			}
			else
			{
				EnterHazard(F, *VictimPawn);
			}
			return true;
		}
		case ECase::LavaQuad:
			if (InPhase >= LavaAfterHitDelaySeconds)
			{
				EnterHazard(F, *VictimPawn);
			}
			return true;
		default:
			F.Check(false, TEXT("phase"), FString::Printf(TEXT("unexpected phase %d"), F.Phase));
			SetPhase(F, 100);
			return true;
		}
	}

	void Command(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		AVeloxGameState* GameState = World ? World->GetGameState<AVeloxGameState>() : nullptr;
		const UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(World);
		if (!Bots || !GameState)
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("velox.combat.FtHazard: needs the authority world of L_HazardTest (-game)."));
			return;
		}
		TSharedRef<FHazardFt> Ft = MakeShared<FHazardFt>();
		Ft->World = World;
		Ft->bQuit = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		GHazardFt = Ft;
		const bool bHasBoth = Hazards && Hazards->HasHazards() && Hazards->IsInHazard(LavaSpot + FVector(0.0, 0.0, 10.0), EVeloxHazardType::Lava)
			&& Hazards->IsInHazard(OffEdgeSpot + FVector(0.0, 0.0, -800.0), EVeloxHazardType::Void);
		Ft->Check(bHasBoth, TEXT("map_hazards"), FString::Printf(TEXT("volumes=%d"), Hazards ? Hazards->GetVolumes().Num() : 0));
		if (!bHasBoth)
		{
			Ft->Finish();
			return;
		}
		if (IConsoleVariable* Peaceful = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.bots.Peaceful")))
		{
			Peaceful->Set(1, ECVF_SetByCode);
		}
		for (TActorIterator<AVeloxDummySpawner> It(World); It; ++It)
		{
			It->Destroy();
		}
		for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
		{
			// The -game local player's pawn stands on a PlayerStart and is no part of the test.
			if (Cast<AVeloxDummyController>(It->GetController()) || Cast<APlayerController>(It->GetController()))
			{
				It->Destroy();
			}
		}
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("velox.match.Phase Live"));
		}
		TWeakPtr<FHazardFt> Weak = Ft;
		Ft->KillHandle = GameState->OnKill.AddLambda([Weak](const FVeloxKillEvent& Event)
		{
			const TSharedPtr<FHazardFt> F = Weak.Pin();
			if (F && !F->bKilled && Event.Victim.IsValid() && Event.Victim.Get() == F->VictimPS.Get())
			{
				F->bKilled = true;
				F->Kill = Event;
			}
		});
		Bots->CallWhenNavReady([Weak]()
		{
			const TSharedPtr<FHazardFt> F = Weak.Pin();
			if (!F)
			{
				return;
			}
			if (!StartCase(F.ToSharedRef()))
			{
				F->Finish();
				return;
			}
			F->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				const TSharedPtr<FHazardFt> Inner = Weak.Pin();
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

	FAutoConsoleCommandWithWorldAndArgs Cmd(TEXT("velox.combat.FtHazard"),
		TEXT("velox.combat.FtHazard [quit]: FT_Hazard on L_HazardTest (-game -nullrhi -UseFixedTimeStep -FPS=60): lava ticks and death time, void credit by rocket, "
			 "walk-off and expired credit and own rocket are suicides, Quad does not scale lava. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));
}

#endif // !UE_BUILD_SHIPPING
