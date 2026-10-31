// Velox - game mode definition (ADR 0004 section 5): FFA and Duel differ only in these values. Assets
// /Game/Data/DA_GameMode_FFA and DA_GameMode_Duel are created by Tools/editor/create_item_content.py.

#pragma once

#include "Engine/DataAsset.h"
#include "GameModes/VeloxMatchRules.h"
#include "GameplayTagContainer.h"

#include "VeloxGameModeDefinition.generated.h"

class UVeloxCombatSettings;
class UVeloxPawnData;

/** What a dead human sees until the respawn (ADR 0004 C11, ADR 0003 B43). */
UENUM(BlueprintType)
enum class EVeloxDeathCamera : uint8
{
	/** ADR 0003 section 8: the view target is the corpse (third person, looking down at the body). */
	Corpse,
	/** The engine's spectating state with an AVeloxSpectatorPawn (client-local free fly), the view point clamped by the server. */
	FreeSpectate,
};

UCLASS(BlueprintType, Const)
class VELOX_API UVeloxGameModeDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Velox.Mode.FFA / Velox.Mode.Duel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Mode")
	FGameplayTag ModeTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Mode")
	FText DisplayName;

	/** Frags that end the match at once (0 = none; FFA 20, Duel 0). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules", meta = (ClampMin = "0"))
	int32 FragLimit = 20;

	/** Live length in seconds (0 = none). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules", meta = (ClampMin = "0", Units = "s"))
	int32 TimeLimitSeconds = 600;

	/** Active players needed for the countdown (AI players count). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules", meta = (ClampMin = "1"))
	int32 MinPlayersToStart = 2;

	/** Further joiners become spectators (FFA 16, Duel 2). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules", meta = (ClampMin = "1"))
	int32 MaxActivePlayers = 16;

	/** Every human active player must be ready before the countdown (Duel). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules")
	bool bRequireReady = false;

	/** Warmup countdown before the reset and Live (Q3 g_warmup). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules", meta = (ClampMin = "0", Units = "s"))
	float WarmupCountdownSeconds = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules")
	EVeloxOvertimeRule OvertimeRule = EVeloxOvertimeRule::None;

	/** Length of one timed overtime period (EVeloxOvertimeRule::Timed only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules", meta = (ClampMin = "1", Units = "s"))
	float OvertimeSeconds = 120.f;

	/** Intermission length; every human pressing ready ends it early. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules", meta = (ClampMin = "0", Units = "s"))
	float PostMatchSeconds = 15.f;

	/**
	 * A spectator may take a free slot (join) during Countdown/Live/Overtime. Off in Duel: joins only in Warmup and PostMatch
	 * (ADR 0004 section 5 states the Duel rule; this field makes it data instead of a mode check).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules")
	bool bAllowJoinDuringMatch = true;

	/** An active player leaving Live/Overtime hands the win to the remaining player (Duel). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules")
	bool bLeaverForfeits = false;

	/** Weapons stay after pickup (CTF in M7). Read by the items (task 01). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Items")
	bool bWeaponStay = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Items", meta = (ClampMin = "0", Units = "s"))
	float WeaponRespawnSeconds = 5.f;

	/** Multiplies every item's respawn time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Items", meta = (ClampMin = "0"))
	float ItemRespawnScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Items")
	EVeloxItemTimerVisibility ItemTimers = EVeloxItemTimerVisibility::Spectators;

	/** Death camera of dead humans (C11; q16 default FreeSpectate in FFA and Duel). Bots and dummies are not affected. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Rules")
	EVeloxDeathCamera DeathCamera = EVeloxDeathCamera::FreeSpectate;

	/** ADR 0003 B1: replaces the default combat settings for this mode (null = default). Replicated by the GameState. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Overrides")
	TObjectPtr<const UVeloxCombatSettings> CombatSettingsOverride;

	/** Null = the character's default pawn data. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Overrides")
	TObjectPtr<const UVeloxPawnData> PawnDataOverride;

	/** The phase-rule subset of this definition. */
	FVeloxMatchRulesConfig MakeRulesConfig() const;
};
