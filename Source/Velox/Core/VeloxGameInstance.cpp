// Velox - game instance.

#include "Core/VeloxGameInstance.h"

#include "Core/VeloxLog.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsModule.h"
#include "Misc/NetworkVersion.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/CString.h"
#include "Misc/Parse.h"
#include "UI/VeloxGameUserSettings.h"
#include "UI/VeloxMenuMath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameInstance)

UVeloxGameInstance::UVeloxGameInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

namespace VeloxGameInstancePrivate
{
	/** Task m6-online/02b, ADR 0007 F2 (ii)-b: the network version is the engine's, combined with the gameplay tag net index hash. */
	uint32 GetNetworkVersionWithTagHash()
	{
		return HashCombine(FNetworkVersion::GetLocalNetworkVersion(false), UGameplayTagsManager::Get().GetNetworkGameplayTagNodeIndexHash());
	}

	void OnTagTreeChanged()
	{
		FNetworkVersion::InvalidateNetworkChecksum();
	}
}

void UVeloxGameInstance::BindTagHashIntoNetworkVersion()
{
	// With GameplayTags FastReplication every tag after the first difference shifts its net index, so a server and a client whose
	// tag tables differ misread tags silently (B1). Folding the hash into the network version makes the engine refuse the client at
	// NMT_Hello with its standard version failure.
	static bool bBound = false;
	if (!bBound)
	{
		bBound = true;
		FNetworkVersion::GetLocalNetworkVersionOverride.BindStatic(&VeloxGameInstancePrivate::GetNetworkVersionWithTagHash);
		IGameplayTagsModule::OnGameplayTagTreeChanged.AddStatic(&VeloxGameInstancePrivate::OnTagTreeChanged);
	}
	FNetworkVersion::InvalidateNetworkChecksum();
	const UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
	UE_LOG(LogVeloxNet, Log, TEXT("VeloxNet netversion=%u taghash=%08x tags=%d"), FNetworkVersion::GetLocalNetworkVersion(),
		TagManager.GetNetworkGameplayTagNodeIndexHash(), TagManager.GetNetworkGameplayTagNodeIndex().Num());
}

void UVeloxGameInstance::Init()
{
	Super::Init();

	// The engine itself travels back to GameDefaultMap (L_MainMenu) on these failures (UEngine::HandleNetworkFailure /
	// HandleTravelFailure -> HandleDisconnect -> SetClientTravel("?closed")); we only record the reason for the menu.
	if (GEngine && !IsRunningDedicatedServer())
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::OnEngineNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::OnEngineTravelFailure);
	}

	BindTagHashIntoNetworkVersion();

	UE_LOG(LogVelox, Log, TEXT("UVeloxGameInstance initialised."));
}

void UVeloxGameInstance::OnStart()
{
	Super::OnStart();

	FString Address;
	if (IsRunningDedicatedServer() || !FParse::Value(FCommandLine::Get(), TEXT("-VeloxConnect="), Address) || Address.IsEmpty())
	{
		return;
	}
	UE_LOG(LogVelox, Log, TEXT("VeloxConnect: launch option, connecting to '%s' once the front end is up"), *Address);
	TWeakObjectPtr<UVeloxGameInstance> WeakThis(this);
	int32 Attempts = 0;
	LaunchConnectHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, Address, Attempts](float) mutable
	{
		UVeloxGameInstance* Self = WeakThis.Get();
		if (!Self)
		{
			return false;
		}
		if (!Self->GetFirstLocalPlayerController() && ++Attempts < 600)
		{
			return true; // front end not up yet; try again next tick (about 10 s at 60 fps)
		}
		FString Error;
		if (!Self->ConnectToServer(Address, Error))
		{
			UE_LOG(LogVelox, Warning, TEXT("VeloxConnect: '%s' refused: %s"), *Address, *Error);
		}
		Self->LaunchConnectHandle.Reset();
		return false;
	}));
}

void UVeloxGameInstance::Shutdown()
{
	FTSTicker::RemoveTicker(LaunchConnectHandle);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	NetworkFailureHandle.Reset();
	TravelFailureHandle.Reset();
	Super::Shutdown();
}

void UVeloxGameInstance::OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld)
{
	Super::OnWorldChanged(OldWorld, NewWorld);

	if (NewWorld)
	{
		if (UVeloxGameUserSettings* Settings = UVeloxGameUserSettings::Get())
		{
			Settings->ApplyAudio(NewWorld);
		}
	}
}

