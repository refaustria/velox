// Velox - item test map checks (ADR 0004 sections 8 and 10, task M3-04).
//
//   Velox.Items.ItemTestMap     automation: loads the L_ItemTest package (no world init) and asserts the layout the map
//                               script promises: 23 distinct placed items (4 health, 3 armor, 6 ammo, 6 weapons, 4
//                               powerups), 2 jump pads with solvable launches, 8 PlayerStarts, one CameraActor tagged
//                               Intermission, WorldSettings.DefaultGameMode a Velox game mode.
//   velox.items.FtItemPickup    FT_ItemPickup (console command, `-game -nullrhi L_ItemTest`, standalone): the ADR 0004
//                               section 10 rows against the placed pickups of the real map - Health.Medium refused at 100
//                               and visible; at 50 taken (75), hidden, RespawnServerTime set; +35 s respawn event and the
//                               standing pawn retakes it at once; Weapon.Railgun grants the weapon and its fire spec;
//                               ResetLevel restores every item (powerups back in their initial delay). Quad (x3 damage on a
//                               dummy, task 02) and Haste (GetMaxSpeed x 1.3, task 03) add their step to FtSteps() when
//                               they merge. Logs `VeloxItemFt check=... ok=` lines and `VeloxItemFt result=PASS|FAIL`;
//                               `quit` exits with 0/1.
//   velox.items.Watch           client-side observer for the ItemRun soak (infra/ci/nettest.sh): once per frame logs
//                               `VeloxItemWatch placeholder=hidden|shown item= predicted= t= f=` when a pickup's placeholder
//                               changes and `VeloxItemWatch attr=Health|Armor|Bullets value= t= f=` when the local player's
//                               attribute changes (t = world time, f = frame counter).

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/VeloxGameMode.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Camera/CameraActor.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemLog.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "Movement/VeloxJumpPad.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

namespace VeloxItemFt
{
	/** The map script's promise (Tools/editor/create_item_test_map.py). */
	constexpr int32 ExpectedItems = 23;
	constexpr int32 ExpectedJumpPads = 2;
	constexpr int32 ExpectedPlayerStarts = 8;
	constexpr int32 ExpectedHealth = 4;
	constexpr int32 ExpectedArmor = 3;
	constexpr int32 ExpectedAmmo = 6;
	constexpr int32 ExpectedWeapons = 6;
	constexpr int32 ExpectedPowerups = 4;
	const TCHAR* const MapPath = TEXT("/Game/Maps/L_ItemTest");
	const FName IntermissionTag(TEXT("Intermission"));
}

