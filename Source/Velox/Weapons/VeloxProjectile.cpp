// Velox - projectile actor (server, fake, proxy).

#include "Weapons/VeloxProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Combat/VeloxHitboxComponent.h"
#include "Components/SphereComponent.h"
#include "Core/VeloxCollisionChannels.h"
#include "Core/VeloxGameplayTags.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "GameplayCueInterface.h"
#include "GameplayPrediction.h"
#include "HAL/IConsoleManager.h"
#include "Movement/VeloxMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "TimerManager.h"
#include "Weapons/VeloxWeaponDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxProjectile)

namespace VeloxProjectilePrivate
{
	/** ADR 0003 section 7 replication settings. */
	constexpr float ProjectileNetUpdateFrequency = 10.f;
	constexpr float ProjectileNetPriority = 2.5f;
	/** 100 m: the whole test map (ADR 0003 section 7, no replication graph in M2). */
	constexpr float ProjectileNetCullDistanceCm = 10000.f;

#if !UE_BUILD_SHIPPING
	FVeloxProjectileExplosionInfo GLastServerExplosion;

	bool DebugDrawEnabled()
	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("velox.combat.DebugDraw"));
		return CVar && CVar->GetInt() != 0;
	}
#endif

	const UVeloxCombatSettings& SettingsOf(const UAbilitySystemComponent* ASC)
	{
		const AVeloxPlayerState* PlayerState = ASC ? Cast<AVeloxPlayerState>(ASC->GetOwnerActor()) : nullptr;
		return UVeloxCombatSettings::GetOrDefault(PlayerState ? PlayerState->GetCombatSettings() : nullptr);
	}

	/** World-only LOS on the Weapon channel (pawns and projectiles ignore it). */
	bool HasLineOfSight(const UWorld& World, const FVector& From, const FVector& To)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VeloxSplashLOS), false);
		return !World.LineTraceTestByChannel(From, To, Velox_TraceChannel_Weapon, Params);
	}

	/** Living registered pawns with their current hit shapes (server: what the lag-comp subsystem samples, no rewind). */
	void GatherServerTargets(UWorld& World, TArray<FVeloxHitboxPose, TInlineAllocator<16>>& Poses, TArray<FVeloxProjectileTarget, TInlineAllocator<16>>& Targets)
	{
		Poses.Reset();
		Targets.Reset();
		const UVeloxLagCompensationSubsystem* LagComp = UVeloxLagCompensationSubsystem::Get(&World);
		for (TActorIterator<AVeloxCharacter> It(&World); It; ++It)
		{
			// Registration is the "alive and hittable" gate (corpses unregister, B17); without a subsystem take every pawn.
			if (LagComp && !LagComp->IsRegistered(*It))
			{
				continue;
			}
			if (const UVeloxHitboxComponent* Hitbox = UVeloxHitboxComponent::FindHitboxComponent(*It))
			{
				Hitbox->GetCurrentPose(Poses.AddDefaulted_GetRef());
				Targets.Add({ *It, nullptr });
			}
		}
		for (int32 Index = 0; Index < Targets.Num(); ++Index)
		{
			Targets[Index].Pose = &Poses[Index];
		}
	}

	/** Owning client: the simulated proxies' rendered capsules (what this player sees, ADR 0003 section 7). */
	void GatherRenderedTargets(UWorld& World, const AActor* Owner, TArray<FVeloxHitboxPose, TInlineAllocator<16>>& Poses, TArray<FVeloxProjectileTarget, TInlineAllocator<16>>& Targets)
	{
		Poses.Reset();
		Targets.Reset();
		for (TActorIterator<AVeloxCharacter> It(&World); It; ++It)
		{
			if (*It == Owner || It->GetLocalRole() != ROLE_SimulatedProxy)
			{
				continue;
			}
			if (const UVeloxHitboxComponent* Hitbox = UVeloxHitboxComponent::FindHitboxComponent(*It))
			{
				Hitbox->GetRenderedPose(Poses.AddDefaulted_GetRef());
				Targets.Add({ *It, nullptr });
			}
		}
		for (int32 Index = 0; Index < Targets.Num(); ++Index)
		{
			Targets[Index].Pose = &Poses[Index];
		}
	}

	/** What one ApplyDamage did to a victim other than the shooter (B26 hit confirm, Q3 PERS_HITS excludes self). */
	struct FConfirmTally
	{
		int32 Victims = 0;
		int32 Damage = 0;
		bool bKill = false;
	};

	/** One GE_Damage from SourceASC onto Victim (direct or splash); a non-self victim is counted into Tally. */
	void ApplyDamage(UAbilitySystemComponent& SourceASC, AActor* Victim, const UVeloxWeaponDefinition& Definition, int32 Damage, const FVector& Direction, const FVector& HitLocation,
		uint8 ShotSeq, bool bRadius, AActor* Causer, FConfirmTally& Tally)
	{
		UAbilitySystemComponent* VictimASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Victim);
		if (!VictimASC)
		{
			return;
		}
		const bool bSelf = VictimASC == &SourceASC;
		const UVeloxHealthSet* Health = bSelf ? nullptr : VictimASC->GetSet<UVeloxHealthSet>();
		const float Before = Health ? Health->GetHealth() + Health->GetArmor() : 0.f;
		const bool bWasAlive = Health && Health->GetHealth() > 0.f;
		FVeloxDamageSpecParams Params;
		Params.Damage = Damage;
		Params.DamageTag = Definition.DamageTag;
		Params.Direction = Direction;
		Params.HitLocation = HitLocation;
		Params.KnockbackScale = Definition.KnockbackScale;
		Params.ShotSeq = ShotSeq;
		Params.bSelf = bSelf;
		Params.bRadius = bRadius;
		Params.Causer = Causer;
		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(&SourceASC, Params);
		if (Spec.IsValid())
		{
			SourceASC.ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), VictimASC);
		}
		if (!bSelf)
		{
			// A protected or zero-point hit still counts as a hit (damage 0), like the hitscan confirm.
			++Tally.Victims;
			if (Health)
			{
				Tally.Damage += FMath::Max(FMath::RoundToInt32(Before - (Health->GetHealth() + Health->GetArmor())), 0);
				Tally.bKill |= bWasAlive && Health->GetHealth() <= 0.f;
			}
		}
	}
}

