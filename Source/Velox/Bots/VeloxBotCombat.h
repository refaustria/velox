// Velox - bot combat (ADR 0005 sections 6, 8, 9, 10; task M4-05): perception (FOV + world-only LOS on the Weapon
// channel, damage "hearing", enemy memory), target choice, the fire rule, the strafe / dodge / range-keeping overlay and
// projectile dodging. Two layers:
//   VeloxBotCombat      world-free rules over plain structs (unit-tested in Velox.Bots.CombatRules / ShouldFire).
//   VeloxBotCombatGlue  thin world glue the controller's Perceive / UpdateCombat / Act phases call: it reads the world
//                       (pawns, traces, inventory, projectiles) and writes only the bot's pending FVeloxBotInput; the pawn
//                       is still driven exclusively through AVeloxBotController::ApplyInput (section 1).
// Bots fire with ViewServerTime = Now through the authority-local fire path: zero rewind (section 8).

#pragma once

#include "CoreMinimal.h"
#include "Bots/VeloxAimModel.h"
#include "Bots/VeloxBotTypes.h"
#include "Engine/DeveloperSettings.h"
#include "Math/RandomStream.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "VeloxBotCombat.generated.h"

class AVeloxBotController;
class AVeloxCharacter;
struct FVeloxBotInput;
struct FVeloxBotNavOutput;

