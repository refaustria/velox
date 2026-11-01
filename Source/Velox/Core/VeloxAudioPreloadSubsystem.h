// Velox - owner of the preloaded audio assets (task M6-01b).
//
// Why not the UVeloxAudioSettings CDO: in a cooked build (RequiresCookedData) every UObject created before
// FEngineLoop::PreInit closes the Disregard-for-GC window -- native class default objects, UDeveloperSettings CDOs
// included -- lives in the permanent disregard pool. GC never traces those objects, so a strong reference such an
// object gains later (the old CDO LoadedAssets array) neither keeps its target alive nor is seen by reachability;
// Development builds catch it in VerifyGCAssumptions (fatal "breaking Disregard for GC assumptions" at the first GC),
// Shipping silently frees the assets. The editor never uses the disregard pool, so the bug is invisible in PIE.
// An engine subsystem is created in UEngine::Init, after the window closes, and is referenced by GEngine's subsystem
// collection, so its UPROPERTY references are traced normally. One per process, like the CDO cache it replaces.

#pragma once

#include "Subsystems/EngineSubsystem.h"

#include "VeloxAudioPreloadSubsystem.generated.h"

class UVeloxAudioSettings;

UCLASS()
class VELOX_API UVeloxAudioPreloadSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	/** GEngine's instance, or null before engine init / without an engine. */
	static UVeloxAudioPreloadSubsystem* Get();

	/** Loads every preload path of Settings once per process and holds the strong references (no-op after the first call). */
	void PreloadOnce(const UVeloxAudioSettings& Settings);

	bool HasPreloaded() const { return bPreloaded; }
	const TArray<TObjectPtr<UObject>>& GetLoadedAssets() const { return LoadedAssets; }

	//~USubsystem interface
	virtual void Deinitialize() override;
	//~End of USubsystem interface

private:
	/** Strong, GC-traced references to the preloaded sounds, attenuation, concurrency and mix assets (clients only). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> LoadedAssets;

	bool bPreloaded = false;
};
