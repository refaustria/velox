// Velox - movement debug and tuning tooling (task 05).

#include "Movement/VeloxMoveDebug.h"

#if !UE_BUILD_SHIPPING

#include "Character/VeloxCharacter.h"
#include "Containers/Ticker.h"
#include "Core/VeloxLog.h"
#include "DisplayDebugHelpers.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Movement/VeloxMovementComponent.h"
#include "Movement/VeloxMovementSettings.h"
#include "Movement/VeloxScriptedInputComponent.h"
#include "UObject/UObjectIterator.h"

// ---------------------------------------------------------------------------------------------------------------------
// Field table: every FVeloxMoveParams field is listed once here; cvars, overrides, dump and presets are generated from it.

#define VELOX_MOVE_FLOAT_FIELDS(X) \
	X(UnitScale) X(MaxGroundSpeed) X(GroundAcceleration) X(GroundFriction) X(StopSpeed) X(CrouchSpeedScale) \
	X(StepHeight) X(WalkableFloorAngle) X(EdgePerchRadius) X(AirAcceleration) X(AirMaxWishSpeed) \
	X(AirStopAcceleration) X(AirStrafeAcceleration) X(AirStrafeMaxWishSpeed) X(AirControl) X(AirControlScale) \
	X(JumpVelocity) X(GravityZ) X(RampSlideMinNormalSpeed) X(RampExitMinAngle) X(KnockbackGraceMin) \
	X(KnockbackGraceMax) X(KnockbackGracePerSpeed) X(FixedStep) X(HasteSpeedScale)

#define VELOX_MOVE_BOOL_FIELDS(X) \
	X(bAirControl) X(bAutoHop) X(bRampExitCatchAir) X(bFixedStepIntegration) X(bCombineAirMoves)

namespace VeloxMoveDebug
{
	/** "Not overridden" marker of every field cvar (ADR 0002 section 6). */
	constexpr float Unset = -1e30f;

