// Velox - base game mode.

#include "GameModes/VeloxGameMode.h"

#include "AbilitySystemComponent.h"
#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotDifficulty.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotSettings.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxDummyController.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Core/VeloxGameplayTags.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "GameModes/VeloxSpawnSelection.h"
#include "GameModes/VeloxSpectatorPawn.h"
#include "HAL/IConsoleManager.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/VeloxHUD.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameMode)

namespace VeloxGameModeBotsPrivate
{
	// URL options of ADR 0005 section 12.
	const TCHAR* const BotsOption = TEXT("Bots");
	const TCHAR* const BotsFillOption = TEXT("BotsFill");
	const TCHAR* const BotDifficultyOption = TEXT("BotDifficulty");
}

#if !UE_BUILD_SHIPPING
namespace VeloxGameModeDeathCameraPrivate
{
	// Headless checks of the Corpse rule (task m4-followups/04 step 5) and playtests without regenerating the assets.
	static TAutoConsoleVariable<int32> CVarDeathCamera(TEXT("velox.match.DeathCamera"), -1,
		TEXT("Override of UVeloxGameModeDefinition::DeathCamera: -1 = the mode definition, 0 = Corpse, 1 = FreeSpectate. Non-shipping."), ECVF_Default);
	// UVeloxCombatSettings is a data asset, not a config class, so `-ini:Game:` cannot reach ForceRespawnDelay; the
	// DeathSpectateRun soak shortens the forced respawn with this (C11 (2) fallback).
	static TAutoConsoleVariable<float> CVarForceRespawnDelay(TEXT("velox.combat.ForceRespawnDelay"), -1.f,
		TEXT("Override of UVeloxCombatSettings::ForceRespawnDelay in seconds (< 0 = the asset). Non-shipping."), ECVF_Default);
}
#endif

AVeloxGameMode::AVeloxGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GameStateClass = AVeloxGameState::StaticClass();
	PlayerControllerClass = AVeloxPlayerController::StaticClass();
	PlayerStateClass = AVeloxPlayerState::StaticClass();
	DefaultPawnClass = AVeloxCharacter::StaticClass();
	HUDClass = AVeloxHUD::StaticClass();
	// C11: copied to AGameStateBase::SpectatorClass by InitGameState; spawned by the engine on owning clients only.
	SpectatorClass = AVeloxSpectatorPawn::StaticClass();
}

const UVeloxCombatSettings& AVeloxGameMode::GetCombatSettingsFor(const AController* Controller)
{
	const AVeloxPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AVeloxPlayerState>() : nullptr;
	return UVeloxCombatSettings::GetOrDefault(PlayerState ? PlayerState->GetCombatSettings() : nullptr);
}

void AVeloxGameMode::PlayerDied(AVeloxPlayerState* Victim, AVeloxPlayerState* Killer, const FGameplayTagContainer& DamageTags)
{
	if (!Victim)
	{
		return;
	}
	const bool bSuicide = !Killer || Killer == Victim;
	if (Phase == EVeloxMatchPhase::PostMatch)
	{
		// Intermission (ADR 0004 section 5): frozen players take no damage once task 02 honours MatchFrozen; until then a
		// projectile in flight may still kill, and that frag must not change the final score.
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxKill victim=%s killer=%s ignored (PostMatch)"), *AVeloxPlayerState::VeloxPlayerLabel(Victim), *AVeloxPlayerState::VeloxPlayerLabel(Killer));
		return;
	}
	// Kills count real frags only (ADR 0004 section 6); a suicide is a death and -1 score, as in Q3.
	Victim->AddDeath();
	if (bSuicide)
	{
		Victim->SetScore(Victim->GetScore() - 1.f);
	}
	else
	{
		Killer->SetScore(Killer->GetScore() + 1.f);
		Killer->AddKill();
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxKill victim=%s killer=%s suicide=%d tags=[%s] score_killer=%.0f score_victim=%.0f kills_killer=%d deaths_victim=%d"), *AVeloxPlayerState::VeloxPlayerLabel(Victim),
		*AVeloxPlayerState::VeloxPlayerLabel(Killer), bSuicide ? 1 : 0, *DamageTags.ToStringSimple(), Killer ? Killer->GetScore() : 0.f, Victim->GetScore(), Killer ? Killer->GetKills() : 0,
		Victim->GetDeaths());

	if (AVeloxGameState* VeloxGameState = GetGameState<AVeloxGameState>())
	{
		FVeloxKillFeedEntry FeedEntry;
		FeedEntry.VictimName = AVeloxPlayerState::VeloxPlayerLabel(Victim);
		FeedEntry.KillerName = bSuicide ? FeedEntry.VictimName : AVeloxPlayerState::VeloxPlayerLabel(Killer);
		FeedEntry.VictimColor = Victim->GetPlayerColorIndex();
		FeedEntry.KillerColor = bSuicide ? FeedEntry.VictimColor : Killer->GetPlayerColorIndex();
		FeedEntry.DamageTag = VeloxKillFeed::PickDamageTag(DamageTags);
		FeedEntry.bSuicide = bSuicide;
		VeloxGameState->AppendKillFeedEntry(MoveTemp(FeedEntry));

		FVeloxKillEvent Event;
		Event.Victim = Victim;
		Event.Killer = Killer ? Killer : Victim;
		Event.DamageTags = DamageTags;
		Event.Time = GetWorld()->GetTimeSeconds();
		VeloxGameState->OnKill.Broadcast(Event);
	}
	// Frag limit / sudden death: evaluated on the next tick, outside the killing damage execution (B27).
	RequestMatchEvaluation();
}

