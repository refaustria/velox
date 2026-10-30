// Velox - bot item routing (ADR 0005 sections 2, 4, 7, 13; task M4-06).

#include "Bots/VeloxBotItemRouting.h"

#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotSettings.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxBotTypes.h"
#include "Bots/VeloxNavFilters.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxHealthSet.h"
#include "Engine/World.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxMatchEffects.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/PlatformTime.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "Movement/VeloxMovementComponent.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxBotItemRouting)

namespace VeloxBotItemRoutingPrivate
{
	/** Travel time reported for a zero speed (never chosen over anything reachable). */
	constexpr float UnreachableSeconds = 1.0e6f;

	const TCHAR* FilterName(const TSubclassOf<UNavigationQueryFilter>& Filter)
	{
		if (Filter == UVeloxNavFilter_Full::StaticClass())
		{
			return TEXT("Full");
		}
		if (Filter == UVeloxNavFilter_Hop::StaticClass())
		{
			return TEXT("Hop");
		}
		if (Filter == UVeloxNavFilter_Walk::StaticClass())
		{
			return TEXT("Walk");
		}
		return TEXT("none");
	}

	const TCHAR* GoalName(EVeloxBotGoal Goal)
	{
		switch (Goal)
		{
		case EVeloxBotGoal::Item: return TEXT("Item");
		case EVeloxBotGoal::Fight: return TEXT("Fight");
		case EVeloxBotGoal::Roam: return TEXT("Roam");
		default: return TEXT("None");
		}
	}

	FString PickupLabel(const AVeloxPickup* Pickup)
	{
		const UVeloxItemDefinition* Definition = Pickup ? Pickup->GetDefinition() : nullptr;
		return Definition ? VeloxBotItemRouting::ItemLabel(Definition->ItemTag) : FString(TEXT("-"));
	}

	int32 PickupId(const AVeloxPickup* Pickup)
	{
		return Pickup ? static_cast<int32>(Pickup->GetUniqueID()) : INDEX_NONE;
	}

	/** The enemy a Fight goal is about, from the bot's combat memory (null when forgotten, dead or gone). */
	const FVeloxBotEnemyInfo* FindGoalEnemy(const AVeloxBotController& Bot, const FVeloxBotGoal& Goal)
	{
		const APawn* Enemy = Goal.Type == EVeloxBotGoal::Fight ? Goal.Enemy.Get() : nullptr;
		const AVeloxCharacter* Character = Cast<AVeloxCharacter>(Enemy);
		const APlayerState* EnemyPS = Enemy ? Enemy->GetPlayerState() : nullptr;
		if (!Character || Character->IsCorpse() || !EnemyPS)
		{
			return nullptr;
		}
		return Bot.GetCombatState().FindEnemy(EnemyPS->GetPlayerId());
	}

	/**
	 * Enemy fields of the decision input (ADR 0005 section 2: Fight vs Item with FightItemBias) from the combat state of
	 * task 05: the current target (EnemyId = its PlayerState id) when it is visible. A Fight goal's enemy that is out of
	 * sight but still remembered counts as present (D12 chase: the controller paths to its last seen location until the
	 * memory expires), so a cadence evaluation does not drop the chase it just started.
	 */
	void FillEnemyInput(const AVeloxBotController& Bot, const FVeloxBotGoal& Current, FVeloxBotDecisionInput& In)
	{
		In.bEnemyVisible = false;
		In.EnemyId = INDEX_NONE;
		const UWorld* World = Bot.GetWorld();
		if (World && Bot.IsFightSuppressed(World->GetTimeSeconds()))
		{
			// ADR 0005 D30: after a fight_stall strike the decision routes as if no enemy were in sight (the bot keeps
			// aiming and firing; only the goal changes) until the controller's cooldown ends.
			return;
		}
		const FVeloxBotCombatState& Combat = Bot.GetCombatState();
		const FVeloxBotEnemyInfo* Target = Combat.GetTarget();
		const AVeloxCharacter* TargetPawn = Target ? Target->Pawn.Get() : nullptr;
		if (!(Target && Target->bVisible && TargetPawn && !TargetPawn->IsCorpse()))
		{
			Target = FindGoalEnemy(Bot, Current);
		}
		if (Target)
		{
			In.bEnemyVisible = true;
			In.EnemyId = Target->Id;
			In.EnemyLocation = Target->LastSeenLocation;
		}
	}

	/** Bot facts from the live PlayerState attributes and inventory. */
	bool GatherBotFacts(const AVeloxPlayerState& PS, FVeloxRoutingBotFacts& Out)
	{
		const UVeloxHealthSet* Health = PS.GetHealthSet();
		const UVeloxAbilitySystemComponent* ASC = PS.GetVeloxAbilitySystemComponent();
		if (!Health || !ASC)
		{
			return false;
		}
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PS.GetCombatSettings());
		Out = FVeloxRoutingBotFacts();
		Out.Health = FMath::RoundToInt32(Health->GetHealth());
		Out.MaxHealth = FMath::RoundToInt32(Health->GetMaxHealth());
		Out.HealthHardCap = FMath::RoundToInt32(Settings.HealthHardCap);
		Out.Armor = FMath::RoundToInt32(Health->GetArmor());
		Out.MaxArmor = FMath::RoundToInt32(Health->GetMaxArmor());
		for (const FVeloxAmmoTypeInfo& Row : Settings.AmmoTypes)
		{
			if (Row.Tag.IsValid() && Row.Attribute.IsValid())
			{
				Out.Ammo.Add(Row.Tag, FIntPoint(FMath::RoundToInt32(ASC->GetNumericAttribute(Row.Attribute)), Row.MaxAmount));
			}
		}
		if (const UVeloxWeaponInventoryComponent* Inventory = PS.GetWeaponInventory())
		{
			for (int32 i = 0; i < Inventory->NumWeapons(); ++i)
			{
				const UVeloxWeaponDefinition* Weapon = Inventory->GetDefinition(i);
				if (Weapon && Inventory->OwnsIndex(i))
				{
					Out.OwnedWeaponTags.AddUnique(Weapon->WeaponTag);
					if (Weapon->AmmoTag.IsValid())
					{
						Out.OwnedAmmoTags.AddUnique(Weapon->AmmoTag);
					}
					FVeloxRoutingWeaponAmmo& Use = Out.OwnedWeaponAmmo.FindOrAdd(Weapon->WeaponTag);
					Use.AmmoTag = Weapon->AmmoTag;
					Use.AmmoPerShot = Weapon->AmmoTag.IsValid() ? Weapon->AmmoPerShot : 0;
				}
			}
		}
		return true;
	}

	/** Item facts from the definition and the replicated pickup state (ADR 0004 section 9 API). */
	bool GatherItemFacts(const AVeloxPickup& Pickup, FVeloxRoutingItemFacts& Out)
	{
		const UVeloxItemDefinition* Definition = Pickup.GetDefinition();
		if (!Definition)
		{
			return false;
		}
		Out = FVeloxRoutingItemFacts();
		Out.CandidateId = PickupId(&Pickup);
		Out.Type = Definition->Type;
		Out.Quantity = Definition->GetEffectiveQuantity();
		Out.BotBaseValue = Definition->BotBaseValue;
		Out.bMajorItem = Definition->bMajorItem;
		Out.HealthCap = Definition->HealthCap;
		if (Definition->Type == EVeloxItemType::Ammo)
		{
			Out.AmmoTag = Definition->AmmoTag;
		}
		else if (Definition->Type == EVeloxItemType::Weapon && Definition->Weapon)
		{
			Out.AmmoTag = Definition->Weapon->AmmoTag;
			Out.WeaponTag = Definition->Weapon->WeaponTag;
		}
		Out.bAvailable = Pickup.IsAvailable();
		Out.TimeUntilRespawn = static_cast<float>(Pickup.GetTimeUntilRespawn());
		Out.Location = Pickup.GetActorLocation();
		return true;
	}
}