	// Pass-through at 0 skips the compare cost entirely: one static per field.
#define VELOX_DECLARE_CVAR(Name) \
	static TAutoConsoleVariable<float> CVar_##Name(TEXT("velox.move." #Name), Unset, \
		TEXT("Override of UVeloxMovementSettings::" #Name " (-1e30 = use the asset). Non-shipping."), ECVF_Default);
	VELOX_MOVE_FLOAT_FIELDS(VELOX_DECLARE_CVAR)
	VELOX_MOVE_BOOL_FIELDS(VELOX_DECLARE_CVAR)
#undef VELOX_DECLARE_CVAR

	void ApplyOverrides(FVeloxMoveParams& P)
	{
#define VELOX_APPLY_FLOAT(Name) { const float V = CVar_##Name.GetValueOnAnyThread(); if (V != Unset) { P.Name = V; } }
#define VELOX_APPLY_BOOL(Name) { const float V = CVar_##Name.GetValueOnAnyThread(); if (V != Unset) { P.Name = V > 0.5f; } }
		VELOX_MOVE_FLOAT_FIELDS(VELOX_APPLY_FLOAT)
		VELOX_MOVE_BOOL_FIELDS(VELOX_APPLY_BOOL)
#undef VELOX_APPLY_FLOAT
#undef VELOX_APPLY_BOOL
	}

	static void SetFieldCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*FString::Printf(TEXT("velox.move.%s"), Name)))
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}

	void ClearOverrides()
	{
#define VELOX_CLEAR(Name) SetFieldCVar(TEXT(#Name), Unset);
		VELOX_MOVE_FLOAT_FIELDS(VELOX_CLEAR)
		VELOX_MOVE_BOOL_FIELDS(VELOX_CLEAR)
#undef VELOX_CLEAR
	}

	void ReapplyToAllComponents()
	{
		for (TObjectIterator<UVeloxMovementComponent> It; It; ++It)
		{
			if (It->GetMovementSettings() && It->GetWorld() && !It->IsTemplate())
			{
				It->SetMovementSettings(It->GetMovementSettings());
			}
		}
	}

	bool ApplyPreset(const FString& Name)
	{
		// Reference sets, Q3 values times UnitScale 2.5 (ADR 0002 section 1 table). Unlisted fields stay at the asset.
		constexpr float Q3Unit = 2.5f;
		const bool bQ3 = Name.Equals(TEXT("Q3"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("VQ3"), ESearchCase::IgnoreCase);
		const bool bCPM = Name.Equals(TEXT("CPM"), ESearchCase::IgnoreCase);
		if (!bQ3 && !bCPM)
		{
			return false;
		}
		ClearOverrides();

		// Common to both: Q3 g_speed 320, pm_friction 6, pm_airaccelerate 1, JUMP_VELOCITY 270, g_gravity 800.
		SetFieldCVar(TEXT("UnitScale"), Q3Unit);
		SetFieldCVar(TEXT("MaxGroundSpeed"), 320.f * Q3Unit);
		SetFieldCVar(TEXT("GroundFriction"), 6.f);
		SetFieldCVar(TEXT("StopSpeed"), 100.f * Q3Unit);
		SetFieldCVar(TEXT("AirAcceleration"), 1.f);
		SetFieldCVar(TEXT("AirMaxWishSpeed"), 320.f * Q3Unit);
		SetFieldCVar(TEXT("JumpVelocity"), 270.f * Q3Unit);
		SetFieldCVar(TEXT("GravityZ"), -800.f * Q3Unit);
		if (bQ3)
		{
			// Vanilla: pm_accelerate 10, no CPM air rules, jump only after a release (PMF_JUMP_HELD latch).
			SetFieldCVar(TEXT("GroundAcceleration"), 10.f);
			SetFieldCVar(TEXT("bAirControl"), 0.f);
			SetFieldCVar(TEXT("bAutoHop"), 0.f);
		}
		else
		{
			// CPM: pm_accelerate 15, air stop 2.5, strafe accel 70 with wish cap 30 u, air control 150 / k 32, auto-hop.
			SetFieldCVar(TEXT("GroundAcceleration"), 15.f);
			SetFieldCVar(TEXT("bAirControl"), 1.f);
			SetFieldCVar(TEXT("AirStopAcceleration"), 2.5f);
			SetFieldCVar(TEXT("AirStrafeAcceleration"), 70.f);
			SetFieldCVar(TEXT("AirStrafeMaxWishSpeed"), 30.f * Q3Unit);
			SetFieldCVar(TEXT("AirControl"), 150.f);
			SetFieldCVar(TEXT("AirControlScale"), 32.f * Q3Unit);
			SetFieldCVar(TEXT("bAutoHop"), 1.f);
		}
		ReapplyToAllComponents();
		return true;
	}

	namespace
	{
		const UVeloxMovementComponent* FindLocalComponent()
		{
			if (!GEngine)
			{
				return nullptr;
			}
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				const UWorld* World = Context.World();
				const APlayerController* PC = (World && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)) ? World->GetFirstPlayerController() : nullptr;
				if (const AVeloxCharacter* Character = PC ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr)
				{
					return Character->GetVeloxMovementComponent();
				}
			}
			return nullptr;
		}

		void Row(const TCHAR* Name, float Asset, float Effective)
		{
			// Through the log: the output device of a command run from -ExecCmds does not reach the log file.
			UE_LOG(LogVeloxMovement, Log, TEXT("  %-26s %12.5g %12.5g %s"), Name, Asset, Effective, Asset != Effective ? TEXT("<- cvar override") : TEXT(""));
		}
	}

	void DumpSettings(const UVeloxMovementComponent* Component, FOutputDevice& Ar)
	{
		if (!Component)
		{
			Component = FindLocalComponent();
		}
		const UVeloxMovementSettings* Settings = Component ? Component->GetMovementSettings() : GetDefault<UVeloxMovementSettings>();
		if (!Settings)
		{
			UE_LOG(LogVeloxMovement, Warning, TEXT("velox.move.DumpSettings: component has no movement settings"));
			return;
		}
		const FVeloxMoveParams Base = Settings->BuildBaseSimParams();
		const FVeloxMoveParams Effective = Settings->BuildSimParams();
		UE_LOG(LogVeloxMovement, Log, TEXT("Velox movement settings (%s): asset value, effective value"), *GetNameSafe(Settings));
#define VELOX_DUMP(Name) Row(TEXT(#Name), static_cast<float>(Base.Name), static_cast<float>(Effective.Name));
		VELOX_MOVE_FLOAT_FIELDS(VELOX_DUMP)
		VELOX_MOVE_BOOL_FIELDS(VELOX_DUMP)
#undef VELOX_DUMP
	}

	void BuildStatusLines(const UVeloxMovementComponent& C, bool bDetailed, TArray<FString>& Out)
	{
		const FVeloxMoveParams& P = C.GetSimParams();
		const float UnitScale = P.UnitScale > 0.f ? P.UnitScale : 1.f;
		const FVector V = C.Velocity;
		Out.Add(FString::Printf(TEXT("speed XY %.0f cm/s (%.0f ups)   Z %.0f   %s"), C.GetSpeed2D(), C.GetSpeed2D() / UnitScale, V.Z, C.IsOnGround() ? TEXT("GROUND") : TEXT("AIR")));
		Out.Add(FString::Printf(TEXT("grace %.3f s   latch %d   jumpheld %d"), C.GetKnockbackGrace(), C.IsJumpLatched() ? 1 : 0, C.IsJumpHeld() ? 1 : 0));
		const UWorld* World = C.GetWorld();
		const double Age = (World && C.GetLastCorrectionTime() >= 0.0) ? World->GetTimeSeconds() - C.GetLastCorrectionTime() : -1.0;
		Out.Add(FString::Printf(TEXT("corrections %d   max %.1f cm   last %.1f cm (%s)"), C.GetNumCorrections(), C.GetMaxCorrectionCm(), C.GetLastCorrectionCm(),
			Age >= 0.0 ? *FString::Printf(TEXT("%.1f s ago"), Age) : TEXT("never")));
		if (bDetailed)
		{
			const FVeloxPendingKnockback& Pending = C.GetPendingKnockback();
			Out.Add(FString::Printf(TEXT("mode %d   pending knockback %s %s"), static_cast<int32>(C.MovementMode.GetValue()),
				Pending.IsPending() ? *Pending.Velocity.ToCompactString() : TEXT("none"), Pending.bOverride ? TEXT("(override)") : TEXT("")));
			Out.Add(FString::Printf(TEXT("params: speed %.0f  gAccel %.1f  fric %.1f  airAccel %.2f  autohop %d  aircontrol %d  jump %.0f  g %.0f"), P.MaxGroundSpeed, P.GroundAcceleration,
				P.GroundFriction, P.AirAcceleration, P.bAutoHop ? 1 : 0, P.bAirControl ? 1 : 0, P.JumpVelocity, P.GravityZ));
			Out.Add(FString::Printf(TEXT("substeps/move at 120 fps: %d   (fixed step %.4f s, %s)"), VeloxMoveSim::NumSubsteps(1.f / 120.f, P), P.FixedStep, P.bFixedStepIntegration ? TEXT("on") : TEXT("off")));
			Out.Add(FString::Printf(TEXT("settings: %s   cvar overrides: see velox.move.DumpSettings"), *GetNameSafe(C.GetMovementSettings())));
		}
	}

	void DrawDebug(UCanvas* Canvas, const UVeloxMovementComponent& C, float& YL, float& YPos)
	{
		if (!Canvas)
		{
			return;
		}
		FDisplayDebugManager& Manager = Canvas->DisplayDebugManager;
		Manager.SetDrawColor(FColor::Yellow);
		Manager.DrawString(TEXT("VELOX MOVEMENT"));
		Manager.SetDrawColor(FColor::White);
		TArray<FString> Lines;
		BuildStatusLines(C, true, Lines);
		for (const FString& Line : Lines)
		{
			Manager.DrawString(Line);
		}
		YL = Manager.GetMaxCharHeight();
		YPos = Manager.GetYPos();
	}

	// -----------------------------------------------------------------------------------------------------------------
	// CSV recorder

	namespace
	{
		struct FRecorder
		{
			bool bActive = false;
			FString File;
			FString Buffer;
			int32 Rows = 0;
			double TimeOrigin = -1.0;
			FTSTicker::FDelegateHandle Handle;
		};
		FRecorder GRecorder;

		constexpr TCHAR RecorderHeader[] = TEXT("time,st,x,y,z,vx,vy,vz,speed2d,speed_ups,onground,grace,latch,corrections,corr_last_cm,corr_max_cm\n");

		void Flush()
		{
			if (!GRecorder.Buffer.IsEmpty())
			{
				FFileHelper::SaveStringToFile(GRecorder.Buffer, *GRecorder.File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
				GRecorder.Buffer.Reset();
			}
		}

		bool Sample(float)
		{
			if (!GRecorder.bActive)
			{
				return false;
			}
			const UVeloxMovementComponent* C = FindLocalComponent();
			if (!C)
			{
				return true;
			}
			const UWorld* World = C->GetWorld();
			const FVector L = C->GetOwner()->GetActorLocation();
			const FVector V = C->Velocity;
			float ScriptTime = -1.f;
			if (const UVeloxScriptedInputComponent* Scripted = C->GetOwner()->FindComponentByClass<UVeloxScriptedInputComponent>())
			{
				ScriptTime = Scripted->IsRunning() ? Scripted->GetScriptTime() : -1.f;
			}
			if (GRecorder.TimeOrigin < 0.0)
			{
				GRecorder.TimeOrigin = World->GetTimeSeconds();
			}
			const float UnitScale = C->GetSimParams().UnitScale > 0.f ? C->GetSimParams().UnitScale : 1.f;
			GRecorder.Buffer += FString::Printf(TEXT("%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%.4f,%d,%d,%.3f,%.3f\n"),
				World->GetTimeSeconds() - GRecorder.TimeOrigin, ScriptTime, L.X, L.Y, L.Z, V.X, V.Y, V.Z, C->GetSpeed2D(), C->GetSpeed2D() / UnitScale,
				C->IsOnGround() ? 1 : 0, C->GetKnockbackGrace(), C->IsJumpLatched() ? 1 : 0, C->GetNumCorrections(), C->GetLastCorrectionCm(), C->GetMaxCorrectionCm());
			if (++GRecorder.Rows % 120 == 0)
			{
				Flush();
			}
			return true;
		}
	}

	bool StartRecording(const FString& File)
	{
		if (GRecorder.bActive)
		{
			StopRecording();
		}
		const FString Full = FPaths::ConvertRelativePathToFull(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), File);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Full), true);
		if (!FFileHelper::SaveStringToFile(FString(RecorderHeader), *Full, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogVeloxMovement, Error, TEXT("velox.move.Record: cannot write %s"), *Full);
			return false;
		}
		GRecorder.File = Full;
		GRecorder.Buffer.Reset();
		GRecorder.Rows = 0;
		GRecorder.TimeOrigin = -1.0;
		GRecorder.bActive = true;
		GRecorder.Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Sample), 0.f);
		UE_LOG(LogVeloxMovement, Log, TEXT("velox.move.Record: recording to %s"), *Full);
		return true;
	}

	void StopRecording()
	{
		if (!GRecorder.bActive)
		{
			return;
		}
		GRecorder.bActive = false;
		FTSTicker::GetCoreTicker().RemoveTicker(GRecorder.Handle);
		Flush();
		UE_LOG(LogVeloxMovement, Log, TEXT("velox.move.Record: stopped, %d rows in %s"), GRecorder.Rows, *GRecorder.File);
	}

	bool IsRecording()
	{
		return GRecorder.bActive;
	}

	// -----------------------------------------------------------------------------------------------------------------
	// Console commands

	namespace
	{
		FAutoConsoleCommandWithOutputDevice DumpSettingsCommand(TEXT("velox.move.DumpSettings"),
			TEXT("Prints asset and effective movement settings (cvar overrides marked)."),
			FConsoleCommandWithOutputDeviceDelegate::CreateLambda([](FOutputDevice& Ar) { DumpSettings(nullptr, Ar); }));

		FAutoConsoleCommand PresetCommand(TEXT("velox.move.Preset"),
			TEXT("velox.move.Preset Q3|CPM: sets the reference rule set through the field cvars (set it on server and clients)."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 1 || !ApplyPreset(Args[0]))
				{
					UE_LOG(LogVeloxMovement, Warning, TEXT("Usage: velox.move.Preset Q3|CPM"));
				}
				else
				{
					UE_LOG(LogVeloxMovement, Log, TEXT("velox.move.Preset %s applied"), *Args[0]);
				}
			}));

		FAutoConsoleCommand ClearOverridesCommand(TEXT("velox.move.ClearOverrides"),
			TEXT("Unsets every velox.move.<Field> override."),
			FConsoleCommandDelegate::CreateLambda([]() { ClearOverrides(); ReapplyToAllComponents(); }));

		FAutoConsoleCommand RecordCommand(TEXT("velox.move.Record"),
			TEXT("velox.move.Record <file.csv>: records the local pawn every frame (project-relative path); without argument stops."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.IsEmpty())
				{
					StopRecording();
				}
				else
				{
					StartRecording(Args[0]);
				}
			}));

		FAutoConsoleCommandWithWorldAndArgs ProbeCommand(TEXT("velox.move.Probe"),
			TEXT("velox.move.Probe X0 Y0 Z0 X1 Y1 Z1: sweeps the local pawn's capsule (pawn channel) between two points and logs every hit."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
			{
				const UVeloxMovementComponent* Component = FindLocalComponent();
				const ACharacter* Character = Component ? Cast<ACharacter>(Component->GetOwner()) : nullptr;
				if (!Character || Args.Num() < 6)
				{
					UE_LOG(LogVeloxMovement, Warning, TEXT("Usage: velox.move.Probe X0 Y0 Z0 X1 Y1 Z1 (needs a local Velox pawn)"));
					return;
				}
				const FVector From(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
				const FVector To(FCString::Atod(*Args[3]), FCString::Atod(*Args[4]), FCString::Atod(*Args[5]));
				const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
				FCollisionQueryParams Params(SCENE_QUERY_STAT(VeloxProbe), false, Character);
				TArray<FHitResult> Hits;
				Component->GetWorld()->SweepMultiByChannel(Hits, From, To, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(Capsule->GetUnscaledCapsuleRadius(), Capsule->GetUnscaledCapsuleHalfHeight()), Params);
				UE_LOG(LogVeloxMovement, Log, TEXT("velox.move.Probe %s -> %s: %d hit(s)"), *From.ToCompactString(), *To.ToCompactString(), Hits.Num());
				for (const FHitResult& Hit : Hits)
				{
					UE_LOG(LogVeloxMovement, Log, TEXT("  %s / %s blocking=%d time=%.3f at %s"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), Hit.bBlockingHit, Hit.Time, *Hit.ImpactPoint.ToCompactString());
				}
			}));

		FAutoConsoleCommand StopRecordCommand(TEXT("velox.move.StopRecord"), TEXT("Stops the CSV recorder."),
			FConsoleCommandDelegate::CreateLambda([]() { StopRecording(); }));

		FAutoConsoleCommand RunScriptCommand(TEXT("velox.move.RunScript"),
			TEXT("velox.move.RunScript <name|path> [quit]: replays an input script on the local pawn once it stands on the ground; quit exits with 0/1."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.IsEmpty())
				{
					UE_LOG(LogVeloxMovement, Warning, TEXT("Usage: velox.move.RunScript <name|path> [quit]"));
					return;
				}
				UVeloxScriptedInputComponent::RequestRun(Args[0], Args.Num() > 1 && Args[1].Equals(TEXT("quit"), ESearchCase::IgnoreCase));
			}));
	}
}

#endif // !UE_BUILD_SHIPPING
