// Velox - player/bot character (ADR 0001 sections 1 and 2).

#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "GameplayTagContainer.h"

#include "VeloxCharacter.generated.h"

class UAbilitySystemComponent;
class UInputComponent;
class UVeloxAbilitySystemComponent;
class UVeloxMovementComponent;
class UVeloxScriptedInputComponent;
class UVeloxPawnData;
struct FInputActionValue;

UCLASS(Config = Game)
class VELOX_API AVeloxCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AVeloxCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~IAbilitySystemInterface
	/** Forwards to the PlayerState's ASC; null until a Velox PlayerState is assigned. */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End of IAbilitySystemInterface

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UVeloxAbilitySystemComponent* GetVeloxAbilitySystemComponent() const;

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UVeloxMovementComponent* GetVeloxMovementComponent() const;

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	const UVeloxPawnData* GetPawnData() const;

	//~APawn interface
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	/** Standing eye height comes from pawn data (the engine would restore the class default on Restart/UnCrouch). */
	virtual void RecalculateBaseEyeHeight() override;
	//~End of APawn interface

	//~AActor interface
	/** `showdebug VeloxMovement` forwards to UVeloxMovementComponent::DisplayVeloxDebug (ACharacter only does so for Physics). */
	virtual void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;
	//~End of AActor interface

protected:
	//~AActor interface
	virtual void PostInitializeComponents() override;
	//~End of AActor interface

	//~APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	//~End of APawn interface

	/** Server: PossessedBy. Owning client: OnRep_PlayerState. Owner = PlayerState, avatar = this pawn. */
	void InitAbilityActorInfo();

	/** Capsule size and eye height from pawn data; identical on server and clients. */
	void ApplyPawnGeometry();

	void Input_Move(const FInputActionValue& InputActionValue);
	void Input_MoveCompleted(const FInputActionValue& InputActionValue);
	void Input_LookMouse(const FInputActionValue& InputActionValue);
	void Input_JumpStarted(const FInputActionValue& InputActionValue);
	void Input_JumpCompleted(const FInputActionValue& InputActionValue);
	void Input_CrouchStarted(const FInputActionValue& InputActionValue);
	void Input_CrouchCompleted(const FInputActionValue& InputActionValue);
	void Input_AbilityInputTagPressed(FGameplayTag InputTag);
	void Input_AbilityInputTagReleased(FGameplayTag InputTag);

	/** Pawn data loaded in PostInitializeComponents. Set in DefaultGame.ini or a Blueprint subclass. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Pawn")
	TSoftObjectPtr<UVeloxPawnData> DefaultPawnData;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Velox|Pawn")
	TObjectPtr<const UVeloxPawnData> PawnData;

private:
	TArray<uint32> AbilityInputBindHandles;

	/** Test tooling (task 05): scripted input replay and test knockback RPC. Created in non-shipping builds only. */
	UPROPERTY()
	TObjectPtr<UVeloxScriptedInputComponent> ScriptedInput;
};
