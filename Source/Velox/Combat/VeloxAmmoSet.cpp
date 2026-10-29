// Velox - ammo attributes.

#include "Combat/VeloxAmmoSet.h"

#include "Combat/VeloxCombatSettings.h"
#include "Core/VeloxGameplayTags.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAmmoSet)

UVeloxAmmoSet::UVeloxAmmoSet()
{
	InitBullets(0.f);
	InitShells(0.f);
	InitCells(0.f);
	InitRockets(0.f);
	InitLightning(0.f);
	InitSlugs(0.f);
}

const TArray<TPair<FGameplayTag, FGameplayAttribute>>& UVeloxAmmoSet::GetAmmoTypes()
{
	using namespace VeloxGameplayTags;
	static const TArray<TPair<FGameplayTag, FGameplayAttribute>> Types = {
		{ Ammo_Bullets, GetBulletsAttribute() },
		{ Ammo_Shells, GetShellsAttribute() },
		{ Ammo_Cells, GetCellsAttribute() },
		{ Ammo_Rockets, GetRocketsAttribute() },
		{ Ammo_Lightning, GetLightningAttribute() },
		{ Ammo_Slugs, GetSlugsAttribute() },
	};
	return Types;
}

FGameplayAttribute UVeloxAmmoSet::GetAmmoAttribute(const FGameplayTag& AmmoTag, const UVeloxCombatSettings& Settings)
{
	const FVeloxAmmoTypeInfo* Info = Settings.FindAmmoType(AmmoTag);
	return Info ? Info->Attribute : FGameplayAttribute();
}

float UVeloxAmmoSet::ClampAmmo(const FGameplayAttribute& Attribute, float NewValue, const UVeloxCombatSettings& Settings)
{
	const FVeloxAmmoTypeInfo* Info = Settings.FindAmmoType(Attribute);
	if (!Info)
	{
		return NewValue;
	}
	return FMath::Clamp(FMath::RoundToFloat(NewValue), 0.f, static_cast<float>(Info->MaxAmount));
}

void UVeloxAmmoSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	NewValue = ClampAmmo(Attribute, NewValue, GetCombatSettings());
}

void UVeloxAmmoSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	NewValue = ClampAmmo(Attribute, NewValue, GetCombatSettings());
}

void UVeloxAmmoSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Ammo is the owner's business only (HUD); others never see it.
	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	Params.Condition = COND_OwnerOnly;
	Params.RepNotifyCondition = REPNOTIFY_Always;
	DOREPLIFETIME_WITH_PARAMS_FAST(UVeloxAmmoSet, Bullets, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UVeloxAmmoSet, Shells, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UVeloxAmmoSet, Cells, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UVeloxAmmoSet, Rockets, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UVeloxAmmoSet, Lightning, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UVeloxAmmoSet, Slugs, Params);
}

void UVeloxAmmoSet::OnRep_Bullets(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UVeloxAmmoSet, Bullets, OldValue); }
void UVeloxAmmoSet::OnRep_Shells(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UVeloxAmmoSet, Shells, OldValue); }
void UVeloxAmmoSet::OnRep_Cells(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UVeloxAmmoSet, Cells, OldValue); }
void UVeloxAmmoSet::OnRep_Rockets(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UVeloxAmmoSet, Rockets, OldValue); }
void UVeloxAmmoSet::OnRep_Lightning(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UVeloxAmmoSet, Lightning, OldValue); }
void UVeloxAmmoSet::OnRep_Slugs(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UVeloxAmmoSet, Slugs, OldValue); }