/** Combat tunables that are not per preset (DefaultGame.ini [/Script/Velox.VeloxBotCombatSettings]). */
UCLASS(Config = Game, DefaultConfig, Meta = (DisplayName = "Velox Bot Combat"))
class VELOX_API UVeloxBotCombatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	//~UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return TEXT("Velox"); }
	//~End of UDeveloperSettings interface

	static const UVeloxBotCombatSettings& Get();

	// --- Perception (section 6) ---

	/** Perception updates of consecutive BotIndex values are this far apart inside one interval (s; one 60 Hz tick). */
	UPROPERTY(Config, EditAnywhere, Category = "Perception", Meta = (ClampMin = "0", Units = "s"))
	float PerceptionStaggerSeconds = 1.f / 60.f;

	/** Second LOS point above the capsule centre (cm; the head of the 70 cm half-height capsule). */
	UPROPERTY(Config, EditAnywhere, Category = "Perception", Meta = (ClampMin = "0", Units = "cm"))
	float HeadOffsetCm = 55.f;

	/** A visible target is kept unless another visible enemy is closer than its distance / TargetStickiness. */
	UPROPERTY(Config, EditAnywhere, Category = "Perception", Meta = (ClampMin = "1"))
	float TargetStickiness = 1.5f;

	// --- Fire rule (section 8) ---

	/** Splash weapons: the aim ray's world hit and the target must both be farther than SplashRadius + this (cm). */
	UPROPERTY(Config, EditAnywhere, Category = "Fire", Meta = (ClampMin = "0", Units = "cm"))
	float SelfSplashMarginCm = 50.f;

	/** Splash weapons may fire when the aim ray's world hit lies within SplashRadius x this of the target ("ground rockets"). */
	UPROPERTY(Config, EditAnywhere, Category = "Fire", Meta = (ClampMin = "0", ClampMax = "1"))
	float SplashHitFraction = 0.8f;

	/**
	 * Direct-hit weapons (no splash) also need the aim ray to pass within this of the desired point (cm; capsule radius
	 * 37.5 + margin): Q3 BotCheckAttack's "the trace would hit the enemy" test. 0 = cone only. Saves ammo at long range,
	 * where the preset cone is wider than the target.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Fire", Meta = (ClampMin = "0", Units = "cm"))
	float DirectHitRadiusCm = 60.f;

	/** Splash weapons aim this far below the target's capsule centre (at the feet: Q3 bots ground their rockets), cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Fire", Meta = (ClampMin = "0", Units = "cm"))
	float SplashAimDropCm = 50.f;

	// --- Weapon choice (section 9) ---

	/** Weapon choice re-evaluation interval (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Weapons", Meta = (ClampMin = "0", Units = "s"))
	float WeaponChoiceIntervalSeconds = 0.25f;

	/** The held weapon's score multiplier (a switch costs 0.45 s). */
	UPROPERTY(Config, EditAnywhere, Category = "Weapons", Meta = (ClampMin = "1"))
	float SwitchHysteresis = 1.25f;

	/** Splash weapons score 0 closer than SplashRadius + this (cm). */
	UPROPERTY(Config, EditAnywhere, Category = "Weapons", Meta = (ClampMin = "0", Units = "cm"))
	float WeaponSplashSafetyMarginCm = 100.f;

	// --- Overlay (section 10) ---

	/** Weight of the path direction when the overlay is blended with an item path (an item goal is active). */
	UPROPERTY(Config, EditAnywhere, Category = "Overlay", Meta = (ClampMin = "0"))
	float FightPathWeight = 0.5f;

	/** Weight of the radial range-keeping component (toward / away from the enemy outside the weapon's best band). */
	UPROPERTY(Config, EditAnywhere, Category = "Overlay", Meta = (ClampMin = "0"))
	float RangeKeepWeight = 0.6f;

	/** Farther than this from the enemy the overlay closes in at full weight (cm). */
	UPROPERTY(Config, EditAnywhere, Category = "Overlay", Meta = (ClampMin = "0", Units = "cm"))
	float FightLeashCm = 2500.f;

	/** Nav raycast length of the strafe direction at each flip (cm): a pit or a wall flips the strafe side. */
	UPROPERTY(Config, EditAnywhere, Category = "Overlay", Meta = (ClampMin = "0", Units = "cm"))
	float StrafeProbeCm = 300.f;

	/** A dodge jump holds the key this long (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Overlay", Meta = (ClampMin = "0", Units = "s"))
	float DodgeJumpHoldSeconds = 0.1f;

	/** Damage from an unseen attacker keeps the bot strafing toward it for this long (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Overlay", Meta = (ClampMin = "0", Units = "s"))
	float UnderFireSeconds = 1.5f;

	// --- Projectile dodge (section 10) ---

	/** Projectiles are considered when their closest approach is within this (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Dodge", Meta = (ClampMin = "0", Units = "s"))
	float ProjectileDodgeHorizonSeconds = 1.f;

	/** ... and closer than SplashRadius + this (cm). */
	UPROPERTY(Config, EditAnywhere, Category = "Dodge", Meta = (ClampMin = "0", Units = "cm"))
	float ProjectileDodgeMarginCm = 100.f;

	/** A projectile dodge overrides the lateral wish this long (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Dodge", Meta = (ClampMin = "0", Units = "s"))
	float ProjectileDodgeHoldSeconds = 0.4f;

	// --- Rocket jump (section 4) ---

	/** Rocket-jump flight: a horizontal velocity error below this needs no air steering (cm/s). */
	UPROPERTY(Config, EditAnywhere, Category = "RocketJump", Meta = (ClampMin = "0", Units = "cm/s"))
	float RocketJumpArriveDeadbandCmS = 30.f;

	// --- Budget (section 13) ---

	/** VeloxBotBudget log interval (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", Meta = (ClampMin = "0.5", Units = "s"))
	float BudgetLogIntervalSeconds = 5.f;
};

// -------------------------------------------------------------------------------------------- world-free rules

/** Why the fire rule says no (debug line, tests). */
enum class EVeloxFireVerdict : uint8
{
	Fire,
	NoWeapon,
	NotReady,
	NoAmmo,
	NoTarget,
	OutOfRange,
	OffTarget,
	SelfSplash,
};

