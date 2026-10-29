// Velox - non-shipping item console commands (ADR 0004 task 01 acceptance). Authority world (standalone / listen host).
//   velox.items.Spawn <ItemName> [DistanceCm]   spawns DA_Item_<ItemName> ("Health.Medium", "Armor_Red", ...) on the floor
//                                               DistanceCm in front of the local player (default DefaultSpawnDistanceCm;
//                                               0 = under the player, who then stands in it).
//   velox.items.SpawnAt <ItemName> <x> <y> <z>  spawns it with its origin on the floor below x y z (also on a dedicated
//                                               server: soaks place items in L_CombatTest from the server's ExecCmds).
//   velox.items.List                           logs every registered pickup with its state.
//   velox.items.PickupTest [ItemName] [quit]    headless acceptance (default Health.Medium): health set to 100, the item
//                                               spawned under the player is refused; 50 damage -> taken while standing
//                                               (50 -> 75), respawn after RespawnSeconds, the standing player retakes it
//                                               (75 -> 100). Logs `VeloxPickupTest result=PASS|FAIL`; quit exits with 0/1.

#include "AbilitySystemComponent.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Components/CapsuleComponent.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemLog.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
namespace VeloxItemCheats
{
	constexpr double DefaultSpawnDistanceCm = 200.0;
	constexpr double FloorTraceUpCm = 50.0;
	constexpr double FloorTraceDownCm = 1000.0;
	constexpr int32 MaxRetries = 200;
	constexpr float RetryIntervalSeconds = 0.1f;
	/** PickupTest: settle time between a state change and its check (several server frames). */
	constexpr float SettleSeconds = 0.5f;
	/** PickupTest: health drops from MaxHealth (refused) to MaxHealth - TestDamage (taken). */
	constexpr int32 TestDamage = 50;

	UWorld* AuthorityWorld(UWorld* World)
	{
		return World && World->GetNetMode() != NM_Client ? World : nullptr;
	}

	AVeloxCharacter* LocalPawn(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC && PC->IsLocalController() ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
	}

