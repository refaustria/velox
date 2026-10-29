// Velox - powerup effects, regeneration, grant/extend.

#include "Items/VeloxPowerupEffects.h"

#include "AbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxDamageMath.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "GameModes/VeloxPlayerState.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxPowerupEffects)

namespace VeloxPowerupTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Powerup, "GameplayCue.Powerup", "Looping powerup cues (ADR 0004 section 3).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Powerup_Quad, "GameplayCue.Powerup.Quad", "Quad Damage active (blue body tint).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Powerup_Haste, "GameplayCue.Powerup.Haste", "Haste active (yellow body tint).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Powerup_Regen, "GameplayCue.Powerup.Regen", "Regeneration active (red body tint).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Powerup_BattleSuit, "GameplayCue.Powerup.BattleSuit", "Battle Suit active (green body tint).");

	FGameplayTag PowerupTagForCue(const FGameplayTag& CueTag)
	{
		if (CueTag == GameplayCue_Powerup_Quad) { return VeloxGameplayTags::Powerup_Quad; }
		if (CueTag == GameplayCue_Powerup_Haste) { return VeloxGameplayTags::Powerup_Haste; }
		if (CueTag == GameplayCue_Powerup_Regen) { return VeloxGameplayTags::Powerup_Regen; }
		if (CueTag == GameplayCue_Powerup_BattleSuit) { return VeloxGameplayTags::Powerup_BattleSuit; }
		return FGameplayTag();
	}
}

namespace VeloxPowerupEffectsPrivate
{
	const UVeloxCombatSettings& PowerupSettingsFor(const UAbilitySystemComponent* ASC)
	{
		const AVeloxPlayerState* PlayerState = ASC ? Cast<AVeloxPlayerState>(ASC->GetOwnerActor()) : nullptr;
		return UVeloxCombatSettings::GetOrDefault(PlayerState ? PlayerState->GetCombatSettings() : nullptr);
	}

	struct FRegenStatics
	{
		FGameplayEffectAttributeCaptureDefinition HealthDef;
		FGameplayEffectAttributeCaptureDefinition MaxHealthDef;

		FRegenStatics()
		{
			HealthDef = FGameplayEffectAttributeCaptureDefinition(UVeloxHealthSet::GetHealthAttribute(), EGameplayEffectAttributeCaptureSource::Target, false);
			MaxHealthDef = FGameplayEffectAttributeCaptureDefinition(UVeloxHealthSet::GetMaxHealthAttribute(), EGameplayEffectAttributeCaptureSource::Target, false);
		}
	};

	const FRegenStatics& RegenStatics()
	{
		static FRegenStatics Statics;
		return Statics;
	}

	FGameplayEffectQuery PowerupQuery(TSubclassOf<UGameplayEffect> EffectClass)
	{
		FGameplayEffectQuery Query;
		Query.EffectDefinition = EffectClass;
		return Query;
	}
}

// ------------------------------------------------------------------------------------------------------------- Effects

UVeloxGE_Powerup::UVeloxGE_Powerup()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FSetByCallerFloat Duration;
	Duration.DataTag = VeloxGameplayTags::SetByCaller_Duration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(Duration);
	// No stacking (the class default): a second pickup extends the active effect in place (GrantOrExtend).
}

void UVeloxGE_Powerup::InitPowerup(const FGameplayTag& InPowerupTag, const FGameplayTag& CueTag)
{
	PowerupTag = InPowerupTag;
	// Native GE components must be default subobjects listed in GEComponents (UGameplayEffect::PostInitProperties).
	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GrantPowerup"));
	GEComponents.Add(TargetTags);
	FInheritedTagContainer Granted;
	Granted.AddTag(InPowerupTag);
	TargetTags->SetAndApplyTargetTagChanges(Granted);
	// Looping cue: OnActive/WhileActive on application, Removed with the effect; Mixed replication sends it to every client.
	GameplayCues.Add(FGameplayEffectCue(CueTag, 0.f, 0.f));
}

UVeloxGE_Powerup_Quad::UVeloxGE_Powerup_Quad()
{
	InitPowerup(VeloxGameplayTags::Powerup_Quad, VeloxPowerupTags::GameplayCue_Powerup_Quad);
}