inline const TCHAR* LexToString(EVeloxFireVerdict Verdict)
{
	switch (Verdict)
	{
	case EVeloxFireVerdict::Fire: return TEXT("fire");
	case EVeloxFireVerdict::NoWeapon: return TEXT("noweapon");
	case EVeloxFireVerdict::NotReady: return TEXT("notready");
	case EVeloxFireVerdict::NoAmmo: return TEXT("noammo");
	case EVeloxFireVerdict::NoTarget: return TEXT("notarget");
	case EVeloxFireVerdict::OutOfRange: return TEXT("range");
	case EVeloxFireVerdict::OffTarget: return TEXT("offtarget");
	case EVeloxFireVerdict::SelfSplash: return TEXT("selfsplash");
	}
	return TEXT("?");
}

/** One tick of fire-rule observations (section 8). */
struct FVeloxFireInput
{
	bool bHasWeapon = false;
	/** The weapon switch has finished (no pending switch, SwitchEndTime passed). */
	bool bWeaponReady = true;
	bool bHasAmmo = false;
	bool bTargetVisible = false;
	FVector Eye = FVector::ZeroVector;
	/** Current aim (control rotation forward), unit. */
	FVector AimDir = FVector::ForwardVector;
	/** Where the aim wants to point without error (the lead point for projectiles, else the aim point), for the cone. */
	FVector DesiredPoint = FVector::ZeroVector;
	/** The target's capsule centre (range, splash distance). */
	FVector TargetCentre = FVector::ZeroVector;
	/** UVeloxWeaponDefinition::SplashRadiusCm (0 = no splash rules). */
	float SplashRadiusCm = 0.f;
	/** The aim ray hits world geometry at AimHitPoint (splash weapons trace it). */
	bool bAimHit = false;
	FVector AimHitPoint = FVector::ZeroVector;
	/** The weapon preference band [MinRange, MaxRange] (cm); MaxRange <= 0 = no band. */
	float MinRangeCm = 0.f;
	float MaxRangeCm = 0.f;
	float SelfSplashMarginCm = 50.f;
	float SplashHitFraction = 0.8f;
	/** Direct-hit weapons: the aim ray must pass within this of DesiredPoint (0 = cone only). */
	float DirectHitRadiusCm = 0.f;
};

/** One enemy candidate for target selection. */
struct FVeloxTargetCandidate
{
	int32 Id = INDEX_NONE;
	bool bVisible = false;
	float DistanceCm = 0.f;
};

/** Inputs of the combat overlay (section 10), XY world plane. */
struct FVeloxCombatWishInput
{
	/** The path follower's wish (unit or zero). */
	FVector2D PathWish = FVector2D::ZeroVector;
	/** Blend with the path (an item goal is active) instead of replacing it. */
	bool bBlendPath = false;
	float PathWeight = 0.5f;
	/** Bot -> enemy (not normalised); zero = no enemy direction (dodge only). */
	FVector2D ToEnemy = FVector2D::ZeroVector;
	/** The current weapon's best band (cm); BestMax <= 0 disables range keeping. */
	float BestMinCm = 0.f;
	float BestMaxCm = 0.f;
	float LeashCm = 2500.f;
	float RangeWeight = 0.6f;
	/** +1 / -1: left / right of the enemy direction. */
	float StrafeSign = 1.f;
	float StrafeAmplitude = 0.7f;
	/** Projectile dodge: the lateral component becomes DodgeDir (unit) at full weight. */
	bool bDodge = false;
	FVector2D DodgeDir = FVector2D::ZeroVector;
};

struct FVeloxProjectileThreat
{
	bool bThreat = false;
	/** Seconds until the closest approach (>= 0). */
	float TimeToClosest = 0.f;
	float ClosestDistanceCm = 0.f;
	/** XY unit direction perpendicular to the projectile's path, away from it. */
	FVector2D AwayDir = FVector2D::ZeroVector;
};

