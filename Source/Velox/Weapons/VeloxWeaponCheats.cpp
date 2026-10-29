// Velox - non-shipping weapon test commands.

#include "Weapons/VeloxWeaponCheats.h"

#if !UE_BUILD_SHIPPING

#include "AbilitySystemComponent.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"
#include "Weapons/VeloxWeaponTable.h"

namespace VeloxWeaponCheats
{
	namespace
	{
		struct FShotRpcHold
		{
			float Seconds = 0.f;
			int32 Slot = INDEX_NONE;
			int32 Count = 0;
		};
		FShotRpcHold GShotRpcHold;
	}

	void SetShotRpcHold(float Seconds, int32 Slot, int32 Count)
	{
		GShotRpcHold.Seconds = FMath::Max(Seconds, 0.f);
		GShotRpcHold.Slot = Slot;
		GShotRpcHold.Count = Seconds > 0.f ? FMath::Max(Count, 0) : 0;
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotRpcHold armed ms=%.0f slot=%d count=%d"), GShotRpcHold.Seconds * 1000.f, GShotRpcHold.Slot, GShotRpcHold.Count);
	}

	bool ConsumeShotRpcHold(int32 Slot, float& OutSeconds)
	{
		if (GShotRpcHold.Count <= 0 || GShotRpcHold.Slot != Slot || GShotRpcHold.Seconds <= 0.f)
		{
			return false;
		}
		--GShotRpcHold.Count;
		OutSeconds = GShotRpcHold.Seconds;
		return true;
	}

