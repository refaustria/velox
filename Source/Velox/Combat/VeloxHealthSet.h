// Velox - health and armor (ADR 0003 section 2). The damage execution writes the meta attributes IncomingDamage,
// IncomingArmorDamage and IncomingKnockback; PostGameplayEffectExecute (authority only) moves them into Health/Armor and
// the single knockback entry point UVeloxMovementComponent::AddKnockback(..., EVeloxKnockbackSource::Server).

#pragma once

#include "Combat/VeloxAttributeSet.h"
#include "GameplayTagContainer.h"

#include "VeloxHealthSet.generated.h"

struct FGameplayEffectSpec;
struct FVeloxGameplayEffectContext;
struct FGameplayEffectModCallbackData;

/** One applied damage execution (server). */
struct FVeloxDamageEvent
{
	TWeakObjectPtr<AActor> Instigator;
	TWeakObjectPtr<AActor> Causer;
	/** Spec asset + dynamic tags (Velox.Damage.<Weapon>, Velox.Damage.Self). */
	FGameplayTagContainer DamageTags;
	float HealthLoss = 0.f;
	float ArmorLoss = 0.f;
	/** World-space velocity change handed to AddKnockback (zero when none). */
	FVector Knockback = FVector::ZeroVector;
	float OldHealth = 0.f;
	float NewHealth = 0.f;
	float OldArmor = 0.f;
	float NewArmor = 0.f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FVeloxDamageEventDelegate, const FVeloxDamageEvent& /*Event*/);

UCLASS(BlueprintType)
class VELOX_API UVeloxHealthSet : public UVeloxAttributeSet
{
	GENERATED_BODY()

public:
	UVeloxHealthSet();

	VELOX_ATTRIBUTE_ACCESSORS(UVeloxHealthSet, Health);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxHealthSet, MaxHealth);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxHealthSet, Armor);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxHealthSet, MaxArmor);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxHealthSet, IncomingDamage);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxHealthSet, IncomingArmorDamage);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxHealthSet, IncomingKnockback);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxHealthSet, IncomingHealing);

	/** Server: every damage execution (also zero-loss hits, e.g. protected or armor-only). */
	mutable FVeloxDamageEventDelegate OnDamaged;

	/** Server: Health reached 0 (once until Health is raised above 0 again, e.g. by GE_SpawnInit). */
	mutable FVeloxDamageEventDelegate OnOutOfHealth;

	/** Clamp rules (integers): Health [0, HealthHardCap], MaxHealth [1, HealthHardCap], Armor [0, MaxArmor], MaxArmor >= 0. */
	float ClampAttribute(const FGameplayAttribute& Attribute, float NewValue) const;

	//~UAttributeSet interface
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~End of UAttributeSet interface

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Armor(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxArmor(const FGameplayAttributeData& OldValue);

	/** Commits the three damage metas (IncomingDamage is the commit attribute, written last by the execution). */
	void ApplyIncomingDamage(const FGameplayEffectModCallbackData& Data);
	/** Server: GameplayCue.Character.Damage with Location = the damage source (victim HUD direction indicator). */
	void ExecuteDamageCue(const FGameplayEffectModCallbackData& Data, const FVeloxGameplayEffectContext* Context, float Amount) const;

private:
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "Velox|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Health;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "Velox|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MaxHealth;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Armor, Category = "Velox|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Armor;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxArmor, Category = "Velox|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MaxArmor;

	// Meta attributes: server-side scratch values of one execution, never replicated.
	UPROPERTY(BlueprintReadOnly, Category = "Velox|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData IncomingDamage;

	UPROPERTY(BlueprintReadOnly, Category = "Velox|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData IncomingArmorDamage;

	UPROPERTY(BlueprintReadOnly, Category = "Velox|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData IncomingKnockback;

	/** M3 pickups. */
	UPROPERTY(BlueprintReadOnly, Category = "Velox|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData IncomingHealing;

	bool bOutOfHealth = false;
};
