// Velox - server anomaly log (ADR 0007 section 5.4, task m6-online/09).
//
// Prints one VeloxAnomaly line (VeloxAnomalyStats.h, VeloxAnomaly::FormatLine) per player whose counters changed since
// its last line, every velox.net.AnomalyLogIntervalSeconds (60 s), and one final line per player with any non-zero
// counter at logout (at=logout) or when the world tears down with the player still connected (at=shutdown). Active on
// server-like worlds only (dedicated, or a headless listen host: VeloxServerKind::IsServerLike), checked when it prints.
// Log only: thresholds (all 0 by default) add exceeds= to a line, nothing kicks in M6. No tick; the timer and the logout
// delegate are the only entry points.

#pragma once

#include "Net/VeloxAnomalyStats.h"
#include "Subsystems/WorldSubsystem.h"

#include "VeloxAnomalyLog.generated.h"

class AController;
class AGameModeBase;
class AVeloxPlayerState;

UCLASS(Config = Game)
class VELOX_API UVeloxAnomalyLog : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~End of USubsystem interface

	//~UWorldSubsystem interface
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~End of UWorldSubsystem interface

	/** Prints the interval lines now (the timer's callback; also a test entry). Returns the number of lines printed. */
	int32 LogInterval();
	/** Prints the final line of one player if it has any counter and none was printed yet. Returns whether it printed. */
	bool LogFinal(AVeloxPlayerState& PlayerState, const TCHAR* At);

	/** Thresholds (ADR 0007 section 5.4): 0 = disabled, the M6 default. Exceeded names are logged, never acted on. */
	UPROPERTY(Config, EditAnywhere, Category = "Velox|Anomaly")
	FVeloxAnomalyThresholds Thresholds;

private:
	bool IsActive() const;
	void PrintLine(AVeloxPlayerState& PlayerState, const TCHAR* At);
	void HandleLogout(AGameModeBase* GameMode, AController* Exiting);
	void HandleWorldTearDown(UWorld* InWorld);

	FTimerHandle IntervalTimer;
	FDelegateHandle LogoutHandle;
	FDelegateHandle TearDownHandle;
};
