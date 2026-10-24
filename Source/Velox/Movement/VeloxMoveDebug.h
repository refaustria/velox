// Velox - movement debug and tuning tooling (ADR 0002 section 6, task 05). Non-shipping only.
//
//   velox.move.<Field>          one float cvar per UVeloxMovementSettings field, -1e30 = use the asset (bools: 0/1)
//   velox.move.DumpSettings     effective values, asset value and override marker
//   velox.move.Preset Q3|CPM    reference rule sets (Q3 = VQ3 without auto-hop, CPM = air control + auto-hop)
//   velox.move.ClearOverrides   back to the asset
//   velox.move.Record <file>    CSV run recorder on the local pawn (no argument or velox.move.StopRecord stops)
//   velox.move.RunScript <path> [quit]   see UVeloxScriptedInputComponent
//   showdebug VeloxMovement     component debug block (AVeloxCharacter::DisplayDebug)

#pragma once

#include "CoreMinimal.h"
#include "Movement/VeloxMoveSim.h"

class FOutputDevice;
class UCanvas;
class UVeloxMovementComponent;

#if !UE_BUILD_SHIPPING
namespace VeloxMoveDebug
{
	/** Replaces every field whose velox.move.<Field> cvar is set. Called by UVeloxMovementSettings::BuildSimParams. */
	VELOX_API void ApplyOverrides(FVeloxMoveParams& Params);

	/** Unsets every velox.move.<Field> cvar (tests, "back to the asset"). */
	VELOX_API void ClearOverrides();

	/** "Q3" or "CPM" (case-insensitive); clears all overrides first, then sets the preset's fields. False if unknown. */
	VELOX_API bool ApplyPreset(const FString& Name);

	/** Re-runs SetMovementSettings on every Velox movement component so the engine-side fields follow the cvars. */
	VELOX_API void ReapplyToAllComponents();

	VELOX_API void DumpSettings(const UVeloxMovementComponent* Component, FOutputDevice& Ar);

	/** Text lines of the HUD speed block / showdebug block (Detailed adds the effective parameters). */
	VELOX_API void BuildStatusLines(const UVeloxMovementComponent& Component, bool bDetailed, TArray<FString>& OutLines);

	/** The showdebug VeloxMovement block. */
	VELOX_API void DrawDebug(UCanvas* Canvas, const UVeloxMovementComponent& Component, float& YL, float& YPos);

	// --- CSV recorder (owning client) ---
	VELOX_API bool StartRecording(const FString& File);
	VELOX_API void StopRecording();
	VELOX_API bool IsRecording();
}
#endif
