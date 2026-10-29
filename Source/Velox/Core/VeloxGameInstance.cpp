// Velox - game instance.

#include "Core/VeloxGameInstance.h"

#include "Core/VeloxLog.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/VeloxGameUserSettings.h"
#include "UI/VeloxMenuMath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameInstance)

UVeloxGameInstance::UVeloxGameInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
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

	UE_LOG(LogVelox, Log, TEXT("UVeloxGameInstance initialised."));
}

void UVeloxGameInstance::Shutdown()
{
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
	FString Normalized;
	if (!VeloxMenuMath::NormalizeConnectAddress(Address, DefaultServerPort, Normalized, OutError))
	{
		UE_LOG(LogVelox, Log, TEXT("VeloxMenu connect refused address='%s': %s"), *Address, *OutError);
		return false;
	}
	PendingMenuMessage.Reset();
	LastConnectTarget = Normalized;
	const FString URL = BuildConnectURL(Normalized);
	UE_LOG(LogVelox, Log, TEXT("VeloxMenu connect address=%s url=%s"), *Normalized, *URL);
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
