// Velox - game mode definition.

#include "GameModes/VeloxGameModeDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameModeDefinition)

FVeloxMatchRulesConfig UVeloxGameModeDefinition::MakeRulesConfig() const
{
	FVeloxMatchRulesConfig Rules;
	Rules.FragLimit = FragLimit;
	Rules.TimeLimitSeconds = TimeLimitSeconds;
	Rules.MinPlayersToStart = MinPlayersToStart;
	Rules.bRequireReady = bRequireReady;
	Rules.WarmupCountdownSeconds = WarmupCountdownSeconds;
	Rules.OvertimeRule = OvertimeRule;
	Rules.OvertimeSeconds = OvertimeSeconds;
	Rules.PostMatchSeconds = PostMatchSeconds;
	Rules.bLeaverForfeits = bLeaverForfeits;
	return Rules;
}
