// Velox - HUD view model.

#include "UI/VeloxHUDViewModel.h"

#include "AbilitySystemComponent.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "Items/VeloxPowerupEffects.h"
#include "UI/VeloxUISettings.h"
#include "Weapons/VeloxCombatFeedbackComponent.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxHUDViewModel)

namespace
{
	/** The four powerups in display order. */
	struct FVeloxHUDPowerupEntry
	{
		TSubclassOf<UGameplayEffect> Effect;
		const TCHAR* Name;
	};

	TArray<FVeloxHUDPowerupEntry> VeloxHUDPowerupTable()
	{
		return {
			{ UVeloxGE_Powerup_Quad::StaticClass(), TEXT("Quad Damage") },
			{ UVeloxGE_Powerup_Haste::StaticClass(), TEXT("Haste") },
			{ UVeloxGE_Powerup_Regen::StaticClass(), TEXT("Regeneration") },
			{ UVeloxGE_Powerup_BattleSuit::StaticClass(), TEXT("Battle Suit") },
		};
	}

	FColor VeloxHUDPaletteColor(const UVeloxCombatSettings& Settings, uint8 Index)
	{
		return Settings.PlayerColorPalette.IsValidIndex(Index) ? Settings.PlayerColorPalette[Index] : FColor::White;
	}

	int32 VeloxHUDAmmoOf(const UAbilitySystemComponent* ASC, const UVeloxWeaponDefinition& Definition, const UVeloxCombatSettings& Settings)
	{
		if (!ASC || Definition.AmmoPerShot <= 0 || !Definition.AmmoTag.IsValid())
		{
			return -1;
		}
		const FGameplayAttribute Attribute = UVeloxAmmoSet::GetAmmoAttribute(Definition.AmmoTag, Settings);
		return Attribute.IsValid() ? FMath::RoundToInt32(ASC->GetNumericAttribute(Attribute)) : -1;
	}
}

void UVeloxHUDViewModel::Reset()
{
	bHasPlayer = false;
	bAlive = false;
	Health = 0;
	Armor = 0;
	WeaponName.Reset();
	Ammo = -1;
	CurrentWeaponIndex = INDEX_NONE;
	WeaponRows.Reset();
	Powerups.Reset();
	ClockText.Reset();
	BannerText.Reset();
	bShowDeath = false;
	DeathTitle.Reset();
	DeathPrompt.Reset();
	HitMarkerAlpha = 0.f;
	bHitMarkerKill = false;
	for (float& Intensity : DamageIntensity)
	{
		Intensity = 0.f;
	}
	Feed.Reset();
	bScoreboardVisible = false;
	bScoreboardForced = false;
	ScoreboardTitle.Reset();
	ScoreboardLimits.Reset();
	ScoreboardClock.Reset();
	ScoreboardWinner.Reset();
	ScoreRows.Reset();
	SpectatorNames.Reset();
	bShowReadyButton = false;
	bLocalReady = false;
	ReadyButtonText.Reset();
	ReadyPrompt.Reset();
	PickupMessage.Reset();
	PickupMessageAlpha = 0.f;
	bShowItemTimers = false;
	ItemTimers.Reset();
	bSpectator = false;
}

void UVeloxHUDViewModel::Unbind()
{
	if (bBoundRegistry)
	{
		if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(BoundWorld.Get()))
		{
			Registry->OnPickupTaken.Remove(PickupTakenHandle);
		}
		PickupTakenHandle.Reset();
		bBoundRegistry = false;
	}
	BoundWorld.Reset();
	LocalPlayerState.Reset();
}

