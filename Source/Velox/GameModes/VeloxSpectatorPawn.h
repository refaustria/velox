// Velox - spectator pawn (ADR 0004 C11): the free fly of dead humans and of spectators. The engine spawns it on the
// owning client only (APlayerController::SpawnSpectatorPawn: IsLocalController(), SetReplicates(false) on the spawned
// pawn); this class never touches bReplicates. Input through Enhanced Input with the character's UVeloxInputConfig tags.

#pragma once

#include "GameFramework/SpectatorPawn.h"

#include "VeloxSpectatorPawn.generated.h"

class UVeloxCombatSettings;
struct FInputActionValue;

UCLASS(NotPlaceable, Transient)
class VELOX_API AVeloxSpectatorPawn : public ASpectatorPawn
{
	GENERATED_BODY()

public:
	AVeloxSpectatorPawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** USpectatorPawnMovement speed, acceleration and deceleration from Settings' DeathSpectate* values. */
	void ApplyMovementSettings(const UVeloxCombatSettings& Settings);

	//~APawn interface
	/** Super (no net-role change: the pawn is unreplicated), then the movement settings of the owner's PlayerState. */
	virtual void PossessedBy(AController* NewController) override;
	/** IA_Move (view-yaw plane), IA_Look_Mouse, IA_Jump = up, IA_Crouch = down; nothing else (no ASC, no abilities). */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	//~End of APawn interface

	//~AActor interface
	virtual void Tick(float DeltaSeconds) override;
	//~End of AActor interface

private:
	void Input_Move(const FInputActionValue& Value);
	void Input_LookMouse(const FInputActionValue& Value);
	void Input_UpStarted(const FInputActionValue& Value);
	void Input_UpCompleted(const FInputActionValue& Value);
	void Input_DownStarted(const FInputActionValue& Value);
	void Input_DownCompleted(const FInputActionValue& Value);

	/** Vertical input held (jump = up, crouch = down); applied every tick. */
	bool bUpHeld = false;
	bool bDownHeld = false;
};
