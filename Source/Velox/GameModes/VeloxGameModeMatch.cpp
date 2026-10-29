// Velox - game mode: match phase machine, ready-up, spectator slots, intermission (ADR 0004 section 5; task 05).
// The transition table is world-free in VeloxMatchRules; this file feeds it from the world and applies the verdicts.

#include "AbilitySystemComponent.h"
#include "Camera/CameraActor.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameModes/VeloxGameMode.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxMatchEffects.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace VeloxGameModeMatchPrivate
{
	const TCHAR* const FragLimitOption = TEXT("FragLimit");
	const TCHAR* const TimeLimitOption = TEXT("TimeLimit");
}

double AVeloxGameMode::GetServerTime() const
{
	// On the server GetServerWorldTimeSeconds() is the world time; clients derive every clock from the same value.
	if (const AGameStateBase* State = GameState)
	{
		return State->GetServerWorldTimeSeconds();
	}
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void AVeloxGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	GameModeDefinition = GameModeDefinitionAsset.IsNull() ? nullptr : GameModeDefinitionAsset.LoadSynchronous();
	if (!GameModeDefinitionAsset.IsNull() && !GameModeDefinition)
	{
		UE_LOG(LogVeloxMatch, Error, TEXT("VeloxMatch %s: mode definition [%s] failed to load; match rules off"), *GetClass()->GetName(), *GameModeDefinitionAsset.ToString());
	}
	bMatchRulesEnabled = GameModeDefinition != nullptr;
	if (bMatchRulesEnabled)
	{
		MatchRules = GameModeDefinition->MakeRulesConfig();
		// Q3 fraglimit / timelimit as URL options (server command line, tests): ?FragLimit=3?TimeLimit=120 (seconds).
		MatchRules.FragLimit = FMath::Max(0, UGameplayStatics::GetIntOption(Options, VeloxGameModeMatchPrivate::FragLimitOption, MatchRules.FragLimit));
		MatchRules.TimeLimitSeconds = FMath::Max(0, UGameplayStatics::GetIntOption(Options, VeloxGameModeMatchPrivate::TimeLimitOption, MatchRules.TimeLimitSeconds));
	}
	// The GameState may already exist (AGameModeBase::PreInitializeComponents spawns it and calls InitGameState); hand
	// the rules over to it now as well. InitGameState covers the other order and every ResetLevel.
	ApplyMatchRulesToGameState();
	UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch init mode=%s definition=%s rules=%d frag_limit=%d time_limit=%d min_players=%d max_active=%d ready=%d countdown=%.1f overtime=%d post_match=%.1f forfeit=%d"),
		*GetClass()->GetName(), *GetNameSafe(GameModeDefinition), bMatchRulesEnabled ? 1 : 0, MatchRules.FragLimit, MatchRules.TimeLimitSeconds, MatchRules.MinPlayersToStart,
		GameModeDefinition ? GameModeDefinition->MaxActivePlayers : 0, MatchRules.bRequireReady ? 1 : 0, MatchRules.WarmupCountdownSeconds, static_cast<int32>(MatchRules.OvertimeRule),
		MatchRules.PostMatchSeconds, MatchRules.bLeaverForfeits ? 1 : 0);
}

void AVeloxGameMode::InitGameState()
{
	Super::InitGameState();
	ApplyMatchRulesToGameState();
}

void AVeloxGameMode::ApplyMatchRulesToGameState()
{
	if (AVeloxGameState* VeloxGameState = GetGameState<AVeloxGameState>())
	{
		VeloxGameState->InitMatchRules(GameModeDefinition, bMatchRulesEnabled ? MatchRules.FragLimit : 0, bMatchRulesEnabled ? MatchRules.TimeLimitSeconds : 0);
	}
}

bool AVeloxGameMode::ReadyToStartMatch_Implementation()
{
	if (!bMatchRulesEnabled)
	{
		return Super::ReadyToStartMatch_Implementation();
	}
	return !bDelayedStart && GetMatchState() == MatchState::WaitingToStart && GameState != nullptr;
}

void AVeloxGameMode::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	if (!bMatchRulesEnabled || bPhaseMachineStarted)
	{
		return;
	}
	bPhaseMachineStarted = true;
	SetPhase(EVeloxMatchPhase::Warmup, EVeloxMatchReason::None, 0.0, nullptr);
	GetWorldTimerManager().SetTimer(RulesTimer, this, &ThisClass::OnRulesTimer, FMath::Max(RulesTimerIntervalSeconds, 0.1f), true);
}

