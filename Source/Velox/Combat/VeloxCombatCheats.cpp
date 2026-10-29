// Velox - non-shipping combat console commands (ADR 0003 task 01 acceptance).
//   velox.combat.TestDamage <Damage> [Armor] [Self 0|1] [Pellets] [DirX DirY DirZ]
//   velox.combat.TestRespawn

#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Movement/VeloxMovementComponent.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
namespace VeloxCombatCheats
{
	/** The authority world for World: itself, or the in-process PIE server when run on a PIE client. */
	UWorld* AuthorityWorld(UWorld* World)
	{
		if (!World || World->GetNetMode() != NM_Client)
		{
			return World;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (Context.WorldType == EWorldType::PIE && Candidate && Candidate != World && Candidate->GetNetMode() != NM_Client && Candidate->GetNetMode() != NM_Standalone)
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	/** Server pawns the command targets: the local player's pawn in a listen/standalone world, else every Velox player pawn. */
	TArray<AVeloxCharacter*> TargetPawns(UWorld* World)
	{
		TArray<AVeloxCharacter*> Pawns;
		const APlayerController* LocalPC = World->GetFirstPlayerController();
		if (LocalPC && LocalPC->IsLocalController())
		{
			if (AVeloxCharacter* Pawn = Cast<AVeloxCharacter>(LocalPC->GetPawn()))
			{
				Pawns.Add(Pawn);
				return Pawns;
			}
		}
		for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
		{
			if (It->GetPlayerState<AVeloxPlayerState>())
			{
				Pawns.Add(*It);
			}
		}
		return Pawns;
	}

	struct FTestDamageArgs
	{
		int32 Damage = 0;
		int32 Armor = -1;
		bool bSelf = false;
		int32 Pellets = 1;
		TOptional<FVector> Direction;
	};

	void ApplyTestDamage(TWeakObjectPtr<UWorld> WeakWorld, FTestDamageArgs Args, int32 RetriesLeft)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return;
		}
		const TArray<AVeloxCharacter*> Pawns = TargetPawns(World);
		if (Pawns.IsEmpty())
		{
			if (RetriesLeft > 0)
			{
				// -ExecCmds runs before the first possession; wait for the pawn.
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateStatic(&ApplyTestDamage, WeakWorld, Args, RetriesLeft - 1), 0.1f, false);
				return;
			}
			UE_LOG(LogVeloxCombat, Warning, TEXT("TestDamage: no Velox player pawn in %s"), *World->GetName());
			return;
		}

