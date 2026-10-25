// Velox - hitscan fire ability.

#include "Weapons/VeloxGameplayAbility_FireHitscan.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Combat/VeloxHitboxComponent.h"
#include "Core/VeloxCollisionChannels.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "TimerManager.h"
#include "Weapons/VeloxShotTargetData.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameplayAbility_FireHitscan)

namespace VeloxHitscanPrivate
{
	int32 PlayerIdOf(const FGameplayAbilityActorInfo* ActorInfo)
	{
		const APlayerState* PlayerState = ActorInfo ? Cast<APlayerState>(ActorInfo->OwnerActor.Get()) : nullptr;
		return PlayerState ? PlayerState->GetPlayerId() : 0;
	}
}

UVeloxGameplayAbility_FireHitscan::UVeloxGameplayAbility_FireHitscan(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UVeloxGameplayAbility_FireHitscan::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	const UVeloxWeaponDefinition* Definition = GetCurrentDefinition();
	APawn* Pawn = GetShooterPawn();
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!Definition || !Pawn || !ASC || !World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (IsServerForRemoteShooter(ActorInfo))
	{
		// The shot arrives as target data right behind this activation (both reliable, same channel).
		const FPredictionKey Key = ActivationInfo.GetActivationPredictionKey();
		TargetDataDelegateHandle = ASC->AbilityTargetDataSetDelegate(Handle, Key).AddUObject(this, &ThisClass::OnServerTargetData);
		ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, Key);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	AController* Controller = GetShooterController();
	if (!Controller)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// B6: origin from the pawn's view location, direction from the control rotation (this frame's mouse delta included).
	FShotInput Shot;
	Shot.Origin = Pawn->GetPawnViewLocation();
	Shot.Dir = Controller->GetControlRotation().Vector();
	if (const AVeloxPlayerController* VeloxPC = Cast<AVeloxPlayerController>(Controller))
	{
		// Once per shot with the centre ray, also for the shotgun (B14). Server side: now, no hint.
		const FVeloxShotTiming Timing = VeloxPC->BuildShotTiming(Shot.Origin, Shot.Dir, Definition->RangeCm);
		Shot.ViewServerTime = Timing.ViewServerTime;
		Shot.HintTarget = Timing.HintTarget;
		Shot.HintTargetStamp = Timing.HintTargetStamp;
	}
	else
	{
		Shot.ViewServerTime = World->GetTimeSeconds();
	}
	UVeloxWeaponInventoryComponent* Inventory = GetInventory();
	Shot.ShotSeq = Inventory ? Inventory->AllocateShotSeq() : 0;
	const double Now = World->GetTimeSeconds();

	if (ActorInfo->IsNetAuthority())
	{
		// Listen host, bots, standalone (review step 6): no RPC, resolve with the server's timing.
		ResolveServerShot(*Definition, Shot);
		AdvanceNextFireTime(Now, Definition->GetSafeFireInterval());
		EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
		return;
	}

	// Predicting client (review client steps 4-5). Inside the activation's prediction window.
	FGameplayAbilityTargetData_VeloxShot* Data = new FGameplayAbilityTargetData_VeloxShot();
	Data->Origin = Shot.Origin;
	Data->Dir = Shot.Dir;
	Data->ViewServerTime = static_cast<float>(Shot.ViewServerTime);
	Data->ShotSeq = Shot.ShotSeq;
	Data->HintTarget = Shot.HintTarget;
	Data->bHasHint = Shot.HintTarget.IsValid();
	Data->HintTargetStamp = Data->bHasHint ? Shot.HintTargetStamp : 0.f;
	const FGameplayAbilityTargetDataHandle DataHandle(Data);
	ASC->CallServerSetReplicatedTargetData(Handle, ActivationInfo.GetActivationPredictionKey(), DataHandle, FGameplayTag(), ASC->ScopedPredictionKey);

	ApplyAmmoCost(*Definition);
	PlayLocalShotCosmetics(*Definition, Shot);
	AdvanceNextFireTime(Now, Definition->GetSafeFireInterval());
	UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxShotSent seq=%u weapon=%s view=%.3f hint=%s"), Shot.ShotSeq, *GetNameSafe(Definition), Shot.ViewServerTime, *GetNameSafe(Shot.HintTarget.Get()));
	// The server ends its own instance after the target data; no end RPC per shot.
	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}

