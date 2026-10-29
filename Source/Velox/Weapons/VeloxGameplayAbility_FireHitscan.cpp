// Velox - hitscan fire ability.

#include "Weapons/VeloxGameplayAbility_FireHitscan.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Combat/VeloxHitboxComponent.h"
#include "Core/VeloxCollisionChannels.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "Movement/VeloxMovementComponent.h"
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

	/** B21: every hitscan cue carries the server's pattern seed and the unit centre direction (receivers rebuild pellets). */
	void WritePattern(FGameplayCueParameters& Params, uint8 PatternSeq, const FVector& CentreDir)
	{
		if (FVeloxGameplayEffectContext* Context = FVeloxGameplayEffectContext::Get(Params.EffectContext))
		{
			Context->PatternSeq = PatternSeq;
			Context->HitDirection = CentreDir;
		}
	}

	/**
	 * Task 08 (VeloxShotFired aim_err_cm): distance of the centre ray to the rendered centre of Target, or, without a
	 * target, of the simulated proxy whose rendered centre lies nearest the ray (ahead of the origin). -1 without one.
	 */
	float AimErrorCm(const UWorld& World, const APawn* Shooter, const FVector& Origin, const FVector& Dir, const AActor* Target, FString& OutTargetName, FVector& OutCentre)
	{
		float Best = -1.f;
		OutTargetName = TEXT("none");
		OutCentre = FVector::ZeroVector;
		FVeloxHitboxPose Pose;
		for (TActorIterator<ACharacter> It(&World); It; ++It)
		{
			const ACharacter* Character = *It;
			if (Character == Shooter || (Target && Character != Target) || (!Target && Character->GetLocalRole() != ROLE_SimulatedProxy))
			{
				continue;
			}
			const UVeloxHitboxComponent* Hitbox = UVeloxHitboxComponent::FindHitboxComponent(Character);
			if (!Hitbox)
			{
				continue;
			}
			Hitbox->GetRenderedPose(Pose);
			if (Pose.Shapes.IsEmpty())
			{
				continue;
			}
			const FVector ToCentre = Pose.Shapes[0].Centre - Origin;
			const double Along = FVector::DotProduct(ToCentre, Dir);
			if (Along <= 0.0)
			{
				continue;
			}
			const float Err = static_cast<float>((ToCentre - Dir * Along).Size());
			if (Best < 0.f || Err < Best)
			{
				Best = Err;
				OutTargetName = Character->GetName();
				OutCentre = Pose.Shapes[0].Centre;
			}
		}
		return Best;
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
	FVeloxShotTiming Timing;
	if (const AVeloxPlayerController* VeloxPC = Cast<AVeloxPlayerController>(Controller))
	{
		// Once per shot with the centre ray, also for the shotgun (B14). Server side: now, no hint.
		Timing = VeloxPC->BuildShotTiming(Shot.Origin, Shot.Dir, Definition->RangeCm);
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
	VeloxFireDebug::NoteLocalShot();

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
#if !UE_BUILD_SHIPPING
	{
		// Task 08 / ADR 0003 B17, B24: the client side of the soak's per-shot record (the server logs VeloxShot with the
		// same seq). aim_err_cm against the hinted proxy (else the proxy nearest the ray); view_delay_ms = the
		// InterpolationDelay used, server_est = EstimateServerTime() at fire time (= view + delay).
		FString AimTarget;
		FVector AimCentre;
		const float AimErr = VeloxHitscanPrivate::AimErrorCm(*World, Pawn, Shot.Origin, Shot.Dir, Shot.HintTarget.Get(), AimTarget, AimCentre);
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotFired t=%.3f seq=%u weapon=%s aim_err_cm=%.1f aim_target=%s aim_pos=(%.1f,%.1f,%.1f) view_delay_ms=%.1f delay_src=%s server_est=%.4f view=%.4f hint=%s hint_stamp=%.4f"),
			Now, Shot.ShotSeq, *GetNameSafe(Definition), AimErr, *AimTarget, AimCentre.X, AimCentre.Y, AimCentre.Z, Timing.InterpolationDelay * 1000.f,
			Timing.bDelayFromProxies ? TEXT("proxies") : TEXT("fallback"), Shot.ViewServerTime + Timing.InterpolationDelay, Shot.ViewServerTime,
			*GetNameSafe(Shot.HintTarget.Get()), Shot.HintTargetStamp);
	}
#endif
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
	// Throttled per player (nit 12): the first rejections of each second at Log (task 08 counts them), the rest Verbose.
	UVeloxWeaponInventoryComponent* Inventory = GetInventory();
	const UWorld* World = GetWorld();
	const bool bLog = !Inventory || !World || Inventory->ShouldLogRejectionAtLog(World->GetTimeSeconds());
	const FString Line = FString::Printf(TEXT("VeloxShotRejected seq=%u shooter=%s weapon=%s reason=%s"), ShotSeq, *GetNameSafe(GetShooterPawn()), *GetNameSafe(GetCurrentDefinition()), Reason);
	if (bLog)
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("%s"), *Line);
	}
	else
	{
		UE_LOG(LogVeloxCombat, Verbose, TEXT("%s"), *Line);
	}
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
	if (!Definition || !World)
	{
		Reject(0, TEXT("no_weapon"));
	}
	else
	{
		// Review server step 1 (world-free, Velox.Combat.ShotValidation): bucket + spacing first, then the payload.
		// Nothing touches the subsystem on rejection.
		const double Now = World->GetTimeSeconds();
		FVeloxValidatedShot Valid;
		const EVeloxShotReject Reason = ValidateServerShot(Raw, Now, *Definition, Valid);
		if (Reason != EVeloxShotReject::None)
		{
			Reject(Valid.ShotSeq, LexToString(Reason));
		}
		else
		{
			// B18: a shot of the pending weapon within the switch tolerance completes the switch first.
			FinalisePendingSwitch(*Definition);
			// B25: no self-knockback potential; keeps the owner's confirmed self-knockback sequence near the counter.
			if (const ACharacter* ShooterCharacter = Cast<ACharacter>(GetShooterPawn()))
			{
				if (UVeloxMovementComponent* ShooterMovement = Cast<UVeloxMovementComponent>(ShooterCharacter->GetCharacterMovement()))
				{
					ShooterMovement->ServerNoteShot(Valid.ShotSeq);
				}
			}
			FShotInput Shot;
			Shot.Origin = Valid.Origin;
			Shot.Dir = Valid.Dir;
			Shot.ViewServerTime = Valid.ViewServerTime;
			Shot.HintTarget = Valid.HintTarget;
			Shot.HintTargetStamp = Valid.HintTargetStamp;
			Shot.ShotSeq = Valid.ShotSeq;
			ResolveServerShot(*Definition, Shot);
			AdvanceNextFireTime(Now, Definition->GetSafeFireInterval());
		}
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

	// Review server step 2: the request, pellets from the server's own stream. B21: the seed is the server's own counter
	// (one per accepted shot; a rejected shot never gets here), never the client's ShotSeq, so a client cannot pick its
	// pattern.
	UVeloxWeaponInventoryComponent* Inventory = GetInventory();
	const uint8 PatternSeq = Inventory ? Inventory->AllocateServerPatternSeq() : 0;
	FVeloxShotRequest Request;
	Request.Shooter = Shooter;
	Request.Origin = Shot.Origin;
	Request.RangeCm = Definition.RangeCm;
	Request.bPenetratePawns = Definition.bPenetratesPawns;
	Request.MaxPenetrations = Definition.MaxPenetrations;
	Request.ShotSeq = Shot.ShotSeq;
	VeloxShotDirs::Build(Definition, Shot.Dir, VeloxShotDirs::MakeSeed(PatternSeq, VeloxHitscanPrivate::PlayerIdOf(CurrentActorInfo)), Request.Dirs);
	LastServerPatternSeq = PatternSeq;
	LastServerDirs = Request.Dirs;
	UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxShotPattern seq=%u pattern=%u rays=%d"), Shot.ShotSeq, PatternSeq, Request.Dirs.Num());
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

	// Step 5: ammo, cues. The fire cue runs with the activation's prediction key on a remote shot (the predicting client
	// skips it, having drawn its own); the impact cue is the server's alone (B21).
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
		// Shotgun miss: the centre ray's world end (receivers may rebuild the pellets from PatternSeq, B21).
		FHitResult WorldHit;
		const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VeloxShotCentre), true, Shooter);
		if (World->LineTraceSingleByChannel(WorldHit, Start, End, Velox_TraceChannel_Weapon, QueryParams))
		{
			End = WorldHit.ImpactPoint;
			Normal = WorldHit.ImpactNormal;
		}
	}
	FGameplayCueParameters CueParams = MakeCueParameters(Start, End, Normal, Shot.ShotSeq);
	VeloxHitscanPrivate::WritePattern(CueParams, PatternSeq, Shot.Dir);
	if (Definition.FireCueTag.IsValid())
	{
		ASC->ExecuteGameplayCue(Definition.FireCueTag, CueParams);
	}
	if (Definition.ImpactCueTag.IsValid())
	{
		// The target-data RPC runs inside the client's prediction window (ScopedPredictionKey = the client's key), so
		// without an explicit invalid key the owner would drop this multicast. Unkeyed: the owner gets it like everyone.
		FScopedPredictionWindow UnkeyedWindow(ASC, FPredictionKey(), false);
		ASC->ExecuteGameplayCue(Definition.ImpactCueTag, CueParams);
	}
	if (Definition.bBeam)
	{
		// The inventory owns the keep-alive timer: EndAbility clears every timer of the ability object.
		if (Inventory)
		{
			Inventory->KeepBeamAlive(Definition, CueParams);
		}
	}

	if (Result.Hits.Num() > 0)
	{
		if (AVeloxPlayerController* VeloxPC = Cast<AVeloxPlayerController>(GetShooterController()))
		{
			VeloxPC->ClientConfirmHit(Shot.ShotSeq, static_cast<uint8>(FMath::Clamp(TotalDamage, 0, 255)), LastServerShot.bKill ? VeloxHitConfirmFlags::Kill : VeloxHitConfirmFlags::None);
		}
	}
}