EVeloxDeathCamera AVeloxGameMode::GetDeathCamera() const
{
#if !UE_BUILD_SHIPPING
	const int32 Override = VeloxGameModeDeathCameraPrivate::CVarDeathCamera.GetValueOnGameThread();
	if (Override == 0 || Override == 1)
	{
		return Override == 0 ? EVeloxDeathCamera::Corpse : EVeloxDeathCamera::FreeSpectate;
	}
#endif
	return GameModeDefinition ? GameModeDefinition->DeathCamera : GetDefault<UVeloxGameModeDefinition>()->DeathCamera;
}

const FBox& AVeloxGameMode::GetSpectateBounds()
{
	if (!bSpectateBoundsCached && GetWorld())
	{
		bSpectateBoundsCached = true;
		SpectateBounds = FBox(ForceInit);
		int32 NumVolumes = 0;
		for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
		{
			const FBox Box = It->GetComponentsBoundingBox(true);
			if (Box.IsValid)
			{
				SpectateBounds += Box;
				++NumVolumes;
			}
		}
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxSpectate bounds volumes=%d box=%s"), NumVolumes, SpectateBounds.IsValid ? *SpectateBounds.ToString() : TEXT("unbounded"));
	}
	return SpectateBounds;
}

void AVeloxGameMode::NotifyControllerDied(AController* Controller, AVeloxCharacter* Corpse)
{
	UWorld* World = GetWorld();
	if (!Controller || !World)
	{
		return;
	}
	// C11 death camera (humans only; AI controllers take neither branch, B28).
	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		const EVeloxDeathCamera Camera = GetDeathCamera();
		AVeloxPlayerController* VeloxPC = Cast<AVeloxPlayerController>(PC);
		if (Camera == EVeloxDeathCamera::FreeSpectate && VeloxPC)
		{
			VeloxPC->BeginDeathSpectate(Corpse);
		}
		else if (Corpse)
		{
			PC->SetViewTarget(Corpse);
		}
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDeath step=6 spectate=%s controller=%s"), Camera == EVeloxDeathCamera::FreeSpectate && VeloxPC ? TEXT("free") : TEXT("corpse"), *Controller->GetName());
	}
	const UVeloxCombatSettings& Settings = GetCombatSettingsFor(Controller);
	const bool bAI = !Controller->IsPlayerController();
	FPendingRespawn& Pending = PendingRespawns.FindOrAdd(Controller);
	World->GetTimerManager().ClearTimer(Pending.Timer);
	Pending.DeathTime = World->GetTimeSeconds();
	if (bAI && !bAutoRespawnAIControllers)
	{
		PendingRespawns.Remove(Controller);
		return;
	}
	// Humans: forced respawn after ForceRespawnDelay unless they press earlier (any time after MinRespawnDelay).
	float ForceDelay = Settings.ForceRespawnDelay;
#if !UE_BUILD_SHIPPING
	if (const float Override = VeloxGameModeDeathCameraPrivate::CVarForceRespawnDelay.GetValueOnGameThread(); Override >= 0.f)
	{
		ForceDelay = Override;
	}
