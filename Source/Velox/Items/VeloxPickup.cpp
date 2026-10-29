// Velox - placed item.

#include "Items/VeloxPickup.h"

#include "AbilitySystemComponent.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/VeloxCollisionChannels.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxMatchEffects.h"
#include "GameModes/VeloxPlayerState.h"
#include "GameplayEffect.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemLog.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPowerupEffects.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxPickup)

namespace VeloxPickupGeometry
{
	// Q3 bg_public.h ITEM_RADIUS 15 and the item bounds z -15..+21 around the item origin, which FinishSpawningItem
	// drops to 15 u above the floor; 1 Q3 unit = 2.5 cm (UnitScale). Rule constants, like the pawn geometry.
	constexpr double CmPerQ3Unit = 2.5;
	constexpr double ItemRadius = 15.0 * CmPerQ3Unit;
	constexpr double ItemOriginAboveFloor = 15.0 * CmPerQ3Unit;
	constexpr double ItemMinZ = -15.0 * CmPerQ3Unit;
	constexpr double ItemMaxZ = 21.0 * CmPerQ3Unit;
	/** Trigger centre above the actor origin (the floor point) and half height: 0..90 cm. */
	constexpr double TriggerCenterZ = ItemOriginAboveFloor + (ItemMinZ + ItemMaxZ) * 0.5;
	constexpr double TriggerHalfHeight = (ItemMaxZ - ItemMinZ) * 0.5;
}

AVeloxPickup::AVeloxPickup(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// ADR 0004 section 1: every item is relevant to everyone (item timers, bots), dormant between state changes.
	bReplicates = true;
	bAlwaysRelevant = true;
	bNetLoadOnClient = true;
	NetDormancy = DORM_DormantAll;
	SetNetUpdateFrequency(10.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->InitBoxExtent(FVector(VeloxPickupGeometry::ItemRadius, VeloxPickupGeometry::ItemRadius, VeloxPickupGeometry::TriggerHalfHeight));
	Trigger->SetRelativeLocation(FVector(0.0, 0.0, VeloxPickupGeometry::TriggerCenterZ));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetCollisionResponseToChannel(Velox_TraceChannel_Weapon, ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(Velox_ObjectChannel_Projectile, ECR_Ignore);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCanEverAffectNavigation(false);

	Placeholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Placeholder"));
	Placeholder->SetupAttachment(Root);
	Placeholder->SetRelativeLocation(FVector(0.0, 0.0, VeloxPickupGeometry::TriggerCenterZ));
	Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Placeholder->SetCollisionResponseToAllChannels(ECR_Ignore);
	Placeholder->SetGenerateOverlapEvents(false);
	Placeholder->SetCanEverAffectNavigation(false);
	Placeholder->SetCastShadow(false);
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (CubeMesh.Succeeded())
		{
			Placeholder->SetStaticMesh(CubeMesh.Object);
		}
	}
	PlaceholderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_PlayerBody.M_PlayerBody")));
}

AVeloxPickup* AVeloxPickup::SpawnPickup(UWorld* World, const UVeloxItemDefinition* InDefinition, const FVector& FloorLocation)
{
	if (!World || !InDefinition || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	AVeloxPickup* Pickup = World->SpawnActor<AVeloxPickup>(AVeloxPickup::StaticClass(), FloorLocation, FRotator::ZeroRotator, Params);
	if (Pickup)
	{
		Pickup->SetDefinition(InDefinition);
		Pickup->FinishSpawning(FTransform(FRotator::ZeroRotator, FloorLocation));
	}
	return Pickup;
}

const UVeloxItemDefinition* AVeloxPickup::LoadDefinitionByName(const FString& ItemName)
{
	FString Name = ItemName.TrimStartAndEnd().Replace(TEXT("."), TEXT("_"));
	Name.RemoveFromStart(TEXT("Velox_Item_"));
	if (!Name.StartsWith(TEXT("DA_Item_")))
	{
		Name = TEXT("DA_Item_") + Name;
	}
	const FString Path = FString::Printf(TEXT("/Game/Items/%s.%s"), *Name, *Name);
	return LoadObject<UVeloxItemDefinition>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void AVeloxPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AVeloxPickup, Definition, COND_InitialOnly);
	DOREPLIFETIME(AVeloxPickup, RespawnServerTime);
	DOREPLIFETIME(AVeloxPickup, LastTakenBy);
	DOREPLIFETIME(AVeloxPickup, TakeCount);
}

void AVeloxPickup::SetDefinition(const UVeloxItemDefinition* InDefinition)
{
	if (HasActorBegunPlay())
	{
		UE_LOG(LogVeloxItems, Warning, TEXT("%s: SetDefinition after BeginPlay ignored."), *GetName());
		return;
	}
	Definition = InDefinition;
}

void AVeloxPickup::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnTriggerBeginOverlap);
}