// ------------------------------------------------------------------------------------------------ world-free rules

namespace VeloxBotItemRouting
{
	FVeloxItemScoringInput MakeScoringInput(const FVeloxRoutingItemFacts& Item, const FVeloxRoutingBotFacts& Bot, float Travel)
	{
		FVeloxItemScoringInput In;
		In.CandidateId = Item.CandidateId;
		In.Type = Item.Type;
		In.Quantity = Item.Quantity;
		In.BotBaseValue = Item.BotBaseValue;
		In.bMajorItem = Item.bMajorItem;
		In.HealthCap = Item.HealthCap;
		In.Health = Bot.Health;
		In.MaxHealth = Bot.MaxHealth;
		In.HealthHardCap = Bot.HealthHardCap;
		In.Armor = Bot.Armor;
		In.MaxArmor = Bot.MaxArmor;
		if (Item.AmmoTag.IsValid())
		{
			if (const FIntPoint* Ammo = Bot.Ammo.Find(Item.AmmoTag))
			{
				In.Ammo = Ammo->X;
				In.AmmoMax = Ammo->Y;
			}
			In.bOwnsWeaponUsingAmmo = Bot.OwnedAmmoTags.Contains(Item.AmmoTag);
		}
		In.bOwnsWeapon = Item.WeaponTag.IsValid() && Bot.OwnedWeaponTags.Contains(Item.WeaponTag);
		In.bAvailable = Item.bAvailable;
		In.TimeUntilRespawn = Item.TimeUntilRespawn;
		In.TravelSeconds = Travel;
		In.OwnedWeaponStrength = Bot.WeaponStrength;
		In.UnownedWeaponNeedFloor = Bot.UnownedWeaponNeedFloor;
		// Duel denial: a major item the bot can use is worth at least MajorItemMinNeed of its base value (taking it also
		// starts the opponent's wait). Need 0 (nothing to grant: the pickup would refuse) stays 0.
		const float Need = VeloxItemValue::Need(In);
		if (Item.bMajorItem && Bot.MajorItemMinNeed > 0.f && Need > 0.f && Need < Bot.MajorItemMinNeed)
		{
			In.BotBaseValue *= Bot.MajorItemMinNeed / Need;
		}
		return In;
	}

	float WeaponStrength(const FVeloxRoutingBotFacts& Bot, TConstArrayView<FVeloxBotWeaponPreference> Preferences)
	{
		float MaxWeight = 0.f;
		for (const FVeloxBotWeaponPreference& Pref : Preferences)
		{
			MaxWeight = FMath::Max(MaxWeight, Pref.Weight);
		}
		if (MaxWeight <= 0.f)
		{
			return 1.f;
		}
		float Best = 0.f;
		for (const FVeloxBotWeaponPreference& Pref : Preferences)
		{
			const FVeloxRoutingWeaponAmmo* Use = Bot.OwnedWeaponAmmo.Find(Pref.Weapon);
			if (!Use || Pref.Weight <= Best)
			{
				continue;
			}
			const FIntPoint* Have = Use->AmmoTag.IsValid() && Use->AmmoPerShot > 0 ? Bot.Ammo.Find(Use->AmmoTag) : nullptr;
			if (!Have || Have->X >= Use->AmmoPerShot)
			{
				Best = Pref.Weight;
			}
		}
		return FMath::Clamp(Best / MaxWeight, 0.f, 1.f);
	}

	void FillDerivedBotFacts(FVeloxRoutingBotFacts& Bot, TConstArrayView<FVeloxBotWeaponPreference> Preferences, float UnownedWeaponNeedFloor)
	{
		Bot.WeaponStrength = WeaponStrength(Bot, Preferences);
		Bot.UnownedWeaponNeedFloor = UnownedWeaponNeedFloor;
	}

	bool ShouldMarkUnreachable(const FVeloxRoutingPathResult& Path, bool bAirborneStart)
	{
		return Path.bFound && Path.bPartial && !bAirborneStart;
	}

	float TravelSeconds(double DistanceCm, float SpeedCmS)
	{
		return SpeedCmS > UE_KINDA_SMALL_NUMBER ? static_cast<float>(FMath::Max(DistanceCm, 0.0) / SpeedCmS) : VeloxBotItemRoutingPrivate::UnreachableSeconds;
	}

	float PathTravelSeconds(const FVeloxRoutingPathResult& Path, float SpeedCmS)
	{
		return TravelSeconds(FMath::Max(Path.Cost, Path.Length), SpeedCmS);
	}