// -------------------------------------------------------------------------------------------------------- automation

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxItemsItemTestMapTest, "Velox.Items.ItemTestMap", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxItemsItemTestMapTest::RunTest(const FString& Parameters)
{
	using namespace VeloxItemFt;

	UPackage* Package = LoadPackage(nullptr, MapPath, LOAD_None);
	UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
	if (!TestNotNull(TEXT("L_ItemTest loads (run Tools/editor/create_item_test_map.py)"), World) || !TestNotNull(TEXT("persistent level"), World->PersistentLevel.Get()))
	{
		return false;
	}

	TMap<FString, int32> DefinitionNames;
	int32 Health = 0, Armor = 0, Ammo = 0, Weapons = 0, Powerups = 0, Pickups = 0, Pads = 0, Starts = 0, Intermission = 0, BadPads = 0, NoDefinition = 0;
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		if (const AVeloxPickup* Pickup = Cast<AVeloxPickup>(Actor))
		{
			++Pickups;
			const UVeloxItemDefinition* Definition = Pickup->GetDefinition();
			if (!Definition)
			{
				++NoDefinition;
				continue;
			}
			++DefinitionNames.FindOrAdd(Definition->GetName());
			switch (Definition->Type)
			{
			case EVeloxItemType::Health: ++Health; break;
			case EVeloxItemType::Armor: ++Armor; break;
			case EVeloxItemType::Ammo: ++Ammo; break;
			case EVeloxItemType::Weapon: ++Weapons; break;
			case EVeloxItemType::Powerup: ++Powerups; break;
			}
		}
		else if (const AVeloxJumpPad* Pad = Cast<AVeloxJumpPad>(Actor))
		{
			++Pads;
			// Default movement gravity; the map script validates the same arcs.
			const FVector Launch = Pad->ComputeLaunchVelocity(-2000.f);
			if (AVeloxJumpPad::CheckLaunchVelocity(Launch, 4000.f) != EVeloxJumpPadLaunchCheck::Ok || !Pad->GetTarget())
			{
				++BadPads;
			}
		}
		else if (Cast<APlayerStart>(Actor))
		{
			++Starts;
		}
		else if (Cast<ACameraActor>(Actor) && Actor->ActorHasTag(IntermissionTag))
		{
			++Intermission;
		}
	}
	TestEqual(TEXT("placed pickups"), Pickups, ExpectedItems);
	TestEqual(TEXT("every pickup has a definition"), NoDefinition, 0);
	TestEqual(TEXT("distinct definitions (every item once)"), DefinitionNames.Num(), ExpectedItems);
	TestEqual(TEXT("health items"), Health, ExpectedHealth);
	TestEqual(TEXT("armor items"), Armor, ExpectedArmor);
	TestEqual(TEXT("ammo items"), Ammo, ExpectedAmmo);
	TestEqual(TEXT("weapon items"), Weapons, ExpectedWeapons);
	TestEqual(TEXT("powerup items"), Powerups, ExpectedPowerups);
	TestEqual(TEXT("jump pads"), Pads, ExpectedJumpPads);
	TestEqual(TEXT("jump pads with a solvable launch and a target"), BadPads, 0);
	TestEqual(TEXT("PlayerStarts"), Starts, ExpectedPlayerStarts);
	TestEqual(TEXT("Intermission camera"), Intermission, 1);
	const AWorldSettings* Settings = World->GetWorldSettings();
	TestTrue(TEXT("DefaultGameMode is a Velox game mode"), Settings && Settings->DefaultGameMode && Settings->DefaultGameMode->IsChildOf(AVeloxGameMode::StaticClass()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// ------------------------------------------------------------------------------------------------------ console tools

#if !UE_BUILD_SHIPPING
namespace VeloxItemFt
{
	constexpr int32 MaxRetries = 200;
	constexpr float RetryIntervalSeconds = 0.1f;
	/** Settle time between a state change and its check (several server frames). */
	constexpr float SettleSeconds = 0.5f;
	/** Health.Medium: the player is hurt by this much below the item's refusal point. */
	constexpr float HurtHealth = 50.f;
	/** After a take the respawn must be this close to RespawnSeconds (settle time and frame slack). */
	constexpr double RespawnSlackSeconds = 0.75;

	UWorld* FindGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && World && World->GetFirstPlayerController())
			{
				return World;
			}
		}
		return nullptr;
	}

	AVeloxCharacter* LocalPawnOf(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC && PC->IsLocalController() ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
	}

	const UVeloxHealthSet* HealthSetOf(const AVeloxCharacter* Pawn)
	{
		const AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
		return PS ? PS->GetHealthSet() : nullptr;
	}

	float HealthOfPawn(const AVeloxCharacter* Pawn)
	{
		const UVeloxHealthSet* Set = HealthSetOf(Pawn);
		return Set ? Set->GetHealth() : -1.f;
	}

	/** Server-side: health to exactly Target (environment damage down, GE_Heal up). */
	void SetPawnHealth(AVeloxCharacter& Pawn, float Target)
	{
		UVeloxAbilitySystemComponent* ASC = Pawn.GetVeloxAbilitySystemComponent();
		const float Health = HealthOfPawn(&Pawn);
		if (!ASC || Health < 0.f)
		{
			return;
		}
		const int32 Delta = FMath::RoundToInt32(Target - Health);
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
			Params.HitLocation = Pawn.GetActorLocation();
			Params.bNoKnockback = true;
			Params.bNoArmor = true;
			Spec = VeloxEffects::MakeDamageSpec(ASC, Params);
		}
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}

	/** Teleports Pawn to stand on the floor point FloorPoint; overlaps update at once. */
	void StandOn(AVeloxCharacter& Pawn, const FVector& FloorPoint)
	{
		Pawn.SetActorLocation(FloorPoint + FVector(0.0, 0.0, Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0), false, nullptr, ETeleportType::TeleportPhysics);
	}

	// ------------------------------------------------------------------------------------------------ FT_ItemPickup

	struct FFtState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AVeloxCharacter> Pawn;
		TWeakObjectPtr<AVeloxPickup> Medium;
		TWeakObjectPtr<AVeloxPickup> Railgun;
		bool bQuit = false;
		bool bPassed = true;
		int32 RespawnEvents = 0;
		float HealthAfterTake = 0.f;
		FDelegateHandle RespawnHandle;
	};

	using FFtStep = void (*)(const TSharedRef<FFtState>&);

	void FtCheck(FFtState& State, bool bOk, const TCHAR* What, const FString& Detail)
	{
		State.bPassed &= bOk;
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemFt check=%s ok=%d %s"), What, bOk ? 1 : 0, *Detail);
	}

	void FtFinish(const TSharedRef<FFtState>& State)
	{
		if (UWorld* World = State->World.Get())
		{
			if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World))
			{
				Registry->OnPickupRespawned.Remove(State->RespawnHandle);
			}
		}
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemFt result=%s"), State->bPassed ? TEXT("PASS") : TEXT("FAIL"));
		if (State->bQuit)
		{
			FPlatformMisc::RequestExitWithStatus(false, State->bPassed ? 0 : 1);
		}
	}

	void FtAfter(const TSharedRef<FFtState>& State, float Seconds, FFtStep Step)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return;
		}
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([State, Step]() { Step(State); }), FMath::Max(Seconds, 0.01f), false);
	}

	AVeloxPickup* FindPlaced(UWorld* World, const TCHAR* AssetName)
	{
		const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		if (Registry)
		{
			for (AVeloxPickup* Pickup : Registry->GetPickups())
			{
				if (Pickup->GetDefinition() && Pickup->GetDefinition()->GetName() == AssetName)
				{
					return Pickup;
				}
			}
		}
		return nullptr;
	}

	void StepReset(const TSharedRef<FFtState>& State)
	{
		UWorld* World = State->World.Get();
		AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
		const UVeloxItemRegistrySubsystem* Registry = World ? UVeloxItemRegistrySubsystem::Get(World) : nullptr;
		if (!GameMode || !Registry)
		{
			FtCheck(*State, false, TEXT("reset_setup"), TEXT("no game mode or registry"));
			FtFinish(State);
			return;
		}
		// Medium was retaken and the railgun taken: both unavailable now. ResetLevel puts every item back.
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemFt ResetLevel medium_available=%d railgun_available=%d"), State->Medium.IsValid() && State->Medium->IsAvailable() ? 1 : 0,
			State->Railgun.IsValid() && State->Railgun->IsAvailable() ? 1 : 0);
		GameMode->ResetLevel();
		int32 Restored = 0, Bad = 0, Powerups = 0;
		FString BadNames;
		for (const AVeloxPickup* Pickup : Registry->GetPickups())
		{
			const UVeloxItemDefinition* Definition = Pickup->GetDefinition();
			if (!Definition)
			{
				++Bad;
				continue;
			}
			bool bOk;
			if (Definition->InitialSpawnDelay.Max > 0.f)
			{
				// Powerups: back in a fresh initial delay (Reset reschedules it).
				++Powerups;
				const double Left = Pickup->GetTimeUntilRespawn();
				bOk = !Pickup->IsAvailable() && Left > Definition->InitialSpawnDelay.Min - 0.5 && Left <= Definition->InitialSpawnDelay.Max + 0.01;
			}
			else
			{
				bOk = Pickup->IsAvailable() && Pickup->IsPlaceholderVisible() && Pickup->GetLastTakenBy() == nullptr;
			}
			Restored += bOk ? 1 : 0;
			if (!bOk)
			{
				++Bad;
				BadNames += Definition->GetName() + TEXT(" ");
			}
		}
		FtCheck(*State, Bad == 0 && Restored == Registry->GetPickups().Num(), TEXT("reset_level_restores_every_item"),
			FString::Printf(TEXT("restored=%d of %d powerups_in_delay=%d bad=%s"), Restored, Registry->GetPickups().Num(), Powerups, *BadNames));
		FtCheck(*State, State->Medium.IsValid() && State->Medium->IsAvailable() && State->Railgun.IsValid() && State->Railgun->IsAvailable(), TEXT("reset_medium_and_railgun_back"),
			TEXT("the two taken items are available again"));
		FtFinish(State);
	}

	// Task 02 (Quad: tag and x3 damage on a dummy) and task 03 (Haste: GetMaxSpeed x 1.3) insert their steps between
	// StepWeapon and StepReset: each ends with FtAfter(State, <settle>, <next step>).

	void StepWeapon(const TSharedRef<FFtState>& State)
	{
		UWorld* World = State->World.Get();
		AVeloxCharacter* Pawn = State->Pawn.Get();
		AVeloxPickup* Railgun = World ? FindPlaced(World, TEXT("DA_Item_Weapon_Railgun")) : nullptr;
		AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
		UVeloxWeaponInventoryComponent* Inventory = PS ? PS->GetWeaponInventory() : nullptr;
		const UVeloxItemDefinition* Definition = Railgun ? Railgun->GetDefinition() : nullptr;
		if (!Railgun || !Inventory || !Definition || !Definition->Weapon)
		{
			FtCheck(*State, false, TEXT("weapon_setup"), TEXT("no railgun pickup, inventory or weapon definition"));
			FtFinish(State);
			return;
		}
		State->Railgun = Railgun;
		const int32 RG = Inventory->IndexOf(Definition->Weapon);
		FtCheck(*State, RG != INDEX_NONE && !Inventory->OwnsIndex(RG), TEXT("weapon_not_owned_before"), FString::Printf(TEXT("index=%d"), RG));
		StandOn(*Pawn, Railgun->GetActorLocation());
		FtAfter(State, SettleSeconds, [](const TSharedRef<FFtState>& S)
		{
			AVeloxCharacter* P = S->Pawn.Get();
			AVeloxPlayerState* Ps = P ? P->GetPlayerState<AVeloxPlayerState>() : nullptr;
			UVeloxWeaponInventoryComponent* Inv = Ps ? Ps->GetWeaponInventory() : nullptr;
			const AVeloxPickup* Rail = S->Railgun.Get();
			const int32 Index = Inv && Rail ? Inv->IndexOf(Rail->GetDefinition()->Weapon) : INDEX_NONE;
			FtCheck(*S, Rail && Rail->GetTakeCount() == 1 && !Rail->IsAvailable(), TEXT("weapon_taken"), FString::Printf(TEXT("take=%u"), Rail ? Rail->GetTakeCount() : 0));
			FtCheck(*S, Index != INDEX_NONE && Inv->OwnsIndex(Index) && (Inv->GetOwnedMask() & (1u << Index)) != 0, TEXT("weapon_owned"), FString::Printf(TEXT("mask=0x%x"), Inv ? Inv->GetOwnedMask() : 0));
			FtCheck(*S, Inv && Inv->FindFireSpec(Index) != nullptr, TEXT("weapon_fire_spec_granted"), TEXT("fire ability spec on the ASC"));
			StepReset(S);
		});
	}

	void StepRetake(const TSharedRef<FFtState>& State)
	{
		const AVeloxPickup* Medium = State->Medium.Get();
		AVeloxCharacter* Pawn = State->Pawn.Get();
		if (!Medium || !Pawn)
		{
			FtCheck(*State, false, TEXT("alive"), TEXT("pickup or pawn gone"));
			FtFinish(State);
			return;
		}
		const float Expected = FMath::Min(State->HealthAfterTake + Medium->GetDefinition()->Quantity, HealthSetOf(Pawn)->GetMaxHealth());
		FtCheck(*State, State->RespawnEvents >= 1, TEXT("respawn_event_after_35s"), FString::Printf(TEXT("events=%d"), State->RespawnEvents));
		FtCheck(*State, Medium->GetTakeCount() == 2 && !Medium->IsAvailable(), TEXT("standing_pawn_retakes_at_once"), FString::Printf(TEXT("take=%u available=%d"), Medium->GetTakeCount(), Medium->IsAvailable() ? 1 : 0));
		FtCheck(*State, FMath::IsNearlyEqual(HealthOfPawn(Pawn), Expected), TEXT("retake_health"), FString::Printf(TEXT("health=%.0f expected=%.0f"), HealthOfPawn(Pawn), Expected));
		StepWeapon(State);
	}

	void StepTaken(const TSharedRef<FFtState>& State)
	{
		const AVeloxPickup* Medium = State->Medium.Get();
		AVeloxCharacter* Pawn = State->Pawn.Get();
		if (!Medium || !Pawn)
		{
			FtCheck(*State, false, TEXT("alive"), TEXT("pickup or pawn gone"));
			FtFinish(State);
			return;
		}
		const float MaxHealth = HealthSetOf(Pawn)->GetMaxHealth();
		const float Expected = FMath::Min(HurtHealth + Medium->GetDefinition()->Quantity, MaxHealth);
		State->HealthAfterTake = HealthOfPawn(Pawn);
		const double Remaining = Medium->GetTimeUntilRespawn();
		const double Respawn = Medium->GetDefinition()->RespawnSeconds;
		FtCheck(*State, Medium->GetTakeCount() == 1 && !Medium->IsAvailable() && !Medium->IsPlaceholderVisible(), TEXT("taken_at_50_hidden"),
			FString::Printf(TEXT("take=%u available=%d visible=%d"), Medium->GetTakeCount(), Medium->IsAvailable() ? 1 : 0, Medium->IsPlaceholderVisible() ? 1 : 0));
		FtCheck(*State, FMath::IsNearlyEqual(State->HealthAfterTake, Expected), TEXT("health_50_to_75"), FString::Printf(TEXT("health=%.0f expected=%.0f"), State->HealthAfterTake, Expected));
		FtCheck(*State, Medium->GetRespawnServerTime() > 0.f && Remaining > Respawn - RespawnSlackSeconds && Remaining <= Respawn, TEXT("respawn_server_time_set"),
			FString::Printf(TEXT("respawn_server_time=%.3f remaining=%.3f respawn_s=%.1f"), Medium->GetRespawnServerTime(), Remaining, Respawn));
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemFt waiting %.2f s for the respawn"), Remaining + SettleSeconds);
		FtAfter(State, static_cast<float>(Remaining) + SettleSeconds, &StepRetake);
	}

	void StepRefused(const TSharedRef<FFtState>& State)
	{
		const AVeloxPickup* Medium = State->Medium.Get();
		AVeloxCharacter* Pawn = State->Pawn.Get();
		if (!Medium || !Pawn)
		{
			FtCheck(*State, false, TEXT("alive"), TEXT("pickup or pawn gone"));
			FtFinish(State);
			return;
		}
		FtCheck(*State, Medium->GetTakeCount() == 0 && Medium->IsAvailable() && Medium->IsPlaceholderVisible() && FMath::IsNearlyEqual(HealthOfPawn(Pawn), HealthSetOf(Pawn)->GetMaxHealth()),
			TEXT("refused_at_100_visible"), FString::Printf(TEXT("take=%u available=%d visible=%d health=%.0f"), Medium->GetTakeCount(), Medium->IsAvailable() ? 1 : 0,
				Medium->IsPlaceholderVisible() ? 1 : 0, HealthOfPawn(Pawn)));
		// Touches are evaluated every frame: hurt while standing in it and the player takes it without leaving the trigger.
		SetPawnHealth(*Pawn, HurtHealth);
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemFt hurt to %.0f while standing in the item"), HealthOfPawn(Pawn));
		FtAfter(State, SettleSeconds, &StepTaken);
	}

	void StepStart(const TSharedRef<FFtState>& State, int32 RetriesLeft)
	{
		UWorld* World = State->World.Get();
		AVeloxCharacter* Pawn = LocalPawnOf(World);
		const UVeloxAbilitySystemComponent* ASC = Pawn ? Pawn->GetVeloxAbilitySystemComponent() : nullptr;
		const bool bReady = ASC && HealthOfPawn(Pawn) > 0.f && !ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected) && Pawn->GetVelocity().IsNearlyZero(1.0);
		if (!bReady)
		{
			if (RetriesLeft > 0 && World)
			{
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([State, RetriesLeft]() { StepStart(State, RetriesLeft - 1); }), RetryIntervalSeconds, false);
				return;
			}
			FtCheck(*State, false, TEXT("setup"), TEXT("no settled, unprotected local pawn"));
			FtFinish(State);
			return;
		}
		State->Pawn = Pawn;
		UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		AVeloxPickup* Medium = FindPlaced(World, TEXT("DA_Item_Health_Medium"));
		State->Medium = Medium;
		const int32 Items = Registry ? Registry->GetPickups().Num() : 0;
		FtCheck(*State, World->GetMapName().Contains(TEXT("L_ItemTest")) && Items == ExpectedItems && Medium, TEXT("map_items"),
			FString::Printf(TEXT("map=%s items=%d medium=%s"), *World->GetMapName(), Items, *GetNameSafe(Medium)));
		if (!Medium)
		{
			FtFinish(State);
			return;
		}
		State->RespawnHandle = Registry->OnPickupRespawned.AddLambda([State](AVeloxPickup*) { ++State->RespawnEvents; });
		SetPawnHealth(*Pawn, HealthSetOf(Pawn)->GetMaxHealth());
		StandOn(*Pawn, Medium->GetActorLocation());
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemFt start health=%.0f pawn=%s medium=%s"), HealthOfPawn(Pawn), *Pawn->GetActorLocation().ToCompactString(), *Medium->GetActorLocation().ToCompactString());
		FtAfter(State, SettleSeconds, &StepRefused);
	}

	void FtItemPickupCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		TSharedRef<FFtState> State = MakeShared<FFtState>();
		State->World = InWorld && InWorld->GetNetMode() != NM_Client ? InWorld : nullptr;
		for (const FString& Arg : Args)
		{
			State->bQuit |= Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase);
		}
		if (!State->World.IsValid())
		{
			UE_LOG(LogVeloxItems, Warning, TEXT("velox.items.FtItemPickup: needs the authority world (standalone or listen host) of L_ItemTest."));
			State->bPassed = false;
			FtFinish(State);
			return;
		}
		StepStart(State, MaxRetries);
	}

	FAutoConsoleCommandWithWorldAndArgs FtItemPickupCmd(
		TEXT("velox.items.FtItemPickup"),
		TEXT("velox.items.FtItemPickup [quit]: FT_ItemPickup on the placed items of L_ItemTest (run `UnrealEditor-Cmd Velox.uproject L_ItemTest -game -nullrhi`); logs VeloxItemFt result=. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FtItemPickupCommand));

	// ------------------------------------------------------------------------------------------------------ Watch

	struct FWatchState
	{
		TWeakObjectPtr<UWorld> World;
		TMap<TWeakObjectPtr<const AVeloxPickup>, bool> Visible;
		float Health = -1.f;
		float Armor = -1.f;
		float Bullets = -1.f;
		FTSTicker::FDelegateHandle Handle;
	};

	TSharedPtr<FWatchState> WatchState;

	void WatchAttribute(const TCHAR* Name, float& Last, float Value, double Now)
	{
		if (!FMath::IsNearlyEqual(Last, Value))
		{
			UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemWatch attr=%s value=%.0f t=%.3f f=%llu"), Name, Value, Now, static_cast<unsigned long long>(GFrameCounter));
			Last = Value;
		}
	}

	bool WatchTick(float DeltaSeconds)
	{
		UWorld* World = FindGameWorld();
		if (!World || !WatchState.IsValid())
		{
			return true;
		}
		if (WatchState->World.Get() != World)
		{
			WatchState->World = World;
			WatchState->Visible.Reset();
			WatchState->Health = WatchState->Armor = WatchState->Bullets = -1.f;
		}
		const double Now = World->GetTimeSeconds();
		if (const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World))
		{
			for (const AVeloxPickup* Pickup : Registry->GetPickups())
			{
				const bool bVisible = Pickup->IsPlaceholderVisible();
				bool* Last = WatchState->Visible.Find(Pickup);
				if (!Last)
				{
					WatchState->Visible.Add(Pickup, bVisible);
				}
				else if (*Last != bVisible)
				{
					*Last = bVisible;
					UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemWatch placeholder=%s item=%s predicted=%d t=%.3f f=%llu"), bVisible ? TEXT("shown") : TEXT("hidden"),
						*GetNameSafe(Pickup->GetDefinition()), Pickup->IsPredictedTaken() ? 1 : 0, Now, static_cast<unsigned long long>(GFrameCounter));
				}
			}
		}
		const AVeloxCharacter* Pawn = LocalPawnOf(World);
		const AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
		if (PS && PS->GetHealthSet() && PS->GetAmmoSet())
		{
			WatchAttribute(TEXT("Health"), WatchState->Health, PS->GetHealthSet()->GetHealth(), Now);
			WatchAttribute(TEXT("Armor"), WatchState->Armor, PS->GetHealthSet()->GetArmor(), Now);
			WatchAttribute(TEXT("Bullets"), WatchState->Bullets, PS->GetAmmoSet()->GetBullets(), Now);
		}
		return true;
	}

	void WatchCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (WatchState.IsValid())
		{
			return;
		}
		WatchState = MakeShared<FWatchState>();
		WatchState->Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&WatchTick), 0.f);
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemWatch started"));
	}

	FAutoConsoleCommandWithWorldAndArgs WatchCmd(
		TEXT("velox.items.Watch"),
		TEXT("velox.items.Watch: per-frame observer (any net mode): logs VeloxItemWatch lines for placeholder changes and the local player's Health / Armor / Bullets. Used by the ItemRun soak. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WatchCommand));
}
#endif // !UE_BUILD_SHIPPING
