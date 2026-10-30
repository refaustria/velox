// Velox - player controller. Pushes the default Enhanced Input mapping context for local players.

#pragma once

#include "GameFramework/PlayerController.h"
#include "Net/VeloxLagCompensationSubsystem.h"

#include "VeloxPlayerController.generated.h"

class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;
class UVeloxInputConfig;
class UEnhancedInputUserSettings;
class UVeloxPauseMenuWidget;
struct FVeloxShotTiming;

/** Flags of AVeloxPlayerController::ClientConfirmHit. */
namespace VeloxHitConfirmFlags
{
	inline constexpr uint8 None = 0;
	inline constexpr uint8 Kill = 1 << 0;
}

UCLASS(Config = Game)
class VELOX_API AVeloxPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AVeloxPlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Lag-compensation timing for a shot along Origin/Dir fired now (ADR 0003 section 6, B6: call where the fire
	 * ability samples its aim): ViewServerTime and the rendered-proxy hint. Include Net/VeloxLagCompensationSubsystem.h.
	 */
	FVeloxShotTiming BuildShotTiming(const FVector& Origin, const FVector& Dir, float RangeCm) const;

	/**
	 * Server -> shooter: shot ShotSeq hit (ADR 0003 section 5; Q3 server-confirmed hit marker, nothing is predicted).
	 * TotalDamage = health + armor removed, clamped to 255; Flags = VeloxHitConfirmFlags. Unreliable, 4 bytes.
	 */
	UFUNCTION(Client, Unreliable)
	void ClientConfirmHit(uint8 ShotSeq, uint8 TotalDamage, uint8 Flags);

	/**
	 * Client -> server: respawn (ADR 0003 section 8). Accepted while the controller waits for a respawn and
	 * MinRespawnDelay has passed since the death; otherwise ignored (the client keeps retrying while fire/jump is held).
	 */
	UFUNCTION(Server, Reliable)
	void ServerRequestRespawn();

	/** Last server-confirmed hit of this player (HUD hit marker): world time (-1 = none), damage and whether it killed. */
	double GetLastHitConfirmTime() const { return LastHitConfirmTime; }
	uint8 GetLastHitConfirmDamage() const { return LastHitConfirmDamage; }
	bool WasLastHitConfirmKill() const { return bLastHitConfirmKill; }

	/** True while this controller has no pawn (dead and waiting, or not yet spawned); local world time it lost its pawn. */
	bool IsWaitingForRespawn() const { return GetPawn() == nullptr && PawnLostTime >= 0.0; }
	double GetPawnLostTime() const { return PawnLostTime; }

	/** Q3 cg_autoswitch (ADR 0004 section 2): UVeloxGameUserSettings::bAutoSwitchOnPickup, read live. */
	bool ShouldAutoSwitchOnPickup() const;

	//~AController interface
	virtual void SetPawn(APawn* InPawn) override;
	//~End of AController interface

	// ---- In-game menu (ADR 0004 section 7; task M3-10) ----

	/** IA_Menu (InputTag.Menu, Escape): opens the pause menu (resume, settings, disconnect), or closes it. */
	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	void ToggleMenu();

	bool IsMenuOpen() const;
	UVeloxPauseMenuWidget* GetPauseMenuWidget() const { return PauseMenu; }

	// ---- Match flow (ADR 0004 section 5; task 05) ----

	/** Client -> server: ready-up (IA_Ready / scoreboard button / `Ready` console command). Spectators are ignored. */
	UFUNCTION(Server, Reliable)
	void ServerSetReady(bool bInReady);

	/** Client -> server: a spectator takes a free player slot (Duel: only in Warmup/PostMatch). */
	UFUNCTION(Server, Reliable)
	void ServerRequestJoin();

	// ---- Bots (ADR 0005 section 12; M4-04) ----

	/**
	 * Client -> server: the `AddBot [Easy|Medium|Hard|Expert] [Name]` console command typed on a client. Non-shipping
	 * builds only (the shipping server ignores it until M6's admin auth) and rate-limited per controller.
	 */
	UFUNCTION(Server, Reliable)
	void ServerAddBot(const FString& DifficultyName, const FString& BotName);

	/** Client -> server: `KickBot [Name|all]` from a client console. Same guard and rate limit as ServerAddBot. */
	UFUNCTION(Server, Reliable)
	void ServerKickBot(const FString& NameOrAll);

	/** Minimum time between two accepted bot commands of one client (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bots", meta = (ClampMin = "0", Units = "s"))
	float BotCommandMinIntervalSeconds = 0.5f;

	/** Console: toggles ready (soaks, headless tests, until IA_Ready lands in task 06). */
	UFUNCTION(Exec)
	void Ready();

	/** Console: leave spectating for a free slot. */
	UFUNCTION(Exec)
	void Join();

	/**
	 * Server -> client at PostMatch: view ViewTarget (the map's intermission camera, else the own pawn), freeze local
	 * movement input (the server has frozen the pawn) and flag the intermission (task 08 forces the scoreboard open).
	 */
	UFUNCTION(Client, Reliable)
	void ClientEnterIntermission(AActor* ViewTarget);

	/** Owning client and server: between ClientEnterIntermission and the level reset, or while the replicated phase is PostMatch (a late joiner / lost RPC). */
	bool IsInIntermission() const;

	/** Scoreboard key (IA_Scoreboard, Tab) is held; the HUD view model shows the scoreboard while it is (ADR 0004 section 7). */
	bool IsScoreboardHeld() const { return bScoreboardHeld; }
	void SetScoreboardHeld(bool bHeld) { bScoreboardHeld = bHeld; }

	/** Server side of ClientEnterIntermission (state and view target; the RPC is sent by the caller). */
	void EnterIntermission(AActor* ViewTarget);

	//~AActor interface
	/** Level reset (match restart): leaves the intermission. */
	virtual void Reset() override;
	//~End of AActor interface

	//~APlayerController interface
	virtual void ClientReset_Implementation() override;
	//~End of APlayerController interface