void UVeloxGameplayAbility_FireHitscan::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (TargetDataDelegateHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
		{
			const FPredictionKey Key = ActivationInfo.GetActivationPredictionKey();
			ASC->AbilityTargetDataSetDelegate(Handle, Key).Remove(TargetDataDelegateHandle);
			ASC->ConsumeClientReplicatedTargetData(Handle, Key);
		}
		TargetDataDelegateHandle.Reset();
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UVeloxGameplayAbility_FireHitscan::Reject(uint8 ShotSeq, const TCHAR* Reason)
{
	LastServerShot = FVeloxServerShotInfo();
	LastServerShot.ShotSeq = ShotSeq;
	LastServerShot.bRejected = true;
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotRejected seq=%u shooter=%s weapon=%s reason=%s"), ShotSeq, *GetNameSafe(GetShooterPawn()), *GetNameSafe(GetCurrentDefinition()), Reason);
}

void UVeloxGameplayAbility_FireHitscan::OnServerTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag)
{
	UAbilitySystemComponent* ASC = CurrentActorInfo ? CurrentActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC || !IsActive())
	{
		return;
	}
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());

	const UVeloxWeaponDefinition* Definition = GetCurrentDefinition();
	const UWorld* World = GetWorld();
	const FGameplayAbilityTargetData* Raw = Data.Num() > 0 ? Data.Get(0) : nullptr;
	const FGameplayAbilityTargetData_VeloxShot* ShotData =
		(Raw && Raw->GetScriptStruct() == FGameplayAbilityTargetData_VeloxShot::StaticStruct()) ? static_cast<const FGameplayAbilityTargetData_VeloxShot*>(Raw) : nullptr;
	const uint8 Seq = ShotData ? ShotData->ShotSeq : 0;
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	// Review server step 1: the bucket first (cheapest), then the payload. Nothing touches the subsystem on rejection.
	FVector Dir = ShotData ? FVector(ShotData->Dir) : FVector::ZeroVector;
	if (!Definition || !World)
	{
		Reject(Seq, TEXT("no_weapon"));
	}
	else if (!ConsumeServerFireToken(Now, *Definition))
	{
		Reject(Seq, TEXT("rate"));
	}
	else if (!ShotData)
	{
		Reject(Seq, TEXT("type"));
	}
	else if (ShotData->Origin.ContainsNaN())
	{
		Reject(Seq, TEXT("origin"));
	}
	else if (Dir.ContainsNaN() || Dir.IsNearlyZero())
	{
		Reject(Seq, TEXT("dir"));
	}
	else if (!FMath::IsFinite(ShotData->ViewServerTime) || !FMath::IsFinite(ShotData->HintTargetStamp))
	{
		Reject(Seq, TEXT("time"));
	}
	else
	{
		FShotInput Shot;
		Shot.Origin = ShotData->Origin;
		Shot.Dir = Dir.GetSafeNormal();
		Shot.ViewServerTime = ShotData->ViewServerTime;
		Shot.HintTarget = ShotData->bHasHint ? ShotData->HintTarget : TWeakObjectPtr<AActor>();
		Shot.HintTargetStamp = ShotData->bHasHint ? ShotData->HintTargetStamp : 0.f;
		Shot.ShotSeq = Seq;
		ResolveServerShot(*Definition, Shot);
		AdvanceNextFireTime(Now, Definition->GetSafeFireInterval());
	}
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
}