void UVeloxHUDViewModel::UpdateFromGame(const AVeloxPlayerController& Controller)
{
	const UWorld* World = Controller.GetWorld();
	const AVeloxPlayerState* PlayerState = Controller.GetPlayerState<AVeloxPlayerState>();
	if (!World || !PlayerState)
	{
		Reset();
		return;
	}
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const UVeloxCombatSettings& Combat = UVeloxCombatSettings::GetOrDefault(PlayerState->GetCombatSettings());
	const double Now = World->GetTimeSeconds();
	const AVeloxCharacter* Pawn = Cast<AVeloxCharacter>(Controller.GetPawn());

	bHasPlayer = true;
	bAlive = Pawn != nullptr;

	// Health, armor.
	if (const UVeloxHealthSet* HealthSet = PlayerState->GetHealthSet())
	{
		Health = FMath::RoundToInt32(HealthSet->GetHealth());
		Armor = FMath::RoundToInt32(HealthSet->GetArmor());
		MaxHealth = FMath::RoundToInt32(HealthSet->GetMaxHealth());
	}

	// Weapons, ammo.
	const UAbilitySystemComponent* ASC = PlayerState->GetAbilitySystemComponent();
	WeaponRows.Reset();
	WeaponName.Reset();
	Ammo = -1;
	CurrentWeaponIndex = INDEX_NONE;
	if (const UVeloxWeaponInventoryComponent* Inventory = PlayerState->GetWeaponInventory())
	{
		TArray<VeloxHUDFormat::FWeaponBarInput> Inputs;
		for (int32 Index = 0; Index < Inventory->NumWeapons(); ++Index)
		{
			if (const UVeloxWeaponDefinition* Definition = Inventory->GetDefinition(Index))
			{
				VeloxHUDFormat::FWeaponBarInput& Input = Inputs.AddDefaulted_GetRef();
				Input.Index = Index;
				Input.Slot = Definition->Slot;
				Input.Name = Definition->DisplayName.ToString();
				Input.Ammo = VeloxHUDAmmoOf(ASC, *Definition, Combat);
			}
		}
		CurrentWeaponIndex = Inventory->GetCurrentIndex();
		WeaponRows = VeloxHUDFormat::WeaponBarRows(Inputs, Inventory->GetEffectiveOwnedMask(), CurrentWeaponIndex);
		for (const VeloxHUDFormat::FWeaponBarRow& Row : WeaponRows)
		{
			if (Row.bCurrent)
			{
				WeaponName = Row.Name;
				Ammo = Row.Ammo;
			}
		}
	}

	// Powerups: the remaining time of each active effect (no ratio, ADR 0004).
	Powerups.Reset();
	if (ASC)
	{
		for (const FVeloxHUDPowerupEntry& Entry : VeloxHUDPowerupTable())
		{
			const float Remaining = VeloxPowerups::GetRemainingSeconds(*ASC, Entry.Effect);
			if (Remaining > 0.f)
			{
				FVeloxHUDPowerup& Powerup = Powerups.AddDefaulted_GetRef();
				Powerup.Name = Entry.Name;
				Powerup.Remaining = Remaining;
				Powerup.Seconds = VeloxHUDFormat::PowerupSeconds(Remaining);
			}
		}
	}

	// Match clock and phase banner.
	const AVeloxGameState* GameState = World->GetGameState<AVeloxGameState>();
	ClockText.Reset();
	BannerText.Reset();
	if (GameState)
	{
		VeloxHUDFormat::FClockInput Clock;
		Clock.Phase = GameState->GetPhase();
		Clock.Now = GameState->GetServerWorldTimeSeconds();
		Clock.PhaseStart = GameState->GetPhaseStartServerTime();
		Clock.PhaseEnd = GameState->GetPhaseEndServerTime();
		Clock.LiveStart = GameState->GetLiveStartServerTime();
		Clock.TimeLimitSeconds = GameState->GetTimeLimitSeconds();
		ClockText = VeloxHUDFormat::MatchClockText(Clock);
		const APlayerState* Winner = GameState->GetWinnerPlayerState();
		BannerText = VeloxHUDFormat::PhaseBanner(Clock, UI.FightBannerSeconds, Winner ? AVeloxPlayerState::VeloxPlayerLabel(Winner) : FString());
	}

	// Death overlay.
	bShowDeath = Controller.IsWaitingForRespawn();
	DeathTitle.Reset();
	DeathPrompt.Reset();
	if (bShowDeath)
	{
		const double Waited = Now - Controller.GetPawnLostTime();
		DeathTitle = TEXT("YOU WERE FRAGGED");
		DeathPrompt = Waited < Combat.MinRespawnDelay ? FString::Printf(TEXT("Respawn in %.1f"), Combat.MinRespawnDelay - Waited) : FString(TEXT("Press FIRE to respawn"));
	}

	// Hit marker (state in the controller, set by ClientConfirmHit).
	HitMarkerAlpha = 0.f;
	bHitMarkerKill = Controller.WasLastHitConfirmKill();
	HitMarkerSize = Combat.HitMarkerSize;
	if (Controller.GetLastHitConfirmTime() >= 0.0 && Combat.HitMarkerSeconds > 0.f)
	{
		const double Age = Now - Controller.GetLastHitConfirmTime();
		HitMarkerAlpha = (Age >= 0.0 && Age < Combat.HitMarkerSeconds) ? static_cast<float>(1.0 - Age / Combat.HitMarkerSeconds) : 0.f;
	}

	// Damage direction indicators: the strongest record of each quadrant, relative to where the player looks now.
	for (float& Intensity : DamageIntensity)
	{
		Intensity = 0.f;
	}
	if (const UVeloxCombatFeedbackComponent* Feedback = Pawn ? Pawn->GetCombatFeedbackComponent() : nullptr)
	{
		const float ViewYaw = Controller.GetControlRotation().Yaw;
		for (const FVeloxDamageDirection& Record : Feedback->GetDamageDirections())
		{
			if (Record.Distance2D < UI.DamageIndicatorMinDistance)
			{
				continue;
			}
			const int32 Quadrant = static_cast<int32>(VeloxHUDFormat::QuadrantFor(Record.WorldYaw, ViewYaw));
			DamageIntensity[Quadrant] = FMath::Max(DamageIntensity[Quadrant], VeloxHUDFormat::DamageIntensity(Now - Record.Time, UI.DamageIndicatorSeconds));
		}
	}

	// Kill feed: the newest GetKillFeedLength() entries that have not expired.
	Feed.Reset();
	if (GameState)
	{
		const double ServerNow = GameState->GetServerWorldTimeSeconds();
		const TArray<FVeloxKillFeedEntry> Entries = GameState->GetKillFeedOldestFirst();
		for (const FVeloxKillFeedEntry& Entry : Entries)
		{
			if (!VeloxHUDFormat::FeedEntryVisible(ServerNow, Entry.ServerTime, UI.FeedEntrySeconds))
			{
				continue;
			}
			FVeloxHUDFeedLine& Line = Feed.AddDefaulted_GetRef();
			const FString Weapon = VeloxHUDFormat::FeedWeaponLabel(Entry);
			Line.bSuicide = Entry.bSuicide;
			Line.Text = VeloxHUDFormat::FeedLine(Entry);
			Line.KillerColor = VeloxHUDPaletteColor(Combat, Entry.KillerColor);
			Line.VictimColor = VeloxHUDPaletteColor(Combat, Entry.VictimColor);
			if (Entry.bSuicide)
			{
				Line.Killer = Entry.VictimName;
				Line.KillerColor = Line.VictimColor;
				Line.Middle = Weapon.IsEmpty() ? FString(TEXT(" (suicide)")) : FString::Printf(TEXT(" [%s] (suicide)"), *Weapon);
			}
			else
			{
				Line.Killer = Entry.KillerName;
				Line.Middle = Weapon.IsEmpty() ? FString(TEXT(" killed ")) : FString::Printf(TEXT(" [%s] "), *Weapon);
				Line.Victim = Entry.VictimName;
			}
		}
		const int32 Excess = Feed.Num() - GameState->GetKillFeedLength();
		if (Excess > 0)
		{
			Feed.RemoveAt(0, Excess);
		}
	}

	// Crosshair: the player's user settings, read live (so Apply needs no event).
	const UVeloxGameUserSettings& UserSettings = UVeloxGameUserSettings::GetOrDefault();
	CrosshairStyle = UserSettings.CrosshairStyle;
	CrosshairSize = UserSettings.CrosshairSize;
	CrosshairColor = UserSettings.CrosshairColor;

	// Item registry: the "took" message needs the take event, so listen once per world.
	if (!bBoundRegistry || BoundWorld.Get() != World)
	{
		Unbind();
		if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World))
		{
			PickupTakenHandle = Registry->OnPickupTaken.AddUObject(this, &UVeloxHUDViewModel::HandlePickupTaken);
			BoundWorld = World;
			bBoundRegistry = true;
		}
	}
	LocalPlayerState = PlayerState;
	bSpectator = PlayerState->IsOnlyASpectator();

	const double MessageAge = Now - PickupMessageStart;
	PickupMessageAlpha = 0.f;
	if (!PickupMessage.IsEmpty() && MessageAge >= 0.0 && MessageAge < UI.PickupMessageSeconds)
	{
		const double FadeStart = UI.PickupMessageSeconds * 0.6;
		PickupMessageAlpha = MessageAge < FadeStart ? 1.f : static_cast<float>((UI.PickupMessageSeconds - MessageAge) / (UI.PickupMessageSeconds - FadeStart));
	}
	if (PickupMessageAlpha <= 0.f)
	{
		PickupMessage.Reset();
	}

	UpdateScoreboard(Controller, GameState);
	UpdateItemTimers();
}