	void SelectQueryCandidates(TConstArrayView<FVeloxRoutingPreScore> PreScored, int32 MaxQueries, int32 KeepId, bool bDuel, TArray<int32>& OutIds)
	{
		OutIds.Reset();
		if (MaxQueries <= 0)
		{
			return;
		}
		auto Has = [&PreScored](int32 Id) { return PreScored.ContainsByPredicate([Id](const FVeloxRoutingPreScore& P) { return P.CandidateId == Id; }); };
		if (KeepId != INDEX_NONE && Has(KeepId))
		{
			OutIds.Add(KeepId);
		}
		if (bDuel && OutIds.Num() < MaxQueries)
		{
			const FVeloxRoutingPreScore* Camp = nullptr;
			float CampLead = 0.f;
			for (const FVeloxRoutingPreScore& P : PreScored)
			{
				if (!P.bMajorItem || P.bAvailable)
				{
					continue;
				}
				const float Lead = P.TimeUntilRespawn - P.StraightSeconds;
				if (!Camp || Lead < CampLead || (Lead == CampLead && P.CandidateId < Camp->CandidateId))
				{
					Camp = &P;
					CampLead = Lead;
				}
			}
			if (Camp && !OutIds.Contains(Camp->CandidateId))
			{
				OutIds.Add(Camp->CandidateId);
			}
		}
		TArray<const FVeloxRoutingPreScore*> Ranked;
		for (const FVeloxRoutingPreScore& P : PreScored)
		{
			if (P.PreScore > 0.f && !OutIds.Contains(P.CandidateId))
			{
				Ranked.Add(&P);
			}
		}
		Ranked.Sort([](const FVeloxRoutingPreScore& A, const FVeloxRoutingPreScore& B)
		{
			return A.PreScore != B.PreScore ? A.PreScore > B.PreScore : A.CandidateId < B.CandidateId;
		});
		for (const FVeloxRoutingPreScore* P : Ranked)
		{
			if (OutIds.Num() >= MaxQueries)
			{
				break;
			}
			OutIds.Add(P->CandidateId);
		}
	}

	FVeloxGoalItemCandidate MakeGoalCandidate(const FVeloxRoutingItemFacts& Item, const FVeloxRoutingBotFacts& Bot, const FVeloxRoutingPathResult& Path,
		float SpeedCmS, const FVeloxItemScoringParams& Params, float CommitTravelSeconds)
	{
		FVeloxGoalItemCandidate C;
		C.CandidateId = Item.CandidateId;
		C.Location = Item.Location;
		C.bMajorItem = Item.bMajorItem;
		C.bAvailable = Item.bAvailable;
		C.TimeUntilRespawn = Item.TimeUntilRespawn;
		C.bReachable = Path.bFound && !Path.bPartial;
		C.TravelSeconds = C.bReachable ? PathTravelSeconds(Path, SpeedCmS) : VeloxBotItemRoutingPrivate::UnreachableSeconds;
		C.Score = C.bReachable ? CommittedScore(MakeScoringInput(Item, Bot, C.TravelSeconds), Params, CommitTravelSeconds) : 0.f;
		return C;
	}

	float CommittedScore(const FVeloxItemScoringInput& In, const FVeloxItemScoringParams& Params, float CommitTravelSeconds)
	{
		FVeloxItemScoringParams Relaxed = Params;
		if (!In.bAvailable && Params.ItemTimingSkill > UE_KINDA_SMALL_NUMBER && CommitTravelSeconds > 0.f)
		{
			// Gate Wait <= MaxWait x Skill becomes Wait <= MaxWait x Skill + CommitTravelSeconds.
			Relaxed.MaxWaitSeconds += CommitTravelSeconds / Params.ItemTimingSkill;
		}
		return VeloxItemValue::Score(In, Relaxed);
	}

	void ApplyMinScore(TArray<FVeloxGoalItemCandidate>& Candidates, float MinScore)
	{
		if (MinScore <= 0.f)
		{
			return;
		}
		for (FVeloxGoalItemCandidate& C : Candidates)
		{
			if (C.Score < MinScore)
			{
				C.Score = 0.f;
			}
		}
	}

	bool IsEvaluationDue(double Now, double NextEvaluationTime, bool bGoalLost, bool bEventPending)
	{
		return bEventPending || bGoalLost || Now >= NextEvaluationTime;
	}

	FVector CampPoint(const FVector& ItemLocation, const FVector& BotLocation, float OffsetCm)
	{
		const FVector Dir = (BotLocation - ItemLocation).GetSafeNormal2D();
		return Dir.IsNearlyZero() ? ItemLocation : ItemLocation + Dir * FMath::Max(OffsetCm, 0.f);
	}

	bool IsRefusedAtArrival(bool bArrived, bool bItemAvailable, double ArrivedSince, double Now, float RefuseSeconds)
	{
		return bArrived && bItemAvailable && ArrivedSince >= 0.0 && Now - ArrivedSince >= RefuseSeconds;
	}

	FString ItemLabel(const FGameplayTag& ItemTag)
	{
		if (!ItemTag.IsValid())
		{
			return FString(TEXT("-"));
		}
		FString Label = ItemTag.ToString();
		Label.RemoveFromStart(TEXT("Velox.Item."));
		return Label.IsEmpty() ? FString(TEXT("-")) : Label;
	}
}

// ------------------------------------------------------------------------------------------------ world glue

UVeloxBotItemRouter* UVeloxBotItemRouter::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UVeloxBotItemRouter>() : nullptr;
}

bool UVeloxBotItemRouter::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->GetNetMode() != NM_Client;
}

bool UVeloxBotItemRouter::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVeloxBotItemRouter::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(&InWorld))
	{
		RespawnedHandle = Registry->OnPickupRespawned.AddUObject(this, &ThisClass::OnPickupRespawned);
		TakenHandle = Registry->OnPickupTaken.AddUObject(this, &ThisClass::OnPickupTaken);
	}
}

void UVeloxBotItemRouter::Deinitialize()
{
	if (UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(GetWorld()))
	{
		Registry->OnPickupRespawned.Remove(RespawnedHandle);
		Registry->OnPickupTaken.Remove(TakenHandle);
	}
	States.Reset();
	Super::Deinitialize();
}

UVeloxBotItemRouter::FBotRouteState& UVeloxBotItemRouter::FindOrAddState(AVeloxBotController& Bot)
{
	FBotRouteState& S = States.FindOrAdd(TObjectKey<AVeloxBotController>(&Bot));
	S.Bot = &Bot;
	return S;
}

const FVeloxBotRouteRecord* UVeloxBotItemRouter::FindLastEvaluation(const AVeloxBotController* Bot) const
{
	const FBotRouteState* S = Bot ? States.Find(TObjectKey<AVeloxBotController>(Bot)) : nullptr;
	return S && S->Last.Time >= 0.0 ? &S->Last : nullptr;
}

void UVeloxBotItemRouter::RequestEvaluation(const AVeloxBotController* Bot)
{
	if (FBotRouteState* S = Bot ? States.Find(TObjectKey<AVeloxBotController>(Bot)) : nullptr)
	{
		S->bEventPending = true;
	}
}