AVeloxProjectile::AVeloxProjectile(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	using namespace VeloxProjectilePrivate;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// The sphere only carries the radius and the Projectile object type: every query is done by FVeloxProjectileSim
	// (world: a sweep on the Weapon channel; pawns: hit shapes), so the sphere never blocks a pawn's movement.
	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitSphereRadius(2.f);
	CollisionComponent->SetCollisionObjectType(Velox_ObjectChannel_Projectile);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionComponent->SetGenerateOverlapEvents(false);
	CollisionComponent->SetCanEverAffectNavigation(false);
	RootComponent = CollisionComponent;

	// ADR 0003 section 7: replicated once, then dormant (the destruction still replicates); no movement replication.
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(ProjectileNetUpdateFrequency);
	NetPriority = ProjectileNetPriority;
	SetNetCullDistanceSquared(FMath::Square(ProjectileNetCullDistanceCm));
	NetDormancy = DORM_DormantAll;
}

void AVeloxProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.Condition = COND_InitialOnly;
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxProjectile, StartLocation, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxProjectile, LaunchVelocity, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxProjectile, SpawnServerTime, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxProjectile, ShotSeq, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AVeloxProjectile, Definition, Params);
}

float AVeloxProjectile::GetRadius() const
{
	return Definition ? FMath::Max(Definition->ProjectileRadiusCm, 0.f) : CollisionComponent->GetUnscaledSphereRadius();
}