void UVeloxGameplayAbility_FireHitscan::ResolveServerShot(const UVeloxWeaponDefinition& Definition, const FShotInput& Shot)
{
	APawn* Shooter = GetShooterPawn();
	UAbilitySystemComponent* ASC = CurrentActorInfo ? CurrentActorInfo->AbilitySystemComponent.Get() : nullptr;
	UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	const AVeloxPlayerState* PlayerState = CurrentActorInfo ? Cast<AVeloxPlayerState>(CurrentActorInfo->OwnerActor.Get()) : nullptr;
	UVeloxLagCompensationSubsystem* LagComp = UVeloxLagCompensationSubsystem::Get(World);
	LastServerShot = FVeloxServerShotInfo();
	LastServerShot.ShotSeq = Shot.ShotSeq;
	if (!ASC || !World || !LagComp)
	{
		UE_LOG(LogVeloxCombat, Warning, TEXT("VeloxShotRejected seq=%u shooter=%s reason=no_lag_compensation"), Shot.ShotSeq, *GetNameSafe(Shooter));
		LastServerShot.bRejected = true;
		return;
	}

	// Review server step 2: the request, pellets from the server's own stream.
	FVeloxShotRequest Request;
	Request.Shooter = Shooter;
	Request.Origin = Shot.Origin;
	Request.RangeCm = Definition.RangeCm;
	Request.bPenetratePawns = Definition.bPenetratesPawns;
	Request.MaxPenetrations = Definition.MaxPenetrations;
	Request.ShotSeq = Shot.ShotSeq;
	VeloxShotDirs::Build(Definition, Shot.Dir, VeloxShotDirs::MakeSeed(Shot.ShotSeq, VeloxHitscanPrivate::PlayerIdOf(CurrentActorInfo)), Request.Dirs);
	Request.ApplySettings(PlayerState ? PlayerState->GetCombatSettings() : nullptr);
	Request.ViewServerTime = Shot.ViewServerTime;
	Request.HintTarget = Shot.HintTarget;
	Request.HintTargetStamp = Shot.HintTargetStamp;

	// Step 3.
	FVeloxShotResult Result;
	LagComp->TraceShot(Request, Result);

	// Step 4: one damage spec per victim, nearest first; a victim killed earlier in the list still gets its hit applied.
	int32 TotalDamage = 0;
	for (const FVeloxShotHit& Hit : Result.Hits)
	{
		UAbilitySystemComponent* VictimASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Hit.Pawn);
		if (!VictimASC)
		{
			continue;
		}
		const UVeloxHealthSet* Health = VictimASC->GetSet<UVeloxHealthSet>();
		const float Before = Health ? Health->GetHealth() + Health->GetArmor() : 0.f;
		const bool bWasAlive = Health && Health->GetHealth() > 0.f;

		FVeloxDamageSpecParams Params;
		Params.Damage = Definition.Damage;
		Params.PelletHits = Hit.PelletHits;
		Params.DamageTag = Definition.DamageTag;
		Params.Direction = Shot.Dir;
		Params.HitLocation = Hit.Location;
		Params.KnockbackScale = Definition.KnockbackScale;
		Params.ShotSeq = Shot.ShotSeq;
		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(ASC, Params);
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), VictimASC);
		}
		++LastServerShot.NumHits;
		LastServerShot.PelletHits += Hit.PelletHits;
		if (Health)
		{
			TotalDamage += FMath::Max(FMath::RoundToInt32(Before - (Health->GetHealth() + Health->GetArmor())), 0);
			LastServerShot.bKill |= bWasAlive && Health->GetHealth() <= 0.f;
		}
	}
	LastServerShot.TotalDamage = TotalDamage;

	// Step 5: ammo, cues (with the activation's prediction key on a remote shot: the predicting client skips them).
	ApplyAmmoCost(Definition);

	const FVector Start = Result.Origin;
	FVector End = Start + Shot.Dir * Definition.RangeCm;
	FVector Normal = -Shot.Dir;
	if (!Definition.IsPelletWeapon() && Result.Rays.Num() > 0)
	{
		End = Result.Rays[0].End;
		Normal = Result.Rays[0].bHitWorld ? Result.Rays[0].ImpactNormal : -Shot.Dir;
	}
	else if (Result.Hits.Num() > 0)
	{
		End = Result.Hits[0].Location;
	}
	else
	{
		// Shotgun miss: the centre ray's world end (the pellets' own impacts are predicted by the shooter only).
		FHitResult WorldHit;
		const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VeloxShotCentre), true, Shooter);
		if (World->LineTraceSingleByChannel(WorldHit, Start, End, Velox_TraceChannel_Weapon, QueryParams))
		{
			End = WorldHit.ImpactPoint;
			Normal = WorldHit.ImpactNormal;
		}
	}
	const FGameplayCueParameters CueParams = MakeCueParameters(Start, End, Normal, Shot.ShotSeq);
	if (Definition.FireCueTag.IsValid())
	{
		ASC->ExecuteGameplayCue(Definition.FireCueTag, CueParams);
	}
	if (Definition.ImpactCueTag.IsValid())
	{
		ASC->ExecuteGameplayCue(Definition.ImpactCueTag, CueParams);
	}
	if (Definition.bBeam)
	{
		// The inventory owns the keep-alive timer: EndAbility clears every timer of the ability object.
		if (UVeloxWeaponInventoryComponent* Inventory = GetInventory())
		{
			Inventory->KeepBeamAlive(Definition, CueParams);
		}
	}

	if (Result.Hits.Num() > 0)
	{
		if (AVeloxPlayerController* VeloxPC = Cast<AVeloxPlayerController>(GetShooterController()))
		{
			VeloxPC->ClientConfirmHit(Shot.ShotSeq, static_cast<uint8>(FMath::Clamp(TotalDamage, 0, 255)), LastServerShot.bKill ? EVeloxHitConfirmFlags::Kill : EVeloxHitConfirmFlags::None);
		}
	}
}