double AVeloxPickup::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

double AVeloxPickup::GetTimeUntilRespawn() const
{
	return IsAvailable() ? 0.0 : FMath::Max(static_cast<double>(RespawnServerTime) - GetServerNow(), 0.0);
}

bool AVeloxPickup::IsPlaceholderVisible() const
{
	return IsAvailable() && !bPredictionPending;
}

void AVeloxPickup::BeginPlay()
{
	Super::BeginPlay();

	if (!Definition)
	{
		UE_LOG(LogVeloxItems, Warning, TEXT("%s: no item definition; the pickup does nothing."), *GetName());
	}

	if (!IsNetMode(NM_DedicatedServer))
	{
		if (Definition)
		{
			Placeholder->SetRelativeScale3D(Definition->PlaceholderScale);
		}
		if (UMaterialInterface* Base = PlaceholderMaterial.IsNull() ? nullptr : PlaceholderMaterial.LoadSynchronous())
		{
			PlaceholderMID = Placeholder->CreateDynamicMaterialInstance(0, Base);
			if (PlaceholderMID && Definition)
			{
				PlaceholderMID->SetVectorParameterValue(PlaceholderColorParameter, FLinearColor(Definition->PlaceholderColor));
			}
		}
		// The placeholder rotation is client-side only.
		SetActorTickEnabled(!bSuspended);
	}

	if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(GetWorld()))
	{
		Registry->RegisterPickup(this);
	}

	if (HasAuthority())
	{
		ScheduleInitialSpawn();
		// Pawns already standing here are found by AActor::UpdateInitialOverlaps right after BeginPlay (begin-overlap events).
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupSpawn item=%s at=%s available_in=%.2f"), *GetNameSafe(Definition), *GetActorLocation().ToCompactString(), GetTimeUntilRespawn());
	}
	else
	{
		SeenTakeCount = TakeCount;
		SeenRespawnServerTime = RespawnServerTime;
	}
	UpdatePlaceholder();
}

void AVeloxPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(GetWorld()))
	{
		Registry->UnregisterPickup(this);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
		World->GetTimerManager().ClearTimer(PredictionTimerHandle);
		World->GetTimerManager().ClearTimer(ResetCheckTimerHandle);
	}
	DropPredictedWeapon();
	Super::EndPlay(EndPlayReason);
}

void AVeloxPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bOverlapRecheck)
	{
		CheckOverlappingPawns(false);
	}
	if (!bSuspended && !IsNetMode(NM_DedicatedServer))
	{
		Placeholder->AddLocalRotation(FRotator(0.0, PlaceholderRotationDegreesPerSecond * DeltaSeconds, 0.0));
	}
}

void AVeloxPickup::SetOverlapRecheck(bool bEnable)
{
	bOverlapRecheck = bEnable;
	const bool bWantsTick = bOverlapRecheck || (!bSuspended && !IsNetMode(NM_DedicatedServer));
	if (IsActorTickEnabled() != bWantsTick)
	{
		SetActorTickEnabled(bWantsTick);
	}
}

// ------------------------------------------------------------------------------------------------ Shared

bool AVeloxPickup::IsPawnEligible(const AVeloxCharacter* Pawn) const
{
	if (!Pawn || Pawn->IsCorpse() || !IsValid(Pawn))
	{
		return false;
	}
	const UVeloxAbilitySystemComponent* ASC = Pawn->GetVeloxAbilitySystemComponent();
	// MatchFrozen = intermission (ADR 0004 section 5): no pickups.
	return ASC && !ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead) && !ASC->HasMatchingGameplayTag(VeloxMatchTags::State_MatchFrozen);
}

