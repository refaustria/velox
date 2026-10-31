// Velox - map content tests (ADR 0006 section 5 / task m5-01): every shipped (non-test) map must carry baked lighting.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMapLightingBuiltTest, "Velox.Maps.LightingBuilt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace VeloxMapTests
{
	/** Maps that may stay unbaked: test maps, the menu backdrop, and blockouts. */
	static bool IsExemptMap(const FString& ShortName)
	{
		static const TCHAR* const Exempt[] = { TEXT("L_Empty"), TEXT("L_MovementTest"), TEXT("L_CombatTest"), TEXT("L_ItemTest"), TEXT("L_MainMenu") };
		for (const TCHAR* E : Exempt)
		{
			if (ShortName == E) { return true; }
		}
		return ShortName.EndsWith(TEXT("_Blockout"));
	}
}

bool FVeloxMapLightingBuiltTest::RunTest(const FString& Parameters)
{
	TArray<FString> Entries;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("MapsToCook"), Entries, GGameIni);

	int32 Checked = 0;
	for (const FString& Entry : Entries)
	{
		// Entry looks like (FilePath="/Game/Maps/L_DM1")
		int32 Q0 = INDEX_NONE;
		if (!Entry.FindChar(TEXT('"'), Q0)) { continue; }
		const int32 Q1 = Entry.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Q0 + 1);
		if (Q1 == INDEX_NONE) { continue; }
		const FString PackagePath = Entry.Mid(Q0 + 1, Q1 - Q0 - 1);
		if (VeloxMapTests::IsExemptMap(FPackageName::GetShortName(PackagePath))) { continue; }

		++Checked;
		UPackage* Package = LoadPackage(nullptr, *PackagePath, LOAD_None);
		UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s loads as a world"), *PackagePath), World)) { continue; }
		const ULevel* Level = World->PersistentLevel;
		TestTrue(*FString::Printf(TEXT("%s has baked lighting (UMapBuildDataRegistry)"), *PackagePath),
			Level != nullptr && Level->MapBuildData != nullptr);
	}
	AddInfo(FString::Printf(TEXT("checked %d shipped maps"), Checked));
	return true;
}

#endif