FString AVeloxGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	APlayerState* NewPS = NewPlayerController ? NewPlayerController->PlayerState.Get() : nullptr;
	if (!bMatchRulesEnabled || !NewPS || NewPS->IsOnlyASpectator())
	{
		if (NewPS && NewPS->IsOnlyASpectator())
		{
			UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch spectator player=%s reason=requested"), *AVeloxPlayerState::VeloxPlayerLabel(NewPS));
		}
		return Error;
	}
	// The new PlayerState is already in the PlayerArray; count the others.
	int32 Others = 0;
	for (const APlayerState* Other : GameState->PlayerArray)
	{
		Others += (Other != NewPS && IsActivePlayer(Other)) ? 1 : 0;
	}
	if (Others >= GameModeDefinition->MaxActivePlayers)
	{
		NewPlayerController->StartSpectatingOnly();
		UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch spectator player=%s reason=full active=%d max_active=%d"), *AVeloxPlayerState::VeloxPlayerLabel(NewPS), Others, GameModeDefinition->MaxActivePlayers);
	}
	return Error;
}

void AVeloxGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (bMatchRulesEnabled && NewPlayer)
	{
		const APlayerState* PS = NewPlayer->PlayerState;
		UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch join player=%s spectator=%d phase=%s active=%d"), *AVeloxPlayerState::VeloxPlayerLabel(PS), PS && PS->IsOnlyASpectator() ? 1 : 0,
			LexToString(Phase), GetNumActivePlayers());
		RequestMatchEvaluation();
	}
}

void AVeloxGameMode::Reset()
{
	Super::Reset();
	ClearAllPendingRespawns();
}

bool AVeloxGameMode::ShouldReset_Implementation(AActor* ActorToReset)
{
	return !ActorToReset->IsA<ASpectatorPawn>() && Super::ShouldReset_Implementation(ActorToReset);
}

void AVeloxGameMode::ClearAllPendingRespawns()
{
	if (UWorld* World = GetWorld())
	{
		for (TPair<TWeakObjectPtr<const AController>, FPendingRespawn>& Pair : PendingRespawns)
		{
			World->GetTimerManager().ClearTimer(Pair.Value.Timer);
		}
	}
	PendingRespawns.Reset();
}

bool AVeloxGameMode::IsActivePlayer(const APlayerState* PlayerState) const
{
	if (!PlayerState || PlayerState->IsOnlyASpectator() || PlayerState->IsInactive() || !IsValid(PlayerState))
	{
		return false;
	}
	for (const TWeakObjectPtr<const APlayerState>& Leaving : LeavingPlayerStates)
	{
		if (Leaving.Get() == PlayerState)
		{
			return false;
		}
	}
	return true;
}

int32 AVeloxGameMode::GetNumActivePlayers() const
{
	int32 Count = 0;
	if (const AGameStateBase* State = GameState)
	{
		for (const APlayerState* PS : State->PlayerArray)
		{
			Count += IsActivePlayer(PS) ? 1 : 0;
		}
	}
	return Count;
}

APlayerState* AVeloxGameMode::FindPlayerStateById(int32 PlayerId) const
{
	if (PlayerId == INDEX_NONE || !GameState)
	{
		return nullptr;
	}
	for (APlayerState* PS : GameState->PlayerArray)
	{
		if (PS && PS->GetPlayerId() == PlayerId)
		{
			return PS;
		}
	}
	return nullptr;
}

FVeloxMatchInput AVeloxGameMode::BuildMatchInput() const
{
	FVeloxMatchInput Input;
	Input.Rules = MatchRules;
	Input.Phase = Phase;
	Input.Now = GetServerTime();
	Input.PhaseEndTime = PhaseEndTime;
	Input.bActivePlayerLeft = bActivePlayerLeft;
	if (const AGameStateBase* State = GameState)
	{
		for (const APlayerState* PS : State->PlayerArray)
		{
			if (!IsActivePlayer(PS))
			{
				continue;
			}
			const AVeloxPlayerState* VeloxPS = Cast<AVeloxPlayerState>(PS);
			FVeloxMatchPlayer& Player = Input.Players.AddDefaulted_GetRef();
			Player.Id = PS->GetPlayerId();
			Player.Score = FMath::RoundToInt(PS->GetScore());
			Player.bHuman = !PS->IsABot();
			Player.bReady = VeloxPS && VeloxPS->IsReady();
		}
	}
	return Input;
}

