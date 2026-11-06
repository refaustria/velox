// Velox - server kind helpers (ADR 0007 section 1 (a), task m6-online/02).
//
// A packaged Game build cannot be a dedicated server (IsRunningDedicatedServer() is false whenever
// FPlatformProperties::IsGameOnly(), ADR 0007 fact 2), so the deployed server is a *headless listen host*: NM_ListenServer,
// a spectator-only local player (?SpectatorOnly=1 on the host URL) and -nullrhi. Code that skips cost on a dedicated server
// (ADR 0007 fact 9) may extend the skip to such a host with IsServerLike(); correctness rules keyed on NM_ListenServer
// (jump pads, B12/B25, lag compensation) must keep using the net mode directly.

#pragma once

#include "CoreMinimal.h"
#include "Core/VeloxLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreMisc.h"
#include "Misc/Parse.h"

namespace VeloxServerKind
{
	/** -nullrhi on the process command line, read once. */
	inline bool IsNullRhiProcess()
	{
		static const bool bNullRhi = FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));
		return bNullRhi;
	}

	/** The rule itself, world-free (unit row Velox.GameMode.HostSemantics). */
	inline bool IsHeadlessHostRule(ENetMode NetMode, bool bLocalPlayerOnlySpectator, bool bNullRhi)
	{
		return NetMode == NM_ListenServer && bLocalPlayerOnlySpectator && bNullRhi;
	}

	/** The world's first local player controller's PlayerState is bOnlySpectator (false while it has none). */
	inline bool IsLocalPlayerOnlySpectator(const UWorld& World)
	{
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				return PC->PlayerState && PC->PlayerState->IsOnlyASpectator();
			}
		}
		return false;
	}

	inline const UWorld* WorldOf(const UObject* WorldContext)
	{
		return (WorldContext && GEngine) ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	}

	/** Listen server under -nullrhi whose local player is a spectator only: nobody ever sees what this process renders. */
	inline bool IsHeadlessHost(const UObject* WorldContext)
	{
		const UWorld* World = WorldOf(WorldContext);
		const bool bHeadless = World && World->GetNetMode() == NM_ListenServer && IsNullRhiProcess() && IsLocalPlayerOnlySpectator(*World);
		static bool bLogged = false;
		if (bHeadless && !bLogged)
		{
			bLogged = true;
			UE_LOG(LogVelox, Log, TEXT("VeloxServerKind headless_host=1 world=%s (listen, spectator-only local player, -nullrhi): server-like"), *World->GetName());
		}
		return bHeadless;
	}

	/** A dedicated server, or a headless listen host: a server whose own presentation nobody sees. */
	inline bool IsServerLike(const UObject* WorldContext)
	{
		const UWorld* World = WorldOf(WorldContext);
		if (IsRunningDedicatedServer() || (World && World->GetNetMode() == NM_DedicatedServer))
		{
			return true;
		}
		return IsHeadlessHost(World);
	}
}