AVeloxProjectile* AVeloxProjectile::SpawnServerProjectile(UWorld& World, TSubclassOf<AVeloxProjectile> ProjectileClass, const FVeloxProjectileLaunch& Launch)
{
	if (!Launch.Definition || !Launch.Shooter)
	{
		return nullptr;
	}
	UClass* Class = ProjectileClass ? ProjectileClass.Get() : AVeloxProjectile::StaticClass();
	const FTransform SpawnTransform(Launch.Dir.Rotation(), Launch.Origin);
	AVeloxProjectile* Projectile = World.SpawnActorDeferred<AVeloxProjectile>(Class, SpawnTransform, Launch.Shooter, Launch.Shooter, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile)
	{
		return nullptr;
	}
	Projectile->Definition = Launch.Definition;
	Projectile->ShotSeq = Launch.ShotSeq;
	Projectile->StartLocation = Launch.Origin;
	Projectile->LaunchVelocity = Launch.Dir * Launch.Definition->ProjectileSpeed;
	Projectile->ForwardSimSeconds = FMath::Max(Launch.ForwardSimSeconds, 0.f);
	Projectile->SpawnServerTime = static_cast<float>(World.GetTimeSeconds() - Projectile->ForwardSimSeconds);
	Projectile->SourceASC = Launch.SourceASC;
	Projectile->SelfSplashMovement = Launch.OwnerMovement;
	Projectile->SelfSplashEventId = Launch.SelfSplashEventId;
	Projectile->SelfSplashOrigin = Launch.SelfSplashOrigin;
	Projectile->State.Location = Launch.Origin;
	Projectile->State.Velocity = Projectile->LaunchVelocity;
	Projectile->State.Age = 0.0;
	Projectile->FinishSpawning(SpawnTransform);

	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxProjectileSpawn seq=%u weapon=%s owner=%s origin=%s fwd_ms=%.1f self_event=%d"), Launch.ShotSeq, *GetNameSafe(Launch.Definition),
		*GetNameSafe(Launch.Shooter), *Launch.Origin.ToCompactString(), Projectile->ForwardSimSeconds * 1000.f, Launch.SelfSplashEventId);

	// Forward simulation as a sweep: a hit inside the window explodes now (the projectile then never replicates).
	if (Projectile->ForwardSimSeconds > 0.f)
	{
		Projectile->TickServer(Projectile->ForwardSimSeconds);
	}
	return Projectile;
}

AVeloxProjectile* AVeloxProjectile::SpawnFakeProjectile(UWorld& World, TSubclassOf<AVeloxProjectile> ProjectileClass, const FVeloxProjectileLaunch& Launch)
{
	if (!Launch.Definition || !Launch.Shooter)
	{
		return nullptr;
	}
	UClass* Class = ProjectileClass ? ProjectileClass.Get() : AVeloxProjectile::StaticClass();
	const FTransform SpawnTransform(Launch.Dir.Rotation(), Launch.Origin);
	AVeloxProjectile* Projectile = World.SpawnActorDeferred<AVeloxProjectile>(Class, SpawnTransform, Launch.Shooter, Launch.Shooter, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile)
	{
		return nullptr;
	}
	Projectile->bFake = true;
	Projectile->SetReplicates(false);
	Projectile->Definition = Launch.Definition;
	Projectile->ShotSeq = Launch.ShotSeq;
	Projectile->StartLocation = Launch.Origin;
	Projectile->LaunchVelocity = Launch.Dir * Launch.Definition->ProjectileSpeed;
	Projectile->SourceASC = Launch.SourceASC;
	Projectile->SelfSplashMovement = Launch.OwnerMovement;
	Projectile->SelfSplashEventId = Launch.SelfSplashEventId;
	Projectile->SelfSplashOrigin = Launch.SelfSplashOrigin;
	Projectile->State.Location = Launch.Origin;
	Projectile->State.Velocity = Projectile->LaunchVelocity;
	Projectile->FinishSpawning(SpawnTransform);
	return Projectile;
}

void AVeloxProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (bFake || HasAuthority())
	{
		return;
	}

	// Replicated real projectile on a client.
	State.Location = StartLocation;
	State.Velocity = LaunchVelocity;
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn && OwnerPawn->IsLocallyControlled())
	{
		// The owner keeps its fake: the real one carries authority through the impact cue, not through visibility.
		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);
		UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxProjectileReal seq=%u hidden for the owner %s"), ShotSeq, *GetNameSafe(OwnerPawn));
		return;
	}
	TickProxy(0.f);
}

void AVeloxProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bExploded)
	{
		return;
	}
	if (bFake)
	{
		TickFake(DeltaSeconds);
	}
	else if (HasAuthority())
	{
		TickServer(DeltaSeconds);
	}
	else
	{
		TickProxy(DeltaSeconds);
	}
}

bool AVeloxProjectile::SweepWorld(const FVector& Start, const FVector& End, double& OutDist, FVector& OutNormal) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VeloxProjectileSweep), false, this);
	if (const AActor* OwnerActor = GetOwner())
	{
		Params.AddIgnoredActor(OwnerActor);
	}
	FHitResult Hit;
	const float Radius = GetRadius();
	const bool bHit = Radius > 0.f
		? World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Velox_TraceChannel_Weapon, FCollisionShape::MakeSphere(Radius), Params)
		: World->LineTraceSingleByChannel(Hit, Start, End, Velox_TraceChannel_Weapon, Params);
	if (!bHit)
	{
		return false;
	}
	OutDist = Hit.bStartPenetrating ? 0.0 : static_cast<double>(Hit.Distance);
	OutNormal = Hit.ImpactNormal;
	return true;
}

double AVeloxProjectile::ComputeWorldFlight(const UWorld& World, const UVeloxWeaponDefinition& Definition, const FVector& Origin, const FVector& Dir, const AActor* IgnoreActor, FVector& OutLocation)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VeloxProjectileFlight), false, IgnoreActor);
	const float Radius = FMath::Max(Definition.ProjectileRadiusCm, 0.f);
	auto Sweep = [&World, &Params, Radius](const FVector& Start, const FVector& End, double& OutDist, FVector& OutNormal)
	{
		FHitResult Hit;
		const bool bHit = Radius > 0.f
			? World.SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Velox_TraceChannel_Weapon, FCollisionShape::MakeSphere(Radius), Params)
			: World.LineTraceSingleByChannel(Hit, Start, End, Velox_TraceChannel_Weapon, Params);
		if (bHit)
		{
			OutDist = Hit.bStartPenetrating ? 0.0 : static_cast<double>(Hit.Distance);
			OutNormal = Hit.ImpactNormal;
		}
		return bHit;
	};
	FVector Normal;
	bool bHitWorld = false;
	return FVeloxProjectileSim::WorldFlight(Origin, Dir, Definition.ProjectileSpeed, Definition.ProjectileLifetime, Sweep, OutLocation, Normal, bHitWorld);
}

void AVeloxProjectile::TickServer(float DeltaSeconds)
{
	using namespace VeloxProjectilePrivate;

	UWorld* World = GetWorld();
	if (!World || !Definition || bExploded)
	{
		return;
	}
	TArray<FVeloxHitboxPose, TInlineAllocator<16>> Poses;
	TArray<FVeloxProjectileTarget, TInlineAllocator<16>> Targets;
	GatherServerTargets(*World, Poses, Targets);

	FVeloxProjectileImpact Impact;
	auto Sweep = [this](const FVector& Start, const FVector& End, double& OutDist, FVector& OutNormal) { return SweepWorld(Start, End, OutDist, OutNormal); };
	const bool bImpact = FVeloxProjectileSim::Step(State, DeltaSeconds, Definition->ProjectileLifetime, GetRadius(), Sweep, Targets, GetOwner(), Impact);
	SetActorLocation(State.Location, false, nullptr, ETeleportType::TeleportPhysics);
	if (bImpact)
	{
		ServerExplode(Impact);
	}
}