		for (AVeloxCharacter* Pawn : Pawns)
		{
			UVeloxAbilitySystemComponent* ASC = Pawn->GetVeloxAbilitySystemComponent();
			const AVeloxPlayerState* PlayerState = Pawn->GetPlayerState<AVeloxPlayerState>();
			const UVeloxHealthSet* HealthSet = PlayerState ? PlayerState->GetHealthSet() : nullptr;
			if (!ASC || !HealthSet)
			{
				continue;
			}
			if (Args.Armor >= 0)
			{
				ASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), static_cast<float>(Args.Armor));
			}

			// Default direction: backwards and up, so the knockback is visible in the movement log.
			const FVector Direction = Args.Direction.IsSet() ? Args.Direction.GetValue() : (Pawn->GetActorUpVector() - Pawn->GetActorForwardVector());

			FVeloxDamageSpecParams Params;
			Params.Damage = Args.Damage;
			Params.PelletHits = Args.Pellets;
			Params.DamageTag = VeloxGameplayTags::Damage_Environment;
			Params.Direction = Direction;
			Params.HitLocation = Pawn->GetActorLocation();
			Params.bSelf = Args.bSelf;

			UE_LOG(LogVeloxCombat, Log, TEXT("TestDamage: %d x %d on %s (health %.0f armor %.0f self %d dir %s)"), Args.Damage, Args.Pellets, *Pawn->GetName(),
				HealthSet->GetHealth(), HealthSet->GetArmor(), Args.bSelf ? 1 : 0, *Direction.GetSafeNormal().ToCompactString());

			// World damage is built on the victim's own ASC; self damage is only what the flag says.
			const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(ASC, Params);
			if (Spec.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			}

			const UVeloxMovementComponent* Movement = Pawn->GetVeloxMovementComponent();
			UE_LOG(LogVeloxCombat, Log, TEXT("TestDamage: result health %.0f armor %.0f, pending knockback %s (%.0f cm/s)"), HealthSet->GetHealth(), HealthSet->GetArmor(),
				Movement ? *Movement->GetPendingKnockback().Velocity.ToCompactString() : TEXT("-"), Movement ? Movement->GetPendingKnockback().Velocity.Size() : 0.0);

			// Show that the next moves consumed it (velocity, grace).
			TWeakObjectPtr<AVeloxCharacter> WeakPawn = Pawn;
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakPawn]()
			{
				if (const UVeloxMovementComponent* Moved = WeakPawn.IsValid() ? WeakPawn->GetVeloxMovementComponent() : nullptr)
				{
					UE_LOG(LogVeloxCombat, Log, TEXT("TestDamage: 0.05 s later velocity %s grace %.3f s pending %d"), *Moved->Velocity.ToCompactString(), Moved->GetKnockbackGrace(), Moved->HasPendingKnockback() ? 1 : 0);
				}
			}), 0.05f, false);
		}
	}

	void TestDamage(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("Usage: velox.combat.TestDamage <Damage> [Armor (-1 keeps)] [Self 0|1] [Pellets] [DirX DirY DirZ]"));
			return;
		}
		FTestDamageArgs Parsed;
		Parsed.Damage = FCString::Atoi(*Args[0]);
		Parsed.Armor = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : -1;
		Parsed.bSelf = Args.IsValidIndex(2) && FCString::Atoi(*Args[2]) != 0;
		Parsed.Pellets = Args.IsValidIndex(3) ? FMath::Max(FCString::Atoi(*Args[3]), 1) : 1;
		if (Args.IsValidIndex(6))
		{
			Parsed.Direction = FVector(FCString::Atod(*Args[4]), FCString::Atod(*Args[5]), FCString::Atod(*Args[6]));
		}

		UWorld* Authority = AuthorityWorld(World);
		if (!Authority)
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("TestDamage: no authority world (run it on the server)"));
			return;
		}
		ApplyTestDamage(Authority, Parsed, 50);
	}

	void RespawnPawns(TWeakObjectPtr<UWorld> WeakWorld, int32 RetriesLeft)
	{
		UWorld* World = WeakWorld.Get();
		AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
		if (!GameMode)
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("TestRespawn: no authority game mode"));
			return;
		}
		const TArray<AVeloxCharacter*> Pawns = TargetPawns(World);
		if (Pawns.IsEmpty() && RetriesLeft > 0)
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateStatic(&RespawnPawns, WeakWorld, RetriesLeft - 1), 0.1f, false);
			return;
		}
		for (AVeloxCharacter* Pawn : Pawns)
		{
			AController* Controller = Pawn->GetController();
			UE_LOG(LogVeloxCombat, Log, TEXT("TestRespawn: destroying %s and restarting %s"), *Pawn->GetName(), *GetNameSafe(Controller));
			Pawn->Destroy();
			if (Controller)
			{
				GameMode->RestartPlayer(Controller);
			}
		}
	}

	void TestRespawn(const TArray<FString>& Args, UWorld* World)
	{
		if (UWorld* Authority = AuthorityWorld(World))
		{
			RespawnPawns(Authority, 50);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs TestDamageCommand(
		TEXT("velox.combat.TestDamage"),
		TEXT("velox.combat.TestDamage <Damage> [Armor (-1 keeps)] [Self 0|1] [Pellets] [DirX DirY DirZ]: applies GE_Damage (Q3 table, knockback through AddKnockback(Server)) to the local player's pawn on the server. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TestDamage));

	FAutoConsoleCommandWithWorldAndArgs TestRespawnCommand(
		TEXT("velox.combat.TestRespawn"),
		TEXT("velox.combat.TestRespawn: destroys the player pawn(s) and restarts them (new pawn, InitAbilityActorInfo, GE_SpawnInit, no re-grant). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TestRespawn));
}
#endif // !UE_BUILD_SHIPPING