bool AVeloxPickup::BuildQuery(const AVeloxCharacter* Pawn, FVeloxPickupQuery& OutQuery) const
{
	const AVeloxPlayerState* PlayerState = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
	const UVeloxHealthSet* HealthSet = PlayerState ? PlayerState->GetHealthSet() : nullptr;
	const UVeloxAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetVeloxAbilitySystemComponent() : nullptr;
	if (!Definition || !HealthSet || !ASC)
	{
		return false;
	}
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PlayerState->GetCombatSettings());

	OutQuery = FVeloxPickupQuery();
	OutQuery.Type = Definition->Type;
	OutQuery.Quantity = Definition->GetEffectiveQuantity();
	OutQuery.HealthCap = Definition->HealthCap;
	OutQuery.Health = HealthSet->GetHealth();
	OutQuery.MaxHealth = HealthSet->GetMaxHealth();
	OutQuery.HealthHardCap = Settings.HealthHardCap;
	OutQuery.Armor = HealthSet->GetArmor();
	OutQuery.MaxArmor = HealthSet->GetMaxArmor();

	FGameplayTag AmmoTag;
	if (Definition->Type == EVeloxItemType::Ammo)
	{
		AmmoTag = Definition->AmmoTag;
	}
	else if (Definition->Type == EVeloxItemType::Weapon)
	{
		if (!Definition->Weapon)
		{
			return false;
		}
		AmmoTag = Definition->Weapon->AmmoTag;
	}
	if (AmmoTag.IsValid())
	{
		const FVeloxAmmoTypeInfo* AmmoInfo = Settings.FindAmmoType(AmmoTag);
		if (!AmmoInfo)
		{
			return false;
		}
		OutQuery.Ammo = FMath::RoundToInt32(ASC->GetNumericAttribute(AmmoInfo->Attribute));
		OutQuery.MaxAmmo = AmmoInfo->MaxAmount;
	}
	else if (Definition->Type == EVeloxItemType::Ammo)
	{
		return false;
	}

	if (Definition->Type == EVeloxItemType::Powerup)
	{
		OutQuery.PowerupSeconds = Definition->PowerupSeconds;
		if (Definition->PowerupEffect)
		{
			FGameplayEffectQuery Query;
			Query.EffectDefinition = Definition->PowerupEffect;
			const TArray<float> Remaining = ASC->GetActiveEffectsTimeRemaining(Query);
			for (const float Seconds : Remaining)
			{
				OutQuery.PowerupRemaining = FMath::Max(OutQuery.PowerupRemaining, Seconds);
			}
		}
	}
	return true;
}

void AVeloxPickup::GetOverlappingPawns(TArray<AVeloxCharacter*>& OutPawns) const
{
	TArray<UPrimitiveComponent*> Components;
	Trigger->GetOverlappingComponents(Components);
	for (UPrimitiveComponent* Component : Components)
	{
		AVeloxCharacter* Pawn = Component ? Cast<AVeloxCharacter>(Component->GetOwner()) : nullptr;
		if (!Pawn || Component != Pawn->GetCapsuleComponent())
		{
			continue;
		}
		if (!HasAuthority() && !Pawn->IsLocallyControlled())
		{
			continue;
		}
		OutPawns.AddUnique(Pawn);
	}
}

bool AVeloxPickup::ShouldRecheck(const AVeloxCharacter* Pawn) const
{
	// A pawn refused for a reason that can pass while it stands there: full health, or the intermission freeze.
	if (IsPawnEligible(Pawn))
	{
		return true;
	}
	const UVeloxAbilitySystemComponent* ASC = Pawn ? Pawn->GetVeloxAbilitySystemComponent() : nullptr;
	return ASC && IsValid(Pawn) && !Pawn->IsCorpse() && ASC->HasMatchingGameplayTag(VeloxMatchTags::State_MatchFrozen);
}

void AVeloxPickup::CheckOverlappingPawns(bool bLogRefusals)
{
	TArray<AVeloxCharacter*> Pawns;
	GetOverlappingPawns(Pawns);
	bool bRefusedPawnInside = false;
	for (AVeloxCharacter* Pawn : Pawns)
	{
		if (HasAuthority())
		{
			if (!IsAvailable())
			{
				break;
			}
			if (!TryPickup(Pawn, bLogRefusals) && IsAvailable() && ShouldRecheck(Pawn))
			{
				bRefusedPawnInside = true;
			}
		}
		else if (IsPlaceholderVisible() && !TryPredictTake(Pawn) && IsPlaceholderVisible() && ShouldRecheck(Pawn))
		{
			bRefusedPawnInside = true;
		}
	}
	SetOverlapRecheck(bRefusedPawnInside);
}