void AVeloxProjectile::ServerExplode(const FVeloxProjectileImpact& Impact)
{
	using namespace VeloxProjectilePrivate;

	if (bExploded)
	{
		return;
	}
	bExploded = true;
	UWorld* World = GetWorld();
	UAbilitySystemComponent* ASC = SourceASC.Get();
	if (!World || !Definition || !ASC)
	{
		Destroy();
		return;
	}
	const UVeloxCombatSettings& Settings = SettingsOf(ASC);
	const FVector Origin = Impact.Location;
	AActor* OwnerPawn = GetOwner();

	// The owner's self splash runs in its move timeline (ScheduleOwnerSelfSplash) when this explosion is the one the
	// schedule predicted (world impact at the scheduled origin). A pawn hit or a different spot: resolve it here.
	bool bSkipOwner = false;
	bool bResolveOwnerHere = false;
	UVeloxMovementComponent* OwnerMovement = SelfSplashMovement.Get();
	if (OwnerMovement && SelfSplashEventId != 0)
	{
		if (!OwnerMovement->HasMoveTimedEvent(SelfSplashEventId))
		{
			bSkipOwner = true; // already resolved in the owner's move stream
		}
		else if (!Impact.Pawn && FVector::Dist(Origin, SelfSplashOrigin) <= GetRadius())
		{
			bSkipOwner = true;
			// Deadline: a client that stops sending moves must not keep its self damage pending forever.
			const float Deadline = ForwardSimSeconds + Settings.MaxRewindSeconds;
			const int32 EventId = SelfSplashEventId;
			const uint8 Seq = ShotSeq;
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(OwnerMovement, [OwnerMovement, EventId, Seq]()
			{
				if (OwnerMovement->RunMoveTimedEventNow(EventId))
				{
					UE_LOG(LogVeloxCombat, Log, TEXT("VeloxSelfSplash seq=%u forced by the deadline (no owner move reached it)"), Seq);
				}
			}), FMath::Max(Deadline, UE_KINDA_SMALL_NUMBER), false);
		}
		else
		{
			OwnerMovement->CancelMoveTimedEvent(SelfSplashEventId);
			bResolveOwnerHere = true;
		}
	}

#if !UE_BUILD_SHIPPING
	FVeloxProjectileExplosionInfo& Info = GLastServerExplosion;
	Info.ShotSeq = ShotSeq;
	Info.Location = Origin;
	Info.DirectVictim = Impact.Pawn;
	Info.Splash.Reset();
	Info.bOwnerSelfSplashDeferred = bSkipOwner;
	++Info.Count;
#endif

	// Direct hit (never the owner: Step ignores it), direction = flight direction.
	FConfirmTally Confirm;
	if (Impact.Pawn && Definition->Damage > 0)
	{
		ApplyDamage(*ASC, Impact.Pawn, *Definition, Definition->Damage, LaunchVelocity, Origin, ShotSeq, false, this, Confirm);
	}

	// Q3 G_RadiusDamage: every living pawn but the direct victim with Dist < radius and LOS, zero points included (B3).
	TArray<FVeloxHitboxPose, TInlineAllocator<16>> Poses;
	TArray<FVeloxProjectileTarget, TInlineAllocator<16>> Targets;
	GatherServerTargets(*World, Poses, Targets);
	if (bSkipOwner)
	{
		Targets.RemoveAll([OwnerPawn](const FVeloxProjectileTarget& T) { return T.Actor == OwnerPawn; });
	}
	TArray<FVeloxSplashHit, TInlineAllocator<16>> Splash;
	auto LineOfSight = [World](const FVector& From, const FVector& To) { return HasLineOfSight(*World, From, To); };
	FVeloxProjectileSim::CollectSplash(Origin, Targets, Impact.Pawn, Definition->SplashDamage, Definition->SplashRadiusCm, Settings.RadiusKnockbackZOffset, LineOfSight, Splash);
	for (const FVeloxSplashHit& Hit : Splash)
	{
		ApplyDamage(*ASC, Hit.Actor, *Definition, Hit.Points, Hit.Dir, Origin, ShotSeq, true, this, Confirm);
#if !UE_BUILD_SHIPPING
		Info.Splash.Emplace(Hit.Actor, Hit.Points);
#endif
	}
	if (bResolveOwnerHere && OwnerMovement)
	{
		OwnerMovement->ServerResolveSelfKnockbackShot(ShotSeq);
	}

	// B26: one server-confirmed hit marker per explosion with a non-owner victim (also after the shooter died: the
	// controller comes from the PlayerState that owns the source ASC).
	const uint8 ConfirmDamage = static_cast<uint8>(FMath::Clamp(Confirm.Damage, 0, static_cast<int32>(MAX_uint8)));
	bool bConfirmSent = false;
	if (Confirm.Victims > 0)
	{
		const APlayerState* ShooterPS = Cast<APlayerState>(ASC->GetOwnerActor());
		if (AVeloxPlayerController* VeloxPC = ShooterPS ? Cast<AVeloxPlayerController>(ShooterPS->GetPlayerController()) : nullptr)
		{
			VeloxPC->ClientConfirmHit(ShotSeq, ConfirmDamage, Confirm.bKill ? VeloxHitConfirmFlags::Kill : VeloxHitConfirmFlags::None);
			bConfirmSent = true;
		}
	}
