// Velox - bot difficulty preset (ADR 0005 section 11).

#include "Bots/VeloxBotDifficulty.h"

#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxBotDifficulty)

#if !UE_BUILD_SHIPPING
// Field table: every overridable POD field once; cvars velox.bots.<Field> are generated from it (pattern of velox.move.*).
#define VELOX_BOT_FLOAT_FIELDS(X) \
	X(ReactionSeconds) X(ReactionJitter) X(AimErrorDeg) X(ErrorCorrelationSeconds) X(TrackingSmoothingSeconds) X(MaxTurnRateDegPerSec) \
	X(LeadScale) X(FireConeDeg) X(DirectHitRadiusScale) X(FireReleaseConeScale) X(FireHoldAfterLostSeconds) X(PerceptionFovDeg) X(PerceptionRangeCm) X(PerceptionIntervalSeconds) X(EnemyMemorySeconds) \
	X(EstimatedTravelSpeed) X(ItemTimingSkill) X(MaxWaitSeconds) X(GoalReevaluateSeconds) X(GoalSwitchHysteresis) \
	X(FightItemBias) X(StrafeAmplitude) X(StrafeFlipSecondsMin) X(StrafeFlipSecondsMax) X(DodgeJumpChancePerSec) \
	X(ProjectileDodgeReactionSeconds) X(PreferredRange)

#define VELOX_BOT_BOOL_FIELDS(X) \
	X(bLeadProjectiles) X(bBunnyHop) X(bStrafeJump) X(bRocketJumps) X(bDodgeProjectiles)

namespace VeloxBotDifficultyDebug
{
	/** "Not overridden" marker of every field cvar. */
	constexpr float Unset = -1e30f;

#define VELOX_BOT_DECLARE_CVAR(Name) \
	static TAutoConsoleVariable<float> CVar_##Name(TEXT("velox.bots." #Name), Unset, \
		TEXT("Override of UVeloxBotDifficulty::" #Name " (-1e30 = use the asset). Non-shipping."), ECVF_Default);
	VELOX_BOT_FLOAT_FIELDS(VELOX_BOT_DECLARE_CVAR)
	VELOX_BOT_BOOL_FIELDS(VELOX_BOT_DECLARE_CVAR)
#undef VELOX_BOT_DECLARE_CVAR

	static void ApplyOverrides(FVeloxBotDifficultyParams& P)
	{
#define VELOX_BOT_APPLY_FLOAT(Name) { const float V = CVar_##Name.GetValueOnAnyThread(); if (V != Unset) { P.Name = V; } }
#define VELOX_BOT_APPLY_BOOL(Name) { const float V = CVar_##Name.GetValueOnAnyThread(); if (V != Unset) { P.Name = V > 0.5f; } }
		VELOX_BOT_FLOAT_FIELDS(VELOX_BOT_APPLY_FLOAT)
		VELOX_BOT_BOOL_FIELDS(VELOX_BOT_APPLY_BOOL)
#undef VELOX_BOT_APPLY_FLOAT
#undef VELOX_BOT_APPLY_BOOL
	}
}
#endif

FVeloxBotDifficultyParams UVeloxBotDifficulty::BuildParams() const
{
	FVeloxBotDifficultyParams P = BuildBaseParams();
#if !UE_BUILD_SHIPPING
	VeloxBotDifficultyDebug::ApplyOverrides(P);
#endif
	return P;
}

FVeloxBotDifficultyParams UVeloxBotDifficulty::BuildBaseParams() const
{
	FVeloxBotDifficultyParams P;
	P.ReactionSeconds = ReactionSeconds;
	P.ReactionJitter = ReactionJitter;
	P.AimErrorDeg = AimErrorDeg;
	P.ErrorCorrelationSeconds = ErrorCorrelationSeconds;
	P.TrackingSmoothingSeconds = TrackingSmoothingSeconds;
	P.MaxTurnRateDegPerSec = MaxTurnRateDegPerSec;
	P.bLeadProjectiles = bLeadProjectiles;
	P.LeadScale = LeadScale;
	P.FireConeDeg = FireConeDeg;
	P.DirectHitRadiusScale = DirectHitRadiusScale;
	P.FireReleaseConeScale = FireReleaseConeScale;
	P.FireHoldAfterLostSeconds = FireHoldAfterLostSeconds;

	P.PerceptionFovDeg = PerceptionFovDeg;
	P.PerceptionRangeCm = PerceptionRangeCm;
	P.PerceptionIntervalSeconds = PerceptionIntervalSeconds;
	P.EnemyMemorySeconds = EnemyMemorySeconds;

	P.bBunnyHop = bBunnyHop;
	P.bStrafeJump = bStrafeJump;
	P.bRocketJumps = bRocketJumps;
	P.EstimatedTravelSpeed = EstimatedTravelSpeed;

	P.ItemTimingSkill = ItemTimingSkill;
	P.MaxWaitSeconds = MaxWaitSeconds;
	P.GoalReevaluateSeconds = GoalReevaluateSeconds;
	P.GoalSwitchHysteresis = GoalSwitchHysteresis;
	P.FightItemBias = FightItemBias;

	P.StrafeAmplitude = StrafeAmplitude;
	P.StrafeFlipSecondsMin = StrafeFlipSecondsMin;
	P.StrafeFlipSecondsMax = FMath::Max(StrafeFlipSecondsMin, StrafeFlipSecondsMax);
	P.DodgeJumpChancePerSec = DodgeJumpChancePerSec;
	P.bDodgeProjectiles = bDodgeProjectiles;
	P.ProjectileDodgeReactionSeconds = ProjectileDodgeReactionSeconds;

	P.WeaponPreferences = WeaponPreferences;
	P.PreferredRange = PreferredRange;
	return P;
}

FPrimaryAssetId UVeloxBotDifficulty::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(FPrimaryAssetType(TEXT("VeloxBotDifficulty")), GetFName());
}
