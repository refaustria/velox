// Velox - gameplay effect context carrying hit data (ADR 0003 section 2; Lyra context-subclass idea). Allocated for every
// spec by UVeloxAbilitySystemGlobals::AllocGameplayEffectContext.

#pragma once

#include "Engine/NetSerialization.h"
#include "GameplayEffectTypes.h"

#include "VeloxGameplayEffectContext.generated.h"

/** FVeloxGameplayEffectContext::Flags. */
enum class EVeloxDamageFlags : uint8
{
	None = 0,
	/** Victim is the instigator (half damage, SelfKnockbackScale). */
	Self = 1 << 0,
	/** Splash damage (direction from RadiusKnockbackDir). */
	Radius = 1 << 1,
	/** Q3 DAMAGE_NO_ARMOR. */
	NoArmor = 1 << 2,
	/** Q3 DAMAGE_NO_KNOCKBACK. */
	NoKnockback = 1 << 3,
};
ENUM_CLASS_FLAGS(EVeloxDamageFlags);

USTRUCT()
struct VELOX_API FVeloxGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

	/** The Velox context of a handle, or null when the handle holds another context type. */
	static const FVeloxGameplayEffectContext* Get(const FGameplayEffectContextHandle& Handle);
	static FVeloxGameplayEffectContext* Get(FGameplayEffectContextHandle& Handle);

	bool HasFlag(EVeloxDamageFlags Flag) const { return EnumHasAnyFlags(static_cast<EVeloxDamageFlags>(Flags), Flag); }
	void AddFlag(EVeloxDamageFlags Flag) { Flags |= static_cast<uint8>(Flag); }

	//~FGameplayEffectContext interface
	virtual FGameplayEffectContext* Duplicate() const override;
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	virtual FString ToString() const override;
	//~End of FGameplayEffectContext interface

	/** Unit direction of the knockback (shot direction, projectile velocity or radius direction); zero = no knockback. */
	UPROPERTY()
	FVector_NetQuantizeNormal HitDirection = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize100 HitLocation = FVector::ZeroVector;

	/** Per-weapon knockback scale. */
	UPROPERTY()
	float KnockbackScale = 1.f;

	/** Shot sequence of the firing player (task 04/05). */
	UPROPERTY()
	uint8 ShotSeq = 0;

	/**
	 * ADR 0003 B21: the server's pattern seed of a hitscan shot (UVeloxWeaponInventoryComponent::AllocateServerPatternSeq),
	 * set on every hitscan cue by the server. A receiver rebuilds the MG spread / SG pellets with
	 * VeloxShotDirs::Build(Definition, HitDirection, VeloxShotDirs::MakeSeed(PatternSeq, PlayerId of the instigator)).
	 * 0 on predicted (owner) cues and on non-hitscan contexts.
	 */
	UPROPERTY()
	uint8 PatternSeq = 0;

	/** EVeloxDamageFlags. */
	UPROPERTY()
	uint8 Flags = 0;
};

template<>
struct TStructOpsTypeTraits<FVeloxGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FVeloxGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};