#endif
	const float Delay = bAI ? Settings.MinRespawnDelay : ForceDelay;
	World->GetTimerManager().SetTimer(Pending.Timer, FTimerDelegate::CreateUObject(this, &ThisClass::OnRespawnTimer, TWeakObjectPtr<AController>(Controller), true), Delay, false);
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxRespawn waiting controller=%s ai=%d min=%.2f s %s in %.2f s"), *Controller->GetName(), bAI ? 1 : 0, Settings.MinRespawnDelay, bAI ? TEXT("auto") : TEXT("forced"), Delay);
}

void AVeloxGameMode::OnRespawnTimer(TWeakObjectPtr<AController> Controller, bool bForced)
{
	if (Controller.IsValid())
	{
		TryRespawn(Controller.Get(), bForced);
	}
}

bool AVeloxGameMode::TryRespawn(AController* Controller, bool bForced)
{
	UWorld* World = GetWorld();
	FPendingRespawn* Pending = Controller ? PendingRespawns.Find(Controller) : nullptr;
	if (!World || !Pending)
	{
		return false;
	}
	if (Controller->GetPawn())
	{
		World->GetTimerManager().ClearTimer(Pending->Timer);
		PendingRespawns.Remove(Controller);
		return false;
	}
	if (bMatchRulesEnabled && Phase == EVeloxMatchPhase::PostMatch)
	{
		// PostMatch (engine WaitingPostMatch): no respawns during the intermission; the restart spawns everyone.
		return false;
	}
	const UVeloxCombatSettings& Settings = GetCombatSettingsFor(Controller);
	const double Now = World->GetTimeSeconds();
	if (!bForced && Now + KINDA_SMALL_NUMBER < Pending->DeathTime + Settings.MinRespawnDelay)
	{
		return false;
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxRespawn controller=%s after %.2f s (%s)"), *Controller->GetName(), Now - Pending->DeathTime, bForced ? TEXT("forced/auto") : TEXT("requested"));
	World->GetTimerManager().ClearTimer(Pending->Timer);
	PendingRespawns.Remove(Controller);
	RestartPlayer(Controller);
	return true;
}

void AVeloxGameMode::AssignPlayerColor(AVeloxPlayerState& PlayerState)
{
	if (PlayerState.HasPlayerColor())
	{
		return;
	}
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PlayerState.GetCombatSettings());
	const int32 NumColors = Settings.PlayerColorPalette.Num();
	if (NumColors == 0)
	{
		return;
	}
	TBitArray<> Used(false, NumColors);
	int32 NumOthers = 0;
	if (const AGameStateBase* State = GameState)
	{
		for (const APlayerState* Other : State->PlayerArray)
		{
			const AVeloxPlayerState* OtherVelox = Cast<AVeloxPlayerState>(Other);
			if (OtherVelox && OtherVelox != &PlayerState && OtherVelox->HasPlayerColor())
			{
				++NumOthers;
				Used[OtherVelox->GetPlayerColorIndex() % NumColors] = true;
			}
		}
	}
	const int32 Free = Used.Find(false);
	const int32 Index = Free != INDEX_NONE ? Free : NumOthers % NumColors;
	PlayerState.SetPlayerColorIndex(static_cast<uint8>(Index));
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxColor player=%s index=%d palette=%d"), *AVeloxPlayerState::VeloxPlayerLabel(&PlayerState), Index, NumColors);
}

bool AVeloxGameMode::IsStartOccupied(const FVector& Start, const AVeloxCharacter& Pawn)
{
	// Two upright capsules overlap when they are closer than the sum of radii (horizontally) and half heights (vertically).
	const FVector Delta = Pawn.GetActorLocation() - Start;
	return FMath::Sqrt(FMath::Square(Delta.X) + FMath::Square(Delta.Y)) < 2.0 * VeloxCharacterGeometry::CapsuleRadius && FMath::Abs(Delta.Z) < 2.0 * VeloxCharacterGeometry::CapsuleHalfHeight;
}

bool AVeloxGameMode::ShouldSpawnAtStartSpot(AController* Player)
{
	// The engine reuses Controller->StartSpot; every (re)spawn goes through ChoosePlayerStart instead.
	return false;
}