void AVeloxGameMode::RequestMatchEvaluation()
{
	if (!bMatchRulesEnabled || bEvaluationPending || !GetWorld())
	{
		return;
	}
	bEvaluationPending = true;
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ThisClass::EvaluateMatch));
}

void AVeloxGameMode::OnRulesTimer()
{
	EvaluateMatch();
}

void AVeloxGameMode::EvaluateMatch()
{
	bEvaluationPending = false;
	if (!bMatchRulesEnabled || !bPhaseMachineStarted || bResettingLevel)
	{
		return;
	}
	LeavingPlayerStates.RemoveAll([](const TWeakObjectPtr<const APlayerState>& Leaving) { return !Leaving.IsValid(); });

	const FVeloxMatchVerdict Verdict = VeloxMatchRules::Evaluate(BuildMatchInput());
	bActivePlayerLeft = false;
	if (!Verdict.bChange)
	{
		return;
	}
	if (Verdict.bResetLevel)
	{
		RestartMatch(Verdict.NewPhase, Verdict.Reason, Verdict.PhaseEndTime);
	}
	else
	{
		SetPhase(Verdict.NewPhase, Verdict.Reason, Verdict.PhaseEndTime, FindPlayerStateById(Verdict.WinnerId));
	}
}

void AVeloxGameMode::SetPhase(EVeloxMatchPhase NewPhase, EVeloxMatchReason Reason, double InPhaseEndTime, APlayerState* Winner)
{
	const EVeloxMatchPhase OldPhase = Phase;
	const double Now = GetServerTime();
	Phase = NewPhase;
	PhaseEndTime = InPhaseEndTime;
	if (NewPhase == EVeloxMatchPhase::Live)
	{
		LiveStartTime = Now;
		OvertimeCount = 0;
	}
	else if (NewPhase == EVeloxMatchPhase::Overtime)
	{
		++OvertimeCount;
	}
	else if (NewPhase == EVeloxMatchPhase::Warmup)
	{
		LiveStartTime = 0.0;
		OvertimeCount = 0;
	}

	// Ready is per phase (ADR 0004 section 5), except that it carries from Warmup into Countdown: the countdown keeps
	// requiring it, so a player who un-readies aborts the countdown.
	const bool bKeepReady = OldPhase == EVeloxMatchPhase::Warmup && NewPhase == EVeloxMatchPhase::Countdown;
	if (!bKeepReady && GameState)
	{
		for (APlayerState* PS : GameState->PlayerArray)
		{
			if (AVeloxPlayerState* VeloxPS = Cast<AVeloxPlayerState>(PS))
			{
				VeloxPS->SetReady(false);
			}
		}
	}

	GetWorldTimerManager().ClearTimer(PhaseEndTimer);
	if (PhaseEndTime > 0.0)
	{
		// Exact edge for the countdown / time limit / intermission; the 1 Hz timer is only the safety net.
		GetWorldTimerManager().SetTimer(PhaseEndTimer, this, &ThisClass::OnRulesTimer, FMath::Max(static_cast<float>(PhaseEndTime - Now), KINDA_SMALL_NUMBER), false);
	}

	UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch phase=%s old=%s reason=%s now=%.3f end=%.3f duration=%.1f active=%d overtime=%d winner=%s"), LexToString(NewPhase), LexToString(OldPhase),
		LexToString(Reason), Now, PhaseEndTime, PhaseEndTime > 0.0 ? PhaseEndTime - Now : 0.0, GetNumActivePlayers(), OvertimeCount, *AVeloxPlayerState::VeloxPlayerLabel(Winner));

	if (NewPhase == EVeloxMatchPhase::PostMatch)
	{
		EnterPostMatch();
	}

	if (AVeloxGameState* VeloxGameState = GetGameState<AVeloxGameState>())
	{
		FVeloxMatchPhaseState State;
		State.Phase = NewPhase;
		State.PhaseStartServerTime = Now;
		State.PhaseEndServerTime = PhaseEndTime;
		State.LiveStartServerTime = LiveStartTime;
		State.OvertimeCount = OvertimeCount;
		State.Winner = NewPhase == EVeloxMatchPhase::PostMatch ? Winner : nullptr;
		VeloxGameState->SetMatchPhaseState(State);
	}
}

