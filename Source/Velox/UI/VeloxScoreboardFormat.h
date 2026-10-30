// Velox - world-free scoreboard / prompt / item-timer formatting (ADR 0004 sections 4, 5 and 7). Header-only like
// VeloxHUDFormat.h so Velox.UI.ScoreboardFormat tests it directly; UVeloxHUDViewModel feeds it from the gameplay objects.

#pragma once

#include "CoreMinimal.h"
#include "GameModes/VeloxMatchRules.h"
#include "UI/VeloxHUDFormat.h"

namespace VeloxScoreboardFormat
{
	/** One player line of the scoreboard. */
	struct FRow
	{
		FString Name;
		int32 Score = 0;
		int32 Kills = 0;
		int32 Deaths = 0;
		int32 PingMs = 0;
		/** AI-controlled player: the ping column shows BOT (ADR 0005 section 12). */
		bool bIsBot = false;
		bool bReady = false;
		bool bLocal = false;
		FColor Color = FColor::White;
		/** One letter per active powerup (Q H R B), empty when none. */
		FString Powerups;
	};

	/** ADR 0004 section 6: score descending, kills descending, deaths ascending, then the name (case-insensitive). */
	inline bool RowBefore(const FRow& A, const FRow& B)
	{
		if (A.Score != B.Score)
		{
			return A.Score > B.Score;
		}
		if (A.Kills != B.Kills)
		{
			return A.Kills > B.Kills;
		}
		if (A.Deaths != B.Deaths)
		{
			return A.Deaths < B.Deaths;
		}
		return A.Name.Compare(B.Name, ESearchCase::IgnoreCase) < 0;
	}

	inline void SortRows(TArray<FRow>& Rows)
	{
		Rows.StableSort(&RowBefore);
	}

	/** Ping column text: the ping in ms, "BOT" for bots (they have no network ping). */
	inline FString PingText(const FRow& Row)
	{
		return Row.bIsBot ? FString(TEXT("BOT")) : FString::FromInt(Row.PingMs);
	}

	/** The row as plain text: "Name  Score  Kills  Deaths  Ping|BOT  READY|-" (the powerup letters follow the name when held). */
	inline FString RowText(const FRow& Row)
	{
		const FString Name = Row.Powerups.IsEmpty() ? Row.Name : FString::Printf(TEXT("%s [%s]"), *Row.Name, *Row.Powerups);
		return FString::Printf(TEXT("%s  %d  %d  %d  %s  %s"), *Name, Row.Score, Row.Kills, Row.Deaths, *PingText(Row), Row.bReady ? TEXT("READY") : TEXT("-"));
	}

	/** "Fraglimit 20   Timelimit 10:00" with the parts that are 0 left out. */
	inline FString LimitsText(int32 FragLimit, int32 TimeLimitSeconds)
	{
		FString Text;
		if (FragLimit > 0)
		{
			Text = FString::Printf(TEXT("Fraglimit %d"), FragLimit);
		}
		if (TimeLimitSeconds > 0)
		{
			Text += FString::Printf(TEXT("%sTimelimit %s"), Text.IsEmpty() ? TEXT("") : TEXT("   "), *VeloxHUDFormat::Clock(TimeLimitSeconds, false));
		}
		return Text;
	}

	/** Inputs of the ready prompt. */
	struct FReadyInput
	{
		EVeloxMatchPhase Phase = EVeloxMatchPhase::Warmup;
		bool bSpectator = false;
		bool bReady = false;
		/** The mode needs everybody ready to start (Duel). */
		bool bRequireReady = false;
	};

	/** Whether the ready toggle means anything now: an active player in Warmup of a ready-gated mode, or in PostMatch. */
	inline bool CanToggleReady(const FReadyInput& Input)
	{
		if (Input.bSpectator)
		{
			return false;
		}
		return Input.Phase == EVeloxMatchPhase::PostMatch || (Input.Phase == EVeloxMatchPhase::Warmup && Input.bRequireReady);
	}

	/** Prompt line under the banner. Empty when ready does nothing in the phase. */
	inline FString ReadyPromptText(const FReadyInput& Input, const FString& KeyLabel)
	{
		if (!CanToggleReady(Input))
		{
			return FString();
		}
		if (Input.Phase == EVeloxMatchPhase::PostMatch)
		{
			return Input.bReady ? FString(TEXT("READY - waiting for the others")) : FString::Printf(TEXT("Press %s to play the next match now"), *KeyLabel);
		}
		return Input.bReady ? FString(TEXT("READY - waiting for the opponent")) : FString::Printf(TEXT("Press %s when you are ready"), *KeyLabel);
	}

	/** The ready button's label. */
	inline FString ReadyButtonText(bool bReady, const FString& KeyLabel)
	{
		return bReady ? FString::Printf(TEXT("READY (%s to cancel)"), *KeyLabel) : FString::Printf(TEXT("Ready (%s)"), *KeyLabel);
	}

	/**
	 * The pickup message: the item name for the local player's own take, "Taker took Item" when a spectator watches
	 * a major item taken. Empty otherwise.
	 */
	inline FString PickupMessageText(const FString& ItemName, const FString& TakerName, bool bLocalTaker, bool bShowOthers)
	{
		if (ItemName.IsEmpty())
		{
			return FString();
		}
		if (bLocalTaker)
		{
			return ItemName;
		}
		return bShowOthers && !TakerName.IsEmpty() ? FString::Printf(TEXT("%s took %s"), *TakerName, *ItemName) : FString();
	}

	/** Who sees the item timers: the mode rule against the viewer's role. */
	inline bool ItemTimersVisible(EVeloxItemTimerVisibility Rule, bool bSpectator)
	{
		return Rule == EVeloxItemTimerVisibility::Everyone || (Rule == EVeloxItemTimerVisibility::Spectators && bSpectator);
	}

	/** One item timer line. */
	struct FItemTimer
	{
		FString ItemName;
		bool bAvailable = true;
		/** Seconds until the respawn (taken items). */
		double SecondsLeft = 0.0;
		FString TakenBy;
		FColor TakenByColor = FColor::White;
		/** "Mega Health 00:23 (Anarki)" or "Mega Health UP". */
		FString Text;
	};

	inline FString ItemTimerText(const FItemTimer& Timer)
	{
		if (Timer.bAvailable)
		{
			return FString::Printf(TEXT("%s UP"), *Timer.ItemName);
		}
		const FString Clock = VeloxHUDFormat::Clock(Timer.SecondsLeft, true);
		return Timer.TakenBy.IsEmpty() ? FString::Printf(TEXT("%s %s"), *Timer.ItemName, *Clock) : FString::Printf(TEXT("%s %s (%s)"), *Timer.ItemName, *Clock, *Timer.TakenBy);
	}

	/** Taken items by the time they return (soonest first), then the available ones by name. */
	inline void SortTimers(TArray<FItemTimer>& Timers)
	{
		Timers.StableSort([](const FItemTimer& A, const FItemTimer& B)
		{
			if (A.bAvailable != B.bAvailable)
			{
				return !A.bAvailable;
			}
			if (!A.bAvailable && !FMath::IsNearlyEqual(A.SecondsLeft, B.SecondsLeft))
			{
				return A.SecondsLeft < B.SecondsLeft;
			}
			return A.ItemName.Compare(B.ItemName, ESearchCase::IgnoreCase) < 0;
		});
	}
}