FString UVeloxGameInstance::BuildConnectURL(const FString& Address) const
{
	return VeloxSettingsMath::AppendNameOption(Address, UVeloxGameUserSettings::GetOrDefault().PlayerName);
}

void UVeloxGameInstance::OnEngineNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// Only failures of this instance's client connection (a listen host losing a client is not our problem).
	if (!NetDriver || NetDriver->GetNetMode() != NM_Client || (World && World->GetGameInstance() != this))
	{
		return;
	}
	PendingMenuMessage = VeloxMenuMath::NetworkFailureMessage(FailureType, ErrorString);
	UE_LOG(LogVelox, Log, TEXT("VeloxMenu network failure type=%s error='%s' -> returning to the main menu"), ENetworkFailure::ToString(FailureType), *ErrorString);
}

void UVeloxGameInstance::OnEngineTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (World && World->GetGameInstance() != this)
	{
		return;
	}
	PendingMenuMessage = VeloxMenuMath::TravelFailureMessage(FailureType, ErrorString);
	UE_LOG(LogVelox, Log, TEXT("VeloxMenu travel failure type=%s error='%s' -> returning to the main menu"), ETravelFailure::ToString(FailureType), *ErrorString);
}

bool UVeloxGameInstance::TravelFromFirstController(const FString& URL, FString& OutError)
{
	APlayerController* Controller = GetFirstLocalPlayerController();
	if (!Controller)
	{
		OutError = TEXT("No local player to travel with.");
		return false;
	}
	Controller->ClientTravel(URL, TRAVEL_Absolute);
	return true;
}

bool UVeloxGameInstance::ConnectToServer(const FString& Address, FString& OutError)
{
	return ConnectToServerWithPassword(Address, FString(), OutError);
}

bool UVeloxGameInstance::ConnectToServerWithPassword(const FString& Address, const FString& Password, FString& OutError)
{
	FString Normalized;
	if (!VeloxMenuMath::NormalizeConnectAddress(Address, DefaultServerPort, Normalized, OutError))
	{
		UE_LOG(LogVelox, Log, TEXT("VeloxMenu connect refused address='%s': %s"), *Address, *OutError);
		return false;
	}
	PendingMenuMessage.Reset();
	LastConnectTarget = Normalized;
	const FString NameURL = BuildConnectURL(Normalized);
	FString URL;
	if (!VeloxMenuMath::AppendPasswordOption(NameURL, Password, URL, OutError))
	{
		UE_LOG(LogVelox, Log, TEXT("VeloxMenu connect refused address='%s': %s"), *Address, *OutError);
		return false;
	}
	UE_LOG(LogVelox, Log, TEXT("VeloxMenu connect address=%s url=%s password=%d"), *Normalized, *NameURL, Password.IsEmpty() ? 0 : 1);
	if (!TravelFromFirstController(URL, OutError))
	{
		return false;
	}
	// Remembered only for a connect that was actually started.
	if (UVeloxGameUserSettings* Settings = UVeloxGameUserSettings::Get())
	{
		Settings->LastConnectAddress = Normalized;
		Settings->SaveSettings();
	}
	return true;
}

bool UVeloxGameInstance::HostLocal(FString& OutError)
{
	PendingMenuMessage.Reset();
	const FString URL = BuildConnectURL(HostLocalURL);
	UE_LOG(LogVelox, Log, TEXT("VeloxMenu host local url=%s"), *URL);
	return TravelFromFirstController(URL, OutError);
}

bool UVeloxGameInstance::ReturnToFrontEnd(const FString& Message)
{
	if (!GetWorld())
	{
		return false;
	}
	PendingMenuMessage = Message;
	UE_LOG(LogVelox, Log, TEXT("VeloxMenu return to the front-end message='%s'"), *Message);
	// The engine's own path (UEngine::HandleDisconnect -> SetClientTravel("?closed")): closes the connection or the listen
	// server and loads GameDefaultMap (L_MainMenu), the same route a network failure takes.
	ReturnToMainMenu();
	return true;
}

FString UVeloxGameInstance::ConsumeMenuMessage()
{
	FString Message;
	Swap(Message, PendingMenuMessage);
	return Message;
}
