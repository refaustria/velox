// Velox - HUD view model (ADR 0004 section 7). A plain UObject owned by AVeloxHUD (client only) and refreshed every frame
// from the gameplay objects: the PlayerState attribute sets and weapon inventory, the ASC (powerup remaining time), the
// controller (hit confirm, respawn wait), the pawn's feedback component (damage directions) and the GameState (phase,
// clocks, kill feed). Widgets read this object in NativeTick and never touch gameplay objects: the one rule a Blueprint
// skin must keep. Tests fill the public members by hand.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/VeloxGameUserSettings.h"
#include "UI/VeloxHUDFormat.h"

#include "VeloxHUDViewModel.generated.h"

class AVeloxPlayerController;

/** One active powerup as the HUD shows it: name and the whole seconds still running (Q3 ceil). */
struct FVeloxHUDPowerup
{
	FString Name;
	int32 Seconds = 0;
	/** The exact remaining time (the widget shows Seconds, never a ratio, ADR 0004). */
	float Remaining = 0.f;
};

/** One kill feed line, names coloured by the players' palette colours. */
struct FVeloxHUDFeedLine
{
	FString Killer;
	/** " [Rocket] " or " killed " style connector between the names; the suicide text for suicides. */
	FString Middle;
	FString Victim;
	FColor KillerColor = FColor::White;
	FColor VictimColor = FColor::White;
	bool bSuicide = false;
	/** The whole line as plain text (VeloxHUDFormat::FeedLine). */
	FString Text;
};

UCLASS()
class VELOX_API UVeloxHUDViewModel : public UObject
{
	GENERATED_BODY()

public:
	/** Reads everything from the owning player's gameplay objects. Cheap enough for every frame (< 0.05 ms, ADR 0004 section 11). */
	void UpdateFromGame(const AVeloxPlayerController& Controller);

	/** Clears to the empty state (no player state yet). */
	void Reset();

	// ----- Local player (the numbers are rounded the Q3 way: to the nearest integer).
	bool bHasPlayer = false;
	/** A pawn exists (the crosshair and the weapon block hide while dead). */
	bool bAlive = false;
	int32 Health = 0;
	int32 Armor = 0;
	int32 MaxHealth = 100;
	/** Current weapon: display name, ammo (< 0: none shown) and table index. */
	FString WeaponName;
	int32 Ammo = -1;
	int32 CurrentWeaponIndex = INDEX_NONE;
	TArray<VeloxHUDFormat::FWeaponBarRow> WeaponRows;
	TArray<FVeloxHUDPowerup> Powerups;

	// ----- Match.
	FString ClockText;
	FString BannerText;

	// ----- Death overlay: shown while waiting for the respawn.
	bool bShowDeath = false;
	FString DeathTitle;
	FString DeathPrompt;

	// ----- Feedback.
	/** 0..1: how much of the hit marker's lifetime is left (0 = hidden). */
	float HitMarkerAlpha = 0.f;
	bool bHitMarkerKill = false;
	/** Intensity 0..1 per VeloxHUDFormat::EDamageQuadrant (Front, Right, Back, Left). */
	float DamageIntensity[4] = { 0.f, 0.f, 0.f, 0.f };
	/** Oldest first, only entries still within FeedEntrySeconds. */
	TArray<FVeloxHUDFeedLine> Feed;

	// ----- Crosshair (the player's user settings, applied live).
	EVeloxCrosshairStyle CrosshairStyle = EVeloxCrosshairStyle::Cross;
	float CrosshairSize = 10.f;
	FColor CrosshairColor = FColor::Green;

	/** Hit marker tick length scale of the combat settings (Slate units at scale 1). */
	float HitMarkerSize = 14.f;
};
