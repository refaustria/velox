// Velox - match phase rules (ADR 0004 section 5). World-free: AVeloxGameMode builds an FVeloxMatchInput from the world on
// every kill, join, leave, ready change, phase end and its 1 Hz timer, and applies the verdict. Unit test:
// Velox.GameMode.MatchRules (Tests/VeloxMatchRulesTest.cpp).

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

#include "VeloxMatchRules.generated.h"

VELOX_API DECLARE_LOG_CATEGORY_EXTERN(LogVeloxMatch, Log, All);

/** Match phase, owned by the server (AVeloxGameMode::SetPhase), replicated by AVeloxGameState::Phase. */
UENUM(BlueprintType)
enum class EVeloxMatchPhase : uint8
{
	/** Free play before a match: players spawn and frag, scores are wiped by the reset at the countdown end (Q3 warmup). */
	Warmup,
	/** Enough players (and all ready where required): counting down to the reset and Live. */
	Countdown,
	Live,
	/** Tie at the time limit (Duel sudden death, or timed overtime). */
	Overtime,
	/** Intermission: everyone frozen, scoreboard forced open, no respawns; then back to Warmup. */
	PostMatch,
};

/** What happens on a tie at the time limit. */
UENUM(BlueprintType)
enum class EVeloxOvertimeRule : uint8
{
	/** Ties stand: the match ends without a winner (FFA). */
	None,
	/** The next frag that leaves a unique leader ends the match (Duel). */
	SuddenDeath,
	/** OvertimeSeconds more, then the time-limit rule again (repeats). */
	Timed,
};

/** Who sees item respawn timers in the HUD (ADR 0004 section 4); applied by the HUD view model on the client. */
UENUM(BlueprintType)
enum class EVeloxItemTimerVisibility : uint8
{
	Nobody,
	Spectators,
	Everyone,
};

/** Why the phase changed (log lines, HUD banners later). */
enum class EVeloxMatchReason : uint8
{
	None,
	/** Warmup -> Countdown: enough players (and ready). */
	PlayersReady,
	/** Countdown -> Warmup: the start condition stopped holding. */
	StartConditionLost,
	/** Countdown end -> Live (with the level reset). */
	CountdownDone,
	FragLimit,
	TimeLimit,
	/** Time limit reached on a tie with EVeloxOvertimeRule::None. */
	Draw,
	/** Time limit tie -> Overtime, or timed overtime extended. */
	Overtime,
	/** A frag in sudden death left a unique leader. */
	SuddenDeath,
	/** An active player left a Duel; the remaining player wins. */
	Forfeit,
	/** Live/Overtime without any active player: back to Warmup. */
	Empty,
	/** PostMatch -> Warmup after PostMatchSeconds. */
	IntermissionDone,
	/** PostMatch -> Warmup because every human player is ready. */
	AllReady,
	/** Console command (velox.match.Phase). */
	Forced,
};

VELOX_API const TCHAR* LexToString(EVeloxMatchPhase Phase);
VELOX_API const TCHAR* LexToString(EVeloxMatchReason Reason);
/** Case-insensitive phase name -> phase; false if unknown. */
VELOX_API bool LexTryParseString(EVeloxMatchPhase& OutPhase, const TCHAR* Name);

/** The effective rules of one match: UVeloxGameModeDefinition values plus URL overrides (?FragLimit=, ?TimeLimit=). */
struct FVeloxMatchRulesConfig
{
	/** 0 = no frag limit. */
	int32 FragLimit = 0;
	/** 0 = no time limit. */
	int32 TimeLimitSeconds = 0;
	int32 MinPlayersToStart = 2;
	bool bRequireReady = false;
	float WarmupCountdownSeconds = 20.f;
	EVeloxOvertimeRule OvertimeRule = EVeloxOvertimeRule::None;
	float OvertimeSeconds = 120.f;
	float PostMatchSeconds = 15.f;
	bool bLeaverForfeits = false;
};

/** One active (non-spectator) player as the rules see it. */
struct FVeloxMatchPlayer
{
	/** Opaque id chosen by the caller (the game mode uses the PlayerState's PlayerId). */
	int32 Id = INDEX_NONE;
	int32 Score = 0;
	bool bReady = false;
	/** Humans gate ready-up; AI controllers (bots, dummies) always count as ready, as in Q3. */
	bool bHuman = true;
};

struct FVeloxMatchInput
{
	FVeloxMatchRulesConfig Rules;
	EVeloxMatchPhase Phase = EVeloxMatchPhase::Warmup;
	/** Server world time now and the current phase's end (0 = open-ended). */
	double Now = 0.0;
	double PhaseEndTime = 0.0;
	/** Active players only (spectators excluded). */
	TArray<FVeloxMatchPlayer> Players;
	/** An active player left since the last evaluation (forfeit rule). */
	bool bActivePlayerLeft = false;
};

struct FVeloxMatchVerdict
{
	/** False: stay in the current phase. */
	bool bChange = false;
	EVeloxMatchPhase NewPhase = EVeloxMatchPhase::Warmup;
	EVeloxMatchReason Reason = EVeloxMatchReason::None;
	/** The new phase's end time (0 = open-ended). */
	double PhaseEndTime = 0.0;
	/** Entering Live or Warmup from Countdown end / PostMatch / Empty: reset the level and restart every active player. */
	bool bResetLevel = false;
	/** PostMatch only: the winner's id (INDEX_NONE = draw). */
	int32 WinnerId = INDEX_NONE;
};

namespace VeloxMatchRules
{
	/** Every human active player is ready (vacuously true without humans); AI players always count as ready. */
	VELOX_API bool AllHumansReady(const TArray<FVeloxMatchPlayer>& Players);
	/** Number of human active players. */
	VELOX_API int32 NumHumans(const TArray<FVeloxMatchPlayer>& Players);
	/** Warmup -> Countdown condition: ActivePlayers >= MinPlayersToStart and (!bRequireReady or every human is ready). */
	VELOX_API bool CanStartCountdown(const FVeloxMatchRulesConfig& Rules, const TArray<FVeloxMatchPlayer>& Players);
	/** Id of the player with the strictly highest score; INDEX_NONE on a tie or without players. */
	VELOX_API int32 UniqueLeader(const TArray<FVeloxMatchPlayer>& Players);
	/** Highest score (0 without players). */
	VELOX_API int32 TopScore(const TArray<FVeloxMatchPlayer>& Players);
	/** Phase end time of NewPhase entered at Now (Live: time limit, Countdown: warmup countdown, ...; 0 = open-ended). */
	VELOX_API double PhaseEndFor(const FVeloxMatchRulesConfig& Rules, EVeloxMatchPhase NewPhase, double Now);
	/** The ADR 0004 section 5 transition table. Pure; the caller applies the verdict. */
	VELOX_API FVeloxMatchVerdict Evaluate(const FVeloxMatchInput& Input);
}