void UVeloxHUDViewModel::HandlePickupTaken(AVeloxPickup* Pickup, APlayerState* TakenBy)
{
	const UVeloxItemDefinition* Definition = Pickup ? Pickup->GetDefinition() : nullptr;
	const UWorld* World = BoundWorld.Get();
	if (!Definition || !World)
	{
		return;
	}
	const bool bLocalTaker = TakenBy && TakenBy == LocalPlayerState.Get();
	// Spectators following the match see the major items being taken (the timer rule decides who is "watching").
	const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
	const bool bShowOthers = bSpectator && Definition->bMajorItem && Registry && VeloxScoreboardFormat::ItemTimersVisible(Registry->GetItemTimerVisibility(), bSpectator);
	const FString Text = VeloxScoreboardFormat::PickupMessageText(Definition->DisplayName.ToString(), TakenBy ? AVeloxPlayerState::VeloxPlayerLabel(TakenBy) : FString(), bLocalTaker, bShowOthers);
	if (!Text.IsEmpty())
	{
		PickupMessage = Text;
		PickupMessageStart = World->GetTimeSeconds();
		PickupMessageAlpha = 1.f;
	}
}

void UVeloxHUDViewModel::UpdateScoreboard(const AVeloxPlayerController& Controller, const AVeloxGameState* GameState)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const AVeloxPlayerState* LocalState = Controller.GetPlayerState<AVeloxPlayerState>();
	ScoreRows.Reset();
	SpectatorNames.Reset();
	ScoreboardTitle.Reset();
	ScoreboardLimits.Reset();
	ScoreboardWinner.Reset();
	ScoreboardClock = ClockText;
	bShowReadyButton = false;
	bLocalReady = LocalState && LocalState->IsReady();
	ReadyButtonText.Reset();
	ReadyPrompt.Reset();

	const EVeloxMatchPhase Phase = GameState ? GameState->GetPhase() : EVeloxMatchPhase::Warmup;
	bScoreboardForced = GameState && (Phase == EVeloxMatchPhase::PostMatch || Controller.IsInIntermission());
	bScoreboardVisible = bScoreboardForced || Controller.IsScoreboardHeld();

	if (GameState)
	{
		const UVeloxGameModeDefinition* Mode = GameState->GetGameModeDefinition();
		ScoreboardTitle = Mode ? Mode->DisplayName.ToString() : FString(TEXT("Velox"));
		ScoreboardLimits = VeloxScoreboardFormat::LimitsText(GameState->GetFragLimit(), GameState->GetTimeLimitSeconds());
		if (Phase == EVeloxMatchPhase::PostMatch)
		{
			ScoreboardWinner = BannerText;
		}

		VeloxScoreboardFormat::FReadyInput ReadyInput;
		ReadyInput.Phase = Phase;
		ReadyInput.bSpectator = bSpectator;
		ReadyInput.bReady = bLocalReady;
		ReadyInput.bRequireReady = Mode && Mode->bRequireReady;
		ReadyPrompt = VeloxScoreboardFormat::ReadyPromptText(ReadyInput, UI.ReadyKeyLabel);
		bShowReadyButton = VeloxScoreboardFormat::CanToggleReady(ReadyInput);
		ReadyButtonText = VeloxScoreboardFormat::ReadyButtonText(bLocalReady, UI.ReadyKeyLabel);

		const TArray<TObjectPtr<APlayerState>>& Players = GameState->PlayerArray;
		for (const APlayerState* Player : Players)
		{
			const AVeloxPlayerState* VeloxPlayer = Cast<AVeloxPlayerState>(Player);
			if (!VeloxPlayer)
			{
				continue;
			}
			if (VeloxPlayer->IsOnlyASpectator())
			{
				SpectatorNames += (SpectatorNames.IsEmpty() ? TEXT("") : TEXT(", ")) + AVeloxPlayerState::VeloxPlayerLabel(VeloxPlayer);
				continue;
			}
			VeloxScoreboardFormat::FRow& Row = ScoreRows.AddDefaulted_GetRef();
			Row.Name = AVeloxPlayerState::VeloxPlayerLabel(VeloxPlayer);
			Row.Score = FMath::RoundToInt32(VeloxPlayer->GetScore());
			Row.Kills = VeloxPlayer->GetKills();
			Row.Deaths = VeloxPlayer->GetDeaths();
			Row.bIsBot = VeloxPlayer->IsABot();
			Row.PingMs = Row.bIsBot ? 0 : FMath::RoundToInt32(VeloxPlayer->GetPingInMilliseconds());
			Row.bReady = VeloxPlayer->IsReady();
			Row.bLocal = VeloxPlayer == LocalState;
			Row.Color = VeloxPlayer->GetPlayerColor();
			if (const UAbilitySystemComponent* PlayerASC = VeloxPlayer->GetAbilitySystemComponent())
			{
				using namespace VeloxGameplayTags;
				if (PlayerASC->HasMatchingGameplayTag(Powerup_Quad)) { Row.Powerups += TEXT("Q"); }
				if (PlayerASC->HasMatchingGameplayTag(Powerup_Haste)) { Row.Powerups += TEXT("H"); }
				if (PlayerASC->HasMatchingGameplayTag(Powerup_Regen)) { Row.Powerups += TEXT("R"); }
				if (PlayerASC->HasMatchingGameplayTag(Powerup_BattleSuit)) { Row.Powerups += TEXT("B"); }
			}
		}
		VeloxScoreboardFormat::SortRows(ScoreRows);
	}
}