void UVeloxBotItemRouter::OnPickupRespawned(AVeloxPickup* Pickup)
{
	// A bot camping (or waiting for) this item takes it at once instead of on the next cadence tick.
	for (TPair<TObjectKey<AVeloxBotController>, FBotRouteState>& Pair : States)
	{
		const AVeloxBotController* Bot = Pair.Value.Bot.Get();
		if (Bot && Bot->GetBotState().Goal.Item.Get() == Pickup)
		{
			Pair.Value.bEventPending = true;
		}
	}
}

void UVeloxBotItemRouter::OnPickupTaken(AVeloxPickup* Pickup, APlayerState* /*TakenBy*/)
{
	// Item goals are cleared by the controller itself (its goal is lost: immediate evaluation); camp goals keep the item
	// pointer on a Roam goal, re-evaluate them here (the camped item's timer restarted).
	for (TPair<TObjectKey<AVeloxBotController>, FBotRouteState>& Pair : States)
	{
		const AVeloxBotController* Bot = Pair.Value.Bot.Get();
		if (Bot && Bot->GetBotState().Goal.Type == EVeloxBotGoal::Roam && Bot->GetBotState().Goal.Item.Get() == Pickup)
		{
			Pair.Value.bEventPending = true;
		}
	}
}

EVeloxRouteUpdate UVeloxBotItemRouter::UpdateBot(AVeloxBotController& Bot, const FVeloxBotDifficultyParams& Params, const FVeloxBotGoal& Current, bool bGoalArrived,
	FRandomStream& Stream, FVeloxBotGoal& OutGoal)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	FBotRouteState& S = FindOrAddState(Bot);
	MaybeLogStats(Now);

	// Filter eligibility (section 4): a change (health under MinHealthForRocketJump, rockets gone, a link fallback) changes
	// the travel times; the controller's EnsureMove re-queries the path with the new filter on its own.
	const TSubclassOf<UNavigationQueryFilter> Filter = Bot.GetNavFilter();
	if (!S.bFilterKnown || Filter != S.Filter)
	{
		if (S.bFilterKnown)
		{
			const AVeloxPlayerState* PS = Bot.GetPlayerState<AVeloxPlayerState>();
			const UVeloxHealthSet* Health = PS ? PS->GetHealthSet() : nullptr;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotFilter bot=%s from=%s to=%s health=%.0f goal=%s item=%s t=%.2f"), *Bot.GetBotName(), VeloxBotItemRoutingPrivate::FilterName(S.Filter),
				VeloxBotItemRoutingPrivate::FilterName(Filter), Health ? Health->GetHealth() : -1.f, VeloxBotItemRoutingPrivate::GoalName(Current.Type),
				*VeloxBotItemRoutingPrivate::PickupLabel(Current.Item.Get()), Now);
			// The running evaluation's paths were found with the old filter: start over.
			AbortEvaluation(Bot, S);
			S.bEventPending = true;
		}
		S.Unreachable.Reset();
		S.Filter = Filter;
		S.bFilterKnown = true;
	}

	// Refusal guard: standing at an available goal item that is not taken.
	AVeloxPickup* GoalItem = Current.Type == EVeloxBotGoal::Item ? Current.Item.Get() : nullptr;
	const bool bAtAvailableItem = GoalItem && bGoalArrived && GoalItem->IsAvailable();
	if (!bAtAvailableItem || S.ArrivedItem.Get() != GoalItem)
	{
		S.ArrivedItem = bAtAvailableItem ? GoalItem : nullptr;
		S.ArrivedSince = bAtAvailableItem ? Now : -1.0;
	}
	else if (VeloxBotItemRouting::IsRefusedAtArrival(true, true, S.ArrivedSince, Now, ArrivedRefuseSeconds))
	{
		S.Cooldowns.Add(GoalItem, Now + RefusedItemCooldownSeconds);
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRoute bot=%s refused item=%s cooldown=%.1f s"), *Bot.GetBotName(), *VeloxBotItemRoutingPrivate::PickupLabel(GoalItem), RefusedItemCooldownSeconds);
		S.ArrivedItem = nullptr;
		S.ArrivedSince = -1.0;
		S.bEventPending = true;
	}

	// Section 2 (05/06 integration): a newly visible target is a Fight-vs-Item decision now, not at the next cadence
	// tick; a Fight goal whose enemy died or was forgotten is a lost goal.
	{
		const FVeloxBotCombatState& Combat = Bot.GetCombatState();
		const int32 VisibleTargetId = Combat.HasVisibleTarget() ? Combat.TargetId : INDEX_NONE;
		if (VisibleTargetId != S.LastVisibleTargetId)
		{
			const FVeloxBotEnemyInfo* GoalEnemy = VeloxBotItemRoutingPrivate::FindGoalEnemy(Bot, Current);
			if (VisibleTargetId != INDEX_NONE && (!GoalEnemy || GoalEnemy->Id != VisibleTargetId))
			{
				S.bEventPending = true;
			}
			S.LastVisibleTargetId = VisibleTargetId;
		}
		if (Current.Type == EVeloxBotGoal::Fight && !VeloxBotItemRoutingPrivate::FindGoalEnemy(Bot, Current))
		{
			S.bEventPending = true;
		}
	}

	const bool bHasGoal = Current.Type != EVeloxBotGoal::None;
	const bool bGoalLost = !bHasGoal && S.bHadGoal;
	if (bHasGoal)
	{
		S.bHadGoal = true;
	}

	// A running evaluation: one path query per frame (review D10), finishing in the frame of its last query.
	if (S.Pending.bActive)
	{
		if (ActiveBot.Get() != &Bot)
		{
			// The slot was taken away (this bot missed a frame); start over when the slot is free.
			S.Pending = FPendingEvaluation();
			S.bEventPending = true;
		}
		else if (LastStepFrame == GFrameCounter)
		{
			return EVeloxRouteUpdate::Unchanged;
		}
		else
		{
			return StepEvaluation(Bot, S, Params, Current, Stream, OutGoal);
		}
	}

	if (!VeloxBotItemRouting::IsEvaluationDue(Now, S.NextEvaluationTime, bGoalLost, S.bEventPending))
	{
		return EVeloxRouteUpdate::Unchanged;
	}

	// Stagger (section 13, D10): one bot evaluates at a time and the router runs at most one step per server frame. An
	// active bot that did not step on the previous frame (died, forced goal, frozen, kicked) loses the slot.
	AVeloxBotController* Active = ActiveBot.Get();
	if ((Active && Active != &Bot && GFrameCounter <= LastStepFrame + 1) || LastStepFrame == GFrameCounter)
	{
		++S.DeferredFrames;
		return EVeloxRouteUpdate::Unchanged;
	}
	if (Active && Active != &Bot)
	{
		if (FBotRouteState* StaleState = States.Find(TObjectKey<AVeloxBotController>(Active)))
		{
			AbortEvaluation(*Active, *StaleState);
		}
	}
	ActiveBot = nullptr;

	const uint64 StartCycles = FPlatformTime::Cycles64();
	if (!BeginEvaluation(Bot, S, Params, Current))
	{
		// Not now (no pawn or nav yet, airborne without mesh below): stays due, the slot is not used.
		return EVeloxRouteUpdate::Unchanged;
	}
	S.Pending.Micros += FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles) * 1000.0;
	ActiveBot = &Bot;
	WindowMaxDeferredFrames = FMath::Max(WindowMaxDeferredFrames, S.DeferredFrames);
	S.DeferredFrames = 0;
	S.bEventPending = false;
	return StepEvaluation(Bot, S, Params, Current, Stream, OutGoal);
}

