// Velox - attribute set base (ADR 0001 section 2, ADR 0003 section 2). Attribute sets live on AVeloxPlayerState as
// default subobjects; values are floats holding integers; replication is push-model (GAS marks the properties dirty).

#pragma once

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"

#include "VeloxAttributeSet.generated.h"

class UVeloxAbilitySystemComponent;
class UVeloxCombatSettings;

/** Getter/setter/initter boilerplate for one attribute (engine macros, Lyra naming). */
#define VELOX_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS(Abstract)
class VELOX_API UVeloxAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UVeloxAbilitySystemComponent* GetVeloxAbilitySystemComponent() const;

	/** Combat settings of the owning player state (or the class defaults when there is none, e.g. in tests). */
	const UVeloxCombatSettings& GetCombatSettings() const;
};