void UVeloxHUDViewModel::UpdateItemTimers()
{
	ItemTimers.Reset();
	const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(BoundWorld.Get());
	bShowItemTimers = Registry && VeloxScoreboardFormat::ItemTimersVisible(Registry->GetItemTimerVisibility(), bSpectator);
	if (!bShowItemTimers)
	{
		return;
	}
	for (const AVeloxPickup* Pickup : Registry->GetMajorPickups())
	{
		const UVeloxItemDefinition* Definition = Pickup ? Pickup->GetDefinition() : nullptr;
		if (!Definition)
		{
			continue;
		}
		VeloxScoreboardFormat::FItemTimer& Timer = ItemTimers.AddDefaulted_GetRef();
		Timer.ItemName = Definition->DisplayName.ToString();
		Timer.bAvailable = Pickup->IsAvailable();
		Timer.SecondsLeft = Pickup->GetTimeUntilRespawn();
		if (const APlayerState* Taker = Pickup->GetLastTakenBy())
		{
			Timer.TakenBy = AVeloxPlayerState::VeloxPlayerLabel(Taker);
			if (const AVeloxPlayerState* VeloxTaker = Cast<AVeloxPlayerState>(Taker))
			{
				Timer.TakenByColor = VeloxTaker->GetPlayerColor();
			}
		}
		Timer.Text = VeloxScoreboardFormat::ItemTimerText(Timer);
	}
	VeloxScoreboardFormat::SortTimers(ItemTimers);
}