	/** The floor under Point (Visibility trace, pawns ignored); Fallback when nothing is hit. */
	FVector FloorBelow(UWorld& World, const FVector& Point, const FVector& Fallback, const AActor* Ignore)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(VeloxItemFloor), false, Ignore);
		FHitResult Hit;
		if (World.LineTraceSingleByChannel(Hit, Point + FVector(0.0, 0.0, FloorTraceUpCm), Point - FVector(0.0, 0.0, FloorTraceDownCm), ECC_Visibility, Query))
		{
			return Hit.ImpactPoint;
		}
		return Fallback;
	}

	FVector FeetOf(const AVeloxCharacter& Pawn)
	{
		return Pawn.GetActorLocation() - FVector(0.0, 0.0, Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	void SpawnCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = AuthorityWorld(InWorld);
		AVeloxCharacter* Pawn = LocalPawn(World);
		if (!World || !Pawn || Args.Num() < 1)
		{
			UE_LOG(LogVeloxItems, Warning, TEXT("velox.items.Spawn <ItemName> [DistanceCm]: needs a name and a local player pawn on the authority world."));
			return;
		}
		const UVeloxItemDefinition* Definition = AVeloxPickup::LoadDefinitionByName(Args[0]);
		if (!Definition)
		{
			UE_LOG(LogVeloxItems, Warning, TEXT("velox.items.Spawn: no item definition for '%s' under /Game/Items."), *Args[0]);
			return;
		}
		const double Distance = Args.Num() > 1 ? FCString::Atod(*Args[1]) : DefaultSpawnDistanceCm;
		const FVector Forward = FRotator(0.0, Pawn->GetControlRotation().Yaw, 0.0).Vector();
		const FVector Feet = FeetOf(*Pawn);
		const FVector Floor = FloorBelow(*World, Feet + Forward * Distance, Feet + Forward * Distance, Pawn);
		const AVeloxPickup* Pickup = AVeloxPickup::SpawnPickup(World, Definition, Floor);
		UE_LOG(LogVeloxItems, Log, TEXT("velox.items.Spawn %s at %s -> %s"), *GetNameSafe(Definition), *Floor.ToCompactString(), *GetNameSafe(Pickup));
	}

	void SpawnAtCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = AuthorityWorld(InWorld);
		if (!World || Args.Num() < 4)
		{
			UE_LOG(LogVeloxItems, Warning, TEXT("velox.items.SpawnAt <ItemName> <x> <y> <z>: needs a name and a location on the authority world."));
			return;
		}
		const UVeloxItemDefinition* Definition = AVeloxPickup::LoadDefinitionByName(Args[0]);
		if (!Definition)
		{
			UE_LOG(LogVeloxItems, Warning, TEXT("velox.items.SpawnAt: no item definition for '%s' under /Game/Items."), *Args[0]);
			return;
		}
		const FVector Point(FCString::Atod(*Args[1]), FCString::Atod(*Args[2]), FCString::Atod(*Args[3]));
		const FVector Floor = FloorBelow(*World, Point, Point, nullptr);
		const AVeloxPickup* Pickup = AVeloxPickup::SpawnPickup(World, Definition, Floor);
		UE_LOG(LogVeloxItems, Log, TEXT("velox.items.SpawnAt %s at %s -> %s"), *GetNameSafe(Definition), *Floor.ToCompactString(), *GetNameSafe(Pickup));
	}

	void ListCommand(const TArray<FString>& Args, UWorld* World)
	{
		const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		if (!Registry)
		{
			return;
		}
		for (const AVeloxPickup* Pickup : Registry->GetPickups())
		{
			UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupState item=%s at=%s available=%d respawn_in=%.2f take=%u last=%s visible=%d"), *GetNameSafe(Pickup->GetDefinition()),
				*Pickup->GetActorLocation().ToCompactString(), Pickup->IsAvailable() ? 1 : 0, Pickup->GetTimeUntilRespawn(), Pickup->GetTakeCount(),
				*AVeloxPlayerState::VeloxPlayerLabel(Pickup->GetLastTakenBy()), Pickup->IsPlaceholderVisible() ? 1 : 0);
		}
	}

	// ---------------------------------------------------------------------------------------- PickupTest

	struct FPickupTestState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AVeloxCharacter> Pawn;
		TWeakObjectPtr<AVeloxPickup> Pickup;
		const UVeloxItemDefinition* Definition = nullptr;
		bool bQuit = false;
		bool bPassed = true;
		double TakeTime = 0.0;
		float HealthAfterTake = 0.f;
	};

	void Check(FPickupTestState& State, bool bOk, const TCHAR* What, const FString& Detail)
	{
		State.bPassed &= bOk;
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupTest check=%s ok=%d %s"), What, bOk ? 1 : 0, *Detail);
	}

	float HealthOf(const AVeloxCharacter* Pawn)
	{
		const AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
		return PS && PS->GetHealthSet() ? PS->GetHealthSet()->GetHealth() : -1.f;
	}

	float MaxHealthOf(const AVeloxCharacter* Pawn)
	{
		const AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
		return PS && PS->GetHealthSet() ? PS->GetHealthSet()->GetMaxHealth() : -1.f;
	}

	/** Server-side cheat: health to exactly Target (environment damage down, GE_Heal up). */
	void SetHealth(AVeloxCharacter& Pawn, float Target)
	{
		UVeloxAbilitySystemComponent* ASC = Pawn.GetVeloxAbilitySystemComponent();
		const float Health = HealthOf(&Pawn);
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

	void Finish(const TSharedRef<FPickupTestState>& State)
	{
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupTest result=%s item=%s"), State->bPassed ? TEXT("PASS") : TEXT("FAIL"), *GetNameSafe(State->Definition));
		if (State->bQuit)
		{
			FPlatformMisc::RequestExitWithStatus(false, State->bPassed ? 0 : 1);
		}
	}

	void After(const TSharedRef<FPickupTestState>& State, float Seconds, TFunction<void(const TSharedRef<FPickupTestState>&)> Step)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return;
		}
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([State, Step = MoveTemp(Step)]() { Step(State); }), FMath::Max(Seconds, 0.01f), false);
	}

	void StepRetake(const TSharedRef<FPickupTestState>& State)
	{
		AVeloxPickup* Pickup = State->Pickup.Get();
		AVeloxCharacter* Pawn = State->Pawn.Get();
		if (!Pickup || !Pawn)
		{
			Check(*State, false, TEXT("alive"), TEXT("pickup or pawn gone"));
			Finish(State);
			return;
		}
		const float Expected = FMath::Min(State->HealthAfterTake + Pickup->GetDefinition()->Quantity, MaxHealthOf(Pawn));
		Check(*State, Pickup->GetTakeCount() == 2, TEXT("standing_retake"), FString::Printf(TEXT("take=%u health=%.0f expected=%.0f"), Pickup->GetTakeCount(), HealthOf(Pawn), Expected));
		Check(*State, FMath::IsNearlyEqual(HealthOf(Pawn), Expected), TEXT("retake_health"), FString::Printf(TEXT("health=%.0f"), HealthOf(Pawn)));
		Finish(State);
	}

	void StepTaken(const TSharedRef<FPickupTestState>& State)
	{
		AVeloxPickup* Pickup = State->Pickup.Get();
		AVeloxCharacter* Pawn = State->Pawn.Get();
		UWorld* World = State->World.Get();
		if (!Pickup || !Pawn || !World)
		{
			Check(*State, false, TEXT("alive"), TEXT("pickup or pawn gone"));
			Finish(State);
			return;
		}
		const float Expected = FMath::Min(MaxHealthOf(Pawn) - TestDamage + Pickup->GetDefinition()->Quantity, MaxHealthOf(Pawn));
		State->HealthAfterTake = HealthOf(Pawn);
		const double Remaining = Pickup->GetTimeUntilRespawn();
		const double Respawn = Pickup->GetDefinition()->RespawnSeconds;
		Check(*State, Pickup->GetTakeCount() == 1 && !Pickup->IsAvailable() && !Pickup->IsPlaceholderVisible(), TEXT("taken_after_damage"),
			FString::Printf(TEXT("take=%u available=%d visible=%d"), Pickup->GetTakeCount(), Pickup->IsAvailable() ? 1 : 0, Pickup->IsPlaceholderVisible() ? 1 : 0));
		Check(*State, FMath::IsNearlyEqual(State->HealthAfterTake, Expected), TEXT("health_after_take"), FString::Printf(TEXT("health=%.0f expected=%.0f"), State->HealthAfterTake, Expected));
		Check(*State, Remaining > Respawn - SettleSeconds - 0.1 && Remaining <= Respawn, TEXT("respawn_time"),
			FString::Printf(TEXT("respawn_server_time=%.3f remaining=%.3f respawn_s=%.1f"), Pickup->GetRespawnServerTime(), Remaining, Respawn));
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupTest waiting %.2f s for the respawn"), Remaining + SettleSeconds);
		After(State, static_cast<float>(Remaining) + SettleSeconds, &StepRetake);
	}

	void StepRefused(const TSharedRef<FPickupTestState>& State)
	{
		AVeloxPickup* Pickup = State->Pickup.Get();
		AVeloxCharacter* Pawn = State->Pawn.Get();
		if (!Pickup || !Pawn)
		{
			Check(*State, false, TEXT("alive"), TEXT("pickup or pawn gone"));
			Finish(State);
			return;
		}
		Check(*State, Pickup->GetTakeCount() == 0 && Pickup->IsAvailable() && Pickup->IsPlaceholderVisible() && FMath::IsNearlyEqual(HealthOf(Pawn), MaxHealthOf(Pawn)),
			TEXT("refused_at_max"), FString::Printf(TEXT("take=%u available=%d health=%.0f"), Pickup->GetTakeCount(), Pickup->IsAvailable() ? 1 : 0, HealthOf(Pawn)));
		// Q3 evaluates touches every frame: hurt while standing in it, the player takes it without leaving the trigger.
		SetHealth(*Pawn, MaxHealthOf(Pawn) - TestDamage);
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupTest damaged to %.0f while standing in the item"), HealthOf(Pawn));
		After(State, SettleSeconds, &StepTaken);
	}

	void StepStart(const TSharedRef<FPickupTestState>& State, int32 RetriesLeft)
	{
		UWorld* World = State->World.Get();
		AVeloxCharacter* Pawn = LocalPawn(World);
		const UVeloxAbilitySystemComponent* ASC = Pawn ? Pawn->GetVeloxAbilitySystemComponent() : nullptr;
		const bool bReady = ASC && HealthOf(Pawn) > 0.f && !ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected) && Pawn->GetVelocity().IsNearlyZero(1.0);
		if (!bReady)
		{
			if (RetriesLeft > 0 && World)
			{
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([State, RetriesLeft]() { StepStart(State, RetriesLeft - 1); }), RetryIntervalSeconds, false);
				return;
			}
			Check(*State, false, TEXT("setup"), TEXT("no settled, unprotected local pawn"));
			Finish(State);
			return;
		}
		State->Pawn = Pawn;
		SetHealth(*Pawn, MaxHealthOf(Pawn));
		const FVector Feet = FeetOf(*Pawn);
		State->Pickup = AVeloxPickup::SpawnPickup(World, State->Definition, FloorBelow(*World, Feet, Feet, Pawn));
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupTest start item=%s health=%.0f pawn=%s pickup=%s"), *GetNameSafe(State->Definition), HealthOf(Pawn), *Pawn->GetActorLocation().ToCompactString(),
			*GetNameSafe(State->Pickup.Get()));
		if (!State->Pickup.IsValid())
		{
			Check(*State, false, TEXT("setup"), TEXT("spawn failed"));
			Finish(State);
			return;
		}
		After(State, SettleSeconds, &StepRefused);
	}

	void PickupTestCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		TSharedRef<FPickupTestState> State = MakeShared<FPickupTestState>();
		State->World = AuthorityWorld(InWorld);
		FString ItemName = TEXT("Health.Medium");
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				State->bQuit = true;
			}
			else
			{
				ItemName = Arg;
			}
		}
		State->Definition = AVeloxPickup::LoadDefinitionByName(ItemName);
		if (!State->World.IsValid() || !State->Definition || State->Definition->Type != EVeloxItemType::Health)
		{
			UE_LOG(LogVeloxItems, Warning, TEXT("velox.items.PickupTest: needs the authority world and a health item ('%s')."), *ItemName);
			State->bPassed = false;
			Finish(State);
			return;
		}
		StepStart(State, MaxRetries);
	}

	FAutoConsoleCommandWithWorldAndArgs SpawnCmd(
		TEXT("velox.items.Spawn"),
		TEXT("velox.items.Spawn <ItemName> [DistanceCm]: spawns DA_Item_<ItemName> (e.g. Health.Medium) on the floor in front of the local player (0 = under the player). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnCommand));

	FAutoConsoleCommandWithWorldAndArgs SpawnAtCmd(
		TEXT("velox.items.SpawnAt"),
		TEXT("velox.items.SpawnAt <ItemName> <x> <y> <z>: spawns DA_Item_<ItemName> on the floor below x y z (works on a dedicated server). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnAtCommand));

	FAutoConsoleCommandWithWorldAndArgs ListCmd(
		TEXT("velox.items.List"),
		TEXT("velox.items.List: logs every registered pickup and its state. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ListCommand));

	FAutoConsoleCommandWithWorldAndArgs PickupTestCmd(
		TEXT("velox.items.PickupTest"),
		TEXT("velox.items.PickupTest [HealthItemName] [quit]: refused at 100, taken at 50 while standing, respawn, standing retake; logs VeloxPickupTest result=. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PickupTestCommand));
}
#endif // !UE_BUILD_SHIPPING
