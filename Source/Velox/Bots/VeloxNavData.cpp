// Velox - per-map nav tile size (task M7 H12). See VeloxNavData.h.

#include "Bots/VeloxNavData.h"

#include "Bots/VeloxBotLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

ARecastNavMesh* UVeloxNavDataLibrary::SpawnMapNavMesh(UObject* WorldContext, float TileSizeUU)
{
#if WITH_EDITOR
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->IsGameWorld() || !World->PersistentLevel)
	{
		UE_LOG(LogVeloxBots, Error, TEXT("SpawnMapNavMesh: needs an editor world (got %s)"), *GetNameSafe(World));
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.OverrideLevel = World->PersistentLevel;
	Params.ObjectFlags = RF_Transactional;
	ARecastNavMesh* NavMesh = World->SpawnActor<ARecastNavMesh>(ARecastNavMesh::StaticClass(), FTransform::Identity, Params);
	if (!NavMesh)
	{
		UE_LOG(LogVeloxBots, Error, TEXT("SpawnMapNavMesh: SpawnActor failed in %s"), *World->GetName());
		return nullptr;
	}
	// The runtime nav system accepts a placed instance only when its NavDataConfig is equivalent to a supported agent
	// (RegisterNavData; a fresh actor carries the class default and is dropped with RegistrationFailed_AgentNotValid), so stamp the
	// project's single agent ([/Script/NavigationSystem.NavigationSystemV1] SupportedAgents, "VeloxPawn") on it here.
	const TArray<FNavDataConfig>& Agents = GetDefault<UNavigationSystemV1>()->GetSupportedAgents();
	if (Agents.Num() != 1)
	{
		UE_LOG(LogVeloxBots, Error, TEXT("SpawnMapNavMesh: expected one supported nav agent, config has %d"), Agents.Num());
		NavMesh->Destroy();
		return nullptr;
	}
	NavMesh->SetConfig(Agents[0]);
	NavMesh->TileSizeUU = TileSizeUU;
	UE_LOG(LogVeloxBots, Log, TEXT("SpawnMapNavMesh: %s tile_size=%.0f agent=%s radius=%.1f height=%.1f in %s"), *NavMesh->GetName(), NavMesh->TileSizeUU,
		*NavMesh->GetConfig().Name.ToString(), NavMesh->AgentRadius, NavMesh->AgentHeight, *World->GetName());
	return NavMesh;
#else
	UE_LOG(LogVeloxBots, Error, TEXT("SpawnMapNavMesh: editor only"));
	return nullptr;
#endif
}
