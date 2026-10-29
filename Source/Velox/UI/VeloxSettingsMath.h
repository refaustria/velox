// Velox - world-free settings math (ADR 0004 section 7): Q3 sensitivity convention, clamps, AA mapping, connect options.

#pragma once

#include "CoreMinimal.h"

#include "VeloxSettingsMath.generated.h"

/** Anti-aliasing choice of the settings screen (ADR 0001 forward renderer). */
UENUM(BlueprintType)
enum class EVeloxAntiAliasing : uint8
{
	MSAA4,
	FXAA,
	Off
};

namespace VeloxSettingsMath
{
	/** Q3: degrees per mouse count = 0.022 x sensitivity (m_yaw / m_pitch 0.022, cvar sensitivity). */
	inline constexpr double DegreesPerCountPerSensitivity = 0.022;

	inline constexpr float DefaultSensitivity = 5.f;
	inline constexpr float MinSensitivity = 0.1f;
	inline constexpr float MaxSensitivity = 50.f;

	/** Horizontal FOV (AspectRatio_MaintainXFOV); 105 at 16:9 is about Q3 cg_fov 90 on 4:3. */
	inline constexpr float DefaultFieldOfView = 105.f;
	inline constexpr float MinFieldOfView = 80.f;
	inline constexpr float MaxFieldOfView = 130.f;

	inline constexpr float DefaultCrosshairSize = 24.f;
	inline constexpr float MinCrosshairSize = 4.f;
	inline constexpr float MaxCrosshairSize = 96.f;

	inline constexpr int32 MaxPlayerNameLength = 24;

	/** r.AntiAliasingMethod values (0 none, 1 FXAA, 3 MSAA) and MSAA sample count. */
	inline constexpr int32 AAMethodNone = 0;
	inline constexpr int32 AAMethodFXAA = 1;
	inline constexpr int32 AAMethodMSAA = 3;
	inline constexpr int32 MSAASampleCount = 4;

	inline float ClampSensitivity(float Value) { return FMath::Clamp(Value, MinSensitivity, MaxSensitivity); }
	inline float ClampFieldOfView(float Value) { return FMath::Clamp(Value, MinFieldOfView, MaxFieldOfView); }
	inline float ClampCrosshairSize(float Value) { return FMath::Clamp(Value, MinCrosshairSize, MaxCrosshairSize); }
	inline float ClampVolume(float Value) { return FMath::Clamp(Value, 0.f, 1.f); }

	inline double DegreesPerCount(float Sensitivity) { return DegreesPerCountPerSensitivity * ClampSensitivity(Sensitivity); }

	/** Mouse delta (counts) to a rotation input in degrees; Y is flipped by the invert option. */
	inline FVector2D MouseDeltaToDegrees(const FVector2D& Counts, float Sensitivity, bool bInvertY)
	{
		const double Scale = DegreesPerCount(Sensitivity);
		return FVector2D(Counts.X * Scale, Counts.Y * Scale * (bInvertY ? -1.0 : 1.0));
	}

	/** cvar values of an AA choice (the method, and the MSAA count which MSAA4 sets). */
	inline void AntiAliasingCVars(EVeloxAntiAliasing AntiAliasing, int32& OutMethod, int32& OutMSAACount)
	{
		OutMSAACount = MSAASampleCount;
		switch (AntiAliasing)
		{
		case EVeloxAntiAliasing::FXAA:
			OutMethod = AAMethodFXAA;
			break;
		case EVeloxAntiAliasing::Off:
			OutMethod = AAMethodNone;
			break;
		default:
			OutMethod = AAMethodMSAA;
			break;
		}
	}

	/** A player name safe to put into a URL option: no option/value separators or control characters, trimmed, capped. */
	inline FString SanitizePlayerName(const FString& Name)
	{
		FString Out;
		for (const TCHAR C : Name)
		{
			if (C >= 32 && C != TEXT('?') && C != TEXT('=') && C != TEXT('#') && C != TEXT('"') && C != TEXT('\\') && C != 127)
			{
				Out.AppendChar(C);
			}
		}
		Out.TrimStartAndEndInline();
		return Out.Left(MaxPlayerNameLength);
	}

	/** Address plus `?Name=<sanitised name>` (unchanged when the name is empty or the address already carries a Name option). */
	inline FString AppendNameOption(const FString& Address, const FString& PlayerName)
	{
		const FString Name = SanitizePlayerName(PlayerName);
		if (Name.IsEmpty() || Address.Contains(TEXT("?Name="), ESearchCase::IgnoreCase))
		{
			return Address;
		}
		return Address + TEXT("?Name=") + Name;
	}
}
