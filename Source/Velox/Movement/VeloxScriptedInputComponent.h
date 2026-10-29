// Velox - scripted input (ADR 0002 section 5 net soak, section 7 FT_MovementDeterminism, task 05).
//
// Replays a JSON input script through the same entry points as player input (AddMovementInput, SetLocalMoveInput,
// SetJumpHeld/Jump, controller yaw), so the movement component, saved moves and prediction see exactly what a human
// would produce. Used by the headless tests (World->Tick with a fixed dt) and by `velox.move.RunScript` / `-VeloxScript=`.
//
// Script format (Content/Data/MoveScripts/*.json), times in seconds, yaw in degrees (UE: positive turns right):
//   { "name": "StrafeRun", "startYaw": 0.0, "startLocation": [x, y, z],   // capsule centre; the pawn is teleported there
//                                                                         // (server side, over the test RPC on a client) before row 0
//     "rows": [
//       { "duration": 0.3, "forward": 1, "right": 0, "yawRate": 0, "jump": false,
//         "yaw": 90,                          // optional: set absolute yaw at row start
//         "until": "airborne" | "ground",     // optional: end the row early on that state (duration = maximum)
//         "serverKnockback": [x, y, z],       // optional: fired once at row start (AddKnockback on the server)
//         "override": false,                  // optional: knockback replaces the velocity
//         "label": "text",                    // optional: logged at row start
//         "check": { "name": "text", "box": [minX, minY, maxX, maxY], "onGround": true }  // optional, at row end
//         // Combat rows (ADR 0003 section 9, task 08 FireRun), through the real ability input path (ASC input tags):
//         "weapon": 7,                        // optional: at row start select the weapon in this slot (InputTag.Weapon.Slot<N>
//                                             // pressed and released) unless it is already the current or pending one
//         "fire": true,                       // fire held for the row (InputTag.Fire pressed on the first row, released after)
//         "shots": 10,                        // optional: the row ends early once this many shots were fired since its start
//         "aimAt": [x, y, z],                 // optional: every tick aim at the rendered centre of the other Velox pawn
//                                             // nearest this point (yaw and pitch; overrides yaw/yawRate)
//         "aimPoint": [x, y, z]               // optional: every tick aim at this fixed world point
//       } ] }
// Top-level keys the component ignores ("note", "dummyLocation", ...) are free for tools (infra/ci/nettest.sh reads
// "dummyLocation" to spawn FireRun's target on the server).
// Hit confirms received by the owning client are logged as `VeloxHitConfirm` while a script runs; with the cvar
// velox.move.LogCorrections 1 the `VeloxCorrection` lines are also logged without a script (RocketJumpRun).
// The whole component is test tooling: the knockback RPC is rejected in shipping builds.

#pragma once

#include "Components/ActorComponent.h"
#include "Containers/Array.h"
#include "Delegates/Delegate.h"

#include "VeloxScriptedInputComponent.generated.h"

class AVeloxCharacter;
class UVeloxMovementComponent;

struct FVeloxScriptCheck
{
	FString Name;
	FBox2D Box = FBox2D(ForceInit);
	bool bHasBox = false;
	bool bOnGround = false;
	bool bCheckOnGround = false;
	bool bCrouched = false;
	bool bCheckCrouched = false;
};

struct FVeloxScriptRow
{
	float Duration = 0.f;
	float Forward = 0.f;
	float Right = 0.f;
	float YawRate = 0.f;
	bool bJump = false;
	/** Crouch held (Character->Crouch/UnCrouch on change, as Input_Crouch*). */
	bool bCrouch = false;
	/** Row ends early (script time skips to its end) once the pawn is airborne / on the ground; Duration is the maximum. */
	enum class EUntil : uint8 { None, Airborne, Ground };
	EUntil Until = EUntil::None;
	TOptional<float> Yaw;
	TOptional<FVector> ServerKnockback;
	bool bKnockbackOverride = false;
	FString Label;
	TOptional<FVeloxScriptCheck> Check;

	/** Weapon slot selected at row start (0 = none). */
	int32 WeaponSlot = 0;
	/** Fire held during the row. */
	bool bFire = false;
	/** Row ends early once this many shots were fired since its start (0 = no limit). */
	int32 Shots = 0;
	/** Aim every tick at the rendered centre of the other Velox pawn nearest this point. */
	TOptional<FVector> AimAt;
	/** Aim every tick at this world point. */
	TOptional<FVector> AimPoint;
};

struct FVeloxMoveScript
{
	FString Name;
	float StartYaw = 0.f;
	TOptional<FVector> StartLocation;
	TArray<FVeloxScriptRow> Rows;

	float TotalDuration() const;
};

