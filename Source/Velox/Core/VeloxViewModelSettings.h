// Velox - first-person view model tunables (ADR 0006 section 4.6). One asset: /Game/Characters/Velox/DA_ViewModelSettings_Default
// (bootstrap script). Data only; the C++ defaults are the shipped values, so a missing asset behaves the same.

#pragma once

#include "Engine/DataAsset.h"

#include "VeloxViewModelSettings.generated.h"

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox View Model Settings"))
class VELOX_API UVeloxViewModelSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Horizontal FOV the view model is rendered with (camera FirstPersonFOV), independent of the world FOV. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Render", Meta = (ClampMin = "10", ClampMax = "170", Units = "deg"))
	float FirstPersonFieldOfView = 70.f;

	/** Camera FirstPersonScale: shrinks the view model toward the camera so it cannot clip into walls. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Render", Meta = (ClampMin = "0.01", ClampMax = "1"))
	float FirstPersonScale = 0.6f;

	/** Ground distance of one bob cycle (the bob is distance-driven, Q3 cg_bobup style). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Bob", Meta = (ClampMin = "1", Units = "cm"))
	float BobCycleCm = 260.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Bob", Meta = (ClampMin = "0", Units = "cm"))
	float BobAmplitudeCm = 0.6f;

	/** Sideways bob amplitude as a fraction of BobAmplitudeCm (one cycle per two vertical cycles). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Bob", Meta = (ClampMin = "0"))
	float BobLateralScale = 0.7f;

	/** Below this horizontal speed (or airborne) there is no bob. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Bob", Meta = (ClampMin = "0", Units = "cm/s"))
	float BobMinSpeed = 50.f;

	/** Weapon rotation (deg) per degree of view rotation in one frame, opposing the turn (the weapon lags the view). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Sway", Meta = (ClampMin = "0"))
	float SwayDegPerDeg = 0.35f;

	/** Weapon translation (cm) per degree of sway rotation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Sway", Meta = (ClampMin = "0", Units = "cm"))
	float SwayCmPerDeg = 0.12f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Sway", Meta = (ClampMin = "0", Units = "cm"))
	float SwayMaxCm = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Sway", Meta = (ClampMin = "0", Units = "deg"))
	float SwayMaxDeg = 4.f;

	/** Time constant of the first-order return of the sway to rest. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Sway", Meta = (ClampMin = "0.001", Units = "s"))
	float SwayReturnSeconds = 0.12f;

	/** Backward kick (cm) per Fire cue. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Recoil", Meta = (ClampMin = "0", Units = "cm"))
	float RecoilKickCm = 2.5f;

	/** Muzzle-up pitch (deg) per Fire cue. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Recoil", Meta = (ClampMin = "0", Units = "deg"))
	float RecoilPitchDeg = 2.f;

	/** The kick has decayed below 1 % after this long. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Recoil", Meta = (ClampMin = "0.01", Units = "s"))
	float RecoilReturnSeconds = 0.3f;

	/** Vertical offset of a fully lowered weapon (switch drop; the raise comes back from it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|ViewModel|Switch", Meta = (ClampMin = "0", Units = "cm"))
	float LowerCm = 14.f;
};
