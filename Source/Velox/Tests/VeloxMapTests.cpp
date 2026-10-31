// Velox - map content tests (ADR 0006 section 5, E0.1 (d) / tasks m5-01, m5-16): every shipped (non-test) map must carry real baked lighting:
// a registry, at least one static mesh component whose build data holds a lightmap, and the volumetric lightmap.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "StaticMeshComponentLODInfo.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMapLightingBuiltTest, "Velox.Maps.LightingBuilt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxMapDM1BudgetTest, "Velox.Maps.DM1Budget",
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

	TArray<FString> PackagePaths;
	for (const FString& Entry : Entries)
	{
		// Entry looks like (FilePath="/Game/Maps/L_DM1")
		int32 Q0 = INDEX_NONE;
		if (!Entry.FindChar(TEXT('"'), Q0)) { continue; }
		const int32 Q1 = Entry.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Q0 + 1);
		if (Q1 == INDEX_NONE) { continue; }
		const FString PackagePath = Entry.Mid(Q0 + 1, Q1 - Q0 - 1);
		if (VeloxMapTests::IsExemptMap(FPackageName::GetShortName(PackagePath))) { continue; }
		PackagePaths.AddUnique(PackagePath);
	}
	// Probe override used by Tools/editor/audit_map.py --lighting and the negative check of task m5-16: -VeloxLightingMap=/Game/Maps/X
	// checks that map in addition (exempt or not); it fails exactly like a shipped map that is not baked.
	FString OverrideMap;
	if (FParse::Value(FCommandLine::Get(), TEXT("VeloxLightingMap="), OverrideMap) && !OverrideMap.IsEmpty()) { PackagePaths.AddUnique(OverrideMap); }

	int32 Checked = 0;
	for (const FString& PackagePath : PackagePaths)
	{
		++Checked;
		UPackage* Package = LoadPackage(nullptr, *PackagePath, LOAD_None);
		UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s loads as a world"), *PackagePath), World)) { continue; }
		const ULevel* Level = World->PersistentLevel;
		const UMapBuildDataRegistry* Registry = Level ? Level->MapBuildData.Get() : nullptr;
		TestNotNull(*FString::Printf(TEXT("%s has a UMapBuildDataRegistry"), *PackagePath), Registry);

		int32 Components = 0, Lightmapped = 0;
		bool bVolumetric = false;
		for (const AActor* Actor : Level->Actors)
		{
			if (!Actor) { continue; }
			TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
			for (const UStaticMeshComponent* Comp : Meshes)
			{
				if (!Comp->GetStaticMesh() || Comp->Mobility != EComponentMobility::Static) { continue; }
				++Components;
				if (!Registry || Comp->LODData.Num() == 0) { continue; }
				const FMeshMapBuildData* Data = Registry->GetMeshBuildData(Comp->LODData[0].MapBuildDataId);
				if (Data && Data->LightMap.IsValid()) { ++Lightmapped; }
			}
		}
		if (Registry)
		{
			bVolumetric = Registry->GetLevelPrecomputedVolumetricLightmapBuildData(Level->LevelBuildDataId) != nullptr;
		}
		const FString Figures = FString::Printf(TEXT("%s registry=%d lightmapped=%d/%d vlm=%d"), *PackagePath, Registry ? 1 : 0, Lightmapped, Components, bVolumetric ? 1 : 0);
		AddInfo(Figures);
		UE_LOG(LogTemp, Display, TEXT("VeloxLightingBuilt %s"), *Figures);
		TestTrue(*FString::Printf(TEXT("%s has at least one lightmapped static mesh component (lightmapped=%d/%d)"), *PackagePath, Lightmapped, Components), Lightmapped >= 1);
		TestTrue(*FString::Printf(TEXT("%s has a volumetric lightmap (vlm=%d)"), *PackagePath, bVolumetric ? 1 : 0), bVolumetric);
	}
	AddInfo(FString::Printf(TEXT("checked %d shipped maps"), Checked));
	return true;
}

// Reads Saved/Audit/L_DM1.json written by Tools/editor/audit_map.py (ADR 0006 section 8). Budgets do not depend on the bake, so the
// unbaked map passes. Skips cleanly when the file is absent (the audit has not been run on this checkout).
bool FVeloxMapDM1BudgetTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/Audit/L_DM1.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		AddInfo(FString::Printf(TEXT("skipped: %s not found (run Tools/editor/audit_map.py /Game/Maps/DM1/L_DM1)"), *Path));
		return true;
	}
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("audit JSON parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())) { return false; }

	const TSharedPtr<FJsonObject>* Budget = nullptr;
	const TSharedPtr<FJsonObject>* Fig = nullptr;
	if (!TestTrue(TEXT("audit JSON has budget and figures"), Root->TryGetObjectField(TEXT("budget"), Budget) && Root->TryGetObjectField(TEXT("figures"), Fig))) { return false; }

	// Every figure with a budget of the same name must be within it.
	static const TCHAR* const Keys[] = { TEXT("draw_calls_est"), TEXT("triangles_total"), TEXT("lightmap_mb"), TEXT("texture_mb"), TEXT("lm_gt64"), TEXT("kit_meshes"), TEXT("translucent") };
	for (const TCHAR* Key : Keys)
	{
		double Value = 0.0, Limit = 0.0;
		if (!TestTrue(*FString::Printf(TEXT("figure %s present"), Key), (*Fig)->TryGetNumberField(Key, Value) && (*Budget)->TryGetNumberField(Key, Limit))) { continue; }
		TestTrue(*FString::Printf(TEXT("%s %.2f <= budget %.2f"), Key, Value, Limit), Value <= Limit);
	}
	const TArray<TSharedPtr<FJsonValue>>* Breaches = nullptr;
	if (Root->TryGetArrayField(TEXT("breaches"), Breaches))
	{
		for (const TSharedPtr<FJsonValue>& B : *Breaches) { AddError(FString::Printf(TEXT("audit breach: %s"), *B->AsString())); }
	}
	AddInfo(TEXT("L_DM1 budget JSON checked"));
	return true;
}

#endif