AActor* AVeloxGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UWorld* World = GetWorld();
	TArray<APlayerStart*> Starts;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Starts.Add(*It);
	}
	if (Starts.IsEmpty())
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	TArray<const AVeloxCharacter*> Living;
	for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
	{
		const AVeloxCharacter* Pawn = *It;
		if (!Pawn->IsCorpse() && !Pawn->IsPendingKillPending() && !(Player && Pawn->GetController() == Player))
		{
			Living.Add(Pawn);
		}
	}
	TArray<FVector> Enemies;
	for (const AVeloxCharacter* Pawn : Living)
	{
		Enemies.Add(Pawn->GetActorLocation());
	}
	TArray<FVector> StartLocations;
	TArray<bool> Occupied;
	for (const APlayerStart* Start : Starts)
	{
		StartLocations.Add(Start->GetActorLocation());
		bool bTaken = false;
		for (const AVeloxCharacter* Pawn : Living)
		{
			bTaken |= IsStartOccupied(Start->GetActorLocation(), *Pawn);
		}
		Occupied.Add(bTaken);
	}

	if (!bSpawnRandomReady)
	{
		SpawnRandom.Initialize(SpawnRandomSeed != 0 ? SpawnRandomSeed : FMath::Rand());
		bSpawnRandomReady = true;
	}
	const UVeloxCombatSettings& Settings = GetCombatSettingsFor(Player);
	const VeloxSpawn::FSelection Pick = VeloxSpawn::Select(StartLocations, Occupied, Enemies, Settings.SpawnAvoidRadiusCm, SpawnRandom);
	if (Pick.Index == INDEX_NONE)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxSpawn player=%s start=%s at %s starts=%d candidates=%d enemies=%d nearest_enemy_cm=%.0f telefrag=%d"), *GetNameSafe(Player),
		*Starts[Pick.Index]->GetName(), *StartLocations[Pick.Index].ToCompactString(), Starts.Num(), Pick.NumCandidates, Enemies.Num(),
		Enemies.IsEmpty() ? -1.0 : VeloxSpawn::NearestEnemyDistance(StartLocations[Pick.Index], Enemies), Pick.bTelefrag ? 1 : 0);
	return Starts[Pick.Index];
}

void AVeloxGameMode::RestartPlayer(AController* NewPlayer)
{
	const AVeloxDummyController* Dummy = Cast<AVeloxDummyController>(NewPlayer);
	if (Dummy && Dummy->HasFixedRespawn())
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxRespawn controller=%s at %s class=%s (fixed)"), *NewPlayer->GetName(), *Dummy->GetRespawnTransform().GetLocation().ToCompactString(),
			*GetNameSafe(Dummy->GetRespawnPawnClass()));
		RestartPlayerAtTransform(NewPlayer, Dummy->GetRespawnTransform());
		return;
	}
	Super::RestartPlayer(NewPlayer);
}

UClass* AVeloxGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (const AVeloxDummyController* Dummy = Cast<AVeloxDummyController>(InController); Dummy && Dummy->HasFixedRespawn() && Dummy->GetRespawnPawnClass())
	{
		return Dummy->GetRespawnPawnClass();
	}
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

void AVeloxGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot)
{
	// Living pawns of other controllers in the start's volume die when the new pawn appears (Q3 G_KillBox). The spawn
	// rule only picks an occupied start when all are occupied.
	TArray<TWeakObjectPtr<AVeloxCharacter>> Occupants;
	if (StartSpot && NewPlayer)
	{
		for (TActorIterator<AVeloxCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsCorpse() && It->GetController() != NewPlayer && IsStartOccupied(StartSpot->GetActorLocation(), **It))
			{
				Occupants.Add(*It);
			}
		}
	}

	Super::RestartPlayerAtPlayerStart(NewPlayer, StartSpot);

	AVeloxPlayerState* SpawnerPS = NewPlayer ? NewPlayer->GetPlayerState<AVeloxPlayerState>() : nullptr;
	UVeloxAbilitySystemComponent* SpawnerASC = SpawnerPS ? SpawnerPS->GetVeloxAbilitySystemComponent() : nullptr;
	if (!SpawnerASC)
	{
		return;
	}
	const UVeloxCombatSettings& Settings = GetCombatSettingsFor(NewPlayer);
	for (const TWeakObjectPtr<AVeloxCharacter>& Occupant : Occupants)
	{
		UAbilitySystemComponent* VictimASC = Occupant.IsValid() ? Occupant->GetAbilitySystemComponent() : nullptr;
		if (!VictimASC || Occupant->IsCorpse())
		{
			continue;
		}
		// Protection must not shield the occupant: the spawn is already decided.
		VictimASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(VeloxGameplayTags::State_SpawnProtected));
		FVeloxDamageSpecParams Params;
		Params.Damage = Settings.TelefragDamage;
		Params.DamageTag = VeloxGameplayTags::Damage_Telefrag;
		Params.HitLocation = Occupant->GetActorLocation();
		Params.bNoArmor = true;
		Params.bNoKnockback = true;
		Params.Causer = NewPlayer->GetPawn();
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxTelefrag spawner=%s victim=%s"), *NewPlayer->GetName(), *Occupant->GetName());
		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(SpawnerASC, Params);
		if (Spec.IsValid())
		{
			SpawnerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), VictimASC);
		}
	}
}

