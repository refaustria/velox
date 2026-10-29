// Velox - non-shipping kill feed console commands (ADR 0004 section 6, task 06).
//   velox.feed.Frag [N] [delay s]   authority world: N synthetic frags through AVeloxGameMode::PlayerDied (two temporary
//                                   PlayerStates, destroyed right after, so the feed names must survive them); every
//                                   fourth is a suicide, the weapon tag cycles. Optionally starts after <delay> seconds.
//   velox.feed.List                 any world: the replicated feed as this machine sees it, oldest first.

#include "Combat/VeloxCombatLog.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameModes/VeloxGameMode.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
namespace VeloxKillFeedCheats
{
	int32 GFragCounter = 0;

	void SpawnFrags(TWeakObjectPtr<UWorld> WeakWorld, int32 Count)
	{
		UWorld* World = WeakWorld.Get();
		AVeloxGameMode* GameMode = World ? World->GetAuthGameMode<AVeloxGameMode>() : nullptr;
		if (!GameMode)
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("velox.feed.Frag: no AVeloxGameMode on this world (authority only)"));
			return;
		}
		const FGameplayTag Weapons[] = {VeloxGameplayTags::Damage_Rocket.GetTag(), VeloxGameplayTags::Damage_Rail.GetTag(), VeloxGameplayTags::Damage_Shotgun.GetTag(),
			VeloxGameplayTags::Damage_Plasma.GetTag()};
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Id = ++GFragCounter;
			AVeloxPlayerState* Killer = World->SpawnActor<AVeloxPlayerState>();
			AVeloxPlayerState* Victim = World->SpawnActor<AVeloxPlayerState>();
			if (!Killer || !Victim)
			{
				continue;
			}
			Killer->SetPlayerName(FString::Printf(TEXT("Alpha%d"), Id));
			Victim->SetPlayerName(FString::Printf(TEXT("Bravo%d"), Id));
			const bool bSuicide = Id % 4 == 0;
			FGameplayTagContainer Tags(Weapons[Id % UE_ARRAY_COUNT(Weapons)]);
			if (bSuicide)
			{
				Tags.AddTag(VeloxGameplayTags::Damage_Self);
			}
			GameMode->PlayerDied(Victim, bSuicide ? nullptr : Killer, Tags);
			Killer->Destroy();
			Victim->Destroy();
		}
	}

	void FragCommand(const TArray<FString>& Args, UWorld* World)
	{
		const int32 Count = Args.IsValidIndex(0) ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 100) : 1;
		const float Delay = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 0.f;
		if (!World)
		{
			return;
		}
		if (Delay > 0.f)
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateStatic(&SpawnFrags, TWeakObjectPtr<UWorld>(World), Count), Delay, false);
			return;
		}
		SpawnFrags(World, Count);
	}

	void ListCommand(const TArray<FString>& /*Args*/, UWorld* World)
	{
		const AVeloxGameState* GameState = World ? World->GetGameState<AVeloxGameState>() : nullptr;
		if (!GameState)
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("velox.feed.List: no AVeloxGameState"));
			return;
		}
		const TArray<FVeloxKillFeedEntry> Entries = GameState->GetKillFeedOldestFirst();
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxKillFeedList netmode=%d entries=%d length=%d"), static_cast<int32>(World->GetNetMode()), Entries.Num(), GameState->GetKillFeedLength());
		for (const FVeloxKillFeedEntry& Entry : Entries)
		{
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxKillFeedList seq=%u killer=%s victim=%s tag=%s suicide=%d"), Entry.Seq, *Entry.KillerName, *Entry.VictimName, *Entry.DamageTag.ToString(),
				Entry.bSuicide ? 1 : 0);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs FragCmd(TEXT("velox.feed.Frag"), TEXT("velox.feed.Frag [N] [delay s]: N synthetic frags through PlayerDied (authority world). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FragCommand));

	FAutoConsoleCommandWithWorldAndArgs ListCmd(TEXT("velox.feed.List"), TEXT("velox.feed.List: logs the kill feed as this machine sees it, oldest first. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ListCommand));
}
#endif