void UVeloxGameplayAbility_FireHitscan::PlayLocalShotCosmetics(const UVeloxWeaponDefinition& Definition, const FShotInput& Shot) const
{
	const APawn* Shooter = GetShooterPawn();
	UAbilitySystemComponent* ASC = CurrentActorInfo ? CurrentActorInfo->AbilitySystemComponent.Get() : nullptr;
	UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	if (!ASC || !World)
	{
		return;
	}

	// Rendered capsules of the simulated proxies (what this player sees), never the server subsystem (B10).
	TArray<const UVeloxHitboxComponent*, TInlineAllocator<16>> Proxies;
	if (!Definition.bPenetratesPawns)
	{
		for (TActorIterator<ACharacter> It(World); It; ++It)
		{
			if (*It != Shooter && It->GetLocalRole() == ROLE_SimulatedProxy)
			{
				if (const UVeloxHitboxComponent* Hitbox = UVeloxHitboxComponent::FindHitboxComponent(*It))
				{
					Proxies.Add(Hitbox);
				}
			}
		}
	}

	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VeloxShotCosmetic), true, Shooter);
	FVeloxHitboxPose Pose;
	auto TraceRay = [&](const FVector& RayDir, FVector& OutEnd, FVector& OutNormal)
	{
		double MaxDist = Definition.RangeCm;
		OutEnd = Shot.Origin + RayDir * MaxDist;
		OutNormal = -RayDir;
		FHitResult WorldHit;
		if (World->LineTraceSingleByChannel(WorldHit, Shot.Origin, OutEnd, Velox_TraceChannel_Weapon, QueryParams))
		{
			MaxDist = WorldHit.Distance;
			OutEnd = WorldHit.ImpactPoint;
			OutNormal = WorldHit.ImpactNormal;
		}
		for (const UVeloxHitboxComponent* Hitbox : Proxies)
		{
			Hitbox->GetRenderedPose(Pose);
			double Dist;
			int32 Shape;
			if (FVeloxHitMath::RayPose(Shot.Origin, RayDir, MaxDist, Pose, Dist, Shape) && Dist < MaxDist)
			{
				MaxDist = Dist;
				OutEnd = Shot.Origin + RayDir * Dist;
				OutNormal = -RayDir;
			}
		}
	};

	TArray<FVector, TInlineAllocator<1>> Dirs;
	VeloxShotDirs::Build(Definition, Shot.Dir, VeloxShotDirs::MakeSeed(Shot.ShotSeq, VeloxHitscanPrivate::PlayerIdOf(CurrentActorInfo)), Dirs);

	FVector CentreEnd;
	FVector CentreNormal;
	TraceRay(Definition.IsPelletWeapon() ? Shot.Dir : Dirs[0], CentreEnd, CentreNormal);
	if (Definition.FireCueTag.IsValid())
	{
		ASC->ExecuteGameplayCue(Definition.FireCueTag, MakeCueParameters(Shot.Origin, CentreEnd, CentreNormal, Shot.ShotSeq));
	}
	if (!Definition.ImpactCueTag.IsValid())
	{
		return;
	}
	if (!Definition.IsPelletWeapon())
	{
		ASC->ExecuteGameplayCue(Definition.ImpactCueTag, MakeCueParameters(Shot.Origin, CentreEnd, CentreNormal, Shot.ShotSeq));
		return;
	}
	for (const FVector& PelletDir : Dirs)
	{
		FVector End;
		FVector Normal;
		TraceRay(PelletDir, End, Normal);
		ASC->ExecuteGameplayCue(Definition.ImpactCueTag, MakeCueParameters(Shot.Origin, End, Normal, Shot.ShotSeq));
	}
}