#if !UE_BUILD_SHIPPING
	Info.ConfirmedVictims = Confirm.Victims;
	Info.ConfirmedDamage = ConfirmDamage;
	Info.bKill = Confirm.bKill;
	Info.bConfirmSent = bConfirmSent;
#endif

	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxProjectileExplode seq=%u weapon=%s owner=%s at=%s age_ms=%.1f direct=%s splash=%d self=%s confirm_victims=%d confirm_dmg=%u kill=%d confirm_sent=%d"),
		ShotSeq, *GetNameSafe(Definition), *GetNameSafe(OwnerPawn), *Origin.ToCompactString(), State.Age * 1000.0, *GetNameSafe(Impact.Pawn), Splash.Num(),
		bSkipOwner ? TEXT("move_timed") : (bResolveOwnerHere ? TEXT("here") : TEXT("local")), Confirm.Victims, ConfirmDamage, Confirm.bKill ? 1 : 0, bConfirmSent ? 1 : 0);

	// The authoritative explosion for every client, the owner included: outside any prediction window (an explosion
	// inside the fire RPC's forward simulation would otherwise carry the shot's key and skip the predicting client).
	if (Definition->ImpactCueTag.IsValid())
	{
		FScopedPredictionWindow NoPrediction(ASC, FPredictionKey(), false);
		FGameplayCueParameters Params;
		Params.Location = Origin;
		Params.Normal = Impact.Normal;
		Params.RawMagnitude = Definition->SplashRadiusCm;
		Params.Instigator = OwnerPawn;
		Params.EffectCauser = OwnerPawn;
		Params.EffectContext = ASC->MakeEffectContext();
		Params.EffectContext.AddOrigin(StartLocation);
		if (FVeloxGameplayEffectContext* Context = FVeloxGameplayEffectContext::Get(Params.EffectContext))
		{
			Context->ShotSeq = ShotSeq;
			Context->HitLocation = Origin;
		}
		ASC->ExecuteGameplayCue(Definition->ImpactCueTag, Params);
	}
	Destroy();
}

void AVeloxProjectile::TickFake(float DeltaSeconds)
{
	using namespace VeloxProjectilePrivate;

	UWorld* World = GetWorld();
	if (!World || !Definition)
	{
		Destroy();
		return;
	}
	TArray<FVeloxHitboxPose, TInlineAllocator<16>> Poses;
	TArray<FVeloxProjectileTarget, TInlineAllocator<16>> Targets;
	GatherRenderedTargets(*World, GetOwner(), Poses, Targets);

	FVeloxProjectileImpact Impact;
	auto Sweep = [this](const FVector& Start, const FVector& End, double& OutDist, FVector& OutNormal) { return SweepWorld(Start, End, OutDist, OutNormal); };
	const bool bImpact = FVeloxProjectileSim::Step(State, DeltaSeconds, Definition->ProjectileLifetime, GetRadius(), Sweep, Targets, GetOwner(), Impact);
	SetActorLocation(State.Location, false, nullptr, ETeleportType::TeleportPhysics);
#if !UE_BUILD_SHIPPING
	if (DebugDrawEnabled())
	{
		DrawDebugSphere(World, State.Location, FMath::Max(GetRadius(), 4.f), 6, FColor::Cyan, false, 0.f);
	}
#endif
	if (bImpact)
	{
		FakeDetonate(Impact);
	}
}

