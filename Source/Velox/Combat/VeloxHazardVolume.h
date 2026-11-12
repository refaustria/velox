// Velox - lethal / damaging world space (ADR 0008 section 3.3): lava (tick damage while the pawn overlaps) and void (one
// EnvironmentKillDamage hit on entry). Placed in the level by Tools/editor/veloxmap.py hazard(); never replicated, never
// predicted. The server applies GE_Damage through VeloxEffects::MakeDamageSpec (no armor, no knockback) from the
// credited attacker's ASC (UVeloxHealthComponent::GetCreditedAttacker) or, without one, from the victim's own ASC, so the
// existing death and scoring path (AVeloxCharacter::HandleDeath -> AVeloxGameMode::PlayerDied) decides frag or suicide.
// Clients learn of the damage through health replication, the damage cue and the kill feed.

#pragma once

#include "GameFramework/Actor.h"

#include "VeloxHazardVolume.generated.h"

class AVeloxCharacter;
class UBoxComponent;
class UPrimitiveComponent;
struct FHitResult;

UENUM(BlueprintType)
enum class EVeloxHazardType : uint8
{
	/** LavaDamagePerTick every LavaTickSeconds while overlapping; the box top is the lava surface. */
	Lava,
	/** EnvironmentKillDamage once on entry. */
	Void,
};

/** Per-pawn lava bookkeeping (server). */
struct FVeloxHazardOccupant
{
	TWeakObjectPtr<AVeloxCharacter> Pawn;
	/** World time of the last damage this volume applied to the pawn; negative = none yet. */
	double LastDamageTime = -1.0;
	/** World time the pawn left the volume; negative = still inside. */
	double LeftTime = -1.0;
};

UCLASS()
class VELOX_API AVeloxHazardVolume : public AActor
{
	GENERATED_BODY()

public:
	AVeloxHazardVolume(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "Velox|Hazard")
	EVeloxHazardType GetHazardType() const { return Type; }

	UFUNCTION(BlueprintPure, Category = "Velox|Hazard")
	UBoxComponent* GetBox() const { return Box; }

	/** World point inside the box (any rotation; the generator places axis-aligned boxes). */
	bool ContainsPoint(const FVector& Point) const;

	/** "lava" | "void" (log lines, runners). */
	static const TCHAR* TypeName(EVeloxHazardType InType);

	/** Server: pawns currently tracked (inside, or left less than a second ago). Tests. */
	int32 NumOccupants() const { return Occupants.Num(); }

	/** Seconds a left pawn's last-damage time is kept (ADR 0008 3.3: boundary jitter must not cause a second hit). */
	static constexpr double KeepLeftSeconds = 1.0;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** Static box, query only, overlaps Pawn only; overlap events on the authority only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Hazard")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Hazard")
	EVeloxHazardType Type = EVeloxHazardType::Lava;

private:
	/** True on a server / standalone world (a level actor that does not replicate has ROLE_Authority on clients too). */
	bool IsServerWorld() const;

	/** Applies this volume's damage to Pawn now (server). Returns false if the pawn cannot take it (dead, no ASC). */
	bool ApplyHazardDamage(AVeloxCharacter& Pawn, double Now);

	FVeloxHazardOccupant* FindOccupant(const AVeloxCharacter* Pawn);
	void UpdateTickEnabled();

	TArray<FVeloxHazardOccupant> Occupants;
};
