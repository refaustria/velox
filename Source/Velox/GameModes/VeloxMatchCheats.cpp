// Velox - non-shipping match console commands (ADR 0004 section 5, task 05). Authority world only.
//   velox.match.Phase <Warmup|Countdown|Live|Overtime|PostMatch>   enters the phase as the rules would (Live and Warmup
//                                                                   reset the level).
//   velox.match.Ready [0|1] [idx=N|all]     sets ready for local player N (default 0; split-screen), or for every player.
//   velox.match.OnPhase <Phase> <N> <cmd>   runs <cmd> (rest of the line) on the next tick after the N-th entry into
//                                           <Phase> from now on (headless tests: "OnPhase Live 1 velox.combat.KillTest env").
//   velox.match.AddPlayer                   adds a local (split-screen) player: a real Login/PostLogin on this world.
//   velox.match.RemovePlayer <index>        removes local player <index> (> 0): its controller logs out.
//   velox.match.Join <index>                local player <index> asks for a player slot (as ServerRequestJoin).
// Players also have the console commands `Ready` (toggle) and `Join` (AVeloxPlayerController exec functions).

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameModes/VeloxGameMode.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxMatchRules.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
namespace VeloxMatchCheats
{
	AVeloxGameMode* AuthorityGameMode(UWorld* World)
	{
		AVeloxGameMode* GameMode = World ? World->GetAuthGameMode<AVeloxGameMode>() : nullptr;
		if (!GameMode)
		{
			UE_LOG(LogVeloxMatch, Warning, TEXT("velox.match: no AVeloxGameMode on this world (authority only)"));
		}
		return GameMode;
	}