void AVeloxPickup::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
	const FHitResult& SweepResult)
{
	AVeloxCharacter* Pawn = Cast<AVeloxCharacter>(OtherActor);
	if (!Pawn || OtherComp != Pawn->GetCapsuleComponent())
	{
		return;
	}
	if (HasAuthority())
	{
		if (!TryPickup(Pawn, true) && IsAvailable() && ShouldRecheck(Pawn))
		{
			// Q3 evaluates touches every frame: a pawn refused now (full health) takes it once it can.
			SetOverlapRecheck(true);
		}
	}
	else if (Pawn->IsLocallyControlled())
	{
		if (!TryPredictTake(Pawn) && IsPlaceholderVisible() && ShouldRecheck(Pawn))
		{
			SetOverlapRecheck(true);
		}
	}
}

// ------------------------------------------------------------------------------------------------ Server

void AVeloxPickup::ScheduleInitialSpawn()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
	}
	const double Delay = Definition ? VeloxPickupRules::InitialDelayFor(Definition->InitialSpawnDelay, FMath::FRand()) : 0.0;
	Timer.ScheduleInitial(GetServerNow(), Delay);
	CommitServerState();
	if (!Timer.IsAvailable(GetServerNow()))
	{
		StartRespawnTimer(Delay);
	}
}

void AVeloxPickup::CommitServerState()
{
	FlushNetDormancy();
	RespawnServerTime = static_cast<float>(Timer.RespawnTime);
	UpdatePlaceholder();
}

void AVeloxPickup::StartRespawnTimer(double Seconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (Seconds <= 0.0)
	{
		ServerRespawn();
		return;
	}
	World->GetTimerManager().SetTimer(RespawnTimerHandle, FTimerDelegate::CreateUObject(this, &ThisClass::ServerRespawn), static_cast<float>(Seconds), false);
}

bool AVeloxPickup::TryPickup(AVeloxCharacter* Pawn, bool bLogRefusal)
{
	if (!HasAuthority() || !Definition || !IsAvailable() || !IsPawnEligible(Pawn))
	{
		return false;
	}
	AVeloxPlayerState* PlayerState = Pawn->GetPlayerState<AVeloxPlayerState>();
	FVeloxPickupQuery Query;
	if (!PlayerState || !BuildQuery(Pawn, Query))
	{
		UE_LOG(LogVeloxItems, Warning, TEXT("VeloxPickupRefused item=%s by=%s reason=setup"), *GetNameSafe(Definition), *AVeloxPlayerState::VeloxPlayerLabel(PlayerState));
		return false;
	}

	if (Definition->Type == EVeloxItemType::Powerup && !Definition->PowerupEffect)
	{
		UE_CLOG(bLogRefusal, LogVeloxItems, Warning, TEXT("VeloxPickupRefused item=%s by=%s reason=no_powerup_effect"), *GetNameSafe(Definition),
			*AVeloxPlayerState::VeloxPlayerLabel(PlayerState));
		return false;
	}

	const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(GetWorld());
	const FVeloxItemModeRules Rules = Registry ? Registry->GetModeRules() : FVeloxItemModeRules();
	const bool bStays = VeloxPickupRules::StaysAfterTake(Definition->Type, Rules);
	const UVeloxWeaponInventoryComponent* Inventory = PlayerState->GetWeaponInventory();
	const bool bAlreadyOwnsStayingWeapon = bStays && Inventory && Inventory->OwnsIndex(Inventory->IndexOf(Definition->Weapon));

	const FVeloxPickupGain Gain = VeloxPickupRules::Gain(Query);
	if (!Gain.bGrab || bAlreadyOwnsStayingWeapon)
	{
		UE_CLOG(bLogRefusal, LogVeloxItems, Log, TEXT("VeloxPickupRefused item=%s by=%s reason=%s health=%.0f armor=%.0f ammo=%d"), *GetNameSafe(Definition),
			*AVeloxPlayerState::VeloxPlayerLabel(PlayerState), bAlreadyOwnsStayingWeapon ? TEXT("weapon_stay_owned") : TEXT("rule"), Query.Health, Query.Armor, Query.Ammo);
		return false;
	}
	if (!GrantTo(*Pawn, Gain))
	{
		return false;
	}

	const double Now = GetServerNow();
	const double Respawn = bStays ? 0.0 : VeloxPickupRules::RespawnSecondsFor(Definition->Type, Definition->RespawnSeconds, Rules);
	Timer.Take(Now, Respawn);
	FlushNetDormancy();
	++TakeCount;
	LastTakenBy = PlayerState;
	CommitServerState();

	UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickup item=%s by=%s gain=%d powerup_s=%.1f respawn_in=%.2f t=%.3f take=%u"), *GetNameSafe(Definition), *AVeloxPlayerState::VeloxPlayerLabel(PlayerState),
		Gain.Amount, Gain.PowerupSeconds, Respawn, Now, TakeCount);
	if (UVeloxItemRegistrySubsystem* MutableRegistry = UVeloxItemRegistrySubsystem::Get(GetWorld()))
	{
		MutableRegistry->OnPickupTaken.Broadcast(this, PlayerState);
	}
	if (!Timer.IsAvailable(Now))
	{
		StartRespawnTimer(Respawn);
		SetOverlapRecheck(false);
	}
	return true;
}

