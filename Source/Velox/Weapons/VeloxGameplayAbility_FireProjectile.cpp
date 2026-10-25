// Velox - projectile fire ability.

#include "Weapons/VeloxGameplayAbility_FireProjectile.h"

#include "AbilitySystemComponent.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Core/VeloxCollisionChannels.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/VeloxPlayerState.h"
#include "Movement/VeloxMovementComponent.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "Weapons/VeloxProjectile.h"
#include "Weapons/VeloxProjectileSim.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#if !UE_BUILD_SHIPPING
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxHealthSet.h"
#include "Containers/Ticker.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreMisc.h"
#include "Weapons/VeloxWeaponTable.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameplayAbility_FireProjectile)

// ---------------------------------------------------------------------------------------------------------------------
// FGameplayAbilityTargetData_VeloxProjectileShot

bool FGameplayAbilityTargetData_VeloxProjectileShot::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayAbilityTargetData_VeloxShot::NetSerialize(Ar, Map, bOutSuccess);
	// Full precision: the server schedules the owner's self splash at FireMoveTimeStamp + flight time, like the client.
	Ar << FireMoveTimeStamp;
	bOutSuccess = bOutSuccess && !Ar.IsError();
	return true;
}

void FGameplayAbilityTargetData_VeloxProjectileShot::Quantize()
{
	constexpr int64 MaxBits = 1024;
	FBitWriter Writer(MaxBits, true);
	bool bOk = true;
	bHasHint = false;
	NetSerialize(Writer, nullptr, bOk);
	FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
	NetSerialize(Reader, nullptr, bOk);
}

FString FGameplayAbilityTargetData_VeloxProjectileShot::ToString() const
{
	return FString::Printf(TEXT("%s fire_ts=%.6f"), *FGameplayAbilityTargetData_VeloxShot::ToString(), FireMoveTimeStamp);
}

// ---------------------------------------------------------------------------------------------------------------------
// UVeloxGameplayAbility_FireProjectile

namespace VeloxFireProjectilePrivate
{
	UVeloxMovementComponent* MovementOf(const APawn* Pawn)
	{
		const AVeloxCharacter* Character = Cast<AVeloxCharacter>(Pawn);
		return Character ? Character->GetVeloxMovementComponent() : nullptr;
	}
}

