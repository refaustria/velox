// Velox - player state.

#include "GameModes/VeloxPlayerState.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Character/VeloxCharacter.h"
#include "Core/VeloxCharacterCosmetics.h"
#include "Core/VeloxGameplayTags.h"
#include "Core/VeloxPawnData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxGameMode.h"
#include "HAL/IConsoleManager.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemLog.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "Containers/Ticker.h"
#include "Net/Core/PushModel/PushModel.h"
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
	DOREPLIFETIME(AVeloxPlayerState, SkinIndex);
	DOREPLIFETIME(AVeloxPlayerState, bReady);
	DOREPLIFETIME(AVeloxPlayerState, Kills);
	DOREPLIFETIME(AVeloxPlayerState, Deaths);
	FDoRepLifetimeParams OwnerOnly;
	OwnerOnly.Condition = COND_OwnerOnly;
	OwnerOnly.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxPlayerState, ItemTimers, OwnerOnly);
}

const FVeloxItemTimerEntry* AVeloxPlayerState::GetItemTimer(uint8 ItemIndex) const
{
	return ItemTimers.FindByPredicate([ItemIndex](const FVeloxItemTimerEntry& Entry) { return Entry.ItemIndex == ItemIndex; });
}

void AVeloxPlayerState::SetItemTimers(TArray<FVeloxItemTimerEntry>&& InTimers)
{
	InTimers.Sort([](const FVeloxItemTimerEntry& A, const FVeloxItemTimerEntry& B) { return A.ItemIndex < B.ItemIndex; });
	if (InTimers == ItemTimers)
	{
		return;
	}
	ItemTimers = MoveTemp(InTimers);
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxPlayerState, ItemTimers, this);
	ForceNetUpdate();
}

void AVeloxPlayerState::SetItemTimer(uint8 ItemIndex, float RespawnServerTime)
{
	// Updated in place (sorted, stable positions) so the array delta carries one element per event.
	int32 Insert = 0;
	for (; Insert < ItemTimers.Num() && ItemTimers[Insert].ItemIndex < ItemIndex; ++Insert)
	{
	}
	if (ItemTimers.IsValidIndex(Insert) && ItemTimers[Insert].ItemIndex == ItemIndex)
	{
		if (ItemTimers[Insert].RespawnServerTime == RespawnServerTime)
		{
			return;
		}
		ItemTimers[Insert].RespawnServerTime = RespawnServerTime;
	}
	else
	{
		FVeloxItemTimerEntry Entry;
		Entry.ItemIndex = ItemIndex;
		Entry.RespawnServerTime = RespawnServerTime;
		ItemTimers.Insert(Entry, Insert);
	}
	MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxPlayerState, ItemTimers, this);
	ForceNetUpdate();
}

void AVeloxPlayerState::ClearItemTimers()
{
	if (ItemTimers.Num() > 0)
	{
		ItemTimers.Reset();
		MARK_PROPERTY_DIRTY_FROM_NAME(AVeloxPlayerState, ItemTimers, this);
		ForceNetUpdate();
	}
}

FString AVeloxPlayerState::DescribeItemTimers() const
{
	const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(GetWorld());
	FString Entries;
	for (const FVeloxItemTimerEntry& Entry : ItemTimers)
	{
		const AVeloxPickup* Pickup = Registry ? Registry->FindPickupByIndex(Entry.ItemIndex) : nullptr;
		Entries += FString::Printf(TEXT("%s%s#%u@%.3f"), Entries.IsEmpty() ? TEXT("") : TEXT(","), Pickup ? *GetNameSafe(Pickup->GetDefinition()) : TEXT("?"), Entry.ItemIndex,
			Entry.RespawnServerTime);
	}
	return FString::Printf(TEXT("n=%d entries=[%s]"), ItemTimers.Num(), *Entries);
}

void AVeloxPlayerState::OnRep_ItemTimers()
{
	UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemTimers %s reason=rep player=%s"), *DescribeItemTimers(), *VeloxPlayerLabel(this));
}

#if !UE_BUILD_SHIPPING
namespace VeloxItemTimerLog
{
	// velox.items.LogTimers <seconds>: the soak's client-side evidence (nettest ItemGateRun, ADR 0007 section 5.5). Every
	// <seconds> of wall time: what this machine's local player holds, with its state (a dead player is Spectating, not
	// only a spectator). 0 stops it.
	FTSTicker::FDelegateHandle Handle;

	void LogLocalPlayers()
	{
		if (!GEngine)
		{
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || (Context.WorldType != EWorldType::Game && Context.WorldType != EWorldType::PIE) || World->GetNetMode() == NM_DedicatedServer)
			{
				continue;
			}
			const APlayerController* PC = World->GetFirstPlayerController();
			const AVeloxPlayerState* PS = PC ? PC->GetPlayerState<AVeloxPlayerState>() : nullptr;
			if (!PS)
			{
				continue;
			}
			UE_LOG(LogVeloxItems, Log, TEXT("VeloxItemTimers %s reason=tick player=%s state=%s only_spectator=%d pawn=%d t=%.3f"), *PS->DescribeItemTimers(),
				*AVeloxPlayerState::VeloxPlayerLabel(PS), *PC->GetStateName().ToString(), PS->IsOnlyASpectator() ? 1 : 0, PC->GetPawn() ? 1 : 0, World->GetTimeSeconds());
		}
	}

	void Command(const TArray<FString>& Args)
	{
		const float Seconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 1.f;
		if (Handle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Handle);
			Handle.Reset();
		}
		if (Seconds > 0.f)
		{
			Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { LogLocalPlayers(); return true; }), Seconds);
		}
		UE_LOG(LogVeloxItems, Log, TEXT("velox.items.LogTimers interval=%.2f"), Seconds);
	}

	FAutoConsoleCommand LogTimersCmd(TEXT("velox.items.LogTimers"),
		TEXT("velox.items.LogTimers [seconds=1]: logs the local player's owner-only item timers (VeloxItemTimers n=...) every <seconds>; 0 stops. Non-shipping."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Command));
}
#endif

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
	return UVeloxCharacterCosmetics::ResolveBodyColor(CharacterCosmetics.Get(), SkinIndex, Settings.PlayerColorPalette);
}

void AVeloxPlayerState::SetCharacterCosmetics(const UVeloxCharacterCosmetics* InCosmetics)
{
	CharacterCosmetics = InCosmetics;
}

void AVeloxPlayerState::SetSkinIndex(uint8 InIndex)
{
	if (HasAuthority() && SkinIndex != InIndex)
	{
		SkinIndex = InIndex;
		RefreshPawnColor();
	}
}

void AVeloxPlayerState::OnRep_SkinIndex()
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