UVeloxGE_Powerup_Haste::UVeloxGE_Powerup_Haste()
{
	InitPowerup(VeloxGameplayTags::Powerup_Haste, VeloxPowerupTags::GameplayCue_Powerup_Haste);
}

UVeloxGE_Powerup_Regen::UVeloxGE_Powerup_Regen()
{
	InitPowerup(VeloxGameplayTags::Powerup_Regen, VeloxPowerupTags::GameplayCue_Powerup_Regen);
	// Q3 ClientTimerActions: once per second. A rule, not a tunable (like GE_HealthDecay).
	Period = FScalableFloat(1.0f);
	bExecutePeriodicEffectOnApplication = false;
	FGameplayEffectExecutionDefinition& Execution = Executions.AddDefaulted_GetRef();
	Execution.CalculationClass = UVeloxRegenExecution::StaticClass();
}

UVeloxGE_Powerup_BattleSuit::UVeloxGE_Powerup_BattleSuit()
{
	InitPowerup(VeloxGameplayTags::Powerup_BattleSuit, VeloxPowerupTags::GameplayCue_Powerup_BattleSuit);
}

// -------------------------------------------------------------------------------------------------------- Regeneration

UVeloxRegenExecution::UVeloxRegenExecution()
{
	using namespace VeloxPowerupEffectsPrivate;
	RelevantAttributesToCapture.Add(RegenStatics().HealthDef);
	RelevantAttributesToCapture.Add(RegenStatics().MaxHealthDef);
}

void UVeloxRegenExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	using namespace VeloxPowerupEffectsPrivate;

	const UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
	if (!TargetASC || TargetASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead))
	{
		return;
	}
	const FGameplayTag MatchFrozenTag = VeloxEffects::GetMatchFrozenTag();
	if (MatchFrozenTag.IsValid() && TargetASC->HasMatchingGameplayTag(MatchFrozenTag))
	{
		return;
	}

	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	FAggregatorEvaluateParameters EvalParams;
	EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();
	float Health = 0.f;
	float MaxHealth = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(RegenStatics().HealthDef, EvalParams, Health);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(RegenStatics().MaxHealthDef, EvalParams, MaxHealth);
	if (Health <= 0.f)
	{
		return;
	}

	const UVeloxCombatSettings& Settings = PowerupSettingsFor(TargetASC);
	const int32 Before = FMath::RoundToInt32(Health);
	const int32 After = FVeloxDamageMath::RegenStep(Before, FMath::RoundToInt32(MaxHealth), FMath::RoundToInt32(Settings.HealthHardCap), Settings.BuildRegenParams());
	if (After != Before)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UVeloxHealthSet::GetHealthAttribute(), EGameplayModOp::Additive, static_cast<float>(After - Before)));
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxRegen target=%s health %d->%d (max %.0f)"), *GetNameSafe(TargetASC->GetOwnerActor()), Before, After, MaxHealth);
}

// --------------------------------------------------------------------------------------------------------------- Grant

namespace VeloxPowerups
{
	float GetRemainingSeconds(const UAbilitySystemComponent& ASC, TSubclassOf<UGameplayEffect> EffectClass)
	{
		float Remaining = 0.f;
		if (EffectClass)
		{
			for (const float Seconds : ASC.GetActiveEffectsTimeRemaining(VeloxPowerupEffectsPrivate::PowerupQuery(EffectClass)))
			{
				Remaining = FMath::Max(Remaining, Seconds);
			}
		}
		return Remaining;
	}

