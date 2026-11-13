// Velox - project-wide bot settings (ADR 0005 section 11). Config = Game, so DefaultGame.ini
// [/Script/Velox.VeloxBotSettings] overrides the defaults below. Per-preset values live in UVeloxBotDifficulty.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "VeloxBotSettings.generated.h"

class UVeloxBotDifficulty;

UCLASS(Config = Game, DefaultConfig, Meta = (DisplayName = "Velox Bots"))
class VELOX_API UVeloxBotSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UVeloxBotSettings();

	//~UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return TEXT("Velox"); }
	//~End of UDeveloperSettings interface

	/** The project's settings (class defaults are valid without a config file, so this never returns null). */
	static const UVeloxBotSettings& Get();

	/** Logs the UVeloxBotDifficulty assets under /Game/Bots found through the asset registry (velox.bots.ListPresets). Returns the count. */
	static int32 ListPresets();

	/** Preset used by AddBot without a difficulty and by ?Bots=N without ?BotDifficulty. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots")
	TSoftObjectPtr<UVeloxBotDifficulty> DefaultDifficulty;

	/** Names handed to new bots in order (a number suffix is added when exhausted). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots")
	TArray<FString> BotNames;

	/** Bots added at map start when the URL has no ?Bots=. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots", Meta = (ClampMin = "0"))
	int32 DefaultBotCount = 0;

	/** Item candidates that get a real path query per goal evaluation (ADR 0005 section 7; float per the ADR table). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "1.0"))
	float PathQueryCandidates = 4.f;

	/** Roam goal: radius around the bot where a random navigable point is picked (cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "0.0", Units = "cm"))
	float RoamRadiusCm = 3000.f;

	/** Fight score at full strength and the best weapon (item-score units, value per second; ADR 0005 D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "0.0"))
	float FightBaseScore = 12.f;

	/** Health + armor at which the fight score saturates (D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "1.0"))
	float FightFullStrength = 200.f;

	/** Fight score factor with the weakest weapon (WeaponStrength 0; D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FightWeakWeaponScale = 0.25f;

	/**
	 * Need of an unowned weapon for a bot whose WeaponStrength is 1 (ADR 0005 D22): Need = lerp(1, this, WeaponStrength),
	 * so an armed bot stops hoarding weapons while an MG-only bot still goes and arms.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnownedWeaponNeedFloor = 0.3f;

	/** Pad / rocket jump: arrival radius at the link start (cm; ADR 0005 section 4, D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float LinkStartAcceptanceCm = 30.f;

	/** JumpFast: jump only at >= the link's RequiredSpeed x this (D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JumpFastSpeedFraction = 0.9f;

	/** JumpFast: back-up distance from the link start before a retry (cm; D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float JumpFastRunUpCm = 600.f;

	/** JumpFast: retries after a too-slow approach (D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0"))
	int32 JumpFastRetries = 1;

	/** RocketJump: abort when the explosion did not launch the pawn within this (s; D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.01", Units = "s"))
	float RocketJumpImpulseTimeoutSeconds = 0.3f;

	/** Distance to a nav link end at which the traversal counts as done (cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float LinkEndAcceptanceCm = 150.f;

	/**
	 * Task M7 H7: in the flight of a JUMP_PAD traversal (after the pad's own launch) the bot steers its horizontal velocity
	 * toward the ballistic one that lands on the link end (the rocket jump's air braking); an error below this needs no
	 * steering (cm/s). The old wish "toward End at scale 1" air-accelerated pads launched slower than AirMaxWishSpeed (800):
	 * Sacrarium's PadSouth (679 cm/s) flew at 820-900 and overshot its 400 cm loft. On hazard maps the controller still
	 * zeroes the input afterwards (ADR 0008 3.6.3 pad discipline).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm/s"))
	float PadArriveDeadbandCmS = 30.f;

	/** Distance before a link start at which the takeoff maneuver begins (cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float TakeoffLeadCm = 100.f;

	/**
	 * ADR 0005 D28: a link traversal (or the wait for the ground before it) running this long is aborted with
	 * reason=timeout and counts as a stuck strike (s; the longest legitimate traversal is ~3 s).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.5", Units = "s"))
	float LinkTimeoutSeconds = 6.f;

	/** D28: on the ground in a link's Approach / BackUp more than this below the link start aborts it (reason=below_start; cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float LinkBelowStartCm = 100.f;

	/** D29: on the ground no bunny hop while the velocity points more than this away from the target (degrees). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float MaxHopHeadingDeg = 45.f;

	/** D29: below this horizontal speed the hop heading gate does not apply (cm/s). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm/s"))
	float HopHeadingMinSpeedCmS = 100.f;

	/** No progress for this long counts as stuck (s). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.1", Units = "s"))
	float StuckSeconds = 2.f;

	/**
	 * ADR 0005 D35: while following a plain path segment the follower checks the straight line from the feet (projected
	 * onto the nav mesh) to the segment target with a nav-mesh raycast at most this often; a blocked line lands the bot
	 * and requests a repath (s).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.05", Units = "s"))
	float SegmentBlockCheckSeconds = 0.25f;

	/** D35: a nav raycast hit within this of the segment target is a grazed corner, not a blocked segment (cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float SegmentBlockClearanceCm = 100.f;

	/**
	 * D41: vertical half extent of every grounded "am I on the nav mesh" projection (cm): the controller's off-mesh test,
	 * the item router's ground start projection and the D35 segment check. Smaller than the engine's default query
	 * extent (200) so a thin walkable surface overhead (ramp, catwalk) is not mistaken for the floor under the feet;
	 * stairs and ramps still project, their polys follow the surface within step height. The horizontal half extent is
	 * UVeloxBotItemRouter::AirborneProjectRadiusCm; UVeloxBotItemRouter::GetOnMeshExtent() builds the one extent all three use.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "10.0", Units = "cm"))
	float OnMeshHalfHeightCm = 100.f;

	/**
	 * ADR 0008 3.6.1 edge-aware overlay (only on maps with a hazard volume): the combat overlay's wish is probed at
	 * Feet + Velocity2D x EdgeProbeLookaheadSeconds + Wish x this; a probe that does not project onto the nav mesh with
	 * UVeloxBotItemRouter::GetOnMeshExtent() tries the mirrored lateral, then drops lateral and retreat (cm).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "cm"))
	float EdgeProbeCm = 150.f;

	/** 3.6.1: the edge probe starts where the current horizontal velocity carries the bot in this time (s). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "s"))
	float EdgeProbeLookaheadSeconds = 0.25f;

	/** 3.6.1: the edge probe runs at most this often; its verdict is cached between probes (s; like the D18 headroom sweep). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "s"))
	float EdgeProbeCheckSeconds = 0.1f;

	/**
	 * ADR 0008 3.6.2 hop landing guard (extends D18; only on maps with a hazard volume): on the ground a hop (path bunny
	 * hop or overlay dodge jump) is refused when Feet + Velocity2D x (the bot's own hop air time + this) does not project
	 * onto the nav mesh within (HopLandingProjectRadiusCm, HopLandingProjectRadiusCm, OnMeshHalfHeightCm) (s; the slack
	 * covers the air-strafe gain of the hop).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "s"))
	float HopLandingSlackSeconds = 0.1f;

	/**
	 * 3.6.2: horizontal half extent of the landing projection (cm). About one nav cell (19), not GetOnMeshExtent's 100:
	 * the mesh is eroded by the agent radius (45), so a point 100 cm off the mesh is ~55 cm beyond the ledge and would
	 * still "project" (FtBotEdge: a 0.73 s hop at 800 cm/s landed 20 cm past the platform edge with the wide extent).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "1.0", Units = "cm"))
	float HopLandingProjectRadiusCm = 20.f;

	/**
	 * Task M7 H7 lava avoidance: every Lava AVeloxHazardVolume marks the nav polys inside its box grown by this much
	 * horizontally with UVeloxNavArea_Hazard (paths keep this far off the channel rim; the floor mesh at a ledge is only eroded
	 * by the agent radius, 45) (cm). Applied at load when velox.bots.EdgeAware is on (A/B: -dpcvars=velox.bots.EdgeAware=0).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "cm"))
	float HazardNavPaddingCm = 100.f;

	/** H7: the hazard nav modifier reaches this far below the volume (the bed's polys sit on its bottom) and above it (the rim, cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "cm"))
	float HazardNavPaddingZCm = 80.f;

	/**
	 * H7: the edge probe and the hop landing guard trace this far down from the probe point (WorldStatic) and reject it when
	 * the first static surface under it is a hazard (lava top or bed); deep enough for a probe at hop apex (~110 cm above the
	 * floor) over a bed 60 cm below it, shallow enough to stop above Orbita's void (cm).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "1.0", Units = "cm"))
	float HazardTraceDepthCm = 300.f;

	/** A bot tick above this controller time is logged once (microseconds). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Debug", Meta = (ClampMin = "0.0"))
	float BudgetWarnMicroseconds = 600.f;

	/** Master random seed; 0 seeds from the clock. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Debug")
	int32 DeterministicSeed = 0;
};
