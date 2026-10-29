// Velox - non-shipping death / respawn console commands (ADR 0003 task 06 acceptance).
//   velox.combat.KillTest [env|rail]   kills the nearest dummy (an AI-controlled Velox pawn) on the authority world:
//                                      env  = 1000 environment damage dealt by the local player,
//                                      rail = 100 damage first (health 25), then velox.combat.FireTest RG, so the real
//                                             hitscan path kills it and ClientConfirmHit carries Kill=true.
//   velox.combat.SpawnDummy [x y z] [invulnerable]
//                                      spawns an AVeloxDummySpawner (and its dummy) at x y z, default DefaultDummyDistanceCm
//                                      in front of the local player; invulnerable = permanent spawn protection.
//   velox.combat.KillSelf              1000 environment damage on the local player's own pawn (suicide).
// The dummy then goes through the whole B8 death order, corpse timer and the automatic respawn of AI controllers.

#include "Bots/VeloxDummySpawner.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
namespace VeloxDeathCheats
{
	constexpr int32 MaxRetries = 100;
	constexpr float RetryIntervalSeconds = 0.1f;
	constexpr int32 RailPreDamage = 100;
	constexpr double DefaultDummyDistanceCm = 1000.0;

	UWorld* AuthorityWorld(UWorld* World)
	{
		return World && World->GetNetMode() != NM_Client ? World : nullptr;
	}