void UVeloxBotItemRouter::AbortEvaluation(AVeloxBotController& Bot, FBotRouteState& S)
{
	if (S.Pending.bActive)
	{
		S.Pending = FPendingEvaluation();
		S.bEventPending = true;
	}
	if (ActiveBot.Get() == &Bot)
	{
		ActiveBot = nullptr;
	}
}

bool UVeloxBotItemRouter::BeginEvaluation(AVeloxBotController& Bot, FBotRouteState& S, const FVeloxBotDifficultyParams& Params, const FVeloxBotGoal& Current)
{
	UWorld* World = GetWorld();
	const AVeloxCharacter* Character = Cast<AVeloxCharacter>(Bot.GetPawn());
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	const AVeloxPlayerState* PS = Bot.GetPlayerState<AVeloxPlayerState>();
	const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
	const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ANavigationData* NavData = Bots ? Bots->GetNavData() : nullptr;
	if (!Character || !Movement || !PS || !Bots || !Bots->IsNavReady() || !Registry || !NavSys || !NavData)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	const TSubclassOf<UNavigationQueryFilter> FilterClass = Bot.GetNavFilter();
	const FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(*NavData, &Bot, FilterClass);

	// Path start on the mesh: the feet on the ground; airborne (a bunny-hopping bot touches the ground one tick per hop)
	// the point below the pawn (AirborneProjectDownCm). A start off the mesh would make every candidate look unreachable,
	// so the evaluation waits instead (logged, rate-limited).
	FVector BotLocation = Character->GetNavAgentLocation();
	const FVector AgentLocation = BotLocation;
	const bool bAirborne = !Movement->IsOnGround();
	{
		const FVector Extent(AirborneProjectRadiusCm, AirborneProjectRadiusCm, bAirborne ? AirborneProjectDownCm * 0.5f : GroundProjectHalfHeightCm);
		const FVector Probe = bAirborne ? BotLocation - FVector(0.0, 0.0, AirborneProjectDownCm * 0.5f) : BotLocation;
		FNavLocation Projected;
		if (!NavSys->ProjectPointToNavigation(Probe, Projected, Extent, NavData, Filter))
		{
			if (!bAirborne && Now >= S.NextOffMeshLogTime)
			{
				S.NextOffMeshLogTime = Now + StatsIntervalSeconds;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRoute bot=%s offmesh at=%s (no evaluation until the bot is back on the nav mesh)"), *Bot.GetBotName(), *BotLocation.ToCompactString());
			}
			return false;
		}
		BotLocation = Projected.Location;
	}

	FVeloxRoutingBotFacts BotFacts;
	if (!VeloxBotItemRoutingPrivate::GatherBotFacts(*PS, BotFacts))
	{
		return false;
	}
	VeloxBotItemRouting::FillDerivedBotFacts(BotFacts, Params.WeaponPreferences, UVeloxBotSettings::Get().UnownedWeaponNeedFloor);
	const AVeloxGameState* GameState = World->GetGameState<AVeloxGameState>();
	const UVeloxGameModeDefinition* ModeDefinition = GameState ? GameState->GetGameModeDefinition() : nullptr;
	const bool bDuel = ModeDefinition && ModeDefinition->ModeTag.MatchesTagExact(VeloxMatchTags::Mode_Duel);
	const FVeloxItemScoringParams ScoringParams = VeloxItemValue::MakeParams(Params);
	const float Speed = Params.EstimatedTravelSpeed;
	BotFacts.MajorItemMinNeed = bDuel ? DuelMajorItemMinNeed : 0.f;

	// (1, 2) Facts and straight-line pre-scores of every pickup.
	for (TMap<TWeakObjectPtr<AVeloxPickup>, double>* Map : {&S.Cooldowns, &S.Unreachable})
	{
		for (TMap<TWeakObjectPtr<AVeloxPickup>, double>::TIterator It = Map->CreateIterator(); It; ++It)
		{
			if (!It->Key.IsValid() || It->Value <= Now)
			{
				It.RemoveCurrent();
			}
		}
	}
	const AVeloxPickup* KeepPickup = Current.Item.Get();
	const TArray<AVeloxPickup*> Pickups = Registry->GetPickups();
	TArray<FVeloxRoutingPreScore> PreScores;
	TArray<AVeloxPickup*> PrePickups;
	PreScores.Reserve(Pickups.Num());
	PrePickups.Reserve(Pickups.Num());
	for (AVeloxPickup* Pickup : Pickups)
	{
		FVeloxRoutingItemFacts Item;
		// Refused items and items found unreachable with this filter a moment ago are skipped (the current goal never).
		const bool bSkipped = Pickup != KeepPickup && (S.Cooldowns.Contains(Pickup) || S.Unreachable.Contains(Pickup));
		if (!Pickup || bSkipped || !VeloxBotItemRoutingPrivate::GatherItemFacts(*Pickup, Item))
		{
			continue;
		}
		const float Straight = VeloxBotItemRouting::TravelSeconds(FVector::Dist(BotLocation, Item.Location), Speed);
		FVeloxRoutingPreScore& P = PreScores.AddDefaulted_GetRef();
		P.CandidateId = Item.CandidateId;
		P.PreScore = VeloxItemValue::Score(VeloxBotItemRouting::MakeScoringInput(Item, BotFacts, Straight), ScoringParams);
		P.bMajorItem = Item.bMajorItem;
		P.bAvailable = Item.bAvailable;
		P.TimeUntilRespawn = Item.TimeUntilRespawn;
		P.StraightSeconds = Straight;
		PrePickups.Add(Pickup);
	}

	// (3) Query set, at most PathQueryCandidates; the queries run one per frame from here.
	const int32 KeepId = (Current.Type == EVeloxBotGoal::Item || Current.Type == EVeloxBotGoal::Roam) && KeepPickup ? VeloxBotItemRoutingPrivate::PickupId(KeepPickup) : INDEX_NONE;
	const int32 MaxQueries = FMath::Max(0, FMath::RoundToInt32(UVeloxBotSettings::Get().PathQueryCandidates));
	TArray<int32> QueryIds;
	VeloxBotItemRouting::SelectQueryCandidates(PreScores, MaxQueries, KeepId, bDuel, QueryIds);

	S.Pending = FPendingEvaluation();
	S.Pending.bActive = true;
	S.Pending.StartTime = Now;
	S.Pending.Start = BotLocation;
	S.Pending.bAirborneStart = bAirborne;
	S.Pending.AgentLocation = AgentLocation;
	S.Pending.Filter = FilterClass;
	for (const int32 Id : QueryIds)
	{
		const int32 Index = PreScores.IndexOfByPredicate([Id](const FVeloxRoutingPreScore& P) { return P.CandidateId == Id; });
		if (Index != INDEX_NONE)
		{
			S.Pending.Queue.Add(PrePickups[Index]);
		}
	}
	return true;
}