void AVeloxProjectile::FakeDetonate(const FVeloxProjectileImpact& Impact)
{
	bExploded = true;
	UVeloxMovementComponent* OwnerMovement = SelfSplashMovement.Get();
	if (Impact.Pawn && OwnerMovement && Definition)
	{
		// The fake hit a rendered pawn before the world point the self splash was scheduled for: predict from here, now
		// (the server most likely hits that pawn too and resolves the owner's splash at its explosion).
		if (SelfSplashEventId != 0)
		{
			OwnerMovement->CancelMoveTimedEvent(SelfSplashEventId);
		}
		RunOwnerSelfSplash(*OwnerMovement, *Definition, SourceASC.Get(), Impact.Location, ShotSeq, true, TEXT("fake_pawn_hit"));
	}
	UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxProjectileFake seq=%u detonated at %s pawn=%s"), ShotSeq, *Impact.Location.ToCompactString(), *GetNameSafe(Impact.Pawn));
#if !UE_BUILD_SHIPPING
	if (VeloxProjectilePrivate::DebugDrawEnabled() && Definition)
	{
		DrawDebugSphere(GetWorld(), Impact.Location, Definition->SplashRadiusCm, 12, FColor::Cyan, false, 0.5f);
	}
#endif
	Destroy();
}

void AVeloxProjectile::TickProxy(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!World || !Definition || bProxyStopped)
	{
		return;
	}
	// Dead reckoning in the server-time estimate; the world stops the visual, the impact cue shows the explosion.
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	const double Age = FMath::Clamp(Now - static_cast<double>(SpawnServerTime), 0.0, static_cast<double>(Definition->ProjectileLifetime));
	const FVector Target = FVeloxProjectileSim::PositionAt(StartLocation, LaunchVelocity, Age);
	double Dist = 0.0;
	FVector Normal;
	const FVector Delta = Target - State.Location;
	if (!Delta.IsNearlyZero() && SweepWorld(State.Location, Target, Dist, Normal))
	{
		State.Location += Delta.GetSafeNormal() * Dist;
		bProxyStopped = true;
	}
	else
	{
		State.Location = Target;
	}
	SetActorLocation(State.Location, false, nullptr, ETeleportType::TeleportPhysics);
#if !UE_BUILD_SHIPPING
	if (VeloxProjectilePrivate::DebugDrawEnabled())
	{
		DrawDebugSphere(World, State.Location, FMath::Max(GetRadius(), 4.f), 6, FColor::Red, false, 0.f);
	}
#endif
}

int32 AVeloxProjectile::ScheduleOwnerSelfSplash(UVeloxMovementComponent& OwnerMovement, const UVeloxWeaponDefinition& Definition, UAbilitySystemComponent* SourceASC, float MoveTime,
	const FVector& Origin, uint8 InShotSeq, bool bPredicted)
{
	if (!bPredicted)
	{
		OwnerMovement.ServerBeginSelfKnockbackShot(InShotSeq);
	}
	TWeakObjectPtr<const UVeloxWeaponDefinition> WeakDefinition = &Definition;
	TWeakObjectPtr<UAbilitySystemComponent> WeakASC = SourceASC;
	return OwnerMovement.AddMoveTimedEvent(MoveTime, [WeakDefinition, WeakASC, Origin, InShotSeq, bPredicted](UVeloxMovementComponent& Movement)
	{
		if (const UVeloxWeaponDefinition* Def = WeakDefinition.Get())
		{
			RunOwnerSelfSplash(Movement, *Def, WeakASC.Get(), Origin, InShotSeq, bPredicted, TEXT("move"));
		}
		else if (!bPredicted)
		{
			Movement.ServerResolveSelfKnockbackShot(InShotSeq);
		}
	});
}