bool AVeloxPickup::GrantTo(AVeloxCharacter& Pawn, const FVeloxPickupGain& Gain)
{
	AVeloxPlayerState* PlayerState = Pawn.GetPlayerState<AVeloxPlayerState>();
	UVeloxAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetVeloxAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return false;
	}
	auto ApplyToSelf = [ASC](const FGameplayEffectSpecHandle& Spec)
	{
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	};

	switch (Definition->Type)
	{
	case EVeloxItemType::Health:
		ApplyToSelf(VeloxEffects::MakeHealSpec(ASC, Gain.Amount));
		return true;
	case EVeloxItemType::Armor:
		ApplyToSelf(VeloxEffects::MakeArmorGainSpec(ASC, Gain.Amount));
		return true;
	case EVeloxItemType::Ammo:
		ApplyToSelf(VeloxEffects::MakeAmmoGainSpec(ASC, Definition->AmmoTag, Gain.Amount));
		return true;
	case EVeloxItemType::Weapon:
	{
		UVeloxWeaponInventoryComponent* Inventory = PlayerState->GetWeaponInventory();
		const uint16 OldMask = Inventory ? Inventory->GetOwnedMask() : 0;
		// AddWeapon grants the fire ability once (its spec replicates to the owner, ADR 0003 section 4) and adds the ammo.
		if (!Inventory || !Inventory->AddWeapon(Definition->Weapon, Gain.Amount))
		{
			return false;
		}
		if (Pawn.IsLocallyControlled())
		{
			// Listen-server host / standalone: no OnRep_OwnedMask on the authority.
			Inventory->RunAutoSwitch(OldMask, Inventory->GetOwnedMask());
		}
		return true;
	}
	case EVeloxItemType::Powerup:
		return GrantPowerup(Pawn, Gain);
	}
	return false;
}

bool AVeloxPickup::GrantPowerup(AVeloxCharacter& Pawn, const FVeloxPickupGain& Gain)
{
	// ADR 0004 section 3: Gain.PowerupSeconds is the rule's total (remaining + item seconds); an active powerup is extended
	// in place (ModifyActiveEffectStartTime), with remove + re-apply as the fallback (VeloxPowerups::GrantOrExtend).
	UVeloxAbilitySystemComponent* ASC = Pawn.GetVeloxAbilitySystemComponent();
	if (!ASC || !Definition->PowerupEffect)
	{
		return false;
	}
	const bool bGranted = VeloxPowerups::GrantOrExtend(*ASC, Definition->PowerupEffect, Gain.PowerupSeconds) != EVeloxPowerupGrant::Failed;
	// ADR 0004 section 3 (task 03): Haste/Quad end times into the owner's move timeline (an in-place extension fires no
	// tag event; the character's GE delegates cover it too, this keeps the grant explicit).
	Pawn.ServerRefreshPowerupMoveTimes();
	return bGranted;
}