UVeloxGameplayAbility_FireProjectile::UVeloxGameplayAbility_FireProjectile(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UVeloxGameplayAbility_FireProjectile::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	using namespace VeloxFireProjectilePrivate;

	const UVeloxWeaponDefinition* Definition = GetCurrentDefinition();
	APawn* Pawn = GetShooterPawn();
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!Definition || !Pawn || !ASC || !World || Definition->ProjectileSpeed <= 0.f)
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

	// B6: the eye and the control rotation of this frame (Q3 missiles start at the muzzle = view origin).
	UVeloxWeaponInventoryComponent* Inventory = GetInventory();
	const uint8 ShotSeq = Inventory ? Inventory->AllocateShotSeq() : 0;
	const FVector Eye = Pawn->GetPawnViewLocation();
	const FVector AimDir = Controller->GetControlRotation().Vector();
	const double Now = World->GetTimeSeconds();

	if (ActorInfo->IsNetAuthority())
	{
		// Listen host, bots, standalone: no RPC, no prediction; the owner's splash resolves at the explosion.
		LaunchServerShot(*Definition, Eye, AimDir.GetSafeNormal(), ShotSeq, false, 0.f);
		AdvanceNextFireTime(Now, Definition->GetSafeFireInterval());
		EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
		return;
	}

	// Predicting client. The payload as the server will read it, so both ends fly the same quantized shot.
	FGameplayAbilityTargetData_VeloxProjectileShot* Data = new FGameplayAbilityTargetData_VeloxProjectileShot();
	Data->Origin = Eye;
	Data->Dir = AimDir;
	// Projectiles are not rewound (Q3); the view time only names the shot in logs.
	const AGameStateBase* GameState = World->GetGameState();
	Data->ViewServerTime = static_cast<float>(GameState ? GameState->GetServerWorldTimeSeconds() : Now);
	Data->ShotSeq = ShotSeq;
	UVeloxMovementComponent* Movement = MovementOf(Pawn);
	Data->FireMoveTimeStamp = Movement ? Movement->GetClientMoveTimeStamp() : 0.f;
	Data->Quantize();
	const FVector Origin = Data->Origin;
	const FVector Dir = FVector(Data->Dir).GetSafeNormal();
	const float FireMoveTimeStamp = Data->FireMoveTimeStamp;

	FVeloxProjectileLaunch Launch;
	Launch.Definition = Definition;
	Launch.Shooter = Pawn;
	Launch.SourceASC = ASC;
	Launch.Origin = Origin;
	Launch.Dir = Dir;
	Launch.ShotSeq = ShotSeq;
	if (Movement && Definition->SplashRadiusCm > 0.f)
	{
		// The owner's self splash at the move where the server will evaluate it (same flight, same move time stamp).
		FVector SplashOrigin;
		const double Flight = AVeloxProjectile::ComputeWorldFlight(*World, *Definition, Origin, Dir, Pawn, SplashOrigin);
		const float EventTime = FireMoveTimeStamp + static_cast<float>(Flight);
		Launch.OwnerMovement = Movement;
		Launch.SelfSplashOrigin = SplashOrigin;
		Launch.SelfSplashEventId = AVeloxProjectile::ScheduleOwnerSelfSplash(*Movement, *Definition, ASC, EventTime, SplashOrigin, ShotSeq, true);
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxProjectileFired seq=%u weapon=%s fire_ts=%.6f flight_ms=%.1f event_ts=%.6f splash_origin=%s"), ShotSeq, *GetNameSafe(Definition),
			FireMoveTimeStamp, Flight * 1000.0, EventTime, *SplashOrigin.ToCompactString());
	}
	AVeloxProjectile::SpawnFakeProjectile(*World, Definition->ProjectileClass, Launch);

	const FGameplayAbilityTargetDataHandle DataHandle(Data);
	ASC->CallServerSetReplicatedTargetData(Handle, ActivationInfo.GetActivationPredictionKey(), DataHandle, FGameplayTag(), ASC->ScopedPredictionKey);

	ApplyAmmoCost(*Definition);
	if (Definition->FireCueTag.IsValid())
	{
		ASC->ExecuteGameplayCue(Definition->FireCueTag, MakeCueParameters(Origin, Origin, -Dir, ShotSeq));
	}
	AdvanceNextFireTime(Now, Definition->GetSafeFireInterval());
	// The server ends its own instance after the target data; no end RPC per shot (as the hitscan ability).
	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}

void UVeloxGameplayAbility_FireProjectile::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
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

void UVeloxGameplayAbility_FireProjectile::Reject(uint8 ShotSeq, const TCHAR* Reason)
{
	LastServerShot = FVeloxServerShotInfo();
	LastServerShot.ShotSeq = ShotSeq;
	LastServerShot.bRejected = true;
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotRejected seq=%u shooter=%s weapon=%s reason=%s"), ShotSeq, *GetNameSafe(GetShooterPawn()), *GetNameSafe(GetCurrentDefinition()), Reason);
}

