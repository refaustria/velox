// Velox - player state.

#include "GameModes/VeloxPlayerState.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Character/VeloxCharacter.h"
#include "Core/VeloxGameplayTags.h"
#include "Core/VeloxPawnData.h"
#include "GameModes/VeloxGameMode.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxPlayerState)

AVeloxPlayerState::AVeloxPlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<UVeloxAbilitySystemComponent>(this, TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	HealthSet = CreateDefaultSubobject<UVeloxHealthSet>(TEXT("HealthSet"));
	AmmoSet = CreateDefaultSubobject<UVeloxAmmoSet>(TEXT("AmmoSet"));
	WeaponInventory = CreateDefaultSubobject<UVeloxWeaponInventoryComponent>(TEXT("WeaponInventory"));
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

void AVeloxPlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// ADR 0003 B1: load now (both sides) rather than inside UVeloxHealthSet::ClampAttribute during net serialisation.
	GetCombatSettings();

	if (HasAuthority() && AbilitySystemComponent)
	{
		AbilitySystemComponent->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::HandleAbilityActivated);
	}
}

void AVeloxPlayerState::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (AVeloxGameMode* GameMode = GetWorld()->GetAuthGameMode<AVeloxGameMode>())
		{
			GameMode->AssignPlayerColor(*this);
		}
	}
}

void AVeloxPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVeloxPlayerState, PlayerColorIndex);
	DOREPLIFETIME(AVeloxPlayerState, bReady);
	DOREPLIFETIME(AVeloxPlayerState, Kills);
	DOREPLIFETIME(AVeloxPlayerState, Deaths);
}

void AVeloxPlayerState::Reset()
{
	Super::Reset();
	bReady = false;
	Kills = 0;
	Deaths = 0;
}

void AVeloxPlayerState::AddKill()
{
	++Kills;
	ForceNetUpdate();
}

void AVeloxPlayerState::AddDeath()
{
	++Deaths;
	ForceNetUpdate();
}

bool AVeloxPlayerState::SetReady(bool bInReady)
{
	if (bReady == bInReady)
	{
		return false;
	}
	bReady = bInReady;
	ForceNetUpdate();
	return true;
}

FColor AVeloxPlayerState::GetPlayerColor() const
{
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(GetCombatSettings());
	return Settings.PlayerColorPalette.IsValidIndex(PlayerColorIndex) ? Settings.PlayerColorPalette[PlayerColorIndex] : FColor::White;
}

void AVeloxPlayerState::SetPlayerColorIndex(uint8 InIndex)
{
	if (HasAuthority() && PlayerColorIndex != InIndex)
	{
		PlayerColorIndex = InIndex;
		RefreshPawnColor();
	}
}

void AVeloxPlayerState::OnRep_PlayerColorIndex()
{
	RefreshPawnColor();
}

void AVeloxPlayerState::RefreshPawnColor() const
{
	if (AVeloxCharacter* Character = Cast<AVeloxCharacter>(GetPawn()))
	{
		Character->ApplyPlayerColor();
	}
}

void AVeloxPlayerState::HandleAbilityActivated(UGameplayAbility* Ability)
{
	// Q3 has no spawn protection; ours (default 0 s) ends with the first shot so it cannot be used to camp (ADR 0003 section 8).
	if (AbilitySystemComponent && Ability && Ability->IsA<UVeloxGameplayAbility_Fire>() && UVeloxCombatSettings::GetOrDefault(GetCombatSettings()).bSpawnProtectionEndsOnFire
		&& AbilitySystemComponent->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected))
	{
		AbilitySystemComponent->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(VeloxGameplayTags::State_SpawnProtected));
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxSpawnProtection ended by fire player=%s"), *VeloxPlayerLabel(this));
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
	// Q3 loses every weapon on death; the loadout's ammo is set by GE_SpawnInit right after (B5).
	if (WeaponInventory)
	{
		WeaponInventory->ResetToLoadout();
	}
	ApplySpawnInit();
	ApplySpawnProtection();
}

bool AVeloxPlayerState::GrantDefaultAbilitySets(const UVeloxPawnData* PawnData)
{
	if (!HasAuthority() || !AbilitySystemComponent)
	{
		return false;
	}
	if (bDefaultAbilitySetsGranted)
	{
		if (GrantedPawnData == PawnData)
		{
			UE_LOG(LogVeloxCombat, Log, TEXT("%s: default ability sets already granted (%d abilities, %d effects); respawn re-inits actor info only."), *GetName(),
				DefaultAbilitySetHandles.NumAbilities(), DefaultAbilitySetHandles.NumEffects());
			return false;
		}
		// ADR 0003 B9: a different pawn data (class/skin, bot data) replaces the previous grant.
		UE_LOG(LogVeloxCombat, Log, TEXT("%s: pawn data changed (%s -> %s); taking %d abilities, %d effects back and re-granting."), *GetName(),
			*GetNameSafe(GrantedPawnData), *GetNameSafe(PawnData), DefaultAbilitySetHandles.NumAbilities(), DefaultAbilitySetHandles.NumEffects());
		DefaultAbilitySetHandles.TakeFromAbilitySystem(AbilitySystemComponent);
	}
	bDefaultAbilitySetsGranted = true;
	GrantedPawnData = PawnData;

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

void AVeloxPlayerState::ApplySpawnProtection()
{
	ApplySpawnProtection(UVeloxCombatSettings::GetOrDefault(GetCombatSettings()));
}

void AVeloxPlayerState::ApplySpawnProtection(const UVeloxCombatSettings& Settings)
{
	if (!HasAuthority() || !AbilitySystemComponent || Settings.SpawnProtectionSeconds <= 0.f)
	{
		return;
	}
	const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDurationSpec(AbilitySystemComponent, UVeloxGE_SpawnProtection::StaticClass(), Settings.SpawnProtectionSeconds);
	if (Spec.IsValid())
	{
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		UE_LOG(LogVeloxCombat, Log, TEXT("%s: spawn protection %.2f s (ends on fire %d)"), *GetName(), Settings.SpawnProtectionSeconds, Settings.bSpawnProtectionEndsOnFire ? 1 : 0);
	}
}

FString AVeloxPlayerState::VeloxPlayerLabel(const APlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return TEXT("-");
	}
	const FString Name = PlayerState->GetPlayerName();
	return Name.IsEmpty() ? PlayerState->GetName() : Name;
}