EVeloxRouteUpdate UVeloxBotItemRouter::StepEvaluation(AVeloxBotController& Bot, FBotRouteState& S, const FVeloxBotDifficultyParams& Params, const FVeloxBotGoal& Current,
	FRandomStream& Stream, FVeloxBotGoal& OutGoal)
{
	const uint64 StartCycles = FPlatformTime::Cycles64();
	FPendingEvaluation& E = S.Pending;
	if (E.Results.Num() < E.Queue.Num())
	{
		// (4) One FindPathSync this frame, from the evaluation's start point, with its filter.
		UWorld* World = GetWorld();
		const UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World);
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		ANavigationData* NavData = Bots ? Bots->GetNavData() : nullptr;
		const AVeloxPickup* Pickup = E.Queue[E.Results.Num()].Get();
		FVeloxRoutingPathResult& Path = E.Results.AddDefaulted_GetRef();
		if (Pickup && NavSys && NavData)
		{
			const FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(*NavData, &Bot, E.Filter);
			FPathFindingQuery Query(&Bot, *NavData, E.Start, Pickup->GetActorLocation(), Filter);
			Query.SetAllowPartialPaths(true);
			const uint64 QueryCycles = FPlatformTime::Cycles64();
			const FPathFindingResult Result = NavSys->FindPathSync(Query);
			const double QueryMicros = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - QueryCycles) * 1000.0;
			if (QueryMicros > WindowQueryMicrosMax)
			{
				WindowQueryMicrosMax = QueryMicros;
				WindowSlowestQuery = FString::Printf(TEXT("%s:%s->%s:%s"), *Bot.GetBotName(), *E.Start.ToCompactString(), *VeloxBotItemRoutingPrivate::PickupLabel(Pickup),
					Result.IsPartial() ? TEXT("partial") : TEXT("full"));
			}
			++E.PathQueries;
			// D10: counted in this bot tick's VeloxBotBudget paths=/s (UVeloxBotSubsystem::RecordBotTick).
			Bot.NotePathQuery();
			++TotalPathQueries;
			++WindowPathQueries;
			// A bot standing on its item gets a one- or two-point path; any point counts as found.
			Path.bFound = Result.IsSuccessful() && Result.Path.IsValid() && Result.Path->GetPathPoints().Num() > 0;
			Path.bPartial = Path.bFound && Result.Path->IsPartial();
			Path.Cost = Path.bFound ? Result.Path->GetCost() : 0.0;
			Path.Length = Path.bFound ? Result.Path->GetLength() : 0.0;
			Path.End = Path.bFound ? Result.Path->GetPathPoints().Last().Location : FVector::ZeroVector;
		}
	}
	LastStepFrame = GFrameCounter;
	E.Micros += FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles) * 1000.0;
	if (E.Results.Num() < E.Queue.Num())
	{
		return EVeloxRouteUpdate::Unchanged;
	}

	// Last query done: score and choose in this frame.
	const uint64 FinishCycles = FPlatformTime::Cycles64();
	EVeloxRouteUpdate Update = EVeloxRouteUpdate::Unchanged;
	FinishEvaluation(Bot, S, Params, Current, Stream, Update, OutGoal);
	E.Micros += FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - FinishCycles) * 1000.0;

	S.Last.PathQueries = E.PathQueries;
	S.Last.Micros = E.Micros;
	S.Last.Frames = FMath::Max(E.Results.Num(), 1);
	++TotalEvaluations;
	++WindowEvaluations;
	WindowMicrosSum += E.Micros;
	WindowMicrosMax = FMath::Max(WindowMicrosMax, E.Micros);
	const double Now = GetWorld()->GetTimeSeconds();
	S.NextEvaluationTime = Now + FMath::Max(Params.GoalReevaluateSeconds, 0.05f);
	S.bHadGoal = false;
	S.Pending = FPendingEvaluation();
	ActiveBot = nullptr;
	return Update;
}