void AVeloxProjectile::RunOwnerSelfSplash(UVeloxMovementComponent& OwnerMovement, const UVeloxWeaponDefinition& Definition, UAbilitySystemComponent* InSourceASC, const FVector& Origin,
	uint8 InShotSeq, bool bPredicted, const TCHAR* Reason)
{
	using namespace VeloxProjectilePrivate;

	ACharacter* Pawn = OwnerMovement.GetCharacterOwner();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	UAbilitySystemComponent* VictimASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	const UVeloxHitboxComponent* Hitbox = UVeloxHitboxComponent::FindHitboxComponent(Pawn);
	FVeloxSplashHit Hit;
	bool bHit = false;
	if (World && VictimASC && Hitbox && !VictimASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead))
	{
		FVeloxHitboxPose Pose;
		Hitbox->GetCurrentPose(Pose);
		const UVeloxCombatSettings& Settings = SettingsOf(VictimASC);
		auto LineOfSight = [World](const FVector& From, const FVector& To) { return HasLineOfSight(*World, From, To); };
		bHit = FVeloxProjectileSim::EvaluateSplash(Origin, Pawn, Pose, Definition.SplashDamage, Definition.SplashRadiusCm, Settings.RadiusKnockbackZOffset, LineOfSight, Hit);
		if (bHit && bPredicted)
		{
			const bool bProtected = VictimASC->HasMatchingGameplayTag(VeloxGameplayTags::State_SpawnProtected);
			const FVector Impulse = FVeloxProjectileSim::SelfKnockback(Hit, Definition.KnockbackScale, Settings.BuildCombatParams(), bProtected);
			OwnerMovement.AddKnockback(Impulse, false, EVeloxKnockbackSource::Predicted, InShotSeq);
		}
		else if (bHit)
		{
			// The GE path: half damage, full knockback through UVeloxHealthSet -> AddKnockback(Server) (ADR 0003 section 2).
			// The owner's own splash never confirms a hit (Q3 PERS_HITS excludes self).
			FConfirmTally SelfOnly;
			ApplyDamage(InSourceASC ? *InSourceASC : *VictimASC, Pawn, Definition, Hit.Points, Hit.Dir, Origin, InShotSeq, true, nullptr, SelfOnly);
		}
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxSelfSplash role=%s reason=%s seq=%u ts=%.6f pos=%s origin=%s hit=%d dist=%.2f points=%d"), bPredicted ? TEXT("client") : TEXT("server"), Reason, InShotSeq,
		OwnerMovement.GetCurrentMoveTimeStamp(), Pawn ? *Pawn->GetActorLocation().ToCompactString() : TEXT("-"), *Origin.ToCompactString(), bHit ? 1 : 0, Hit.Dist, Hit.Points);
	if (!bPredicted)
	{
		OwnerMovement.ServerResolveSelfKnockbackShot(InShotSeq);
	}
}

void AVeloxProjectile::OnImpactCue(const AActor* CueAvatar, const FGameplayCueParameters& Parameters)
{
	const APawn* OwnerPawn = Cast<APawn>(CueAvatar);
	UWorld* World = OwnerPawn ? OwnerPawn->GetWorld() : nullptr;
	if (!World || World->GetNetMode() != NM_Client || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}
	const FVeloxGameplayEffectContext* Context = FVeloxGameplayEffectContext::Get(Parameters.EffectContext);
	if (!Context)
	{
		return;
	}
	const FVector CueLocation = Parameters.Location;
	for (TActorIterator<AVeloxProjectile> It(World); It; ++It)
	{
		AVeloxProjectile* Fake = *It;
		if (!Fake->bFake || Fake->bExploded || Fake->GetOwner() != OwnerPawn || Fake->ShotSeq != Context->ShotSeq)
		{
			continue;
		}
		UVeloxMovementComponent* OwnerMovement = Fake->SelfSplashMovement.Get();
		if (OwnerMovement && Fake->SelfSplashEventId != 0 && FVector::Dist(CueLocation, Fake->SelfSplashOrigin) > Fake->GetRadius())
		{
			// The server exploded elsewhere (a pawn the fake did not hit): its own explosion resolves the owner's splash.
			OwnerMovement->CancelMoveTimedEvent(Fake->SelfSplashEventId);
		}
		UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxProjectileFake seq=%u ended by the impact cue at %s"), Fake->ShotSeq, *CueLocation.ToCompactString());
		Fake->bExploded = true;
		Fake->Destroy();
	}
}

#if !UE_BUILD_SHIPPING
const FVeloxProjectileExplosionInfo& AVeloxProjectile::GetLastServerExplosion()
{
	return VeloxProjectilePrivate::GLastServerExplosion;
}
#endif