/** Outcome of the last run; the net soak evaluates windows on top of these basics (corrections are the owning client's). */
struct FVeloxScriptResult
{
	bool bFinished = false;
	bool bPassed = false;
	int32 Ticks = 0;
	int32 NumCorrections = 0;
	float MaxCorrectionCm = 0.f;
	float SumCorrectionCm = 0.f;
	int32 ChecksRun = 0;
	int32 ChecksFailed = 0;
	FString Reason;
	float PeakSpeed2D = 0.f;   // cm/s, max GetSpeed2D() seen at a script tick
	double PeakTime = -1.0;    // world time of the peak
};

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxScriptedInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVeloxScriptedInputComponent();

	/** Name ("StrafeRun"), project-relative path or absolute path of a script. False (with a log) if it cannot be read. */
	bool LoadScriptFromFile(const FString& PathOrName);
	bool LoadScriptFromJson(const FString& Json, const FString& DefaultName);

	/** Starts the loaded script at the next tick (script time 0 applies row 0 in that tick). */
	bool StartScript();
	void StopScript();

	bool IsRunning() const { return bRunning; }
	float GetScriptTime() const { return ScriptTime; }
	const FVeloxMoveScript& GetScript() const { return Script; }
	const FVeloxScriptResult& GetResult() const { return Result; }

	/**
	 * Advances the script by DeltaTime and applies the current row's input. Called from TickComponent unless the caller
	 * drives the script itself (tests call it before World->Tick, so input and move share a frame and dt is exact).
	 */
	void StepScript(float DeltaTime);
	void SetDrivenExternally(bool bExternal) { bDrivenExternally = bExternal; }

	/** Quit the process with exit code 0/1 when the script ends (soak runs). */
	void SetQuitOnFinish(bool bQuit) { bQuitOnFinish = bQuit; }

	/** Fired once when a run ends, after the result is final. */
	FSimpleMulticastDelegate OnScriptFinished;

	/**
	 * Test-only: applies a knockback to this pawn on the server through AddKnockback (the production path for M2 rockets).
	 * Rejected when |Impulse| > 20 x MaxGroundSpeed and in shipping builds.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerApplyTestKnockback(FVector Impulse, bool bOverride);

	/** Test-only: teleports this pawn on the server to Location/Yaw and stops it (script start positions). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerTeleportTestPawn(FVector Location, float InYaw);

	/** True when the script has a start location and the pawn is not at it (within 5 cm XY). */
	bool NeedsRepositioning() const;

	/** Moves the pawn to the script's start location (direct on authority, RPC otherwise). */
	void RepositionToStart();

	/** Server-side resolution of the "serverKnockback" row key (direct call on authority, RPC otherwise). */
	void SendTestKnockback(const FVector& Impulse, bool bOverride);

	//~UActorComponent interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~End of UActorComponent interface

	/** Resolves a script name/path as documented at LoadScriptFromFile; empty if no file exists. */
	static FString ResolveScriptPath(const FString& PathOrName);

	/** Console/command-line request: start Path on the first locally controlled Velox character once it stands on the ground. */
	static void RequestRun(const FString& PathOrName, bool bQuitOnFinish);

protected:
	virtual void BeginPlay() override;

private:
	void ApplyRow(const FVeloxScriptRow& Row, float DeltaTime);
	void EvaluateCheck(const FVeloxScriptRow& Row);
	void Finish(const TCHAR* Reason);
	void ReleaseInput();
	AVeloxCharacter* GetVeloxCharacter() const;

	/** Logs a VeloxCorrection line per new owning-client correction; returns how many arrived since the last call. */
	int32 LogNewCorrections(UVeloxMovementComponent& Movement);
	/** Logs VeloxHitConfirm when the pawn's feedback component counted new hit confirms (ClientConfirmHit). */
	void LogNewHitConfirms();
	/** Combat rows: weapon select at row start, fire press/release, aim. */
	/** True when a switch was requested (the weapon is owned and neither current nor pending). */
	bool SelectWeaponSlot(int32 Slot);
	void SetFireHeld(bool bHeld);
	bool ApplyAim(const FVeloxScriptRow& Row, FRotator& OutRotation) const;

	FVeloxMoveScript Script;
	FVeloxScriptResult Result;

	bool bRunning = false;
	bool bQuitOnFinish = false;
	bool bDrivenExternally = false;
	bool bJumpHeldByScript = false;
	bool bCrouchHeldByScript = false;
	bool bFireHeldByScript = false;
	bool bWeaponSelectedThisTick = false;
	/** Local shot count (UVeloxGameplayAbility_Fire::GetDebugLocalShotCount) when the current row started. */
	int32 RowStartShotCount = 0;
	int32 LastSeenHitConfirms = 0;
	float ScriptTime = 0.f;
	float Yaw = 0.f;
	int32 CurrentRow = INDEX_NONE;
	int32 LastSeenCorrections = 0;
	double TimeStarted = 0.0;

	// Soak instrumentation (see docs/movement-tooling.md, net soak): landing detection and post-correction samples.
	bool bHavePrev = false;
	bool bPrevOnGround = false;
	float PrevVz = 0.f;
	float PrevSpeed2D = 0.f;
	double PostCorrectionSampleAt = -1.0;
};