void UVeloxBotItemRouter::FinishEvaluation(AVeloxBotController& Bot, FBotRouteState& S, const FVeloxBotDifficultyParams& Params, const FVeloxBotGoal& Current,
	FRandomStream& Stream, EVeloxRouteUpdate& OutUpdate, FVeloxBotGoal& OutGoal)
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	const FPendingEvaluation& E = S.Pending;
	const AVeloxPlayerState* PS = Bot.GetPlayerState<AVeloxPlayerState>();
	const APawn* Pawn = Bot.GetPawn();
	FVeloxRoutingBotFacts BotFacts;
	if (!PS || !Pawn || !VeloxBotItemRoutingPrivate::GatherBotFacts(*PS, BotFacts))
	{
		OutUpdate = EVeloxRouteUpdate::Unchanged;
		return;
	}
	VeloxBotItemRouting::FillDerivedBotFacts(BotFacts, Params.WeaponPreferences, UVeloxBotSettings::Get().UnownedWeaponNeedFloor);
	const AVeloxGameState* GameState = World->GetGameState<AVeloxGameState>();
	const UVeloxGameModeDefinition* ModeDefinition = GameState ? GameState->GetGameModeDefinition() : nullptr;
	const bool bDuel = ModeDefinition && ModeDefinition->ModeTag.MatchesTagExact(VeloxMatchTags::Mode_Duel);
	const bool bPostMatch = GameState && GameState->GetPhase() == EVeloxMatchPhase::PostMatch;
	const FVeloxItemScoringParams ScoringParams = VeloxItemValue::MakeParams(Params);
	const float Speed = Params.EstimatedTravelSpeed;
	BotFacts.MajorItemMinNeed = bDuel ? DuelMajorItemMinNeed : 0.f;
	const FVector BotLocation = Pawn->GetNavAgentLocation();

	FVeloxBotRouteRecord Record;
	Record.Time = Now;
	Record.Filter = E.Filter;
	Record.bAirborneStart = E.bAirborneStart;
	Record.WeaponStrength = BotFacts.WeaponStrength;
	FVeloxBotDecisionInput In;
	In.bPostMatch = bPostMatch;
	In.bDuel = bDuel;
	In.CampOverScore = bDuel ? DuelCampOverScore : 0.f;
	In.CampMaxLeadSeconds = bDuel ? DuelCampMaxLeadSeconds : 0.f;
	In.Health = BotFacts.Health;
	In.Armor = BotFacts.Armor;
	In.WeaponStrength = BotFacts.WeaponStrength;
	VeloxBotItemRoutingPrivate::FillEnemyInput(Bot, Current, In);
	{
		// ADR 0005 D4 (task 04b): the fight score parameters are project settings.
		const UVeloxBotSettings& BotSettings = UVeloxBotSettings::Get();
		In.Fight.BaseScore = BotSettings.FightBaseScore;
		In.Fight.FullStrength = BotSettings.FightFullStrength;
		In.Fight.WeakWeaponScale = BotSettings.FightWeakWeaponScale;
	}
	// Item state is read now (it may have changed during the queries); the paths are the evaluation's.
	for (int32 i = 0; i < E.Queue.Num(); ++i)
	{
		AVeloxPickup* Pickup = E.Queue[i].Get();
		FVeloxRoutingItemFacts Item;
		if (!Pickup || !E.Results.IsValidIndex(i) || !VeloxBotItemRoutingPrivate::GatherItemFacts(*Pickup, Item))
		{
			continue;
		}
		// The committed goal keeps its timing window while the bot closes in faster than EstimatedTravelSpeed.
		const float CommitTravel = Pickup == Current.Item.Get() && Pickup == S.CommitItem.Get() ? S.CommitTravelSeconds : 0.f;
		const FVeloxGoalItemCandidate& Candidate = In.Items.Add_GetRef(VeloxBotItemRouting::MakeGoalCandidate(Item, BotFacts, E.Results[i], Speed, ScoringParams, CommitTravel));
		if (VeloxBotItemRouting::ShouldMarkUnreachable(E.Results[i], E.bAirborneStart))
		{
			// Only a partial path from the bot's grounded position proves the item unreachable (D24); a failed query (start /
			// end projection) or a start projected below an airborne pawn does not.
			S.Unreachable.Add(Pickup, Now + UnreachableRetrySeconds);
		}
		Record.CandidatePickups.Add(Pickup);
	}
	if (bDuel)
	{
		VeloxBotItemRouting::ApplyMinScore(In.Items, DuelMinItemScore);
	}

	// (5) Choice with hysteresis against the current goal.
	const AVeloxPickup* CurrentItem = Current.Item.Get();
	In.Current.Type = Current.Type;
	In.Current.ItemId = CurrentItem && (Current.Type == EVeloxBotGoal::Item || Current.Type == EVeloxBotGoal::Roam) ? VeloxBotItemRoutingPrivate::PickupId(CurrentItem) : INDEX_NONE;
	In.Current.Point = Current.Point;
	In.Current.Score = Current.Score;
	const FVeloxBotGoalChoice Choice = VeloxBotDecision::ChooseGoal(In, Params, Stream);
	Record.BiasedFightScore = Params.FightItemBias * VeloxBotDecision::FightScore(In);
	Record.Candidates = In.Items;
	Record.Choice = Choice;

	AVeloxPickup* ChosenPickup = nullptr;
	const int32 ChosenIndex = Choice.ItemId != INDEX_NONE ? In.Items.IndexOfByPredicate([&Choice](const FVeloxGoalItemCandidate& C) { return C.CandidateId == Choice.ItemId; }) : INDEX_NONE;
	if (ChosenIndex != INDEX_NONE)
	{
		ChosenPickup = Record.CandidatePickups[ChosenIndex].Get();
	}

	const FVeloxGoalItemCandidate* Chosen = ChosenIndex != INDEX_NONE ? &In.Items[ChosenIndex] : nullptr;
	const bool bSameItem = ChosenPickup && ChosenPickup == CurrentItem && Choice.Type == Current.Type;
	OutGoal = FVeloxBotGoal();
	if (Choice.Type == EVeloxBotGoal::None)
	{
		OutUpdate = bPostMatch ? (Current.Type == EVeloxBotGoal::None ? EVeloxRouteUpdate::Unchanged : EVeloxRouteUpdate::NewGoal) : EVeloxRouteUpdate::Roam;
	}
	else if (Choice.Type == EVeloxBotGoal::Fight)
	{
		// Section 2 / D12: fight the enemy; the controller requests no path while it is visible and chases its last seen
		// location otherwise. The same enemy keeps the goal (and its chase path).
		const FVeloxBotEnemyInfo* Enemy = Bot.GetCombatState().FindEnemy(Choice.EnemyId);
		APawn* EnemyPawn = Enemy ? Enemy->Pawn.Get() : nullptr;
		if (!EnemyPawn)
		{
			OutUpdate = EVeloxRouteUpdate::Roam;
		}
		else if (Current.Type == EVeloxBotGoal::Fight && Current.Enemy.Get() == EnemyPawn)
		{
			OutUpdate = EVeloxRouteUpdate::Unchanged;
		}
		else
		{
			OutGoal.Type = EVeloxBotGoal::Fight;
			OutGoal.Enemy = EnemyPawn;
			OutGoal.Point = Choice.Point;
			OutGoal.Score = Choice.Score;
			OutUpdate = EVeloxRouteUpdate::NewGoal;
		}
	}
	else if ((Choice.Type == EVeloxBotGoal::Roam && Choice.ItemId == INDEX_NONE) || bSameItem)
	{
		// ChooseGoal kept the current roam point / item.
		OutUpdate = EVeloxRouteUpdate::Unchanged;
	}
	else if (ChosenPickup)
	{
		OutGoal.Type = Choice.Type;
		OutGoal.Item = ChosenPickup;
		OutGoal.Score = Choice.Score;
		OutGoal.Point = ChosenPickup->GetActorLocation();
		if (Choice.Type == EVeloxBotGoal::Roam)
		{
			// Duel camp (section 7): stand CampOffsetCm from the item toward the bot, kept on the item's mesh by a nav raycast.
			const FVector Desired = VeloxBotItemRouting::CampPoint(OutGoal.Point, BotLocation, CampOffsetCm);
			FVector Hit = Desired;
			OutGoal.Point = UNavigationSystemV1::NavigationRaycast(World, OutGoal.Point, Desired, Hit, E.Filter, &Bot) ? Hit : Desired;
		}
		OutUpdate = EVeloxRouteUpdate::NewGoal;
		S.CommitItem = ChosenPickup;
		S.CommitTravelSeconds = Chosen ? FMath::Min(Chosen->TravelSeconds, MaxCommitTravelSeconds) : 0.f;
	}
	else
	{
		OutUpdate = EVeloxRouteUpdate::Roam;
	}

	// Log: changes at Log, confirmations at Verbose.
	FString CandidateText;
	for (int32 i = 0; i < In.Items.Num(); ++i)
	{
		const FVeloxGoalItemCandidate& C = In.Items[i];
		CandidateText += FString::Printf(TEXT("%s%s:%.2f/%.1fs%s%s"), i > 0 ? TEXT(",") : TEXT(""), *VeloxBotItemRoutingPrivate::PickupLabel(Record.CandidatePickups[i].Get()), C.Score,
			C.bReachable ? C.TravelSeconds : -1.f, C.bReachable ? TEXT("") : TEXT("!unreachable"), C.bAvailable ? TEXT("") : *FString::Printf(TEXT("@%.1f"), C.TimeUntilRespawn));
	}
	const bool bCamp = Choice.Type == EVeloxBotGoal::Roam && ChosenPickup != nullptr;
	// D24 diagnostics: an evaluation whose every query came back partial names its start (pawn and projected point) and
	// where each partial path stopped. Rare (expected 0 from a grounded start), so at Log.
	if (E.Results.Num() > 0 && !E.Results.ContainsByPredicate([](const FVeloxRoutingPathResult& R) { return !R.bFound || !R.bPartial; }))
	{
		FString Ends;
		for (int32 i = 0; i < E.Results.Num(); ++i)
		{
			Ends += FString::Printf(TEXT("%s%s->%s"), i > 0 ? TEXT(",") : TEXT(""), *VeloxBotItemRoutingPrivate::PickupLabel(E.Queue[i].Get()), *E.Results[i].End.ToCompactString());
		}
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRouteAllPartial bot=%s start=%s agent=%s projected=%s filter=%s t=%.2f ends=[%s]"), *Bot.GetBotName(),
			E.bAirborneStart ? TEXT("air") : TEXT("ground"), *E.AgentLocation.ToCompactString(), *E.Start.ToCompactString(), VeloxBotItemRoutingPrivate::FilterName(E.Filter), Now, *Ends);
	}
	UE_LOG(LogVeloxBots, Verbose, TEXT("VeloxBotRouteEval bot=%s choice=%s item=%s update=%d paths=%d frames=%d filter=%s candidates=[%s]"), *Bot.GetBotName(),
		VeloxBotItemRoutingPrivate::GoalName(Choice.Type), *VeloxBotItemRoutingPrivate::PickupLabel(ChosenPickup), static_cast<int32>(OutUpdate), E.PathQueries, E.Results.Num(),
		VeloxBotItemRoutingPrivate::FilterName(E.Filter), *CandidateText);
	if (OutUpdate != EVeloxRouteUpdate::Unchanged)
	{
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRoute bot=%s goal=%s item=%s camp=%d score=%.2f travel=%.2f available=%d respawn_in=%.1f lead=%.1f paths=%d filter=%s from=%s fight=%.2f start=%s eval_s=%.3f t=%.2f candidates=[%s]"),
			*Bot.GetBotName(), OutUpdate == EVeloxRouteUpdate::Roam ? TEXT("Roam") : VeloxBotItemRoutingPrivate::GoalName(Choice.Type), *VeloxBotItemRoutingPrivate::PickupLabel(ChosenPickup),
			bCamp ? 1 : 0, Choice.Score, Chosen ? Chosen->TravelSeconds : 0.f, Chosen ? (Chosen->bAvailable ? 1 : 0) : 1, Chosen ? Chosen->TimeUntilRespawn : 0.f,
			Chosen ? Chosen->TimeUntilRespawn - Chosen->TravelSeconds : 0.f, E.PathQueries,
			VeloxBotItemRoutingPrivate::FilterName(E.Filter), VeloxBotItemRoutingPrivate::GoalName(Current.Type), Record.BiasedFightScore, E.bAirborneStart ? TEXT("air") : TEXT("ground"),
			Now - E.StartTime, Now, *CandidateText);
	}
	S.Last = MoveTemp(Record);
}