void AVeloxPickup::ServerRespawn()
{
	if (!HasAuthority())
	{
		return;
	}
	const bool bWasDown = !IsAvailable();
	Timer.MarkRespawned();
	CommitServerState();
	if (bWasDown)
	{
		UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupRespawn item=%s t=%.3f"), *GetNameSafe(Definition), GetServerNow());
		if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(GetWorld()))
		{
			Registry->OnPickupRespawned.Broadcast(this);
		}
	}
	// A pawn standing in the trigger takes it at once (Q3 evaluates touches every frame).
	CheckOverlappingPawns(true);
}

void AVeloxPickup::Reset()
{
	Super::Reset();
	if (!HasAuthority())
	{
		return;
	}
	const bool bWasDown = !IsAvailable();
	Timer.Reset();
	FlushNetDormancy();
	LastTakenBy = nullptr;
	SetOverlapRecheck(false);
	ScheduleInitialSpawn();
	UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupReset item=%s available_in=%.2f"), *GetNameSafe(Definition), GetTimeUntilRespawn());
	if (bWasDown && IsAvailable())
	{
		if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(GetWorld()))
		{
			Registry->OnPickupRespawned.Broadcast(this);
		}
	}
	if (IsAvailable())
	{
		// ADR 0004 C10 / F1: not synchronously. ResetLevel resets controllers first and then every actor in iteration order; an
		// AI pawn (AController::Reset does not unpossess) whose turn comes after this pickup would still be alive and take
		// it. One tick later every pawn of the old round is destroyed and the restarted ones have spawned.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ResetCheckTimerHandle);
			ResetCheckTimerHandle = World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (HasAuthority() && IsAvailable())
				{
					CheckOverlappingPawns(true);
				}
			}));
		}
	}
}

// ------------------------------------------------------------------------------------------------ Owning client

bool AVeloxPickup::TryPredictTake(AVeloxCharacter* Pawn)
{
	if (HasAuthority() || !Definition || !Pawn || !Pawn->IsLocallyControlled() || !IsAvailable() || bPredictionPending || !IsPawnEligible(Pawn))
	{
		return false;
	}
	FVeloxPickupQuery Query;
	if (!BuildQuery(Pawn, Query) || !VeloxPickupRules::CanGrab(Query))
	{
		return false;
	}
	UWorld* World = GetWorld();
	const AVeloxPlayerState* PlayerState = Pawn->GetPlayerState<AVeloxPlayerState>();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PlayerState ? PlayerState->GetCombatSettings() : nullptr);
	const double Now = World->GetTimeSeconds();
	const double Until = Now + FMath::Max(Settings.PickupPredictionTimeout, 0.f);

	bPredictionPending = true;
	PredictedTakeCount = TakeCount;
	PredictedAt = Now;
	if (Definition->Type == EVeloxItemType::Weapon)
	{
		if (UVeloxWeaponInventoryComponent* Inventory = PlayerState ? PlayerState->GetWeaponInventory() : nullptr)
		{
			PredictedWeaponIndex = Inventory->IndexOf(Definition->Weapon);
			PredictedInventory = Inventory;
			// Q3 predicts STAT_WEAPONS: a slot press switches at once; the fire spec replicates during the drop + raise.
			Inventory->PredictOwned(PredictedWeaponIndex, Until);
		}
	}
	World->GetTimerManager().SetTimer(PredictionTimerHandle, FTimerDelegate::CreateUObject(this, &ThisClass::OnPredictionTimeout), FMath::Max(static_cast<float>(Until - Now), UE_KINDA_SMALL_NUMBER), false);
	UpdatePlaceholder();
	// M5 plays the pickup sound here (bGlobalPickupSound items play for everyone from the replicated take instead).
	UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupPredict item=%s t=%.3f take=%u"), *GetNameSafe(Definition), Now, TakeCount);
	return true;
}

void AVeloxPickup::DropPredictedWeapon()
{
	if (PredictedWeaponIndex == INDEX_NONE)
	{
		return;
	}
	if (UVeloxWeaponInventoryComponent* Inventory = PredictedInventory.Get())
	{
		Inventory->DropPredictedOwned(PredictedWeaponIndex);
	}
	PredictedWeaponIndex = INDEX_NONE;
	PredictedInventory.Reset();
}

void AVeloxPickup::ClearPrediction(const TCHAR* Outcome)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	UE_LOG(LogVeloxItems, Log, TEXT("VeloxPickupPredict%s item=%s ms=%.0f take=%u"), Outcome, *GetNameSafe(Definition), (Now - PredictedAt) * 1000.0, TakeCount);
	bPredictionPending = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PredictionTimerHandle);
	}
}

