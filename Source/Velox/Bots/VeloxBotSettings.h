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

	/**
	 * JUMP / JUMP_FAST take-off window (task M7 H11): from this far before the link start (along the link) a ledge ahead
	 * fires the jump (cm). Before H11 the jump fired at this radial distance from the start, up to 160 cm before the edge.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float TakeoffLeadCm = 100.f;

	/**
	 * Task M7 H11: without a ledge seen, the JUMP / JUMP_FAST take-off fires this far past the link start along the link
	 * (cm; authored starts sit 60 cm before the edge, the way back of a both-ways link 100).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float TakeoffMaxPastStartCm = 40.f;

	/**
	 * Task M7 H11: the take-off ledge probe traces down this far beyond this tick's step ahead of the feet along the link
	 * (cm): no floor there (within TakeoffLedgeDepthCm) is the edge, so the jump leaves about this much before it.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float TakeoffLedgeProbeCm = 25.f;

	/**
	 * Task M7 H11: a JUMP / JUMP_FAST take-off needs the along-link speed that lands this far short of the link end (cm;
	 * the smaller authoring inset); slower, the bot backs up along the link for a run-up (JumpFastRunUpCm / JumpFastRetries,
	 * clamped to the mesh behind the start).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float TakeoffLandInsetCm = 60.f;

	/**
	 * Task M7 H11: a JUMP / JUMP_FAST take-off with more speed across the link than this backs up for a straight run-up
	 * like a slow one (cm/s; Nemus: 350-470 across from Log_Land drifted off the 400 cm log). Near the start (within
	 * TakeoffLeadCm) an approach that is mostly across the link (more across than along, above this) backs up at once.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm/s"))
	float TakeoffMaxLateralCmS = 250.f;

	/**
	 * Task M7 H14: the landing zone of a JUMP / JUMP_FAST link is probed this far past the link end along the link (one
	 * nav raycast at the segment start, cm). A zone whose nav reaches this far is not short: no take-off speed cap.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float TakeoffLandProbeCm = 400.f;

	/**
	 * Task M7 H14: on a short landing zone (nav ends within TakeoffLandProbeCm past the link end) the take-off along-link
	 * speed is capped so that the flight lands this far short of the zone's far nav edge (cm; Fornax Isle_In: 300 cm
	 * island, bots took off at 850-1000 cm/s from a bunny hop and flew past it). Excess speed is shed on the ground.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float TakeoffLandFarInsetCm = 40.f;

	/**
	 * Task M7 H14: on the plain segment into a short-landing gap link, from this far before the link start (along the
	 * link) the bot does not hop (a hop landing at the take-off carries its bunny-hop speed into the jump) and coasts
	 * (no wish: ground friction) while its along-link speed is above the cap (cm).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float TakeoffBrakeLeadCm = 250.f;

	/** Task M7 H11: depth of the take-off ledge probe below the feet (cm; deeper than a stair step). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "1.0", Units = "cm"))
	float TakeoffLedgeDepthCm = 60.f;

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

	/**
	 * ADR 0008 G2, task M7 H8 (review F6): horizontal half extent of the edge probe's nav projection (cm). The agent radius:
	 * the mesh is eroded by it, so "projects" means "the capsule centre is over floor". GetOnMeshExtent's 100 accepted
	 * points about 55 cm past a ledge. The vertical half extent is OnMeshHalfHeightCm.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "1.0", Units = "cm"))
	float EdgeProbeProjectRadiusCm = 45.f;

	/** 3.6.1: the edge probe starts where the current horizontal velocity carries the bot in this time (s). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "s"))
	float EdgeProbeLookaheadSeconds = 0.25f;

	/** 3.6.1: the edge probe runs at most this often; its verdict is cached between probes (s; like the D18 headroom sweep). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "s"))
	float EdgeProbeCheckSeconds = 0.1f;

	/**
	 * ADR 0008 3.6.2 hop landing guard (extends D18; only on maps with a hazard volume): on the ground a hop (path bunny
	 * hop or overlay dodge jump) is refused when there is no ground under Feet + Velocity2D x (the bot's own hop air time +
	 * this) within the slope window (H8, VeloxBotCombat::HopGroundWindow), or that ground does not project onto the nav
	 * mesh within (HopLandingProjectRadiusCm, HopLandingProjectRadiusCm, HopLandingProjectHalfHeightCm), or is lava (s; the
	 * slack covers the air-strafe gain of the hop). The verdict is cached EdgeProbeCheckSeconds per grounded stretch.
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
	 * H8 (review F5): vertical half extent of the landing projection around the ground found under the landing (cm). The
	 * ground is found by a trace first, so this only bridges nav poly height (cell height, step) above the surface.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "1.0", Units = "cm"))
	float HopLandingProjectHalfHeightCm = 50.f;

	/**
	 * H8 (review F4): a cached hop landing verdict is reused only while the predicted landing is within this distance of the
	 * landing it was computed for (cm; the projection radius plus a margin: a strafe flip moves it by metres).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "cm"))
	float HopLandingRecheckCm = 30.f;

	/**
	 * Lava avoidance (ADR 0008 G5(b), tasks M7 H7 / H8): UVeloxBotSubsystem marks the nav polys inside every Lava volume's own
	 * footprint (no horizontal padding: H8 review F1, a padded area swallowed Fornax's slag bridge and every lava-hop link end)
	 * with UVeloxNavArea_Hazard, from the bed up to this far above the lava surface. Below the generator's 20 cm minimum drop
	 * from a rim to the surface, so no rim or bridge poly is marked (cm). Applied at load when velox.bots.EdgeAware is on
	 * (A/B: -dpcvars=velox.bots.EdgeAware=0).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", ClampMax = "19.0", Units = "cm"))
	float HazardNavTopCm = 10.f;

	/**
	 * H7: the edge probe and the hop landing guard trace this far down from the probe point (WorldStatic) and reject it when
	 * the first static surface under it is a hazard (lava top or bed); deep enough for a probe at hop apex (~110 cm above the
	 * floor) over a bed 60 cm below it, shallow enough to stop above Orbita's void (cm).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "1.0", Units = "cm"))
	float HazardTraceDepthCm = 300.f;

	/**
	 * Task M7 H13 ground edge guard (hazard maps): every grounded tick off a link the point Feet + Velocity2D x this +
	 * Wish x EdgeGuardWishCm is tested (VeloxBotCombatGlue::IsUnsafeGround); unsafe -> the wish turns inward (toward the
	 * nearest nav point) and no hop. At 880 cm/s the guard acts about 130 cm before the deck edge; VQ3 ground braking
	 * (friction 6, accel 10) stops that speed within about 50 cm (s).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "s"))
	float EdgeGuardLookaheadSeconds = 0.15f;

	/** H13: the wish's share of the guard probe (a bot at rest accelerating toward the edge; cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "cm"))
	float EdgeGuardWishCm = 40.f;

	/** H13: the guard is off within this distance of the start of a nav link the current segment leads into (drop / gap take-offs; cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "cm"))
	float EdgeGuardLinkExemptCm = 300.f;

	/**
	 * H13: IsUnsafeGround's void test: a point off the mesh with Pawn-blocking floor within this depth under it is a ledge,
	 * not the void (Orbita: the pylon tops 460 cm over the hub floor; the decks lie 1100+ cm above the void's kill depth; cm).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "1.0", Units = "cm"))
	float EdgeGuardDropDepthCm = 800.f;

	/** H13: the guard's inward search: the nearest nav point to the probe within this horizontal extent (cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "1.0", Units = "cm"))
	float EdgeGuardInwardExtentCm = 300.f;

	/**
	 * H13 airborne hop guard: in the bot's own hop with jump held, the re-jump of the landing frame (bAutoHop) is released
	 * when the next hop's landing (touchdown + Velocity2D x hop air time) is unsafe; judged once the touchdown is within
	 * this time (s).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Hazard", Meta = (ClampMin = "0.0", Units = "s"))
	float AirHopGuardSeconds = 0.25f;

	/** A bot tick above this controller time is logged once (microseconds). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Debug", Meta = (ClampMin = "0.0"))
	float BudgetWarnMicroseconds = 600.f;

	/** Master random seed; 0 seeds from the clock. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Debug")
	int32 DeterministicSeed = 0;
};
