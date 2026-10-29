// Velox - combat tunables.

#include "Combat/VeloxCombatSettings.h"

#include "Combat/VeloxAmmoSet.h"
#include "Core/VeloxGameplayTags.h"
#include "HAL/IConsoleManager.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponTable.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxCombatSettings)

#if !UE_BUILD_SHIPPING
namespace VeloxCombatCVars
{
	constexpr float Unset = -1e30f;

#define VELOX_COMBAT_FLOAT_FIELDS(X) X(SelfDamageScale) X(SelfKnockbackScale) X(KnockbackSpeedPerDamage) X(MaxKnockbackDamage) X(HitboxTeleportDistanceCm) \
	X(BeamKeepAliveSeconds) X(FireRateJitterToleranceSeconds)
#define VELOX_DECLARE_COMBAT_CVAR(Name) static TAutoConsoleVariable<float> CVar_##Name(TEXT("velox.combat." #Name), Unset, \
		TEXT("Override of UVeloxCombatSettings::" #Name " (-1e30 = use the asset). Non-shipping."), ECVF_Default);
	VELOX_COMBAT_FLOAT_FIELDS(VELOX_DECLARE_COMBAT_CVAR)
#undef VELOX_DECLARE_COMBAT_CVAR

	constexpr int32 UnsetInt = -1;
	static TAutoConsoleVariable<int32> CVar_ArmorProtectionPercent(TEXT("velox.combat.ArmorProtectionPercent"), UnsetInt,
		TEXT("Override of UVeloxCombatSettings::ArmorProtectionPercent (-1 = use the asset). Non-shipping."), ECVF_Default);

	static float Get(const TAutoConsoleVariable<float>& CVar, float AssetValue)
	{
		const float V = CVar.GetValueOnAnyThread();
		return V != Unset ? V : AssetValue;
	}
}
#endif

UVeloxCombatSettings::UVeloxCombatSettings()
{
	// Q3 defaults: every ammo type caps at 200. The spawn loadout (MG/100) references assets and lives in the asset.
	for (const TPair<FGameplayTag, FGameplayAttribute>& Type : UVeloxAmmoSet::GetAmmoTypes())
	{
		FVeloxAmmoTypeInfo& Info = AmmoTypes.AddDefaulted_GetRef();
		Info.Tag = Type.Key;
		Info.Attribute = Type.Value;
		Info.MaxAmount = 200;
	}

	// Eight bright, well separated hues (sRGB). Readability is a non-negotiable (CLAUDE.md): saturated, no greys.
	PlayerColorPalette = {
		FColor(255, 40, 40), FColor(40, 140, 255), FColor(40, 255, 70), FColor(255, 230, 30),
		FColor(255, 60, 220), FColor(30, 240, 240), FColor(255, 140, 20), FColor(160, 90, 255),
	};

	// ADR 0004 section 3: Quad blue, Haste yellow, Regen red, Battle Suit green.
	PowerupTints = {
		{ VeloxGameplayTags::Powerup_Quad, FLinearColor(0.15f, 0.35f, 1.f, 1.f) },
		{ VeloxGameplayTags::Powerup_Haste, FLinearColor(1.f, 0.85f, 0.1f, 1.f) },
		{ VeloxGameplayTags::Powerup_Regen, FLinearColor(1.f, 0.1f, 0.1f, 1.f) },
		{ VeloxGameplayTags::Powerup_BattleSuit, FLinearColor(0.15f, 1.f, 0.2f, 1.f) },
	};
}

int32 UVeloxCombatSettings::GetSpawnAmmo(const FGameplayTag& AmmoTag) const
{
	int32 Sum = 0;
	for (const FVeloxSpawnWeapon& Entry : SpawnLoadout)
	{
		if (Entry.Definition && Entry.Definition->AmmoTag == AmmoTag)
		{
			Sum += FMath::Max(Entry.Ammo, 0);
		}
	}
	return Sum;
}

float UVeloxCombatSettings::GetHitboxTeleportDistanceCm() const
{
#if !UE_BUILD_SHIPPING
	return VeloxCombatCVars::Get(VeloxCombatCVars::CVar_HitboxTeleportDistanceCm, HitboxTeleportDistanceCm);
#else
	return HitboxTeleportDistanceCm;
#endif
}

float UVeloxCombatSettings::GetFireRateJitterToleranceSeconds() const
{
#if !UE_BUILD_SHIPPING
	return VeloxCombatCVars::Get(VeloxCombatCVars::CVar_FireRateJitterToleranceSeconds, FireRateJitterToleranceSeconds);
#else
	return FireRateJitterToleranceSeconds;
#endif
}

float UVeloxCombatSettings::GetBeamKeepAliveSeconds() const
{
#if !UE_BUILD_SHIPPING
	return VeloxCombatCVars::Get(VeloxCombatCVars::CVar_BeamKeepAliveSeconds, BeamKeepAliveSeconds);
#else
	return BeamKeepAliveSeconds;
#endif
}

FVeloxCombatParams UVeloxCombatSettings::BuildCombatParams() const
{
	FVeloxCombatParams P;
	P.ArmorProtectionPercent = ArmorProtectionPercent;
	P.SelfDamageScale = SelfDamageScale;
	P.SelfKnockbackScale = SelfKnockbackScale;
	P.KnockbackSpeedPerDamage = KnockbackSpeedPerDamage;
	float MaxKnockback = MaxKnockbackDamage;
#if !UE_BUILD_SHIPPING
	using namespace VeloxCombatCVars;
	{
		const int32 Pct = CVar_ArmorProtectionPercent.GetValueOnAnyThread();
		P.ArmorProtectionPercent = Pct != UnsetInt ? Pct : P.ArmorProtectionPercent;
	}
	P.SelfDamageScale = Get(CVar_SelfDamageScale, P.SelfDamageScale);
	P.SelfKnockbackScale = Get(CVar_SelfKnockbackScale, P.SelfKnockbackScale);
	P.KnockbackSpeedPerDamage = Get(CVar_KnockbackSpeedPerDamage, P.KnockbackSpeedPerDamage);
	MaxKnockback = Get(CVar_MaxKnockbackDamage, MaxKnockback);
#endif
	P.MaxKnockbackDamage = FMath::RoundToInt32(MaxKnockback);
	P.BattleSuitDirectDamageScale = BattleSuitDirectDamageScale;
	return P;
}

FVeloxRegenParams UVeloxCombatSettings::BuildRegenParams() const
{
	FVeloxRegenParams P;
	P.HealthPerTick = RegenHealthPerTick;
	P.OverTick = RegenOverTick;
	P.SoftCapScale = RegenSoftCapScale;
	return P;
}

const FVeloxAmmoTypeInfo* UVeloxCombatSettings::FindAmmoType(const FGameplayTag& Tag) const
{
	return AmmoTypes.FindByPredicate([&Tag](const FVeloxAmmoTypeInfo& Info) { return Info.Tag == Tag; });
}

const FVeloxAmmoTypeInfo* UVeloxCombatSettings::FindAmmoType(const FGameplayAttribute& Attribute) const
{
	return AmmoTypes.FindByPredicate([&Attribute](const FVeloxAmmoTypeInfo& Info) { return Info.Attribute == Attribute; });
}

const UVeloxCombatSettings& UVeloxCombatSettings::GetOrDefault(const UVeloxCombatSettings* Settings)
{
	return Settings ? *Settings : *GetDefault<UVeloxCombatSettings>();
}