void AVeloxGameMode::RestartMatch(EVeloxMatchPhase NewPhase, EVeloxMatchReason Reason, double InPhaseEndTime)
{
	UWorld* World = GetWorld();
	if (!World || bResettingLevel)
	{
		return;
	}
	TGuardValue<bool> ResetGuard(bResettingLevel, true);
	const double StartSeconds = FPlatformTime::Seconds();
	ClearAllPendingRespawns();

	// The freeze is not part of any pawn: take it off the PlayerState ASCs (they survive the reset).
	int32 NumUnfrozen = 0;
	for (APlayerState* PS : GameState->PlayerArray)
	{
		const AVeloxPlayerState* VeloxPS = Cast<AVeloxPlayerState>(PS);
		if (UAbilitySystemComponent* ASC = VeloxPS ? VeloxPS->GetAbilitySystemComponent() : nullptr)
		{
			NumUnfrozen += ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(VeloxMatchTags::State_MatchFrozen));
		}
	}

	// Engine: ClientReset + Reset on every controller (pawns unpossessed), Reset on every other actor (APawn::Reset
	// destroys pawns and corpses, APlayerState::Reset zeroes the score), then this->Reset (pending respawns dropped).
	int32 NumPawnsBefore = 0;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		++NumPawnsBefore;
	}
	ResetLevel();

	// PostMatch left the engine match in WaitingPostMatch; StartMatch() refuses once a match has started.
	if (GetMatchState() != MatchState::InProgress)
	{
		SetMatchState(MatchState::InProgress);
	}

	int32 NumRestarted = 0;
	for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		if (!Controller || Controller->GetPawn() || !IsActivePlayer(Controller->PlayerState))
		{
			continue;
		}
		APlayerController* PC = Cast<APlayerController>(Controller);
		if (PC && !PlayerCanRestart(PC))
		{
			continue;
		}
		RestartPlayer(Controller);
	}
	// Human controllers may already have been restarted by AGameMode::HandleMatchHasStarted (match back to InProgress).
	int32 NumActive = 0;
	for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It)
	{
		const AController* Controller = It->Get();
		if (Controller && IsActivePlayer(Controller->PlayerState))
		{
			++NumActive;
			NumRestarted += Controller->GetPawn() ? 1 : 0;
		}
	}
	UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch reset_level pawns_before=%d unfrozen=%d spawned=%d/%d took_ms=%.2f"), NumPawnsBefore, NumUnfrozen, NumRestarted, NumActive,
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);

	// A Live time limit counts from now, after the reset.
	const double PhaseEnd = NewPhase == EVeloxMatchPhase::Live ? VeloxMatchRules::PhaseEndFor(MatchRules, NewPhase, GetServerTime()) : InPhaseEndTime;
	SetPhase(NewPhase, Reason, PhaseEnd, nullptr);
}

AActor* AVeloxGameMode::FindIntermissionCamera() const
{
	if (IntermissionCameraTag.IsNone())
	{
		return nullptr;
	}
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(IntermissionCameraTag))
		{
			return *It;
		}
	}
	return nullptr;
}

void AVeloxGameMode::EnterPostMatch()
{
	EndMatch();

	int32 NumFrozen = 0;
	for (APlayerState* PS : GameState->PlayerArray)
	{
		const AVeloxPlayerState* VeloxPS = Cast<AVeloxPlayerState>(PS);
		UVeloxAbilitySystemComponent* ASC = VeloxPS ? VeloxPS->GetVeloxAbilitySystemComponent() : nullptr;
		if (ASC && !ASC->HasMatchingGameplayTag(VeloxMatchTags::State_MatchFrozen))
		{
			const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UVeloxGE_MatchFrozen::StaticClass(), 1.f, ASC->MakeEffectContext());
			if (Spec.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
				++NumFrozen;
			}
		}
	}
	for (TActorIterator<AVeloxCharacter> It(GetWorld()); It; ++It)
	{
		if (UCharacterMovementComponent* Movement = It->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
	}

	AActor* Camera = FindIntermissionCamera();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AVeloxPlayerController* PC = Cast<AVeloxPlayerController>(It->Get());
		if (!PC)
		{
			continue;
		}
		AActor* ViewTarget = Camera ? Camera : PC->GetPawn();
		if (!PC->IsLocalController())
		{
			PC->EnterIntermission(ViewTarget);
		}
		PC->ClientEnterIntermission(ViewTarget);
	}
	UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch freeze asc=%d camera=%s match_state=%s"), NumFrozen, *GetNameSafe(Camera), *GetMatchState().ToString());
}

