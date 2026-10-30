// Velox - project-wide bot settings (ADR 0005 section 11).

#include "Bots/VeloxBotSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Bots/VeloxBotDifficulty.h"
#include "Core/VeloxLog.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxBotSettings)

UVeloxBotSettings::UVeloxBotSettings()
{
	// Latin, on theme (ADR 0005 section 11); DefaultGame.ini carries the same list.
	BotNames = { TEXT("Celer"), TEXT("Rapidus"), TEXT("Fulgur"), TEXT("Ventus"), TEXT("Sagitta"), TEXT("Aquila"), TEXT("Ictus"), TEXT("Impetus"),
		TEXT("Cursor"), TEXT("Volucer"), TEXT("Turbo"), TEXT("Procella"), TEXT("Fulmen"), TEXT("Saltus"), TEXT("Praeceps"), TEXT("Nimbus") };
	DefaultDifficulty = TSoftObjectPtr<UVeloxBotDifficulty>(FSoftObjectPath(TEXT("/Game/Bots/DA_Bot_Medium.DA_Bot_Medium")));
}

const UVeloxBotSettings& UVeloxBotSettings::Get()
{
	return *GetDefault<UVeloxBotSettings>();
}

int32 UVeloxBotSettings::ListPresets()
{
	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	Module.Get().GetAssetsByPath(FName(TEXT("/Game/Bots")), Assets, true);
	int32 Count = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetClassPath == UVeloxBotDifficulty::StaticClass()->GetClassPathName())
		{
			UE_LOG(LogVelox, Log, TEXT("VeloxBotPreset asset=%s"), *Asset.GetObjectPathString());
			++Count;
		}
	}
	const UVeloxBotSettings& Settings = Get();
	UE_LOG(LogVelox, Log, TEXT("VeloxBotPreset count=%d names=%d default=%s"), Count, Settings.BotNames.Num(), *Settings.DefaultDifficulty.ToString());
	return Count;
}

static FAutoConsoleCommand GVeloxBotListPresetsCommand(TEXT("velox.bots.ListPresets"),
	TEXT("Logs the UVeloxBotDifficulty assets under /Game/Bots found by the asset registry."),
	FConsoleCommandDelegate::CreateLambda([]() { UVeloxBotSettings::ListPresets(); }));