namespace VeloxBotCombat
{
	/** Fire rule of ADR 0005 section 8 with the reason; Fire = hold the trigger this tick. */
	VELOX_API EVeloxFireVerdict EvaluateFire(const FVeloxFireInput& In, const FVeloxBotDifficultyParams& Difficulty);

	/** EvaluateFire == Fire (the ADR's signature). */
	VELOX_API bool ShouldFire(const FVeloxFireInput& In, const FVeloxBotDifficultyParams& Difficulty);

	/** Target inside the view cone: angle(ViewDir, Target - Eye) <= FovDeg / 2 (FovDeg >= 360: always). */
	VELOX_API bool InFov(const FVector& ViewDir, const FVector& Eye, const FVector& Target, float FovDeg);

	/**
	 * Section 6 target choice: the nearest visible candidate; a visible current target (CurrentId) is kept unless another
	 * is closer than its distance / Stickiness. INDEX_NONE (array index) when nothing is visible.
	 */
	VELOX_API int32 SelectTarget(TConstArrayView<FVeloxTargetCandidate> Candidates, int32 CurrentId, float Stickiness);

	/**
	 * The combat wish (unit, or zero): lateral = Perp(ToEnemy) x StrafeSign x Amplitude (or DodgeDir x 1 when dodging),
	 * radial = toward the enemy beyond the leash (weight 1) or beyond BestMax (RangeWeight), away inside BestMin
	 * (RangeWeight); with bBlendPath the path wish x PathWeight is added; renormalised.
	 */
	VELOX_API FVector2D CombatWish(const FVeloxCombatWishInput& In);

	/**
	 * Straight-line projectile (Location + Velocity t) against a point: threat when the closest approach within
	 * HorizonSeconds is closer than DangerRadiusCm. AwayDir is perpendicular to the velocity, away from the line.
	 */
	VELOX_API FVeloxProjectileThreat ProjectileThreat(const FVector& Location, const FVector& Velocity, const FVector& Point, float DangerRadiusCm, float HorizonSeconds);

	/** True with probability ChancePerSecond x Dt (clamped to [0, 1]). */
	VELOX_API bool RollChance(float ChancePerSecond, float Dt, FRandomStream& Stream);
}

/** Air steering of the rocket-jump flight (section 4, task 05). */
struct FVeloxBallisticArrive
{
	/** XY unit wish, zero when the flight already carries the pawn to the target. */
	FVector2D WishDir = FVector2D::ZeroVector;
	/** Seconds until the descending pass through the target height; < 0 when the apex stays below it. */
	float TimeToTarget = -1.f;
	/** XY velocity that would land exactly on the target. */
	FVector2D NeededVelocity = FVector2D::ZeroVector;
};

namespace VeloxBotRocketJump
{
	/**
	 * Ballistic arrive: the time T at which the flight (Z velocity VelocityZ, GravityZ < 0) passes the target height on
	 * its way down, the XY velocity (Target - Feet)_xy / T that lands on the target, and the wish = needed - current XY
	 * velocity (Q3 air acceleration along a wish opposed to the velocity brakes; along it, it accelerates). Without a
	 * descending pass (apex below the target) the wish points at the target. A velocity error below DeadbandCmS gives no
	 * wish (no jitter on the final approach).
	 */
	VELOX_API FVeloxBallisticArrive ArriveWish(const FVector& Feet, const FVector& Velocity, const FVector& Target, float GravityZ, float DeadbandCmS);
}

// ---------------------------------------------------------------------------------------------- state and glue

/** One enemy the bot knows about (section 6). */
struct FVeloxBotEnemyInfo
{
	TWeakObjectPtr<AVeloxCharacter> Pawn;
	/** PlayerState::GetPlayerId() (stable across the enemy's respawns): the aim model's TargetId and the memory key. */
	int32 Id = INDEX_NONE;
	double LastSeenTime = -1.0;
	FVector LastSeenLocation = FVector::ZeroVector;
	FVector LastSeenVelocity = FVector::ZeroVector;
	bool bVisible = false;
	/** Damage from it was received (hearing) at HeardTime. */
	double HeardTime = -1.0;
	float DistanceCm = 0.f;
};

