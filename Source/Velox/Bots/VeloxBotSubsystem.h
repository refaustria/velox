// Velox - server-side bot world state (ADR 0005 sections 3, 4, 12): the nav-ready flag (runtime Recast build at map
// load), the nav-link registry (FindLinkByStart for the path follower), the master random seed (velox.bots.Seed), and
// the bot / projectile registries that tasks 04 and 05 fill. Created only on the authority (NetMode != NM_Client).

#pragma once

#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"

#include "VeloxBotSubsystem.generated.h"

class ANavigationData;
class AVeloxBotController;
class UVeloxBotDifficulty;
class AVeloxNavLink;
class AVeloxProjectile;
class UWorld;

/** A link end matched by FindLinkByStart. */
struct FVeloxNavLinkMatch
{
	AVeloxNavLink* Link = nullptr;
	/** True when the point matched the link's end, i.e. a BothWays link traversed right to left. */
	bool bReversed = false;
	/** Horizontal distance of the point from the matched end (cm). */
	double Distance2D = 0.0;
};

UCLASS()
class VELOX_API UVeloxBotSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVeloxBotSubsystem* Get(const UWorld* World);

	/**
	 * World-free link-end matching (FindLinkByStart's rule, testable): the index of the candidate whose end lies within
	 * Radius horizontally and MaxDeltaZ vertically of Point, nearest first; INDEX_NONE if none. Ends[i] is a link start
	 * (or, for BothWays links, also its end as a separate candidate).
	 */
	static int32 FindNearestEnd(const TArray<FVector>& Ends, const FVector& Point, double Radius, double MaxDeltaZ);

	// --- Nav readiness (ADR 0005 section 3) ---

	/** The runtime nav mesh has finished its initial build (tiles > 0, no build task pending). Bots wait for this. */
	bool IsNavReady() const { return bNavReady; }
	/** The map has no NavMeshBoundsVolume, so no nav data will be built (5.8 has no whole-world mode). */
	bool IsNavUnavailable() const { return bNavUnavailable; }
	/** Runs Callback now if the nav is ready, else once when it becomes ready; dropped (warning) on a map without nav bounds. */
	void CallWhenNavReady(TFunction<void()> Callback);
	/** Wall-clock seconds from world init to the nav-ready flag (-1 until ready). */
	double GetNavBuildSeconds() const { return NavBuildSeconds; }
	/** The world's default nav data (the one Recast mesh), or null. */
	ANavigationData* GetNavData() const;

	// --- Nav links (ADR 0005 section 4) ---

	void RegisterNavLink(AVeloxNavLink* Link);
	void UnregisterNavLink(AVeloxNavLink* Link);
	TArray<AVeloxNavLink*> GetNavLinks() const;
	/**
	 * The enabled link whose start (or, for a BothWays link, end) matches a path point: within Radius (cm) horizontally
	 * and the link's snap height vertically, nearest first. Recast inserts the link ends as path points, so the match is
	 * a few centimetres at most; Radius is normally the link's snap radius.
	 */
	FVeloxNavLinkMatch FindLinkByStart(const FVector& Point, double Radius) const;

	// --- Random seed (ADR 0005 section 12) ---

	/** velox.bots.Seed if non-zero (deterministic tests), else the time-based seed drawn at world begin play. */
	int32 GetMasterSeed() const;

	// --- Bots (ADR 0005 section 12) ---

	/**
	 * Adds a bot: spawns an AVeloxBotController (its PlayerState comes from bWantsPlayerState), names it (Name, else the
	 * next free UVeloxBotSettings::BotNames entry), flags it IsABot, gives it a palette colour and restarts it through the
	 * game mode (outside PostMatch). Null when refused: no Velox game mode, or the mode's MaxActivePlayers reached
	 * (`VeloxBot add refused reason=full`). Difficulty null = UVeloxBotSettings::DefaultDifficulty.
	 */
	AVeloxBotController* AddBot(const UVeloxBotDifficulty* Difficulty = nullptr, const FString& Name = FString());
	/** Removes the bot named NameOrAll (case-insensitive), or every bot for "all": pawn and controller destroyed, Logout runs. Count removed. */
	int32 KickBot(const FString& NameOrAll);
	/** Easy | Medium | Hard | Expert (or a DA_Bot_* asset name / object path); empty = the settings default. Null if unknown. */
	static const UVeloxBotDifficulty* LoadDifficulty(const FString& NameOrPath);

	/** Registration (AVeloxBotController::BeginPlay / EndPlay): assigns the lowest free BotIndex (perception stagger). */
	void RegisterBot(AVeloxBotController* Bot);
	void UnregisterBot(AVeloxBotController* Bot);
	TArray<AVeloxBotController*> GetBots() const;
	int32 NumBots() const { return GetBots().Num(); }
	/** The bot whose player name matches (case-insensitive), or null. */
	AVeloxBotController* FindBot(const FString& Name) const;

	void RegisterProjectile(AVeloxProjectile* Projectile);
	void UnregisterProjectile(AVeloxProjectile* Projectile);
	TArray<AVeloxProjectile*> GetProjectiles() const;

protected:
	//~UWorldSubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~End of UWorldSubsystem interface

private:
	void PollNavReady();
	UFUNCTION()
	void OnNavigationGenerationFinished(ANavigationData* NavData);
	void MarkNavReady();
	/** Pad/pickup trigger components the nav system would export (must be zero: ADR 0005 section 3). */
	int32 CountNavRelevantTriggers() const;

	TArray<TWeakObjectPtr<AVeloxNavLink>> NavLinks;
	TArray<TWeakObjectPtr<AVeloxBotController>> Bots;
	TArray<TWeakObjectPtr<AVeloxProjectile>> Projectiles;

	TArray<TFunction<void()>> NavReadyCallbacks;
	FTimerHandle NavPollHandle;
	double WorldInitSeconds = 0.0;
	double NavBuildSeconds = -1.0;
	bool bNavReady = false;
	bool bLoggedBuilding = false;
	bool bWarnedSlowBuild = false;
	/** The map has no nav bounds: no nav data will ever be built (logged once, polling stopped). */
	bool bNavUnavailable = false;

	int32 TimeSeed = 0;
};
