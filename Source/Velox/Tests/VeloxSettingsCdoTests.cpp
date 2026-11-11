// Velox - settings CDOs must not hold runtime-loaded objects (task M6-01b).
//
// In a cooked build the class default objects of native classes live in the Disregard-for-GC pool; GC does not trace them,
// and Development builds die at the first GC if one of them references an object outside that pool and the root set
// (VerifyGCAssumptions). The editor has no disregard pool, so only a packaged run would show it. This test makes the
// pattern visible in the editor:
//   1. static: no UDeveloperSettings subclass of the Velox module has a non-Config property that can hold a strong
//      object reference (Config references are resolved while the CDO is built, inside the disregard window; anything
//      else is filled at runtime);
//   2. dynamic: after the audio preload ran, no Velox settings CDO references an object outside /Script/ packages, and
//      the preloaded assets are held by UVeloxAudioPreloadSubsystem instead.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/VeloxAudioPreloadSubsystem.h"
#include "Core/VeloxAudioSettings.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace VeloxSettingsCdoTestsPrivate
{
	const TCHAR* VeloxScriptPackage = TEXT("/Script/Velox");

	TArray<UClass*> VeloxSettingsClasses()
	{
		TArray<UClass*> Classes;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (Class->IsChildOf(UDeveloperSettings::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
				&& Class->GetOutermost()->GetName() == VeloxScriptPackage)
			{
				Classes.Add(Class);
			}
		}
		return Classes;
	}

	bool IsScriptObject(const UObject* Object)
	{
		return Object && Object->GetOutermost()->GetName().StartsWith(TEXT("/Script/"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxSettingsCdoNoRuntimeRefsTest, "Velox.Core.SettingsCdoNoRuntimeRefs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxSettingsCdoNoRuntimeRefsTest::RunTest(const FString& Parameters)
{
	using namespace VeloxSettingsCdoTestsPrivate;
	const TArray<UClass*> Classes = VeloxSettingsClasses();
	TestTrue(TEXT("the Velox module has settings classes (audio among them)"), Classes.Contains(UVeloxAudioSettings::StaticClass()));

	// 1. Static: no runtime-fillable strong object property on any Velox settings class.
	for (const UClass* Class : Classes)
	{
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			const FProperty* Property = *It;
			const UClass* Owner = Property->GetOwnerClass();
			if (!Owner || Owner->GetOutermost()->GetName() != VeloxScriptPackage || Property->HasAnyPropertyFlags(CPF_Config))
			{
				continue;
			}
			TArray<const FStructProperty*> Encountered;
			if (Property->ContainsObjectReference(Encountered, EPropertyObjectReferenceType::Strong))
			{
				AddError(FString::Printf(TEXT("%s::%s is a non-Config strong object reference on a settings class: its CDO is Disregard-for-GC "
					"in cooked builds; hold runtime objects in a subsystem (see UVeloxAudioPreloadSubsystem)"), *Class->GetName(), *Property->GetName()));
			}
		}
	}

	// 2. Dynamic: run the preload, then no settings CDO may reference a content object.
	UVeloxAudioPreloadSubsystem* Preload = UVeloxAudioPreloadSubsystem::Get();
	if (!TestNotNull(TEXT("UVeloxAudioPreloadSubsystem exists on GEngine"), Preload))
	{
		return false;
	}
	Preload->PreloadOnce(UVeloxAudioSettings::Get());
	TArray<FSoftObjectPath> Paths;
	UVeloxAudioSettings::Get().GetPreloadPaths(Paths);
	TestTrue(TEXT("the audio settings name preload assets"), Paths.Num() > 0);
	TestTrue(TEXT("the subsystem holds the preloaded assets"), Preload->GetLoadedAssets().Num() > 0);
	for (const UClass* Class : Classes)
	{
		UObject* Cdo = Class->GetDefaultObject();
		TArray<UObject*> Referenced;
		FReferenceFinder Finder(Referenced, nullptr, false, true);
		Finder.FindReferences(Cdo);
		for (const UObject* Object : Referenced)
		{
			if (Object && Object != Cdo && !IsScriptObject(Object))
			{
				AddError(FString::Printf(TEXT("%s CDO references %s (not a /Script/ object): Disregard-for-GC violation in a cooked build"),
					*Class->GetName(), *Object->GetPathName()));
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
