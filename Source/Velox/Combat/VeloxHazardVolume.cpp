// Velox - hazard volume (ADR 0008 section 3.3).

#include "Combat/VeloxHazardVolume.h"

#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHazardSubsystem.h"
#include "Combat/VeloxHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameModes/VeloxPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxHazardVolume)

namespace
{
	/** Lava tick comparison slack: a fixed 60 Hz step lands exactly on 0.25 s, float time must not skip a frame. */
	constexpr double TickSlackSeconds = 1.0e-3;
}

AVeloxHazardVolume::AVeloxHazardVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->InitBoxExtent(FVector(100.f));
	Box->SetMobility(EComponentMobility::Static);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	// Off until PostInitializeComponents knows the net mode: overlap events run on the authority only.
	Box->SetGenerateOverlapEvents(false);
	Box->SetCanEverAffectNavigation(false);
	Box->SetHiddenInGame(true);
	Box->ShapeColor = FColor(255, 80, 0);
	RootComponent = Box;

	bReplicates = false;
	bNetLoadOnClient = true;
	SetCanBeDamaged(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // ticks only while it has occupants
}

const TCHAR* AVeloxHazardVolume::TypeName(EVeloxHazardType InType)
{
	return InType == EVeloxHazardType::Void ? TEXT("void") : TEXT("lava");
}

bool AVeloxHazardVolume::IsServerWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

bool AVeloxHazardVolume::ContainsPoint(const FVector& Point) const
{
	if (!Box)
	{
		return false;
	}
	const FVector Local = Box->GetComponentTransform().InverseTransformPosition(Point);
	const FVector Extent = Box->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

void AVeloxHazardVolume::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (Box && IsServerWorld())
	{
		Box->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleBeginOverlap);
		Box->OnComponentEndOverlap.AddDynamic(this, &ThisClass::HandleEndOverlap);
		Box->SetGenerateOverlapEvents(true);
	}
}

void AVeloxHazardVolume::BeginPlay()
{
	Super::BeginPlay();
	if (UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(GetWorld()))
	{
		Hazards->RegisterVolume(this);
	}
}

void AVeloxHazardVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(GetWorld()))
	{
		Hazards->UnregisterVolume(this);
	}
	Occupants.Reset();
	Super::EndPlay(EndPlayReason);
}

FVeloxHazardOccupant* AVeloxHazardVolume::FindOccupant(const AVeloxCharacter* Pawn)
{
	return Occupants.FindByPredicate([Pawn](const FVeloxHazardOccupant& O) { return O.Pawn.Get() == Pawn; });
}

void AVeloxHazardVolume::UpdateTickEnabled()
{
	SetActorTickEnabled(Occupants.Num() > 0);
}

void AVeloxHazardVolume::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
	const FHitResult& SweepResult)
{
	AVeloxCharacter* Pawn = Cast<AVeloxCharacter>(OtherActor);
	if (!Pawn || OtherComp != Pawn->GetCapsuleComponent() || Pawn->IsCorpse() || !IsServerWorld())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	FVeloxHazardOccupant* Occupant = FindOccupant(Pawn);
	if (!Occupant)
	{
		Occupant = &Occupants.AddDefaulted_GetRef();
		Occupant->Pawn = Pawn;
	}
	Occupant->LeftTime = -1.0;
	if (Type == EVeloxHazardType::Void)
	{
		ApplyHazardDamage(*Pawn, Now);
		Occupant->LastDamageTime = Now;
	}
	else
	{
		// Contact damage, unless the last tick was less than LavaTickSeconds ago (boundary jitter: out and back in).
		const AVeloxPlayerState* VictimPS = Pawn->GetPlayerState<AVeloxPlayerState>();
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(VictimPS ? VictimPS->GetCombatSettings() : nullptr);
		if (Occupant->LastDamageTime < 0.0 || Now - Occupant->LastDamageTime >= Settings.LavaTickSeconds - TickSlackSeconds)
		{
			if (ApplyHazardDamage(*Pawn, Now))
			{
				Occupant->LastDamageTime = Now;
			}
		}
	}
	UpdateTickEnabled();
}

void AVeloxHazardVolume::HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	const AVeloxCharacter* Pawn = Cast<AVeloxCharacter>(OtherActor);
	if (!Pawn || OtherComp != Pawn->GetCapsuleComponent() || !GetWorld())
	{
		return;
	}
	if (FVeloxHazardOccupant* Occupant = FindOccupant(Pawn))
	{
		Occupant->LeftTime = GetWorld()->GetTimeSeconds();
	}
}

void AVeloxHazardVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	for (int32 i = Occupants.Num() - 1; i >= 0; --i)
	{
		FVeloxHazardOccupant& Occupant = Occupants[i];
		AVeloxCharacter* Pawn = Occupant.Pawn.Get();
		if (!Pawn)
		{
			Occupants.RemoveAtSwap(i);
			continue;
		}
		if (Occupant.LeftTime >= 0.0)
		{
			if (Now - Occupant.LeftTime > KeepLeftSeconds)
			{
				Occupants.RemoveAtSwap(i);
			}
			continue;
		}
		if (Type != EVeloxHazardType::Lava || Pawn->IsCorpse())
		{
			continue;
		}
		const AVeloxPlayerState* VictimPS = Pawn->GetPlayerState<AVeloxPlayerState>();
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(VictimPS ? VictimPS->GetCombatSettings() : nullptr);
		if (Occupant.LastDamageTime < 0.0 || Now - Occupant.LastDamageTime >= Settings.LavaTickSeconds - TickSlackSeconds)
		{
			if (ApplyHazardDamage(*Pawn, Now))
			{
				Occupant.LastDamageTime = Now;
			}
		}
	}
	UpdateTickEnabled();
}

bool AVeloxHazardVolume::ApplyHazardDamage(AVeloxCharacter& Pawn, double Now)
{
	UVeloxHealthComponent* Health = UVeloxHealthComponent::FindHealthComponent(&Pawn);
	UVeloxAbilitySystemComponent* VictimASC = Pawn.GetVeloxAbilitySystemComponent();
	if (!Health || Health->IsDeadOrDying() || !VictimASC || VictimASC->GetAvatarActor() != &Pawn)
	{
		return false;
	}
	AVeloxPlayerState* VictimPS = Pawn.GetPlayerState<AVeloxPlayerState>();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(VictimPS ? VictimPS->GetCombatSettings() : nullptr);

	// ADR 0008 3.3: the credited attacker's ASC (on its player state, so it outlives the attacker's pawn), else the
	// victim's own ASC with bSelf = false, exactly as FellOutOfWorld.
	AVeloxPlayerState* Credited = Health->GetCreditedAttacker(Now, Settings.HazardKillCreditSeconds);
	UAbilitySystemComponent* CreditedASC = Credited ? Credited->GetAbilitySystemComponent() : nullptr;
	UAbilitySystemComponent* SourceASC = CreditedASC ? CreditedASC : VictimASC;
	if (!CreditedASC)
	{
		Credited = nullptr;
	}

	const FVector Feet = Pawn.GetActorLocation() - FVector(0.0, 0.0, Pawn.GetCapsuleComponent() ? Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0);
	FVeloxDamageSpecParams Params;
	Params.Damage = Type == EVeloxHazardType::Void ? Settings.EnvironmentKillDamage : Settings.LavaDamagePerTick;
	Params.DamageTag = Type == EVeloxHazardType::Void ? VeloxGameplayTags::Damage_Hazard_Void : VeloxGameplayTags::Damage_Hazard_Lava;
	Params.HitLocation = Feet;
	Params.DamageScale = 1.f; // never the attacker's Quad (ADR 0008 3.3; Quad is applied at fire, not in the execution)
	Params.bSelf = false;
	Params.bNoArmor = true;
	Params.bNoKnockback = true;
	Params.Causer = this;

	const FVeloxLastAttacker& Record = Health->GetLastAttacker();
	const double Age = Record.HasRecord() ? Now - Record.ServerTime : -1.0;
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxHazard type=%s victim=%s damage=%d credited=%s age=%.2f at=%.0f,%.0f,%.0f"), TypeName(Type),
		*AVeloxPlayerState::VeloxPlayerLabel(VictimPS), Params.Damage, Credited ? *AVeloxPlayerState::VeloxPlayerLabel(Credited) : TEXT("none"), Age, Feet.X, Feet.Y,
		Feet.Z);

	const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(SourceASC, Params);
	if (!Spec.IsValid())
	{
		return false;
	}
	if (SourceASC == VictimASC)
	{
		VictimASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
	else
	{
		SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), VictimASC);
	}
	return true;
}
