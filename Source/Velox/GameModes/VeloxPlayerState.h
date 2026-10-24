// Velox - player state. Owns the ability system component so abilities and inventory survive respawn (ADR 0001 section 2).

#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/PlayerState.h"

#include "VeloxPlayerState.generated.h"

class UAbilitySystemComponent;
class UVeloxAbilitySystemComponent;

UCLASS(Config = Game)
class VELOX_API AVeloxPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AVeloxPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UObject interface
	virtual void PostInitProperties() override;
	//~End of UObject interface

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End of IAbilitySystemInterface

	UFUNCTION(BlueprintPure, Category = "Velox|PlayerState")
	UVeloxAbilitySystemComponent* GetVeloxAbilitySystemComponent() const;

protected:
	/**
	 * Replication rate for this PlayerState (Hz). The 1 Hz engine default is far too low because the ASC lives here;
	 * the server tick caps the effective rate. Set in DefaultGame.ini; values <= 0 keep the engine default.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Net")
	float ConfigNetUpdateFrequency = 0.0f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Velox|PlayerState")
	TObjectPtr<UVeloxAbilitySystemComponent> AbilitySystemComponent;
};