void AVeloxGameMode::StartBot(AController* Bot)
{
	if (!Bot)
	{
		return;
	}
	++NumBots;
	if (!(bMatchRulesEnabled && Phase == EVeloxMatchPhase::PostMatch))
	{
		RestartPlayer(Bot);
	}
	RequestMatchEvaluation();
}

void AVeloxGameMode::StartPlay()
{
	Super::StartPlay();

	using namespace VeloxGameModeBotsPrivate;
	const UVeloxBotSettings& Settings = UVeloxBotSettings::Get();
	const bool bHasCount = UGameplayStatics::HasOption(OptionsString, BotsOption);
	const int32 Requested = bHasCount ? UGameplayStatics::GetIntOption(OptionsString, BotsOption, 0) : Settings.DefaultBotCount;
	const bool bFill = UGameplayStatics::GetIntOption(OptionsString, BotsFillOption, 0) != 0;
	const int32 MaxActive = GameModeDefinition ? GameModeDefinition->MaxActivePlayers : 0;
	const int32 Count = VeloxBotRules::UrlBotCount(Requested, MaxActive, bFill);
	if (Count <= 0)
	{
		return;
	}
	UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld());
	const FString DifficultyName = UGameplayStatics::ParseOption(OptionsString, BotDifficultyOption);
	const UVeloxBotDifficulty* Difficulty = UVeloxBotSubsystem::LoadDifficulty(DifficultyName);
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBot url bots=%d requested=%d fill=%d max_active=%d difficulty=%s (%s)"), Count, Requested, bFill ? 1 : 0, MaxActive,
		DifficultyName.IsEmpty() ? TEXT("default") : *DifficultyName, *GetNameSafe(Difficulty));
	if (!Bots || (!DifficultyName.IsEmpty() && !Difficulty))
	{
		UE_LOG(LogVeloxBots, Warning, TEXT("VeloxBot url bots not added: %s"), !Bots ? TEXT("no bot subsystem") : TEXT("unknown ?BotDifficulty"));
		return;
	}
	for (int32 i = 0; i < Count; ++i)
	{
		Bots->AddBot(Difficulty);
	}
}

void AVeloxGameMode::Logout(AController* Exiting)
{
	if (const AVeloxBotController* Bot = Cast<AVeloxBotController>(Exiting))
	{
		NumBots = FMath::Max(0, NumBots - 1);
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBot logout name=%s bots=%d"), *Bot->GetBotName(), NumBots);
	}
	if (UWorld* World = GetWorld())
	{
		if (FPendingRespawn* Pending = Exiting ? PendingRespawns.Find(Exiting) : nullptr)
		{
			World->GetTimerManager().ClearTimer(Pending->Timer);
		}
	}
	PendingRespawns.Remove(Exiting);
	if (const APlayerState* ExitingPS = Exiting ? Exiting->PlayerState.Get() : nullptr)
	{
		// Forfeit / countdown abort: the PlayerState stays in the PlayerArray until it is destroyed, so mark it leaving.
		if (bMatchRulesEnabled && IsActivePlayer(ExitingPS))
		{
			bActivePlayerLeft = true;
			UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch leave player=%s phase=%s"), *AVeloxPlayerState::VeloxPlayerLabel(ExitingPS), LexToString(Phase));
		}
		LeavingPlayerStates.Add(ExitingPS);
	}
	Super::Logout(Exiting);
	RequestMatchEvaluation();
}