void AVeloxPickup::OnPredictionTimeout()
{
	if (!bPredictionPending)
	{
		return;
	}
	// No take replicated in time: the server refused (full, raced, dead) or the update is late; show the item again.
	ClearPrediction(TEXT("Refused"));
	DropPredictedWeapon();
	UpdatePlaceholder();
}

void AVeloxPickup::OnRep_RespawnServerTime()
{
	HandleReplicatedState();
}

void AVeloxPickup::OnRep_LastTakenBy()
{
	HandleReplicatedState();
}

void AVeloxPickup::OnRep_TakeCount()
{
	HandleReplicatedState();
}

void AVeloxPickup::HandleReplicatedState()
{
	// The engine applies every property of an update before it calls the rep notifies, so each call sees the whole new
	// state; the Seen* fields make the second and third call of one update no-ops.
	if (!HasActorBegunPlay())
	{
		// Initial state of a pickup spawned while this client was connected or joined: BeginPlay takes it as the baseline.
		return;
	}
	UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(GetWorld());
	if (TakeCount != SeenTakeCount)
	{
		SeenTakeCount = TakeCount;
		const APlayerController* LocalController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const bool bTakenByMe = LastTakenBy && LocalController && LastTakenBy == LocalController->PlayerState;
		if (bPredictionPending)
		{
			if (bTakenByMe)
			{
				ClearPrediction(TEXT("Confirmed"));
				// The weapon bit is now in the replicated mask (or arrives with the PlayerState); the prediction expires.
				PredictedWeaponIndex = INDEX_NONE;
				PredictedInventory.Reset();
			}
			else
			{
				// Someone else was first: the item stays hidden (it is down), the predicted weapon slot goes away.
				ClearPrediction(TEXT("Lost"));
				DropPredictedWeapon();
			}
		}
		// ADR 0004 C10 / F2: a take older than PickupTakeEventMaxAgeSeconds (a placed pickup's first update for a late joiner)
		// updates the state and the placeholder but is not an event. Respawn 0 (weapon stay) never carries an age.
		const AVeloxPlayerState* LocalPS = LocalController ? Cast<AVeloxPlayerState>(LocalController->PlayerState) : nullptr;
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(LocalPS ? LocalPS->GetCombatSettings() : nullptr);
		const FVeloxItemModeRules Rules = Registry ? Registry->GetModeRules() : FVeloxItemModeRules();
		const double RespawnSeconds = Definition ? VeloxPickupRules::RespawnSecondsFor(Definition->Type, Definition->RespawnSeconds, Rules) : 0.0;
		const bool bStale = VeloxPickupRules::IsStaleTake(GetServerNow(), RespawnServerTime, RespawnSeconds, Settings.GetPickupTakeEventMaxAgeSeconds());
		UE_LOG(LogVeloxItems, Verbose, TEXT("VeloxPickupTaken item=%s by=%s take=%u stale=%d"), *GetNameSafe(Definition), *AVeloxPlayerState::VeloxPlayerLabel(LastTakenBy), TakeCount, bStale ? 1 : 0);
		if (Registry && !bStale)
		{
			Registry->OnPickupTaken.Broadcast(this, LastTakenBy);
		}
	}
	if (RespawnServerTime != SeenRespawnServerTime)
	{
		const bool bRespawned = SeenRespawnServerTime != 0.f && RespawnServerTime == 0.f;
		SeenRespawnServerTime = RespawnServerTime;
		if (bRespawned)
		{
			UE_LOG(LogVeloxItems, Verbose, TEXT("VeloxPickupRespawned item=%s"), *GetNameSafe(Definition));
			if (Registry)
			{
				Registry->OnPickupRespawned.Broadcast(this);
			}
		}
	}
	UpdatePlaceholder();
	if (IsAvailable() && !bPredictionPending)
	{
		// Respawned (or refused elsewhere) with the local pawn standing inside: predict the take now.
		CheckOverlappingPawns(false);
	}
}

void AVeloxPickup::UpdatePlaceholder()
{
	if (Placeholder)
	{
		// M5 plays the pickup / respawn sounds on these transitions (skipped when this machine predicted the take).
		Placeholder->SetVisibility(IsPlaceholderVisible());
	}
}