	APlayerController* LocalPlayerController(UWorld* World, int32 Index)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const ULocalPlayer* Player = GameInstance ? GameInstance->GetLocalPlayerByIndex(Index) : nullptr;
		return Player ? Player->GetPlayerController(World) : nullptr;
	}

	void PhaseCommand(const TArray<FString>& Args, UWorld* World)
	{
		EVeloxMatchPhase Phase;
		if (!Args.IsValidIndex(0) || !LexTryParseString(Phase, *Args[0]))
		{
			UE_LOG(LogVeloxMatch, Warning, TEXT("velox.match.Phase <Warmup|Countdown|Live|Overtime|PostMatch>"));
			return;
		}
		if (AVeloxGameMode* GameMode = AuthorityGameMode(World))
		{
			GameMode->ForcePhase(Phase);
		}
	}

	void ReadyCommand(const TArray<FString>& Args, UWorld* World)
	{
		AVeloxGameMode* GameMode = AuthorityGameMode(World);
		if (!GameMode)
		{
			return;
		}
		bool bReady = true;
		bool bAll = false;
		int32 PlayerIndex = 0;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				bAll = true;
			}
			else if (Arg.StartsWith(TEXT("idx="), ESearchCase::IgnoreCase))
			{
				PlayerIndex = FCString::Atoi(*Arg.RightChop(4));
			}
			else if (Arg.IsNumeric())
			{
				bReady = FCString::Atoi(*Arg) != 0;
			}
		}
		if (!bAll)
		{
			GameMode->SetPlayerReady(LocalPlayerController(World, PlayerIndex), bReady);
			return;
		}
		for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It)
		{
			GameMode->SetPlayerReady(It->Get(), bReady);
		}
	}

	void OnPhaseCommand(const TArray<FString>& Args, UWorld* World)
	{
		EVeloxMatchPhase Phase;
		AVeloxGameState* GameState = World ? World->GetGameState<AVeloxGameState>() : nullptr;
		if (Args.Num() < 3 || !LexTryParseString(Phase, *Args[0]) || !Args[1].IsNumeric() || !GameState || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogVeloxMatch, Warning, TEXT("velox.match.OnPhase <Phase> <N> <command...> (authority world with an AVeloxGameState)"));
			return;
		}
		const int32 Occurrence = FMath::Max(1, FCString::Atoi(*Args[1]));
		const FString Command = FString::Join(TArrayView<const FString>(Args).RightChop(2), TEXT(" "));
		// Shared counter and handle: the binding removes itself after it fired.
		TSharedRef<int32> Seen = MakeShared<int32>(0);
		TSharedRef<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TWeakObjectPtr<AVeloxGameState> WeakState(GameState);
		*Handle = GameState->OnPhaseChanged.AddLambda([Phase, Occurrence, Command, Seen, Handle, WeakWorld, WeakState](EVeloxMatchPhase /*Old*/, EVeloxMatchPhase New)
		{
			if (New != Phase || ++(*Seen) != Occurrence)
			{
				return;
			}
			// Removing the binding destroys this lambda and its captures: copy what is still needed first.
			const FString CommandCopy = Command;
			const TWeakObjectPtr<UWorld> WorldCopy = WeakWorld;
			const int32 OccurrenceCopy = Occurrence;
			if (AVeloxGameState* State = WeakState.Get())
			{
				State->OnPhaseChanged.Remove(*Handle);
			}
			if (UWorld* TargetWorld = WorldCopy.Get())
			{
				UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch OnPhase %s #%d -> %s"), LexToString(New), OccurrenceCopy, *CommandCopy);
				TargetWorld->GetTimerManager().SetTimerForNextTick([WorldCopy, CommandCopy]()
				{
					if (UWorld* ExecWorld = WorldCopy.Get())
					{
						GEngine->Exec(ExecWorld, *CommandCopy);
					}
				});
			}
		});
		UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch OnPhase registered phase=%s n=%d command=\"%s\""), LexToString(Phase), Occurrence, *Command);
	}

	void AddPlayerCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!AuthorityGameMode(World))
		{
			return;
		}
		const APlayerController* PC = UGameplayStatics::CreatePlayer(World, -1, true);
		UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch AddPlayer controller=%s player=%s"), *GetNameSafe(PC), PC ? *AVeloxPlayerState::VeloxPlayerLabel(PC->PlayerState) : TEXT("-"));
	}

	void RemovePlayerCommand(const TArray<FString>& Args, UWorld* World)
	{
		const int32 Index = Args.IsValidIndex(0) ? FCString::Atoi(*Args[0]) : INDEX_NONE;
		APlayerController* PC = Index > 0 && AuthorityGameMode(World) ? LocalPlayerController(World, Index) : nullptr;
		if (!PC)
		{
			UE_LOG(LogVeloxMatch, Warning, TEXT("velox.match.RemovePlayer <index > 0>: no such local player"));
			return;
		}
		UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch RemovePlayer index=%d player=%s"), Index, *AVeloxPlayerState::VeloxPlayerLabel(PC->PlayerState));
		UGameplayStatics::RemovePlayer(PC, true);
	}

	void JoinCommand(const TArray<FString>& Args, UWorld* World)
	{
		const int32 Index = Args.IsValidIndex(0) ? FCString::Atoi(*Args[0]) : 0;
		AVeloxGameMode* GameMode = AuthorityGameMode(World);
		if (APlayerController* PC = GameMode ? LocalPlayerController(World, Index) : nullptr)
		{
			GameMode->RequestJoin(PC);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs PhaseCmd(TEXT("velox.match.Phase"),
		TEXT("velox.match.Phase <Warmup|Countdown|Live|Overtime|PostMatch>: enters the phase as the rules would (Live/Warmup reset the level). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PhaseCommand));

	FAutoConsoleCommandWithWorldAndArgs ReadyCmd(TEXT("velox.match.Ready"),
		TEXT("velox.match.Ready [0|1] [idx=N|all]: ready flag of the local player N (default 0) (or every player). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ReadyCommand));

	FAutoConsoleCommandWithWorldAndArgs OnPhaseCmd(TEXT("velox.match.OnPhase"),
		TEXT("velox.match.OnPhase <Phase> <N> <command...>: runs the command on the tick after the N-th entry into Phase. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnPhaseCommand));

	FAutoConsoleCommandWithWorldAndArgs AddPlayerCmd(TEXT("velox.match.AddPlayer"),
		TEXT("velox.match.AddPlayer: adds a local split-screen player (Login/PostLogin on this world). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AddPlayerCommand));

	FAutoConsoleCommandWithWorldAndArgs RemovePlayerCmd(TEXT("velox.match.RemovePlayer"),
		TEXT("velox.match.RemovePlayer <index>: removes local player <index> (> 0); its controller logs out. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RemovePlayerCommand));

	FAutoConsoleCommandWithWorldAndArgs JoinCmd(TEXT("velox.match.Join"),
		TEXT("velox.match.Join [index]: local player <index> asks for a player slot. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&JoinCommand));
}
#endif // !UE_BUILD_SHIPPING