	EVeloxPowerupGrant GrantOrExtend(UAbilitySystemComponent& ASC, TSubclassOf<UVeloxGE_Powerup> EffectClass, float TotalSeconds)
	{
		using namespace VeloxPowerupEffectsPrivate;

		if (!EffectClass || TotalSeconds <= 0.f || !ASC.IsOwnerActorAuthoritative())
		{
			return EVeloxPowerupGrant::Failed;
		}
		// Tolerance of the extension check: float world times around one hour of uptime.
		constexpr float ExtendToleranceSeconds = 0.01f;

		const FGameplayEffectQuery Query = PowerupQuery(EffectClass);
		const TArray<FActiveGameplayEffectHandle> Active = ASC.GetActiveEffects(Query);
		EVeloxPowerupGrant Result = EVeloxPowerupGrant::Applied;
		if (Active.Num() > 0)
		{
			// Extend the longest one in place; with no stacking there is exactly one.
			FActiveGameplayEffectHandle Handle = Active[0];
			float Remaining = -1.f;
			for (const FActiveGameplayEffectHandle& Candidate : Active)
			{
				const FActiveGameplayEffect* Effect = ASC.GetActiveGameplayEffect(Candidate);
				const float Seconds = Effect ? Effect->GetTimeRemaining(ASC.GetWorld() ? ASC.GetWorld()->GetTimeSeconds() : 0.f) : -1.f;
				if (Seconds > Remaining)
				{
					Remaining = Seconds;
					Handle = Candidate;
				}
			}
			const float Diff = TotalSeconds - FMath::Max(Remaining, 0.f);
			ASC.ModifyActiveEffectStartTime(Handle, Diff);
			if (FMath::IsNearlyEqual(GetRemainingSeconds(ASC, EffectClass), TotalSeconds, ExtendToleranceSeconds))
			{
				UE_LOG(LogVeloxCombat, Log, TEXT("VeloxPowerup grant=extend effect=%s owner=%s remaining %.2f -> %.2f"), *GetNameSafe(EffectClass.Get()),
					*GetNameSafe(ASC.GetOwnerActor()), Remaining, TotalSeconds);
				return EVeloxPowerupGrant::Extended;
			}
			// ADR 0004 section 3 fallback: remove and re-apply with the summed duration (cue restarts, no harm).
			ASC.RemoveActiveEffects(Query);
			Result = EVeloxPowerupGrant::Reapplied;
		}

		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDurationSpec(&ASC, EffectClass, TotalSeconds);
		if (!Spec.IsValid() || !ASC.ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).IsValid())
		{
			return EVeloxPowerupGrant::Failed;
		}
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxPowerup grant=%s effect=%s owner=%s remaining %.2f"), Result == EVeloxPowerupGrant::Reapplied ? TEXT("reapply") : TEXT("apply"),
			*GetNameSafe(EffectClass.Get()), *GetNameSafe(ASC.GetOwnerActor()), TotalSeconds);
		return Result;
	}

	bool HasQuad(const UAbilitySystemComponent* ShooterASC)
	{
		return ShooterASC && ShooterASC->HasMatchingGameplayTag(VeloxGameplayTags::Powerup_Quad);
	}

	float GetOutgoingDamageScale(const UAbilitySystemComponent* ShooterASC, const UVeloxCombatSettings& Settings)
	{
		return HasQuad(ShooterASC) ? Settings.QuadDamageFactor : 1.f;
	}
}

// ------------------------------------------------------------------------------------------------------------- Cheats

#if !UE_BUILD_SHIPPING
#include "AbilitySystemGlobals.h"
#include "Character/VeloxCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxPickup.h"
#include "TimerManager.h"

