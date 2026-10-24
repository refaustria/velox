// Velox - custom character movement component (ADR 0001 sections 1 and 3).
// M0: default CMC behaviour, complete saved-move plumbing and settings hook. Quake movement lands in M1.

#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/VeloxNetworkMoveData.h"

#include "VeloxMovementComponent.generated.h"

class UVeloxMovementSettings;

UCLASS(ClassGroup = (Velox), Meta = (BlueprintSpawnableComponent))
class VELOX_API UVeloxMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UVeloxMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Movement tunables. May be null until pawn data has been applied. */
	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	const UVeloxMovementSettings* GetMovementSettings() const;

	/** Must be called identically on server and owning client (pawn data does this in PostInitializeComponents). */
	void SetMovementSettings(const UVeloxMovementSettings* InMovementSettings);

	UFUNCTION(BlueprintPure, Category = "Velox|Movement")
	bool IsJumpHeld() const { return bWantsJumpHeld; }

	/** Locally controlled input state; replicated to the server through FSavedMove_Velox::FLAG_JumpHeld. */
	void SetJumpHeld(bool bHeld) { bWantsJumpHeld = bHeld; }

	//~UCharacterMovementComponent interface
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	//~End of UCharacterMovementComponent interface

protected:
	//~UCharacterMovementComponent interface
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	//~End of UCharacterMovementComponent interface

	UPROPERTY(EditDefaultsOnly, Category = "Velox|Movement")
	TObjectPtr<const UVeloxMovementSettings> MovementSettings;

	/** Jump button is held this move (sampled every move, unlike ACharacter::bPressedJump). */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Movement")
	uint8 bWantsJumpHeld : 1;

private:
	/** Persistent storage installed via SetNetworkMoveDataContainer in the constructor. */
	FVeloxNetworkMoveDataContainer VeloxNetworkMoveDataContainer;
};