protected:
	//~APlayerController interface
	virtual void SetupInputComponent() override;
	/**
	 * Super runs the input stack and then UpdateRotation; afterwards this frame's ability input goes to the ASC, so a
	 * fire ability reads the control rotation including this frame's mouse delta (ADR 0003 B6).
	 */
	virtual void PlayerTick(float DeltaTime) override;
	//~End of APlayerController interface

	/** Mapping context added for local players. Set in DefaultGame.ini or a Blueprint subclass. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Input")
	TSoftObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Input")
	int32 DefaultMappingContextPriority = 0;

	/** Actions (fire, jump) that request a respawn while the player has no pawn. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Input")
	TArray<TSoftObjectPtr<UInputAction>> RespawnInputActions;

	/**
	 * Input config whose InputTag.Ready / InputTag.Scoreboard actions this controller binds natively (never through the
	 * ASC, ADR 0004 section 7). DefaultGame.ini: the same asset as the pawn data's.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Input")
	TSoftObjectPtr<UVeloxInputConfig> UIInputConfig;

	/** The action (IA_Menu, tag InputTag.Menu) that toggles the in-game menu; bound natively here, never through the ASC. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Menu")
	TSoftObjectPtr<UInputAction> MenuInputAction;

	/** The pause menu widget class (a WBP_PauseMenu subclass once authored). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Menu")
	TSubclassOf<UVeloxPauseMenuWidget> PauseMenuWidgetClass;

private:
	void BindMenuInput(UEnhancedInputComponent* EnhancedInput);
	/** Cursor and input mode for the intermission (scoreboard Ready button) or plain game input; no-op while the menu is open. */
	void ApplyUIInputMode();
	void OnMenuInput();
	void OpenMenu();
	void CloseMenu();

	UPROPERTY(Transient)
	TObjectPtr<UVeloxPauseMenuWidget> PauseMenu;

	void OnRespawnInput();

	/** Binds ready (toggle) and scoreboard (hold) from UIInputConfig. */
	void BindUIInput(UEnhancedInputComponent& EnhancedInput);
	void OnReadyInput();
	void OnScoreboardPressed();
	void OnScoreboardReleased();

	/** Registers the mapping context with the Enhanced Input user settings once the subsystem has created them. */
	UFUNCTION()
	void OnInputUserSettingsInitialized(const UEnhancedInputUserSettings* Settings);

	double LastHitConfirmTime = -1.0;
	uint8 LastHitConfirmDamage = 0;
	bool bLastHitConfirmKill = false;
	double PawnLostTime = -1.0;
	TWeakObjectPtr<APawn> LastPawn;
	double LastRespawnRequestTime = -1e9;
	/** Server: world time of the last accepted ServerAddBot / ServerKickBot. */
	double LastBotCommandTime = -1e9;
	/** Server: guard (shipping, rate limit, argument length) for the bot RPCs; logs refusals. */
	bool AcceptBotCommand(const TCHAR* Command, const FString& Args);
	bool bInIntermission = false;
	bool bScoreboardHeld = false;

	/** Last measured interpolation delay (ADR 0003 B30 fallback); BuildShotTiming is const. */
	mutable VeloxLagComp::FDelayMemory ShotDelayMemory;
};
