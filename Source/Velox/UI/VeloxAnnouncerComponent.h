// Velox - the announcer (ADR 0006 section 9, task M5-10). Lives on the local AVeloxPlayerController only (never on a dedicated
// server). Countdown and phase lines come from the replicated AVeloxGameState phase and times, lead lines from the local
// PlayerState's score against the best other player, "excellent" / "impressive" from the replicated kill feed, "quad damage"
// from the powerup cue. One line at a time, a short queue, lead lines rate-limited. It also plays the weapon switch click
// of the local player's inventory (2D).

#pragma once

#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "GameModes/VeloxMatchRules.h"

#include "VeloxAnnouncerComponent.generated.h"

class AVeloxGameState;
class AVeloxPlayerState;
struct FVeloxKillFeedEntry;

/** World-free announcer rules (tests). */
namespace VeloxAnnouncerMath
{
	/** Where the local score stands against the best other score. */
	enum class ELead : uint8 { Tied, Leading, Behind };

	inline ELead ComputeLead(float LocalScore, float BestOtherScore)
	{
		return LocalScore > BestOtherScore ? ELead::Leading : (LocalScore < BestOtherScore ? ELead::Behind : ELead::Tied);
	}

	/** The countdown number (3, 2, 1) to announce for RemainingSeconds, or 0 outside (0, 3]. ceil: 2.4 s left is "three". */
	inline int32 CountdownNumber(double RemainingSeconds)
	{
		if (RemainingSeconds <= 0.0 || RemainingSeconds > 3.0)
		{
			return 0;
		}
		return FMath::Clamp(FMath::CeilToInt32(RemainingSeconds), 1, 3);
	}

	/** The lead line for a change of standing, or NAME_None. Entering the lead is "lead_taken"; leaving it for Behind "lead_lost"; a tie "lead_tied". */
	inline FName LeadLine(ELead Old, ELead New)
	{
		if (Old == New)
		{
			return NAME_None;
		}
		switch (New)
		{
		case ELead::Leading: return TEXT("lead_taken");
		case ELead::Behind: return Old == ELead::Leading ? FName(TEXT("lead_lost")) : NAME_None;
		default: return TEXT("lead_tied");
		}
	}

	/**
	 * A line queue of depth Depth behind the one playing. Push returns false when the line was dropped; when the queue is full the
	 * oldest waiting line is dropped for the new one.
	 */
	struct FLineQueue
	{
		TArray<FName> Waiting;

		void Push(FName Line, int32 Depth)
		{
			if (Depth <= 0)
			{
				return;
			}
			while (Waiting.Num() >= Depth)
			{
				Waiting.RemoveAt(0);
			}
			Waiting.Add(Line);
		}

		FName Pop()
		{
			if (Waiting.IsEmpty())
			{
				return NAME_None;
			}
			const FName Line = Waiting[0];
			Waiting.RemoveAt(0);
			return Line;
		}
	};
}

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxAnnouncerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVeloxAnnouncerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~End of UActorComponent interface

	/** Asks for a line by short name (see UVeloxAudioSettings::AnnouncerLines). Plays now, or waits in the queue. */
	void Announce(FName Line);

	/** The local pawn got the Quad Damage powerup cue. */
	void OnQuadDamage();

	int32 GetNumAnnounced() const { return NumAnnounced; }
	FName GetLastLine() const { return LastLine; }

private:
	void BindGameState();
	void BindInventory(AVeloxPlayerState* LocalState);
	void OnPhaseChanged(EVeloxMatchPhase OldPhase, EVeloxMatchPhase NewPhase);
	void OnKillFeedEntry(const FVeloxKillFeedEntry& Entry);
	void OnWeaponChanged(int32 OldIndex, int32 NewIndex);
	void UpdateLead(const AVeloxGameState& State);
	void PlayLine(FName Line);
	void FinishLine();

	TWeakObjectPtr<AVeloxGameState> BoundState;
	TWeakObjectPtr<AVeloxPlayerState> BoundInventoryOwner;
	FDelegateHandle PhaseHandle;
	FDelegateHandle FeedHandle;
	FDelegateHandle WeaponHandle;

	VeloxAnnouncerMath::FLineQueue Queue;
	FTimerHandle LineTimer;
	bool bPlaying = false;
	bool bMixPushed = false;

	int32 LastCountdownNumber = 0;
	bool bOneMinuteDone = false;
	VeloxAnnouncerMath::ELead LastLead = VeloxAnnouncerMath::ELead::Tied;
	bool bLeadKnown = false;
	double LastLeadLineTime = -1.0e9;
	double LastFragServerTime = -1.0e9;
	int32 ConsecutiveRailFrags = 0;
	int32 NumAnnounced = 0;
	FName LastLine = NAME_None;
};
