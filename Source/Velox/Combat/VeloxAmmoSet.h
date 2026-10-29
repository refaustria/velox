// Velox - ammo attributes, one per Q3 ammo type (ADR 0003 section 2). Replicated to the owner only.

#pragma once

#include "Combat/VeloxAttributeSet.h"
#include "GameplayTagContainer.h"

#include "VeloxAmmoSet.generated.h"

UCLASS(BlueprintType)
class VELOX_API UVeloxAmmoSet : public UVeloxAttributeSet
{
	GENERATED_BODY()

public:
	UVeloxAmmoSet();

	VELOX_ATTRIBUTE_ACCESSORS(UVeloxAmmoSet, Bullets);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxAmmoSet, Shells);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxAmmoSet, Cells);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxAmmoSet, Rockets);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxAmmoSet, Lightning);
	VELOX_ATTRIBUTE_ACCESSORS(UVeloxAmmoSet, Slugs);

	/** The six (ammo tag, attribute) pairs in Q3 weapon order (MG, SG, PG, RL, LG, RG). Fixed in code like the attributes. */
	static const TArray<TPair<FGameplayTag, FGameplayAttribute>>& GetAmmoTypes();

	/** The ammo attribute for an ammo tag (through UVeloxCombatSettings::AmmoTypes); invalid if unknown. */
	static FGameplayAttribute GetAmmoAttribute(const FGameplayTag& AmmoTag, const UVeloxCombatSettings& Settings);

	/** Cost check shared by the fire abilities' CheckCost (task 04): enough ammo for one shot. Integer semantics. */
	static bool CanAfford(float CurrentAmmo, int32 AmmoPerShot) { return AmmoPerShot <= 0 || FMath::FloorToInt32(CurrentAmmo + 0.5f) >= AmmoPerShot; }

	/** Clamp to [0, MaxAmount] of the attribute's ammo row and round to an integer; other attributes pass through. */
	static float ClampAmmo(const FGameplayAttribute& Attribute, float NewValue, const UVeloxCombatSettings& Settings);

	//~UAttributeSet interface
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~End of UAttributeSet interface

protected:
	UFUNCTION()
	void OnRep_Bullets(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Shells(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Cells(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Rockets(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Lightning(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Slugs(const FGameplayAttributeData& OldValue);

private:
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Bullets, Category = "Velox|Ammo", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Bullets;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Shells, Category = "Velox|Ammo", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Shells;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Cells, Category = "Velox|Ammo", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Cells;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Rockets, Category = "Velox|Ammo", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Rockets;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Lightning, Category = "Velox|Ammo", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Lightning;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Slugs, Category = "Velox|Ammo", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Slugs;
};
