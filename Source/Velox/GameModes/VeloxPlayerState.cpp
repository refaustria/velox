// Velox - player state.

#include "GameModes/VeloxPlayerState.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxPawnData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxPlayerState)

AVeloxPlayerState::AVeloxPlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<UVeloxAbilitySystemComponent>(this, TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	HealthSet = CreateDefaultSubobject<UVeloxHealthSet>(TEXT("HealthSet"));
	AmmoSet = CreateDefaultSubobject<UVeloxAmmoSet>(TEXT("AmmoSet"));
}

void AVeloxPlayerState::PostInitProperties()
{
	Super::PostInitProperties();

	// Config values are available here (loaded into the CDO, copied to instances).
	if (ConfigNetUpdateFrequency > 0.0f)
	{
		SetNetUpdateFrequency(ConfigNetUpdateFrequency);
	}
}

UAbilitySystemComponent* AVeloxPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

UVeloxAbilitySystemComponent* AVeloxPlayerState::GetVeloxAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

const UVeloxCombatSettings* AVeloxPlayerState::GetCombatSettings() const
{
	if (!CombatSettings && !CombatSettingsAsset.IsNull())
	{
		CombatSettings = CombatSettingsAsset.LoadSynchronous();
		if (!CombatSettings)
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("%s: failed to load combat settings [%s]; using class defaults."), *GetName(), *CombatSettingsAsset.ToString());
		}
	}
	return CombatSettings;
}

void AVeloxPlayerState::OnPawnPossessed(const UVeloxPawnData* PawnData)
{
	if (!HasAuthority())
	{
		return;
	}
	GrantDefaultAbilitySets(PawnData);
	ApplySpawnInit();
}

bool AVeloxPlayerState::GrantDefaultAbilitySets(const UVeloxPawnData* PawnData)
{
	if (!HasAuthority() || !AbilitySystemComponent)
	{
		return false;
	}
	if (bDefaultAbilitySetsGranted)
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("%s: default ability sets already granted (%d abilities, %d effects); respawn re-inits actor info only."), *GetName(),
			DefaultAbilitySetHandles.NumAbilities(), DefaultAbilitySetHandles.NumEffects());
		return false;
	}
	bDefaultAbilitySetsGranted = true;

	int32 NumSets = 0;
	if (PawnData)
	{
		for (const UVeloxAbilitySet* AbilitySet : PawnData->AbilitySets)
		{
			if (AbilitySet)
			{
				AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, &DefaultAbilitySetHandles, this);
				++NumSets;
			}
		}
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("%s: granted default ability sets once: %d sets, %d abilities, %d effects, %d attribute sets."), *GetName(), NumSets,
		DefaultAbilitySetHandles.NumAbilities(), DefaultAbilitySetHandles.NumEffects(), DefaultAbilitySetHandles.NumAttributeSets());
	return true;
}

void AVeloxPlayerState::ApplySpawnInit()
{
	if (!HasAuthority() || !AbilitySystemComponent)
	{
		return;
	}
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(GetCombatSettings());
	const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeSpawnInitSpec(AbilitySystemComponent, Settings);
	if (!Spec.IsValid())
	{
		return;
	}
	AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	UE_LOG(LogVeloxCombat, Log, TEXT("%s: GE_SpawnInit health %.0f/%.0f armor %.0f/%.0f bullets %.0f (settings %s)"), *GetName(), HealthSet->GetHealth(), HealthSet->GetMaxHealth(),
		HealthSet->GetArmor(), HealthSet->GetMaxArmor(), AmmoSet->GetBullets(), *GetNameSafe(GetCombatSettings()));
}
