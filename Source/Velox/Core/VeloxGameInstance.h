// Velox - game instance. Set via GameInstanceClass in Config/DefaultEngine.ini.

#pragma once

#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"

#include "VeloxGameInstance.generated.h"

class UNetDriver;

UCLASS(Config = Game)
class VELOX_API UVeloxGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UVeloxGameInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UGameInstance interface
	virtual void Init() override;
	virtual void Shutdown() override;
	/** A new world needs the sound mix again (ADR 0004 section 7). */
	virtual void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld) override;
	//~End of UGameInstance interface

	/** Address plus `?Name=<UVeloxGameUserSettings::PlayerName>`: the URL to ClientTravel to (the menu's connect, task 10). */
	UFUNCTION(BlueprintCallable, Category = "Velox|Settings")
	FString BuildConnectURL(const FString& Address) const;

	// ---- Menu flow (ADR 0004 section 7; task M3-10) ----

	/**
	 * Main menu "connect": normalises Address (VeloxMenuMath::NormalizeConnectAddress, DefaultServerPort when it has no
	 * port), remembers it in UVeloxGameUserSettings::LastConnectAddress and ClientTravels to BuildConnectURL(Address)
	 * (TRAVEL_Absolute) from the first local player controller. False and OutError when refused before travelling; a
	 * failure after that comes back through the engine's network/travel failure events (the engine returns to the
	 * GameDefaultMap menu, we keep the reason for it to show).
	 */
	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	bool ConnectToServer(const FString& Address, FString& OutError);

	/** Main menu "host local": ClientTravel to HostLocalURL (a listen server running the FFA mode). */
	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	bool HostLocal(FString& OutError);

	/** Leaves the current game for the front-end map (GameDefaultMap), showing Message there (may be empty). */
	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	bool ReturnToFrontEnd(const FString& Message);

	/** The message the front-end shows (failed connect, kicked...). Returned once, then cleared. */
	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	FString ConsumeMenuMessage();

	/** Same without clearing (tests, dumps). */
	const FString& GetPendingMenuMessage() const { return PendingMenuMessage; }

	/** The normalised address of the last ConnectToServer (empty before the first). */
	const FString& GetLastConnectTarget() const { return LastConnectTarget; }

	/** Sets the message directly (tests). */
	void SetPendingMenuMessage(const FString& Message) { PendingMenuMessage = Message; }

	/** Port added to an address typed without one. */
	int32 GetDefaultServerPort() const { return DefaultServerPort; }

protected:
	/** Used when the typed address has no `:port` (the dedicated server default). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Menu", Meta = (ClampMin = "1", ClampMax = "65535"))
	int32 DefaultServerPort = 7777;

	/** Travel URL of "host local": the item test map as a listen server running the FFA mode alias (DefaultEngine.ini). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Menu")
	FString HostLocalURL = TEXT("/Game/Maps/L_ItemTest?listen?game=FFA");

private:
	void OnEngineNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void OnEngineTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	bool TravelFromFirstController(const FString& URL, FString& OutError);

	FString PendingMenuMessage;
	FString LastConnectTarget;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