void UVeloxGameplayAbility_FireHitscan::PlayLocalShotCosmetics(const UVeloxWeaponDefinition& Definition, const FShotInput& Shot) const
{
	// B21: the client does not know the server's pattern seed. It draws one tracer along its centre ray to its own trace
	// end and predicts no impact; the server's unkeyed impact cue arrives ~RTT later at the true location (Q3).
	const APawn* Shooter = GetShooterPawn();
	UAbilitySystemComponent* ASC = CurrentActorInfo ? CurrentActorInfo->AbilitySystemComponent.Get() : nullptr;
	UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	if (!ASC || !World || !Definition.FireCueTag.IsValid())
	{
		return;
	}

	// The world, then the rendered capsules of the simulated proxies (what this player sees), never the server subsystem (B10).
	double MaxDist = Definition.RangeCm;
	FVector End = Shot.Origin + Shot.Dir * MaxDist;
	FVector Normal = -Shot.Dir;
	FHitResult WorldHit;
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VeloxShotCosmetic), true, Shooter);
	if (World->LineTraceSingleByChannel(WorldHit, Shot.Origin, End, Velox_TraceChannel_Weapon, QueryParams))
	{
		MaxDist = WorldHit.Distance;
		End = WorldHit.ImpactPoint;
		Normal = WorldHit.ImpactNormal;
	}
	if (!Definition.bPenetratesPawns)
	{
		FVeloxHitboxPose Pose;
		for (TActorIterator<ACharacter> It(World); It; ++It)
		{
			const UVeloxHitboxComponent* Hitbox = (*It != Shooter && It->GetLocalRole() == ROLE_SimulatedProxy) ? UVeloxHitboxComponent::FindHitboxComponent(*It) : nullptr;
			if (!Hitbox)
			{
				continue;
			}
			Hitbox->GetRenderedPose(Pose);
			double Dist;
			int32 Shape;
			if (FVeloxHitMath::RayPose(Shot.Origin, Shot.Dir, MaxDist, Pose, Dist, Shape) && Dist < MaxDist)
			{
				MaxDist = Dist;
				End = Shot.Origin + Shot.Dir * Dist;
				Normal = -Shot.Dir;
			}
		}
	}

	FGameplayCueParameters CueParams = MakeCueParameters(Shot.Origin, End, Normal, Shot.ShotSeq);
	VeloxHitscanPrivate::WritePattern(CueParams, 0, Shot.Dir);
	ASC->ExecuteGameplayCue(Definition.FireCueTag, CueParams);
}