void AVeloxGameMode::SetPlayerReady(AController* Controller, bool bReady)
{
	AVeloxPlayerState* VeloxPS = Controller ? Controller->GetPlayerState<AVeloxPlayerState>() : nullptr;
	if (!VeloxPS || VeloxPS->IsOnlyASpectator())
	{
		return;
	}
	if (VeloxPS->SetReady(bReady))
	{
		UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch ready player=%s ready=%d phase=%s"), *AVeloxPlayerState::VeloxPlayerLabel(VeloxPS), bReady ? 1 : 0, LexToString(Phase));
		RequestMatchEvaluation();
	}
}

bool AVeloxGameMode::RequestJoin(APlayerController* Controller)
{
	APlayerState* PS = Controller ? Controller->PlayerState.Get() : nullptr;
	if (!PS || !PS->IsOnlyASpectator())
	{
		return false;
	}
	const int32 MaxActive = GameModeDefinition ? GameModeDefinition->MaxActivePlayers : TNumericLimits<int32>::Max();
	const bool bBetweenMatches = Phase == EVeloxMatchPhase::Warmup || Phase == EVeloxMatchPhase::PostMatch;
	const bool bPhaseAllows = !GameModeDefinition || GameModeDefinition->bAllowJoinDuringMatch || bBetweenMatches;
	const int32 Active = GetNumActivePlayers();
	if (Active >= MaxActive || !bPhaseAllows)
	{
		UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch join refused player=%s phase=%s active=%d max_active=%d"), *AVeloxPlayerState::VeloxPlayerLabel(PS), LexToString(Phase), Active, MaxActive);
		return false;
	}
	// Undo StartSpectatingOnly; AGameMode's counters follow MustSpectate (bOnlySpectator).
	--NumSpectators;
	++NumPlayers;
	PS->SetIsOnlyASpectator(false);
	Controller->bPlayerIsWaiting = true;
	if (PlayerCanRestart(Controller))
	{
		RestartPlayer(Controller);
	}
	UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch join slot player=%s phase=%s active=%d pawn=%s"), *AVeloxPlayerState::VeloxPlayerLabel(PS), LexToString(Phase), GetNumActivePlayers(),
		*GetNameSafe(Controller->GetPawn()));
	RequestMatchEvaluation();
	return true;
}

void AVeloxGameMode::ForcePhase(EVeloxMatchPhase NewPhase)
{
	if (!bMatchRulesEnabled || !bPhaseMachineStarted)
	{
		UE_LOG(LogVeloxMatch, Warning, TEXT("VeloxMatch Phase: match rules are off for %s (no mode definition; use ?game=FFA or Duel)"), *GetClass()->GetName());
		return;
	}
	const double Now = GetServerTime();
	switch (NewPhase)
	{
	case EVeloxMatchPhase::Warmup:
	case EVeloxMatchPhase::Live:
		RestartMatch(NewPhase, EVeloxMatchReason::Forced, 0.0);
		break;
	case EVeloxMatchPhase::PostMatch:
	{
		const FVeloxMatchInput Input = BuildMatchInput();
		SetPhase(NewPhase, EVeloxMatchReason::Forced, VeloxMatchRules::PhaseEndFor(MatchRules, NewPhase, Now), FindPlayerStateById(VeloxMatchRules::UniqueLeader(Input.Players)));
		break;
	}
	case EVeloxMatchPhase::Countdown:
	case EVeloxMatchPhase::Overtime:
		SetPhase(NewPhase, EVeloxMatchReason::Forced, VeloxMatchRules::PhaseEndFor(MatchRules, NewPhase, Now), nullptr);
		break;
	}
}