void UVeloxGameplayAbility_FireProjectile::OnServerTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag)
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
	const FGameplayAbilityTargetData_VeloxProjectileShot* ShotData = (Raw && Raw->GetScriptStruct() == FGameplayAbilityTargetData_VeloxProjectileShot::StaticStruct())
		? static_cast<const FGameplayAbilityTargetData_VeloxProjectileShot*>(Raw) : nullptr;
	const uint8 Seq = ShotData ? ShotData->ShotSeq : 0;
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const FVector Dir = ShotData ? FVector(ShotData->Dir) : FVector::ZeroVector;

	// The hitscan ability's order: the bucket first, then the payload.
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
	else if (Dir.ContainsNaN() || Dir.IsNearlyZero())
	{
		Reject(Seq, TEXT("dir"));
	}
	else if (!FMath::IsFinite(ShotData->FireMoveTimeStamp))
	{
		Reject(Seq, TEXT("time"));
	}
	else
	{
		LaunchServerShot(*Definition, ShotData->Origin, Dir.GetSafeNormal(), Seq, true, ShotData->FireMoveTimeStamp);
		AdvanceNextFireTime(Now, Definition->GetSafeFireInterval());
	}
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
}

void UVeloxGameplayAbility_FireProjectile::LaunchServerShot(const UVeloxWeaponDefinition& Definition, const FVector& ClientOrigin, const FVector& Dir, uint8 ShotSeq, bool bRemote, float ClientFireMoveTimeStamp)
{
	using namespace VeloxFireProjectilePrivate;

	APawn* Shooter = GetShooterPawn();
	UAbilitySystemComponent* ASC = CurrentActorInfo ? CurrentActorInfo->AbilitySystemComponent.Get() : nullptr;
	UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	const AVeloxPlayerState* PlayerState = CurrentActorInfo ? Cast<AVeloxPlayerState>(CurrentActorInfo->OwnerActor.Get()) : nullptr;
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PlayerState ? PlayerState->GetCombatSettings() : nullptr);
	LastServerShot = FVeloxServerShotInfo();
	LastServerShot.ShotSeq = ShotSeq;
	if (!ASC || !World)
	{
		LastServerShot.bRejected = true;
		return;
	}

	// Origin validation, the rules of TraceShot (ADR 0003 section 5, B15): snap to the server eye when the client origin
	// is not finite, farther than MaxOriginErrorCm, or behind world geometry seen from the eye.
	FVector Origin = ClientOrigin;
	const FVector Eye = Shooter->GetPawnViewLocation();
	const TCHAR* SnapReason = nullptr;
	if (Origin.ContainsNaN())
	{
		SnapReason = TEXT("nan");
	}
	else if (FVector::Dist(Origin, Eye) > Settings.MaxOriginErrorCm)
	{
		SnapReason = TEXT("distance");
	}
	else if (FVector::DistSquared(Origin, Eye) > 1.0)
	{
		const FCollisionQueryParams OccParams(SCENE_QUERY_STAT(VeloxProjectileOrigin), false, Shooter);
		if (World->LineTraceTestByChannel(Eye, Origin, Velox_TraceChannel_Weapon, OccParams))
		{
			SnapReason = TEXT("occluded");
		}
	}
	if (SnapReason)
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotOriginSnap seq=%u shooter=%s err_cm=%.1f reason=%s"), ShotSeq, *GetNameSafe(Shooter), Origin.ContainsNaN() ? -1.0 : FVector::Dist(Origin, Eye), SnapReason);
		Origin = Eye;
	}

	FVeloxProjectileLaunch Launch;
	Launch.Definition = &Definition;
	Launch.Shooter = Shooter;
	Launch.SourceASC = ASC;
	Launch.Origin = Origin;
	Launch.Dir = Dir;
	Launch.ShotSeq = ShotSeq;

	UVeloxMovementComponent* Movement = MovementOf(Shooter);
	float FireMoveTimeStamp = 0.f;
	if (bRemote)
	{
		// ADR 0003 section 7: forward simulation by half the ping, capped.
		Launch.ForwardSimSeconds = FVeloxProjectileSim::ForwardSimSeconds(PlayerState ? PlayerState->GetPingInMilliseconds() : 0.f, Settings.ProjectileForwardPredictionMax);
		if (Movement && Definition.SplashRadiusCm > 0.f)
		{
			// The owner's self splash in its own move stream (what the predicting client does), never in server time.
			// The client's stamp is trusted within MaxRewindSeconds of the last move processed (a later one would only
			// delay its own self damage; the explosion's deadline bounds that too).
			const float LastMoveTimeStamp = Movement->GetServerClientMoveTimeStamp();
			FireMoveTimeStamp = FMath::Clamp(ClientFireMoveTimeStamp, LastMoveTimeStamp - Settings.MaxRewindSeconds, LastMoveTimeStamp + Settings.MaxRewindSeconds);
			FVector SplashOrigin;
			const double Flight = AVeloxProjectile::ComputeWorldFlight(*World, Definition, Origin, Dir, Shooter, SplashOrigin);
			const float EventTime = FireMoveTimeStamp + static_cast<float>(Flight);
			Launch.OwnerMovement = Movement;
			Launch.SelfSplashOrigin = SplashOrigin;
			Launch.SelfSplashEventId = AVeloxProjectile::ScheduleOwnerSelfSplash(*Movement, Definition, ASC, EventTime, SplashOrigin, ShotSeq, false);
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxProjectileShot seq=%u weapon=%s fire_ts=%.6f client_ts=%.6f last_ts=%.6f flight_ms=%.1f event_ts=%.6f splash_origin=%s"), ShotSeq,
				*GetNameSafe(&Definition), FireMoveTimeStamp, ClientFireMoveTimeStamp, LastMoveTimeStamp, Flight * 1000.0, EventTime, *SplashOrigin.ToCompactString());
		}
	}
	LastServerProjectile = AVeloxProjectile::SpawnServerProjectile(*World, Definition.ProjectileClass, Launch);
	if (!LastServerProjectile.IsValid() && !Definition.ProjectileClass)
	{
		UE_LOG(LogVeloxCombat, Warning, TEXT("VeloxProjectileShot seq=%u: %s has no ProjectileClass (base class used)"), ShotSeq, *GetNameSafe(&Definition));
	}

	ApplyAmmoCost(Definition);
	if (Definition.FireCueTag.IsValid())
	{
		// Inside the activation's prediction window on a remote shot: the predicting client skips the echo.
		ASC->ExecuteGameplayCue(Definition.FireCueTag, MakeCueParameters(Origin, Origin, -Dir, ShotSeq));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// velox.combat.RocketJumpTest (non-shipping): the offline / online rocket-jump check of task 05.

#if !UE_BUILD_SHIPPING
namespace VeloxRocketJumpTest
{
	/** Spawn protection on L_CombatTest lasts 2 s; the test waits past it on the ground before firing. */
	constexpr double SettleSeconds = 3.0;
	/** The switch to RL (refire wait + drop + raise) is well inside this. */
	constexpr double SwitchTimeoutSeconds = 2.0;
	constexpr double AimSettleSeconds = 0.2;
	constexpr double JumpHoldSeconds = 0.1;
	constexpr double MaxFlightSeconds = 3.0;
	constexpr double AfterLandingSeconds = 0.5;
	constexpr int32 RocketLauncherSlot = 5;

	enum class EPhase : uint8 { WaitPawn, Select, Aim, Fired, Done };

	struct FRun
	{
		EPhase Phase = EPhase::WaitPawn;
		double PhaseStart = -1.0;
		double FireTime = 0.0;
		double LandTime = -1.0;
		double StartZ = 0.0;
		double MaxZ = 0.0;
		float HealthBefore = 0.f;
		/** Correction count at fire time; corrections after it are counted and their maximum kept. */
		int32 SeenCorrections = 0;
		int32 WindowCorrections = 0;
		float WindowMaxCm = 0.f;
		float PitchDeg = -90.f;
		bool bQuit = false;
		bool bFireReleased = false;
		bool bJumpReleased = false;
		bool bLeftGround = false;
	};

	UWorld* FindGameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && World && World->HasBegunPlay() && !Context.PendingNetGame && Context.TravelURL.IsEmpty())
			{
				if (World->GetFirstPlayerController() && World->GetFirstPlayerController()->IsLocalController())
				{
					return World;
				}
			}
		}
		return nullptr;
	}

	void Finish(FRun& Run, bool bPass, const FString& Line)
	{
		UE_LOG(LogVeloxCombat, Display, TEXT("VeloxRocketJump: %s result=%s"), *Line, bPass ? TEXT("PASS") : TEXT("FAIL"));
		Run.Phase = EPhase::Done;
		if (Run.bQuit)
		{
			FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
		}
	}

	/** One ticker step; false when finished. */
	bool Step(FRun& Run)
	{
		UWorld* World = FindGameWorld();
		if (!World)
		{
			return Run.Phase != EPhase::Done;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		AVeloxCharacter* Pawn = PC ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
		AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
		UVeloxWeaponInventoryComponent* Inventory = PS ? PS->GetWeaponInventory() : nullptr;
		UVeloxAbilitySystemComponent* ASC = PS ? PS->GetVeloxAbilitySystemComponent() : nullptr;
		UVeloxMovementComponent* Movement = Pawn ? Pawn->GetVeloxMovementComponent() : nullptr;
		const UVeloxWeaponTable* Table = Inventory ? Inventory->GetWeaponTable() : nullptr;
		const int32 RL = Table ? Table->FindBySlot(RocketLauncherSlot) : INDEX_NONE;
		const double Now = World->GetTimeSeconds();
		if (!Pawn || !Inventory || !ASC || !Movement || RL == INDEX_NONE)
		{
			Run.PhaseStart = -1.0;
			return true;
		}
		if (Run.PhaseStart < 0.0)
		{
			Run.PhaseStart = Now;
		}
		const double InPhase = Now - Run.PhaseStart;
		auto Enter = [&Run, Now](EPhase Phase) { Run.Phase = Phase; Run.PhaseStart = Now; };

		switch (Run.Phase)
		{
		case EPhase::WaitPawn:
			if (Inventory->OwnsIndex(RL) && Inventory->FindFireSpec(RL) && Movement->IsOnGround() && InPhase >= SettleSeconds)
			{
				if (Inventory->GetCurrentIndex() != RL)
				{
					const FGameplayTag SlotTag = FGameplayTag::RequestGameplayTag(*FString::Printf(TEXT("InputTag.Weapon.Slot%d"), RocketLauncherSlot));
					ASC->AbilityInputTagPressed(SlotTag);
					ASC->AbilityInputTagReleased(SlotTag);
				}
				Enter(EPhase::Select);
			}
			return true;
		case EPhase::Select:
			if (Inventory->GetCurrentIndex() == RL && !ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_WeaponSwitching))
			{
				FRotator Rotation = PC->GetControlRotation();
				Rotation.Pitch = Run.PitchDeg;
				PC->SetControlRotation(Rotation);
				Enter(EPhase::Aim);
			}
			else if (InPhase > SwitchTimeoutSeconds)
			{
				Finish(Run, false, TEXT("reason=no_rocket_launcher"));
				return false;
			}
			return true;
		case EPhase::Aim:
		{
			FRotator Rotation = PC->GetControlRotation();
			Rotation.Pitch = Run.PitchDeg;
			PC->SetControlRotation(Rotation);
			if (InPhase < AimSettleSeconds || !Movement->IsOnGround())
			{
				return true;
			}
			// Fire and jump in the same frame (the ticker runs before the world tick: the ability fires in this frame's
			// PlayerTick, the jump is in this frame's move).
			Run.FireTime = Now;
			Run.StartZ = Pawn->GetActorLocation().Z;
			Run.MaxZ = Run.StartZ;
			Run.HealthBefore = PS->GetHealthSet() ? PS->GetHealthSet()->GetHealth() : 0.f;
			Run.SeenCorrections = Movement->GetNumCorrections();
			Movement->SetJumpHeld(true);
			Pawn->Jump();
			ASC->AbilityInputTagPressed(VeloxGameplayTags::InputTag_Fire);
			UE_LOG(LogVeloxCombat, Display, TEXT("VeloxRocketJump: fire+jump t=%.3f z=%.1f health=%.0f pitch=%.1f"), Now, Run.StartZ, Run.HealthBefore, PC->GetControlRotation().Pitch);
			Enter(EPhase::Fired);
			return true;
		}
		case EPhase::Fired:
		{
			if (!Run.bFireReleased)
			{
				ASC->AbilityInputTagReleased(VeloxGameplayTags::InputTag_Fire);
				Run.bFireReleased = true;
			}
			if (!Run.bJumpReleased && InPhase >= JumpHoldSeconds)
			{
				Movement->SetJumpHeld(false);
				Pawn->StopJumping();
				Run.bJumpReleased = true;
			}
			Run.MaxZ = FMath::Max(Run.MaxZ, Pawn->GetActorLocation().Z);
			if (Movement->GetNumCorrections() != Run.SeenCorrections)
			{
				// One per frame at most is seen here; the last one's size stands for the frame (echoes are 0 cm, A24).
				Run.WindowCorrections += Movement->GetNumCorrections() - Run.SeenCorrections;
				Run.SeenCorrections = Movement->GetNumCorrections();
				Run.WindowMaxCm = FMath::Max(Run.WindowMaxCm, Movement->GetLastCorrectionCm());
			}
			Run.bLeftGround |= !Movement->IsOnGround();
			if (Run.bLeftGround && Movement->IsOnGround() && Run.LandTime < 0.0)
			{
				Run.LandTime = Now;
			}
			const bool bLandedLongEnough = Run.LandTime >= 0.0 && Now - Run.LandTime >= AfterLandingSeconds;
			if (bLandedLongEnough || InPhase > MaxFlightSeconds)
			{
				const FVeloxMoveParams& P = Movement->GetSimParams();
				const double JumpHeight = P.GravityZ < 0.f ? FMath::Square(P.JumpVelocity) / (-2.0 * P.GravityZ) : 0.0;
				const double Apex = Run.MaxZ - Run.StartZ;
				const float HealthAfter = PS->GetHealthSet() ? PS->GetHealthSet()->GetHealth() : 0.f;
				const bool bPass = JumpHeight > 0.0 && Apex >= 2.0 * JumpHeight;
				Finish(Run, bPass, FString::Printf(TEXT("apex_cm=%.1f jump_height_cm=%.1f ratio=%.2f health=%.0f->%.0f self_damage=%.0f corrections=%d max_cm=%.2f net=%s"),
					Apex, JumpHeight, JumpHeight > 0.0 ? Apex / JumpHeight : 0.0, Run.HealthBefore, HealthAfter, Run.HealthBefore - HealthAfter, Run.WindowCorrections,
					Run.WindowMaxCm, World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("authority")));
				return false;
			}
			return true;
		}
		case EPhase::Done:
		default:
			return false;
		}
	}

	void Command(const TArray<FString>& Args)
	{
		TSharedRef<FRun> Run = MakeShared<FRun>();
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				Run->bQuit = true;
			}
			else if (Arg.IsNumeric())
			{
				Run->PitchDeg = FCString::Atof(*Arg);
			}
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float) { return Step(*Run); }));
	}

	FAutoConsoleCommand RocketJumpTestCmd(
		TEXT("velox.combat.RocketJumpTest"),
		TEXT("velox.combat.RocketJumpTest [PitchDeg=-90] [quit]: waits for the local pawn to own the rocket launcher (velox.combat.GiveAllWeapons on the server), selects it, looks down, fires and jumps in the same frame and logs VeloxRocketJump: apex, self damage, corrections, PASS if apex >= 2 x jump height. Non-shipping."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Command));
}
#endif // !UE_BUILD_SHIPPING
