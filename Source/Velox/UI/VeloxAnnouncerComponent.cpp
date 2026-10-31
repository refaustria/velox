// Velox - the announcer.

#include "UI/VeloxAnnouncerComponent.h"

#include "Core/VeloxAudioSettings.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundMix.h"
#include "TimerManager.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAnnouncerComponent)

namespace VeloxAnnouncerPrivate
{
	/** Pause between two lines (s), on top of the sound's length. */
	constexpr float LineGapSeconds = 0.15f;
	/** Poll interval (s) of the countdown / score checks. */
	constexpr float PollSeconds = 0.1f;
}

UVeloxAnnouncerComponent::UVeloxAnnouncerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = VeloxAnnouncerPrivate::PollSeconds;
	SetIsReplicatedByDefault(false);
}

void UVeloxAnnouncerComponent::BeginPlay()
{
	Super::BeginPlay();
	UVeloxAudioSettings::EnsureLoaded(GetWorld());
	if (!UVeloxAudioSettings::CanPlayAudio(GetWorld()))
	{
		SetComponentTickEnabled(false);
	}
}

void UVeloxAnnouncerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AVeloxGameState* State = BoundState.Get())
	{
		State->OnPhaseChanged.Remove(PhaseHandle);
		State->OnKillFeedEntry.Remove(FeedHandle);
	}
	if (AVeloxPlayerState* Owner = BoundInventoryOwner.Get())
	{
		if (UVeloxWeaponInventoryComponent* Inventory = Owner->GetWeaponInventory())
		{
			Inventory->OnCurrentWeaponChanged.Remove(WeaponHandle);
		}
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LineTimer);
	}
	if (bMixPushed)
	{
		bMixPushed = false;
		if (USoundMix* Mix = UVeloxAudioSettings::Get().AnnouncerMix.Get())
		{
			UGameplayStatics::PopSoundMixModifier(this, Mix);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UVeloxAnnouncerComponent::BindGameState()
{
	AVeloxGameState* State = GetWorld() ? GetWorld()->GetGameState<AVeloxGameState>() : nullptr;
	if (!State || State == BoundState.Get())
	{
		return;
	}
	BoundState = State;
	PhaseHandle = State->OnPhaseChanged.AddUObject(this, &ThisClass::OnPhaseChanged);
	FeedHandle = State->OnKillFeedEntry.AddUObject(this, &ThisClass::OnKillFeedEntry);
	bLeadKnown = false;
}

void UVeloxAnnouncerComponent::BindInventory(AVeloxPlayerState* LocalState)
{
	if (!LocalState || LocalState == BoundInventoryOwner.Get())
	{
		return;
	}
	if (UVeloxWeaponInventoryComponent* Inventory = LocalState->GetWeaponInventory())
	{
		BoundInventoryOwner = LocalState;
		WeaponHandle = Inventory->OnCurrentWeaponChanged.AddUObject(this, &ThisClass::OnWeaponChanged);
	}
}

void UVeloxAnnouncerComponent::OnWeaponChanged(int32 OldIndex, int32 NewIndex)
{
	if (OldIndex != INDEX_NONE && NewIndex != INDEX_NONE && OldIndex != NewIndex)
	{
		UVeloxAudioSettings::Play2D(GetWorld(), UVeloxAudioSettings::Get().WeaponSwitchSound, TEXT("weapon_switch"));
	}
}

void UVeloxAnnouncerComponent::OnPhaseChanged(EVeloxMatchPhase OldPhase, EVeloxMatchPhase NewPhase)
{
	LastCountdownNumber = 0;
	switch (NewPhase)
	{
	case EVeloxMatchPhase::Live:
		bOneMinuteDone = false;
		bLeadKnown = false;
		LastFragServerTime = -1.0e9;
		ConsecutiveRailFrags = 0;
		// The reset at the end of the countdown (and the first Live of a match) is the "fight"; Overtime -> Live does not exist.
		if (OldPhase == EVeloxMatchPhase::Countdown)
		{
			Announce(TEXT("fight"));
		}
		break;
	case EVeloxMatchPhase::Overtime:
		Announce(TEXT("overtime"));
		break;
	case EVeloxMatchPhase::PostMatch:
		Announce(TEXT("match_over"));
		break;
	default:
		bLeadKnown = false;
		break;
	}
}

void UVeloxAnnouncerComponent::OnKillFeedEntry(const FVeloxKillFeedEntry& Entry)
{
	const AVeloxGameState* State = BoundState.Get();
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const AVeloxPlayerState* Local = Controller ? Controller->GetPlayerState<AVeloxPlayerState>() : nullptr;
	if (!State || !Local || Entry.bSuicide || Entry.KillerName != Local->GetPlayerName())
	{
		return;
	}
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	const double Now = State->GetServerWorldTimeSeconds();
	// A late joiner receives the earlier kills as adds: only fresh ones are events.
	if (Now - Entry.ServerTime > Settings.FragEventMaxAgeSeconds)
	{
		return;
	}
	ConsecutiveRailFrags = Entry.DamageTag == VeloxGameplayTags::Damage_Rail ? ConsecutiveRailFrags + 1 : 0;
	const bool bImpressive = ConsecutiveRailFrags >= Settings.ImpressiveRailCount;
	if (bImpressive)
	{
		ConsecutiveRailFrags = 0;
		Announce(TEXT("impressive"));
	}
	if (!bImpressive && Entry.ServerTime - LastFragServerTime <= Settings.ExcellentWindowSeconds)
	{
		Announce(TEXT("excellent"));
	}
	LastFragServerTime = Entry.ServerTime;
}

void UVeloxAnnouncerComponent::OnQuadDamage()
{
	Announce(TEXT("quad_damage"));
}

void UVeloxAnnouncerComponent::UpdateLead(const AVeloxGameState& State)
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const AVeloxPlayerState* Local = Controller ? Controller->GetPlayerState<AVeloxPlayerState>() : nullptr;
	if (!Local)
	{
		return;
	}
	float BestOther = -TNumericLimits<float>::Max();
	int32 NumOthers = 0;
	for (const APlayerState* Other : State.PlayerArray)
	{
		if (Other && Other != Local && !Other->IsOnlyASpectator())
		{
			BestOther = FMath::Max(BestOther, Other->GetScore());
			++NumOthers;
		}
	}
	if (NumOthers == 0)
	{
		bLeadKnown = false;
		return;
	}
	const VeloxAnnouncerMath::ELead Lead = VeloxAnnouncerMath::ComputeLead(Local->GetScore(), BestOther);
	if (!bLeadKnown)
	{
		// The standing at the start of the match (0 : 0, or whatever a late joiner finds) is silent.
		bLeadKnown = true;
		LastLead = Lead;
		return;
	}
	const FName Line = VeloxAnnouncerMath::LeadLine(LastLead, Lead);
	LastLead = Lead;
	const double Now = State.GetServerWorldTimeSeconds();
	if (!Line.IsNone() && Now - LastLeadLineTime >= UVeloxAudioSettings::Get().LeadLineCooldown)
	{
		LastLeadLineTime = Now;
		Announce(Line);
	}
}

void UVeloxAnnouncerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	BindGameState();
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	BindInventory(Controller ? Controller->GetPlayerState<AVeloxPlayerState>() : nullptr);
	const AVeloxGameState* State = BoundState.Get();
	if (!State)
	{
		return;
	}
	const double Now = State->GetServerWorldTimeSeconds();
	const EVeloxMatchPhase Phase = State->GetPhase();
	if (Phase == EVeloxMatchPhase::Countdown)
	{
		const int32 Number = VeloxAnnouncerMath::CountdownNumber(State->GetPhaseEndServerTime() - Now);
		if (Number != 0 && Number != LastCountdownNumber)
		{
			LastCountdownNumber = Number;
			static const TCHAR* const Names[] = {TEXT(""), TEXT("one"), TEXT("two"), TEXT("three")};
			Announce(Names[Number]);
		}
	}
	else if (Phase == EVeloxMatchPhase::Live || Phase == EVeloxMatchPhase::Overtime)
	{
		const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
		if (Phase == EVeloxMatchPhase::Live && !bOneMinuteDone && State->GetTimeLimitSeconds() > Settings.OneMinuteSeconds && State->GetPhaseEndServerTime() > 0.0)
		{
			const double Remaining = State->GetPhaseEndServerTime() - Now;
			if (Remaining > 0.0 && Remaining <= Settings.OneMinuteSeconds)
			{
				bOneMinuteDone = true;
				Announce(TEXT("one_minute"));
			}
		}
		UpdateLead(*State);
	}
}