	int32 ResolveWeapon(const UVeloxWeaponTable& Table, const FString& Name)
	{
		static const TMap<FString, FString> Short = {
			{ TEXT("MG"), TEXT("MachineGun") }, { TEXT("SG"), TEXT("Shotgun") }, { TEXT("PG"), TEXT("PlasmaGun") },
			{ TEXT("RL"), TEXT("RocketLauncher") }, { TEXT("LG"), TEXT("LightningGun") }, { TEXT("RG"), TEXT("Railgun") } };
		const FString* Mapped = Short.Find(Name.ToUpper());
		const FString Leaf = Mapped ? *Mapped : Name;
		if (Name.IsNumeric())
		{
			return Table.FindBySlot(FCString::Atoi(*Name));
		}
		for (int32 Index = 0; Index < Table.Num(); ++Index)
		{
			const UVeloxWeaponDefinition* Definition = Table.Get(Index);
			if (Definition && (Definition->WeaponTag.ToString().EndsWith(TEXT(".") + Leaf, ESearchCase::IgnoreCase) || Definition->GetName().Equals(Name, ESearchCase::IgnoreCase)))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool GiveAllWeapons(AVeloxPlayerState& PlayerState)
	{
		UVeloxWeaponInventoryComponent* Inventory = PlayerState.GetWeaponInventory();
		UVeloxAbilitySystemComponent* ASC = PlayerState.GetVeloxAbilitySystemComponent();
		const UVeloxWeaponTable* Table = Inventory ? Inventory->GetWeaponTable() : nullptr;
		if (!PlayerState.HasAuthority() || !Inventory || !ASC || !Table)
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("GiveAllWeapons: %s has no inventory/ASC/weapon table (authority %d)."), *PlayerState.GetName(), PlayerState.HasAuthority() ? 1 : 0);
			return false;
		}
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PlayerState.GetCombatSettings());
		for (int32 Index = 0; Index < Table->Num(); ++Index)
		{
			const UVeloxWeaponDefinition* Definition = Table->Get(Index);
			Inventory->AddWeapon(Definition, 0);
			const FVeloxAmmoTypeInfo* Ammo = Definition ? Settings.FindAmmoType(Definition->AmmoTag) : nullptr;
			if (Ammo && Ammo->Attribute.IsValid())
			{
				ASC->SetNumericAttributeBase(Ammo->Attribute, static_cast<float>(Ammo->MaxAmount));
			}
		}
		UE_LOG(LogVeloxCombat, Log, TEXT("GiveAllWeapons: %s owns 0x%04x"), *PlayerState.GetName(), Inventory->GetOwnedMask());
		return true;
	}

	bool FireTestShot(AVeloxCharacter& Shooter, const FString& WeaponName, AActor* AimAt, float PlaceDistanceCm, FString& OutError)
	{
		AVeloxPlayerState* PlayerState = Shooter.GetPlayerState<AVeloxPlayerState>();
		UVeloxWeaponInventoryComponent* Inventory = PlayerState ? PlayerState->GetWeaponInventory() : nullptr;
		UVeloxAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetVeloxAbilitySystemComponent() : nullptr;
		const UVeloxWeaponTable* Table = Inventory ? Inventory->GetWeaponTable() : nullptr;
		AController* Controller = Shooter.GetController();
		if (!Shooter.HasAuthority() || !Inventory || !ASC || !Table || !Controller)
		{
			OutError = TEXT("needs an authority pawn with a Velox PlayerState, inventory, weapon table and controller");
			return false;
		}
		const int32 Index = ResolveWeapon(*Table, WeaponName);
		if (Index == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("unknown weapon '%s'"), *WeaponName);
			return false;
		}
		if (!Inventory->OwnsIndex(Index))
		{
			Inventory->AddWeapon(Table->Get(Index));
		}
		Inventory->SetCurrentIndex(Index);
		FGameplayAbilitySpec* Spec = Inventory->FindFireSpec(Index);
		if (!Spec)
		{
			OutError = FString::Printf(TEXT("%s has no fire ability (projectiles arrive with task 05)"), *GetNameSafe(Table->Get(Index)));
			return false;
		}

		if (AimAt)
		{
			if (PlaceDistanceCm > 0.f)
			{
				FVector Away = Shooter.GetActorLocation() - AimAt->GetActorLocation();
				Away.Z = 0.0;
				Away = Away.IsNearlyZero() ? AimAt->GetActorForwardVector() : Away.GetSafeNormal();
				const FVector Location(AimAt->GetActorLocation().X + Away.X * PlaceDistanceCm, AimAt->GetActorLocation().Y + Away.Y * PlaceDistanceCm, Shooter.GetActorLocation().Z);
				Shooter.SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			}
			Controller->SetControlRotation((AimAt->GetActorLocation() - Shooter.GetPawnViewLocation()).Rotation());
		}

		const FString Before = AimAt ? FString::Printf(TEXT(" target=%s dist_cm=%.0f"), *AimAt->GetName(), FVector::Dist(Shooter.GetPawnViewLocation(), AimAt->GetActorLocation())) : FString();
		if (!ASC->TryActivateAbility(Spec->Handle))
		{
			OutError = FString::Printf(TEXT("%s did not activate (fire rate, ammo, switching or dead)"), *GetNameSafe(Table->Get(Index)));
			return false;
		}
		UE_LOG(LogVeloxCombat, Log, TEXT("FireTest: %s fired %s%s"), *Shooter.GetName(), *GetNameSafe(Table->Get(Index)), *Before);
		return true;
	}

	/** The authority world for World: itself, or the in-process PIE server when run on a PIE client. */
	UWorld* AuthorityWorld(UWorld* World)
	{
		if (!World || World->GetNetMode() != NM_Client)
		{
			return World;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (Context.WorldType == EWorldType::PIE && Candidate && Candidate != World && Candidate->GetNetMode() != NM_Client && Candidate->GetNetMode() != NM_Standalone)
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	AVeloxCharacter* LocalPawn(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC && PC->IsLocalController() ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
	}

	AActor* NearestOtherPawn(UWorld* World, const AVeloxCharacter& Shooter)
	{
		AActor* Best = nullptr;
		double BestDistSq = TNumericLimits<double>::Max();
		for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
		{
			const AVeloxPlayerState* PS = It->GetPlayerState<AVeloxPlayerState>();
			const UVeloxHealthSet* Health = PS ? PS->GetHealthSet() : nullptr;
			if (*It == &Shooter || !Health || Health->GetHealth() <= 0.f)
			{
				continue;
			}
			const double DistSq = FVector::DistSquared(It->GetActorLocation(), Shooter.GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = *It;
			}
		}
		return Best;
	}

	/** -ExecCmds runs before the first possession: retry every 0.1 s until the local pawn exists. */
	void RunWhenPawnExists(TWeakObjectPtr<UWorld> WeakWorld, TFunction<void(AVeloxCharacter&)> Action, int32 RetriesLeft, const TCHAR* Name)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return;
		}
		AVeloxCharacter* Pawn = LocalPawn(World);
		const AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
		if (!Pawn || !PS || !PS->GetWeaponInventory() || PS->GetWeaponInventory()->GetAuthoritativeIndex() == INDEX_NONE)
		{
			if (RetriesLeft > 0)
			{
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Action, RetriesLeft, Name]()
				{
					RunWhenPawnExists(WeakWorld, Action, RetriesLeft - 1, Name);
				}), 0.1f, false);
				return;
			}
			UE_LOG(LogVeloxCombat, Warning, TEXT("%s: no local Velox pawn with a loadout in %s (listen server / standalone only)."), Name, *World->GetName());
			return;
		}
		Action(*Pawn);
	}

	/** Dedicated server: every human player's pawn, as soon as one exists (clients join later than -ExecCmds runs). */
	void GiveAllToPlayers(TWeakObjectPtr<UWorld> WeakWorld, int32 RetriesLeft)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return;
		}
		int32 Given = 0;
		for (TActorIterator<AVeloxCharacter> It(World); It; ++It)
		{
			AVeloxPlayerState* PS = It->GetPlayerState<AVeloxPlayerState>();
			if (PS && Cast<APlayerController>(It->GetController()) && PS->GetWeaponInventory() && PS->GetWeaponInventory()->GetAuthoritativeIndex() != INDEX_NONE)
			{
				Given += GiveAllWeapons(*PS) ? 1 : 0;
			}
		}
		if (Given == 0 && RetriesLeft > 0)
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, RetriesLeft]() { GiveAllToPlayers(WeakWorld, RetriesLeft - 1); }), 0.1f, false);
		}
	}

	void GiveAllWeaponsCommand(const TArray<FString>& Args, UWorld* World)
	{
		UWorld* Authority = AuthorityWorld(World);
		if (!Authority)
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("GiveAllWeapons: no authority world"));
			return;
		}
		if (Authority->GetNetMode() == NM_DedicatedServer)
		{
			GiveAllToPlayers(Authority, 600);
			return;
		}
		RunWhenPawnExists(Authority, [](AVeloxCharacter& Pawn)
		{
			if (AVeloxPlayerState* PS = Pawn.GetPlayerState<AVeloxPlayerState>())
			{
				GiveAllWeapons(*PS);
			}
		}, 50, TEXT("GiveAllWeapons"));
	}

	/**
	 * Network client (not PIE): drives the real input path of the local player instead of activating on the server:
	 * presses InputTag.Weapon.Slot<N> if the weapon is not current (predicted switch), aims at the nearest pawn after
	 * the switch, presses InputTag.Fire for one frame. Exercises target data, server validation and ClientConfirmHit.
	 */
	void ClientFireTest(TWeakObjectPtr<UWorld> WeakWorld, FString Weapon, int32 RetriesLeft)
	{
		UWorld* World = WeakWorld.Get();
		AVeloxCharacter* Pawn = LocalPawn(World);
		AVeloxPlayerState* PS = Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
		UVeloxWeaponInventoryComponent* Inventory = PS ? PS->GetWeaponInventory() : nullptr;
		UVeloxAbilitySystemComponent* ASC = PS ? PS->GetVeloxAbilitySystemComponent() : nullptr;
		const UVeloxWeaponTable* Table = Inventory ? Inventory->GetWeaponTable() : nullptr;
		const int32 Index = Table ? ResolveWeapon(*Table, Weapon) : INDEX_NONE;
		auto Retry = [WeakWorld, Weapon, RetriesLeft](float Delay)
		{
			if (UWorld* W = WeakWorld.Get(); W && RetriesLeft > 0)
			{
				FTimerHandle Handle;
				W->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Weapon, RetriesLeft]() { ClientFireTest(WeakWorld, Weapon, RetriesLeft - 1); }), Delay, false);
			}
		};
		// Wait for the pawn, the loadout and ownership of the weapon (GiveAllWeapons on the server replicates).
		if (!World || !ASC || !Table || Index == INDEX_NONE || !Inventory->OwnsIndex(Index) || !Inventory->FindFireSpec(Index))
		{
			Retry(0.1f);
			return;
		}
		if (Inventory->GetCurrentIndex() != Index)
		{
			const FGameplayTag SlotTag = FGameplayTag::RequestGameplayTag(*FString::Printf(TEXT("InputTag.Weapon.Slot%d"), Table->Get(Index)->Slot));
			UE_LOG(LogVeloxCombat, Log, TEXT("FireTest(client): selecting %s via %s"), *GetNameSafe(Table->Get(Index)), *SlotTag.ToString());
			ASC->AbilityInputTagPressed(SlotTag);
			FTimerHandle Release;
			TWeakObjectPtr<UVeloxAbilitySystemComponent> WeakASC = ASC;
			World->GetTimerManager().SetTimer(Release, FTimerDelegate::CreateLambda([WeakASC, SlotTag]() { if (WeakASC.IsValid()) { WeakASC->AbilityInputTagReleased(SlotTag); } }), 0.05f, false);
			Retry(0.8f);
			return;
		}
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			if (AActor* Target = NearestOtherPawn(World, *Pawn))
			{
				PC->SetControlRotation((Target->GetActorLocation() - Pawn->GetPawnViewLocation()).Rotation());
				UE_LOG(LogVeloxCombat, Log, TEXT("FireTest(client): %s fires %s at %s dist_cm=%.0f"), *Pawn->GetName(), *GetNameSafe(Table->Get(Index)), *Target->GetName(),
					FVector::Dist(Pawn->GetPawnViewLocation(), Target->GetActorLocation()));
			}
		}
		ASC->AbilityInputTagPressed(VeloxGameplayTags::InputTag_Fire);
		FTimerHandle Release;
		TWeakObjectPtr<UVeloxAbilitySystemComponent> WeakASC = ASC;
		World->GetTimerManager().SetTimer(Release, FTimerDelegate::CreateLambda([WeakASC]() { if (WeakASC.IsValid()) { WeakASC->AbilityInputTagReleased(VeloxGameplayTags::InputTag_Fire); } }), 0.01f, false);
	}

	/**
	 * The game world a command should act on once it is settled: -ExecCmds of a connecting client run in the entry
	 * world, before the travel to the server's map. Null while a connection or travel is pending.
	 */
	UWorld* SettledGameWorld(UWorld* Hint)
	{
		if (Hint && Hint->WorldType == EWorldType::PIE)
		{
			return Hint;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game && Context.World())
			{
				return (Context.PendingNetGame || !Context.TravelURL.IsEmpty() || !Context.World()->HasBegunPlay()) ? nullptr : Context.World();
			}
		}
		return Hint;
	}

	void StartFireTest(UWorld* World, const FString& Weapon, float Distance);

	void FireTestCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("Usage: velox.combat.FireTest <MG|SG|PG|RL|LG|RG|slot> [AimDistanceCm: -1 aim at nearest (default), 0 no aim, >0 place + aim]"));
			return;
		}
		const FString Weapon = Args[0];
		const float Distance = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : -1.f;
		TWeakObjectPtr<UWorld> HintWorld = World;
		int32 TriesLeft = 600;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([HintWorld, Weapon, Distance, TriesLeft](float) mutable
		{
			if (UWorld* Settled = SettledGameWorld(HintWorld.Get()))
			{
				StartFireTest(Settled, Weapon, Distance);
				return false;
			}
			return --TriesLeft > 0;
		}), 0.1f);
	}

	void StartFireTest(UWorld* World, const FString& Weapon, float Distance)
	{
		UWorld* Authority = AuthorityWorld(World);
		if (!Authority)
		{
			if (World && World->GetNetMode() == NM_Client)
			{
				ClientFireTest(World, Weapon, 600);
				return;
			}
			UE_LOG(LogVeloxCombat, Warning, TEXT("FireTest: no authority world"));
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld = Authority;
		RunWhenPawnExists(Authority, [WeakWorld, Weapon, Distance](AVeloxCharacter& Pawn)
		{
			AActor* Target = Distance != 0.f ? NearestOtherPawn(WeakWorld.Get(), Pawn) : nullptr;
			FString Error;
			if (!FireTestShot(Pawn, Weapon, Target, Distance, Error))
			{
				UE_LOG(LogVeloxCombat, Warning, TEXT("FireTest %s: %s"), *Weapon, *Error);
			}
		}, 50, TEXT("FireTest"));
	}

	FAutoConsoleCommandWithWorldAndArgs GiveAllWeaponsCmd(
		TEXT("velox.combat.GiveAllWeapons"),
		TEXT("velox.combat.GiveAllWeapons: the local player (listen server / standalone) owns every weapon of the table with ammo at the cap. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiveAllWeaponsCommand));

	void DebugHoldShotRpcCommand(const TArray<FString>& Args)
	{
		if (Args.Num() < 2 || !Args[0].IsNumeric() || !Args[1].IsNumeric())
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("Usage: velox.combat.DebugHoldShotRpc <ms> <slot> [count=1]"));
			return;
		}
		const int32 Count = Args.Num() >= 3 && Args[2].IsNumeric() ? FCString::Atoi(*Args[2]) : 1;
		SetShotRpcHold(FCString::Atof(*Args[0]) / 1000.f, FCString::Atoi(*Args[1]), Count);
	}

	FAutoConsoleCommand DebugHoldShotRpcCmd(
		TEXT("velox.combat.DebugHoldShotRpc"),
		TEXT("velox.combat.DebugHoldShotRpc <ms> <slot> [count=1]: server, the next <count> fire target-data RPCs of the weapon in <slot> are processed <ms> later (forced late fire RPC, ADR 0003 B35). Faithful for RL/RG, whose fire intervals exceed the hold. Non-shipping."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DebugHoldShotRpcCommand));

	FAutoConsoleCommandWithWorldAndArgs FireTestCmd(
		TEXT("velox.combat.FireTest"),
		TEXT("velox.combat.FireTest <MG|SG|PG|RL|LG|RG|slot> [AimDistanceCm]: selects the weapon and fires one shot through its ability along the control rotation (-1/absent: aimed at the nearest pawn, >0: placed at that distance first). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FireTestCommand));
}

#endif // !UE_BUILD_SHIPPING