/** Per-life combat state of one bot (reset when the controller's LifeCount changes). */
struct FVeloxBotCombatState
{
	int32 LifeCount = INDEX_NONE;
	TWeakObjectPtr<AVeloxCharacter> BoundPawn;
	FDelegateHandle DamagedHandle;

	TArray<FVeloxBotEnemyInfo> Enemies;
	/** Enemy id of the target (visible or remembered), INDEX_NONE without one. */
	int32 TargetId = INDEX_NONE;
	double NextPerceiveTime = -1.0;
	/** Damage hearing: the bot turns toward HeardId's location from HeardReactTime on (reaction). */
	int32 HeardId = INDEX_NONE;
	double HeardReactTime = -1.0;

	FVeloxAimState Aim;
	FVeloxAimParams AimParams;
	/** Desired aim point of the last tick (fire cone). */
	FVector DesiredPoint = FVector::ZeroVector;

	float StrafeSign = 1.f;
	double NextFlipTime = -1.0;
	double DodgeJumpUntil = -1.0;
	double ProjectileDodgeAt = -1.0;
	double ProjectileDodgeUntil = -1.0;
	FVector2D ProjectileDodgeDir = FVector2D::ZeroVector;

	double NextWeaponChoiceTime = -1.0;
	EVeloxFireVerdict LastVerdict = EVeloxFireVerdict::NoTarget;
	int32 ShotsPressed = 0;
	int32 TracesTotal = 0;

	const FVeloxBotEnemyInfo* FindEnemy(int32 Id) const { return Id == INDEX_NONE ? nullptr : Enemies.FindByPredicate([Id](const FVeloxBotEnemyInfo& E) { return E.Id == Id; }); }
	const FVeloxBotEnemyInfo* GetTarget() const { return FindEnemy(TargetId); }
	bool HasVisibleTarget() const { const FVeloxBotEnemyInfo* T = GetTarget(); return T && T->bVisible; }
};

namespace VeloxBotCombatGlue
{
	/** Perceive phase: (re)binds the damage hearing per life, then every PerceptionIntervalSeconds (staggered by BotIndex) updates the enemy memory, the target and the projectile threat. */
	VELOX_API void Perceive(AVeloxBotController& Bot, FVeloxBotCombatState& Combat, FRandomStream& Stream, float Dt);

	/** UpdateCombat phase: strafe / dodge / range-keeping overlay on the pending wish (not on a link). */
	VELOX_API void Overlay(const AVeloxBotController& Bot, FVeloxBotCombatState& Combat, FRandomStream& Stream, const FVeloxBotNavOutput& Nav, FVeloxBotInput& Pending, float Dt);

	/**
	 * Act phase: aim model -> View, weapon choice -> SelectSlot, fire rule -> bFireHeld; then the maneuver view requests
	 * override it (review hand-off): bFaceEnd turns the yaw to the link end, the forced pitch wins for its tick, fire =
	 * maneuver fire OR the fire rule (the rule is suppressed while a rocket jump wants the launcher, so the jump's rocket
	 * is not spent early), the maneuver's launcher select wins over Pick. Every random draw comes from the controller's
	 * Stream (passed in).
	 */
	VELOX_API void Act(AVeloxBotController& Bot, FVeloxBotCombatState& Combat, FRandomStream& Stream, const FVeloxBotNavOutput& Nav, const FRotator& CurrentView, FVeloxBotInput& Pending, float Dt);

	/** Unbinds the damage hearing (EndPlay / unpossess). */
	VELOX_API void Unbind(FVeloxBotCombatState& Combat);

	/** ` enemy= vis= fire= shots=` fields of the debug line. */
	VELOX_API FString Describe(const FVeloxBotCombatState& Combat);
}