namespace VeloxPowerupCheats
{
	/** velox.items.PowerupTest: MG on a dummy without Quad, Quad pickup under the player, MG again (x3), Regen 50 -> 95. */
	struct FPowerupTestState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AVeloxCharacter> Player;
		TWeakObjectPtr<AVeloxCharacter> Dummy;
		TWeakObjectPtr<AVeloxPickup> Pickup;
		int32 Step = 0;
		int32 Polls = 0;
		int32 Shots = 0;
		float DummyBefore = 0.f;
		int32 PlainDelta = -1;
		int32 QuadDelta = -1;
		float RegenStart = 0.f;
		bool bQuit = false;
		bool bPassed = true;
	};

	constexpr float PollSeconds = 0.25f;
	constexpr int32 MaxShotsPerPhase = 8;
	constexpr int32 MaxPlayerPolls = 200;
	constexpr int32 MaxQuadPolls = 4 * 75; // initial delay 30-60 s plus margin
	constexpr float DummyDistanceCm = 400.f;
	constexpr float RegenStartHealth = 50.f;
	constexpr float RegenCheckSeconds = 3.2f;

	UWorld* FindAuthorityWorld(UWorld* InWorld)
	{
		if (InWorld && InWorld->GetNetMode() != NM_Client)
		{
			return InWorld;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* W = Context.World();
			if (W && W->IsGameWorld() && W->GetNetMode() != NM_Client)
			{
				return W;
			}
		}
		return nullptr;
	}

	UAbilitySystemComponent* AscOf(AActor* Actor)
	{
		return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
	}

	float HealthOf(AActor* Actor)
	{
		const UAbilitySystemComponent* ASC = AscOf(Actor);
		return ASC ? ASC->GetNumericAttribute(UVeloxHealthSet::GetHealthAttribute()) : -1.f;
	}

	void Finish(const TSharedRef<FPowerupTestState>& State, const TCHAR* Detail)
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxPowerupTest result=%s plain_delta=%d quad_delta=%d detail=%s"), State->bPassed ? TEXT("PASS") : TEXT("FAIL"), State->PlainDelta,
			State->QuadDelta, Detail);
		if (State->bQuit && GEngine)
		{
			GEngine->DeferredCommands.Add(TEXT("quit"));
		}
	}

	void Schedule(const TSharedRef<FPowerupTestState>& State, float Seconds);

	/** Fires one MG shot at the dummy (dummy health pinned at 100 first: no decay, no armor). */
	void FireAtDummy(const TSharedRef<FPowerupTestState>& State)
	{
		if (UAbilitySystemComponent* DummyASC = AscOf(State->Dummy.Get()))
		{
			DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), 0.f);
			DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), 100.f);
		}
		State->DummyBefore = HealthOf(State->Dummy.Get());
		++State->Shots;
		GEngine->Exec(State->World.Get(), TEXT("velox.combat.FireTest MG -1"));
	}

	void Step(TSharedRef<FPowerupTestState> State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return;
		}
		AVeloxCharacter* Player = State->Player.Get();
		switch (State->Step)
		{
		case 0: // wait for the local pawn, spawn the dummy ahead
		{
			for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
			{
				if (It->IsPlayerControlled() && !It->IsCorpse())
				{
					State->Player = *It;
				}
			}
			Player = State->Player.Get();
			if (!Player || !AscOf(Player))
			{
				if (++State->Polls > MaxPlayerPolls)
				{
					State->bPassed = false;
					Finish(State, TEXT("no_player"));
					return;
				}
				break;
			}
			GEngine->Exec(World, TEXT("velox.combat.GiveAllWeapons"));
			const FVector At = Player->GetActorLocation() + Player->GetActorForwardVector() * DummyDistanceCm;
			GEngine->Exec(World, *FString::Printf(TEXT("velox.combat.SpawnDummy %.0f %.0f %.0f"), At.X, At.Y, At.Z));
			State->Step = 1;
			Schedule(State, 1.f);
			return;
		}
		case 1: // find the dummy, first (plain) shot
			for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
			{
				if (*It != Player && !It->IsPlayerControlled() && !It->IsCorpse())
				{
					State->Dummy = *It;
				}
			}
			if (!State->Dummy.IsValid())
			{
				State->bPassed = false;
				Finish(State, TEXT("no_dummy"));
				return;
			}
			FireAtDummy(State);
			State->Step = 2;
			Schedule(State, 0.5f);
			return;
		case 2: // plain result (retry on a miss), then the Quad pickup under the player
		{
			const int32 Delta = FMath::RoundToInt32(State->DummyBefore - HealthOf(State->Dummy.Get()));
			if (Delta == 0 && State->Shots < MaxShotsPerPhase)
			{
				FireAtDummy(State);
				Schedule(State, 0.5f);
				return;
			}
			State->PlainDelta = Delta;
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxPowerupTest phase=plain dummy %.0f -> %.0f delta=%d"), State->DummyBefore, HealthOf(State->Dummy.Get()), Delta);
			const UVeloxItemDefinition* QuadItem = AVeloxPickup::LoadDefinitionByName(TEXT("Powerup.Quad"));
			const FVector Feet = Player->GetActorLocation() - FVector(0, 0, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			State->Pickup = QuadItem ? AVeloxPickup::SpawnPickup(World, QuadItem, Feet) : nullptr;
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxPowerupTest phase=quad_pickup spawned=%s available_in=%.1f"), *GetNameSafe(State->Pickup.Get()),
				State->Pickup.IsValid() ? State->Pickup->GetTimeUntilRespawn() : -1.0);
			State->Polls = 0;
			State->Shots = 0;
			State->Step = 3;
			break;
		}
		case 3: // wait for the Quad pickup to be taken, then fire again
			if (!VeloxPowerups::HasQuad(AscOf(Player)))
			{
				if (++State->Polls > MaxQuadPolls)
				{
					State->bPassed = false;
					Finish(State, TEXT("quad_not_taken"));
					return;
				}
				break;
			}
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxPowerupTest phase=quad_taken remaining=%.1f"), VeloxPowerups::GetRemainingSeconds(*AscOf(Player), UVeloxGE_Powerup_Quad::StaticClass()));
			FireAtDummy(State);
			State->Step = 4;
			Schedule(State, 0.5f);
			return;
		case 4: // quad result, then Regen on the player at 50 health
		{
			const int32 Delta = FMath::RoundToInt32(State->DummyBefore - HealthOf(State->Dummy.Get()));
			if (Delta == 0 && State->Shots < MaxShotsPerPhase)
			{
				FireAtDummy(State);
				Schedule(State, 0.5f);
				return;
			}
			State->QuadDelta = Delta;
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxPowerupTest phase=quad dummy %.0f -> %.0f delta=%d"), State->DummyBefore, HealthOf(State->Dummy.Get()), Delta);
			const UVeloxCombatSettings& Settings = VeloxPowerupEffectsPrivate::PowerupSettingsFor(AscOf(Player));
			State->bPassed &= State->PlainDelta > 0 && State->QuadDelta == FVeloxDamageMath::ScaleDamage(State->PlainDelta, Settings.QuadDamageFactor);
			UAbilitySystemComponent* ASC = AscOf(Player);
			ASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), RegenStartHealth);
			VeloxPowerups::GrantOrExtend(*ASC, UVeloxGE_Powerup_Regen::StaticClass(), 30.f);
			State->RegenStart = HealthOf(Player);
			State->Step = 5;
			Schedule(State, RegenCheckSeconds);
			return;
		}
		case 5:
		{
			const float Health = HealthOf(Player);
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxPowerupTest phase=regen health %.0f -> %.0f after %.1f s"), State->RegenStart, Health, RegenCheckSeconds);
			State->bPassed &= State->RegenStart == RegenStartHealth && Health == RegenStartHealth + 45.f;
			Finish(State, TEXT("done"));
			return;
		}
		default:
			return;
		}
		Schedule(State, PollSeconds);
	}

	void Schedule(const TSharedRef<FPowerupTestState>& State, float Seconds)
	{
		if (UWorld* World = State->World.Get())
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([State]() { Step(State); }), Seconds, false);
		}
	}

	void PowerupTestCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		TSharedRef<FPowerupTestState> State = MakeShared<FPowerupTestState>();
		State->World = FindAuthorityWorld(InWorld);
		State->bQuit = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		if (!State->World.IsValid())
		{
			State->bPassed = false;
			Finish(State, TEXT("no_authority_world"));
			return;
		}
		Schedule(State, PollSeconds);
	}

	void GivePowerupCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = FindAuthorityWorld(InWorld);
		const FString Name = Args.Num() > 0 ? Args[0] : TEXT("Quad");
		const float Seconds = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 30.f;
		UClass* Class = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Velox.VeloxGE_Powerup_%s"), *Name));
		if (!World || !Class || !Class->IsChildOf(UVeloxGE_Powerup::StaticClass()))
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("velox.items.GivePowerup <Quad|Haste|Regen|BattleSuit> [Seconds]: needs the authority world and a powerup name."));
			return;
		}
		for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
		{
			UAbilitySystemComponent* ASC = It->IsPlayerControlled() ? AscOf(*It) : nullptr;
			if (ASC)
			{
				// Like a pickup: remaining + Seconds.
				VeloxPowerups::GrantOrExtend(*ASC, Class, VeloxPowerups::GetRemainingSeconds(*ASC, Class) + Seconds);
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs PowerupTestCmd(TEXT("velox.items.PowerupTest"),
		TEXT("velox.items.PowerupTest [quit]: MG shot on a dummy, Quad pickup under the player (waits for its initial delay), MG shot again (x3), Regen 50 -> 95 in 3 s; logs VeloxPowerupTest result=. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PowerupTestCommand));
	FAutoConsoleCommandWithWorldAndArgs GivePowerupCmd(TEXT("velox.items.GivePowerup"),
		TEXT("velox.items.GivePowerup <Quad|Haste|Regen|BattleSuit> [Seconds=30]: every player pawn gets (or extends) the powerup. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GivePowerupCommand));
}
#endif
