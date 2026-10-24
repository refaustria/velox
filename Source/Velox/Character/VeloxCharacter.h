// Velox - player/bot character (ADR 0001 sections 1 and 2).

#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "GameplayCueInterface.h"
#include "GameplayTagContainer.h"

#include "VeloxCharacter.generated.h"

class UAbilitySystemComponent;
class UInputComponent;
class UVeloxAbilitySystemComponent;
class UVeloxHealthComponent;
class UVeloxHitboxComponent;
class UVeloxMovementComponent;
class UVeloxScriptedInputComponent;
class UVeloxPawnData;
struct FInputActionValue;

/** Class-default pawn geometry (cm), ADR 0003 section 6. Blueprint subclasses may override the resulting defaults. */
namespace VeloxCharacterGeometry
{
	inline constexpr float CapsuleRadius = 37.5f;
	inline constexpr float CapsuleHalfHeight = 70.f;
	inline constexpr float EyeHeight = 55.f;
	inline constexpr float CrouchedHalfHeight = 50.f;
	inline constexpr float CrouchedEyeHeight = 40.f;
}

UCLASS(Config = Game)
class VELOX_API AVeloxCharacter : public ACharacter, public IAbilitySystemInterface, public IGameplayCueInterface
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

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UVeloxHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Velox|Character")
	UVeloxHitboxComponent* GetHitboxComponent() const { return HitboxComponent; }

	//~IGameplayCueInterface
	/** Native cue handling (ADR 0003 section 5); M2 task 04 forwards to UVeloxCombatFeedbackComponent. */
	virtual void HandleGameplayCue(UObject* Self, FGameplayTag GameplayCueTag, EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters) override;
	//~End of IGameplayCueInterface

	//~APawn interface
	/** Server: InitAbilityActorInfo, then the PlayerState grants its default ability sets once and applies GE_SpawnInit. */
	virtual void PossessedBy(AController* NewController) override;
	/** Server: the pawn stops being the ASC avatar (a corpse is never an avatar). */
	virtual void UnPossessed() override;
	virtual void OnRep_PlayerState() override;
	virtual void OnRep_Controller() override;
	//~End of APawn interface

	//~ACharacter interface
	/** Logs `VeloxCrouch: start|end` with half height and eye height (server, owning client and proxies). */
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	//~End of ACharacter interface

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

	void LogCrouchState(const TCHAR* Event) const;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Character", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UVeloxHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Velox|Character", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UVeloxHitboxComponent> HitboxComponent;

private:
	TArray<uint32> AbilityInputBindHandles;

	/** Test tooling (task 05): scripted input replay and test knockback RPC. Created in non-shipping builds only. */
	UPROPERTY()
	TObjectPtr<UVeloxScriptedInputComponent> ScriptedInput;
};