void UVeloxAnnouncerComponent::Announce(FName Line)
{
	const UWorld* World = GetWorld();
	if (!UVeloxAudioSettings::CanPlayAudio(World))
	{
		return;
	}
	UE_LOG(LogVeloxAudio, Log, TEXT("VeloxAudio announce %s%s"), *Line.ToString(), bPlaying ? TEXT(" (queued)") : TEXT(""));
	if (bPlaying)
	{
		Queue.Push(Line, UVeloxAudioSettings::Get().AnnouncerQueueDepth);
		return;
	}
	PlayLine(Line);
}

void UVeloxAnnouncerComponent::PlayLine(FName Line)
{
	UWorld* World = GetWorld();
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	++NumAnnounced;
	LastLine = Line;
	const TSoftObjectPtr<USoundBase> Soft = Settings.FindAnnouncerLine(Line);
	USoundBase* Sound = Soft.IsNull() ? nullptr : Soft.LoadSynchronous();
	if (!World || !Sound)
	{
		UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio announcer line %s has no sound"), *Line.ToString());
		FinishLine();
		return;
	}
	bPlaying = true;
	UE_LOG(LogVeloxAudio, Verbose, TEXT("VeloxAudio play2d announcer %s sound=%s"), *Line.ToString(), *Sound->GetName());
	if (USoundMix* Mix = Settings.AnnouncerMix.LoadSynchronous())
	{
		UGameplayStatics::PushSoundMixModifier(this, Mix);
		bMixPushed = true;
	}
	UGameplayStatics::PlaySound2D(World, Sound);
	World->GetTimerManager().SetTimer(LineTimer, this, &ThisClass::FinishLine, FMath::Max(Sound->GetDuration(), 0.05f) + VeloxAnnouncerPrivate::LineGapSeconds, false);
}

void UVeloxAnnouncerComponent::FinishLine()
{
	bPlaying = false;
	if (bMixPushed)
	{
		bMixPushed = false;
		if (USoundMix* Mix = UVeloxAudioSettings::Get().AnnouncerMix.Get())
		{
			UGameplayStatics::PopSoundMixModifier(this, Mix);
		}
	}
	const FName Next = Queue.Pop();
	if (!Next.IsNone())
	{
		PlayLine(Next);
	}
}