	AVeloxCharacter* LocalPawn(UWorld* World)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		return PC && PC->IsLocalController() ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
	}

	AVeloxCharacter* NearestLiveDummy(UWorld* World, const AVeloxCharacter& From)
	{
		AVeloxCharacter* Best = nullptr;
		double BestDistSq = TNumericLimits<double>::Max();
		for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
		{
			const AVeloxPlayerState* PS = It->GetPlayerState<AVeloxPlayerState>();
			if (*It == &From || It->IsCorpse() || !PS || !PS->GetHealthSet() || PS->GetHealthSet()->GetHealth() <= 0.f || Cast<APlayerController>(It->GetController()))
			{
				continue;
			}
			const double DistSq = FVector::DistSquared(It->GetActorLocation(), From.GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = *It;
			}
		}
		return Best;
	}

	/** Damage dealt by Dealer's ASC to Victim's ASC (a different player: not a self hit, no half damage). */
	void Deal(AVeloxCharacter& Dealer, AVeloxCharacter& Victim, int32 Damage)
	{
		UVeloxAbilitySystemComponent* DealerASC = Dealer.GetVeloxAbilitySystemComponent();
		UVeloxAbilitySystemComponent* VictimASC = Victim.GetVeloxAbilitySystemComponent();
		if (!DealerASC || !VictimASC)
		{
			return;
		}
		FVeloxDamageSpecParams Params;
		Params.Damage = Damage;
		Params.DamageTag = VeloxGameplayTags::Damage_Environment;
		Params.HitLocation = Victim.GetActorLocation();
		Params.bNoKnockback = true;
		Params.Causer = &Dealer;
		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(DealerASC, Params);
		if (Spec.IsValid())
		{
			DealerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), VictimASC);
		}
	}

	void RunKillTest(TWeakObjectPtr<UWorld> WeakWorld, bool bRail, int32 RetriesLeft)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return;
		}
		AVeloxCharacter* Player = LocalPawn(World);
		AVeloxCharacter* Dummy = Player ? NearestLiveDummy(World, *Player) : nullptr;
		if (!Player || !Dummy || !Player->GetPlayerState<AVeloxPlayerState>())
		{
			if (RetriesLeft > 0)
			{
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateStatic(&RunKillTest, WeakWorld, bRail, RetriesLeft - 1), RetryIntervalSeconds, false);
				return;
			}
			UE_LOG(LogVeloxCombat, Warning, TEXT("KillTest: no local player pawn and live dummy in %s"), *World->GetName());
			return;
		}
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Player->GetPlayerState<AVeloxPlayerState>()->GetCombatSettings());
		UE_LOG(LogVeloxCombat, Log, TEXT("KillTest: %s kills %s (%s) at %s"), *Player->GetName(), *Dummy->GetName(), bRail ? TEXT("rail") : TEXT("environment damage"), *Dummy->GetActorLocation().ToCompactString());
		if (bRail)
		{
			Deal(*Player, *Dummy, RailPreDamage);
			GEngine->Exec(World, TEXT("velox.combat.FireTest RG"));
		}
		else
		{
			Deal(*Player, *Dummy, Settings.EnvironmentKillDamage);
		}
	}

	void KillTestCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (UWorld* Authority = AuthorityWorld(World))
		{
			RunKillTest(Authority, Args.IsValidIndex(0) && Args[0].Equals(TEXT("rail"), ESearchCase::IgnoreCase), MaxRetries);
		}
	}

	void SpawnDummyCommand(const TArray<FString>& Args, UWorld* World)
	{
		UWorld* Authority = AuthorityWorld(World);
		if (!Authority)
		{
			return;
		}
		bool bInvulnerable = false;
		TArray<double> Numbers;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("invulnerable"), ESearchCase::IgnoreCase))
			{
				bInvulnerable = true;
			}
			else if (Arg.IsNumeric())
			{
				Numbers.Add(FCString::Atod(*Arg));
			}
		}
		FVector Location;
		if (Numbers.Num() >= 3)
		{
			Location = FVector(Numbers[0], Numbers[1], Numbers[2]);
		}
		else if (const AVeloxCharacter* Player = LocalPawn(Authority))
		{
			Location = Player->GetActorLocation() + Player->GetActorForwardVector() * DefaultDummyDistanceCm;
		}
		else
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("SpawnDummy: give x y z or run with a local player pawn"));
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true; // BeginPlay must not auto-spawn a second dummy
		AVeloxDummySpawner* Spawner = Authority->SpawnActor<AVeloxDummySpawner>(Location, FRotator::ZeroRotator, Params);
		if (Spawner)
		{
			Spawner->bAutoSpawn = false;
			Spawner->bInvulnerable = bInvulnerable;
			Spawner->FinishSpawning(FTransform(FRotator::ZeroRotator, Location));
			const AVeloxCharacter* Dummy = Spawner->SpawnDummy();
			UE_LOG(LogVeloxCombat, Log, TEXT("SpawnDummy: %s at %s invulnerable=%d"), *GetNameSafe(Dummy), *Location.ToCompactString(), bInvulnerable ? 1 : 0);
		}
	}

	void KillSelfCommand(const TArray<FString>& Args, UWorld* World)
	{
		UWorld* Authority = AuthorityWorld(World);
		AVeloxCharacter* Player = Authority ? LocalPawn(Authority) : nullptr;
		UVeloxAbilitySystemComponent* ASC = Player ? Player->GetVeloxAbilitySystemComponent() : nullptr;
		if (!ASC)
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("KillSelf: no local player pawn on an authority world"));
			return;
		}
		FVeloxDamageSpecParams Params;
		Params.Damage = UVeloxCombatSettings::GetOrDefault(Player->GetPlayerState<AVeloxPlayerState>()->GetCombatSettings()).EnvironmentKillDamage;
		Params.DamageTag = VeloxGameplayTags::Damage_Environment;
		Params.HitLocation = Player->GetActorLocation();
		Params.bNoKnockback = true;
		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(ASC, Params);
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}

	FAutoConsoleCommandWithWorldAndArgs KillTestCmd(
		TEXT("velox.combat.KillTest"),
		TEXT("velox.combat.KillTest [env|rail]: kills the nearest AI-controlled dummy (env: 1000 damage; rail: 100 damage then a railgun shot, Kill=true in ClientConfirmHit). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&KillTestCommand));

	FAutoConsoleCommandWithWorldAndArgs SpawnDummyCmd(
		TEXT("velox.combat.SpawnDummy"),
		TEXT("velox.combat.SpawnDummy [x y z] [invulnerable]: spawns a strafing dummy with a spawner (respawns in place); invulnerable = permanent spawn protection. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnDummyCommand));

	FAutoConsoleCommandWithWorldAndArgs KillSelfCmd(
		TEXT("velox.combat.KillSelf"),
		TEXT("velox.combat.KillSelf: 1000 environment damage on the local player's pawn (suicide: death, corpse, respawn wait). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&KillSelfCommand));
}
#endif // !UE_BUILD_SHIPPING
