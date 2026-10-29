// Velox - one item as data (ADR 0004 section 1). Assets: /Game/Items/DA_Item_<Type>_<Name> (Tools/editor/create_item_content.py,
// Q3 1.32 numbers). A placed AVeloxPickup references its definition directly; there is no item table and no net id.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Items/VeloxItemTypes.h"
#include "Math/Color.h"
#include "Math/Interval.h"
#include "Templates/SubclassOf.h"

#include "VeloxItemDefinition.generated.h"

class UGameplayEffect;
class UVeloxWeaponDefinition;

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Item Definition"))
class VELOX_API UVeloxItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The quantity the pickup rules use: Weapon->PickupAmmo for weapons (one source of truth), else Quantity. */
	int32 GetEffectiveQuantity() const;

	/** Velox.Item.<Type>.<Name>. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item", Meta = (Categories = "Velox.Item"))
	FGameplayTag ItemTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item")
	EVeloxItemType Type = EVeloxItemType::Health;

	/** Health / armor / ammo points (Q3 item quantity). Weapons use Weapon->PickupAmmo, powerups PowerupSeconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item", Meta = (ClampMin = "0"))
	int32 Quantity = 0;

	/** Health items only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item", Meta = (EditCondition = "Type == EVeloxItemType::Health"))
	EVeloxHealthCap HealthCap = EVeloxHealthCap::MaxHealth;

	/** Seconds until the item is back after a take (weapons: the game mode's WeaponRespawnSeconds instead). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item", Meta = (ClampMin = "0", Units = "s"))
	float RespawnSeconds = 35.f;

	/** First appearance after level start / reset, uniform in the interval (0-0 except powerups: Q3 45 +- 15 s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item")
	FFloatInterval InitialSpawnDelay = FFloatInterval(0.f, 0.f);

	/** Everyone hears the take (Q3 EV_GLOBAL_ITEM_PICKUP; powerups). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item")
	bool bGlobalPickupSound = false;

	/** Mega, Red / Yellow Armor, powerups: item timers (spectators) and bot priority. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item")
	bool bMajorItem = false;

	/** M4 bots: base value of the item before distance / need weighting (Q3 item weights by tier). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Bots", Meta = (ClampMin = "0"))
	float BotBaseValue = 0.f;

	/** Weapon items: the weapon granted (must be in the combat settings' weapon table). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item", Meta = (EditCondition = "Type == EVeloxItemType::Weapon"))
	TObjectPtr<const UVeloxWeaponDefinition> Weapon;

	/** Ammo items: Velox.Ammo.<Type>. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item", Meta = (Categories = "Velox.Ammo", EditCondition = "Type == EVeloxItemType::Ammo"))
	FGameplayTag AmmoTag;

	/**
	 * Powerup items: the duration effect (task 02 adds UVeloxGE_Powerup and its four subclasses and narrows this type;
	 * until then the grant refuses powerups and logs why).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item", Meta = (EditCondition = "Type == EVeloxItemType::Powerup"))
	TSubclassOf<UGameplayEffect> PowerupEffect;

	/** Powerup items: seconds one pickup adds (Q3 30). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Item", Meta = (ClampMin = "0", Units = "s", EditCondition = "Type == EVeloxItemType::Powerup"))
	float PowerupSeconds = 0.f;

	/** Reserved for M5 FX on the pawn (M3 cosmetics are actor-driven). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics", Meta = (Categories = "GameplayCue"))
	FGameplayTag PickupCueTag;

	/** Greybox look: unlit colour and scale of the placeholder mesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	FColor PlaceholderColor = FColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	FVector PlaceholderScale = FVector(0.4, 0.4, 0.4);
};
