// Velox - HUD view model.

#include "UI/VeloxHUDViewModel.h"

#include "AbilitySystemComponent.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxHealthSet.h"
#include "Engine/World.h"
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
}