void UVeloxBotItemRouter::MaybeLogStats(double Now)
{
	if (StatsWindowStart < 0.0)
	{
		StatsWindowStart = Now;
		return;
	}
	const double Window = Now - StatsWindowStart;
	if (Window < StatsIntervalSeconds)
	{
		return;
	}
	int32 NumBots = 0;
	for (TMap<TObjectKey<AVeloxBotController>, FBotRouteState>::TIterator It = States.CreateIterator(); It; ++It)
	{
		if (!It->Value.Bot.IsValid())
		{
			It.RemoveCurrent();
			continue;
		}
		++NumBots;
	}
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotRouteStats bots=%d evals=%d paths=%d paths_per_s=%.1f eval_us_mean=%.1f eval_us_max=%.1f max_deferred_frames=%d query_us_max=%.1f slowest=%s total_evals=%lld total_paths=%lld"),
		NumBots, WindowEvaluations, WindowPathQueries, Window > 0.0 ? WindowPathQueries / Window : 0.0, WindowEvaluations > 0 ? WindowMicrosSum / WindowEvaluations : 0.0, WindowMicrosMax,
		WindowMaxDeferredFrames, WindowQueryMicrosMax, *WindowSlowestQuery, TotalEvaluations, TotalPathQueries);
	StatsWindowStart = Now;
	WindowEvaluations = 0;
	WindowPathQueries = 0;
	WindowMicrosSum = 0.0;
	WindowMicrosMax = 0.0;
	WindowMaxDeferredFrames = 0;
	WindowQueryMicrosMax = 0.0;
	WindowSlowestQuery = TEXT("-");
}
