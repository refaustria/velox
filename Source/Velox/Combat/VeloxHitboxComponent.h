// Velox - pawn hit volumes for hitscan and lag compensation (ADR 0003 section 6).

#pragma once

#include "Combat/VeloxHitMath.h"
#include "Components/ActorComponent.h"

#include "VeloxHitboxComponent.generated.h"

class ACharacter;

/**
 * Exposes the owning character's hit shapes as an FVeloxHitboxPose. M2: one capsule equal to the collision capsule at
 * its current size (crouch shrinks it). The collision capsule itself ignores the Weapon trace channel; hitscan hits on
 * pawns are resolved against these poses (server: rewound by the lag compensation subsystem, task 03; client: the
 * rendered pose for the cosmetic trace). No tick, no replication: the pose is derived from replicated movement state.
 */
UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxHitboxComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVeloxHitboxComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static UVeloxHitboxComponent* FindHitboxComponent(const AActor* Actor);

	/** Pose at the simulated (server: authoritative) location of the collision capsule. Empty without a capsule. */
	FVeloxHitboxPose GetCurrentPose() const;
	void GetCurrentPose(FVeloxHitboxPose& OutPose) const;

	/**
	 * Pose where the pawn is drawn: the current pose shifted by the network smoothing offset of the mesh (simulated
	 * proxies in Linear smoothing, ADR 0003 section 6). Equals the current pose where nothing is smoothed (server,
	 * owning client) or without a mesh.
	 */
	FVeloxHitboxPose GetRenderedPose() const;
	void GetRenderedPose(FVeloxHitboxPose& OutPose) const;

	/** World-space offset of the drawn mesh from its unsmoothed place (zero on the server and the owning client). */
	FVector GetRenderedOffset() const;

protected:
	//~UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of UActorComponent interface

private:
	ACharacter* GetCharacter() const;
};
