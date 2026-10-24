// Velox - placed actor that spawns a strafing dummy at its own location (L_CombatTest, ADR 0003 section 6).

#pragma once

#include "GameFramework/Actor.h"

#include "VeloxDummySpawner.generated.h"

class AAIController;
class AVeloxCharacter;

UCLASS()
class VELOX_API AVeloxDummySpawner : public AActor
{
	GENERATED_BODY()

public:
	AVeloxDummySpawner(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Spawns the dummy pawn and its controller (authority only). Returns the pawn, or null. */
	UFUNCTION(BlueprintCallable, Category = "Velox|Dummy")
	AVeloxCharacter* SpawnDummy();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velox|Dummy")
	bool bAutoSpawn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velox|Dummy")
	TSubclassOf<AVeloxCharacter> DummyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velox|Dummy")
	TSubclassOf<AAIController> ControllerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velox|Dummy", meta = (ClampMin = "0", Units = "cm"))
	float StrafeHalfWidth = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velox|Dummy", meta = (ClampMin = "0", ClampMax = "1"))
	float StrafeSpeedScale = 1.f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AVeloxCharacter> SpawnedDummy;
};
