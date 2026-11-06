// Velox - map registry (ADR 0007 section 2, task M6-04): the display name and the modes of every map, as data. The heartbeat,
// the server browser, the scoreboard header and M7's map rotation read it. Config/DefaultGame.ini [/Script/Velox.VeloxMapRegistry].

#pragma once

#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"

#include "VeloxMapRegistry.generated.h"

class UWorld;

USTRUCT(BlueprintType)
struct VELOX_API FVeloxMapInfo
{
	GENERATED_BODY()

	/** The map asset ("/Game/Maps/DM1/L_DM1.L_DM1"). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Map", Meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath Map;

	/** The short name ("L_DM1"): what the URL and the heartbeat's `map` use. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Map")
	FString ShortName;

	/** What players read ("Praesidium"). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Map")
	FText DisplayName;

	/** Game modes the map is meant for (Velox.Mode.FFA, Velox.Mode.Duel). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Map")
	TArray<FGameplayTag> Modes;

	/** False for test maps: a public rotation and the browser's map filter skip them. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Map")
	bool bPublic = false;
};

UCLASS(Config = Game, DefaultConfig, Meta = (DisplayName = "Velox Maps"))
class VELOX_API UVeloxMapRegistry : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	//~UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return TEXT("Velox"); }
	//~End of UDeveloperSettings interface

	static const UVeloxMapRegistry& Get();

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Maps")
	TArray<FVeloxMapInfo> Maps;

	/** The entry of World's map (matched by short name, PIE prefix ignored), or null. */
	const FVeloxMapInfo* FindByWorld(const UWorld* World) const;
	/** Case-insensitive; accepts a short name or a package path. */
	const FVeloxMapInfo* FindByShortName(const FString& ShortName) const;

	/** "L_DM1" from a package or map name ("/Game/Maps/DM1/L_DM1", "/Memory/UEDPIE_0_L_DM1", "UEDPIE_0_L_DM1"). World-free. */
	static FString ShortNameOfPackage(const FString& PackageName);
	/** ShortNameOfPackage of the world's package (map name without a package). */
	static FString ShortNameOfWorld(const UWorld* World);
	/** The registry's display name for a short name, else the short name itself. */
	FString DisplayNameFor(const FString& ShortName) const;
};
