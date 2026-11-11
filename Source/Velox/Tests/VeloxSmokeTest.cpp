// Velox - core smoke test (ADR 0001 section 4).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/VeloxGameInstance.h"
#include "GameplayTagsManager.h"
#include "Misc/NetworkVersion.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCoreSmokeTest, "Velox.Core.Smoke",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxCoreSmokeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Velox module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("Velox")));
	TestNotNull(TEXT("UVeloxGameInstance class is registered"), UVeloxGameInstance::StaticClass());
	return true;
}

// B1 (M6-02b): the editor must not register native tags that a cooked build lacks (the engine's editor-only StateTreeTestSuite adds
// "Test.StateTree.*"; it is pulled in by StateTree, which GameplayCameras enables by default). Velox.uproject disables them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxTagTableParityTest, "Velox.Net.TagTableParity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxTagTableParityTest::RunTest(const FString& Parameters)
{
	const UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	const TArray<TSharedPtr<FGameplayTagNode>>& Nodes = Manager.GetNetworkGameplayTagNodeIndex();
	TestTrue(TEXT("tag table is not empty"), Nodes.Num() > 0);
	TestTrue(TEXT("tag net index hash is non-zero"), Manager.GetNetworkGameplayTagNodeIndexHash() != 0);
	int32 EditorOnly = 0;
	for (const TSharedPtr<FGameplayTagNode>& Node : Nodes)
	{
		if (Node.IsValid() && (Node->GetCompleteTagString() == TEXT("Test") || Node->GetCompleteTagString().StartsWith(TEXT("Test."))))
		{
			++EditorOnly;
			AddError(FString::Printf(TEXT("editor-only tag in the net index: %s (a cooked build lacks it, net indices would shift)"), *Node->GetCompleteTagString()));
		}
	}
	TestEqual(TEXT("no editor-only Test / Test.* tags"), EditorOnly, 0);
	return true;
}

// ADR 0007 F2 (ii)-b: the tag table hash is part of the network version, so a mismatched client is refused at NMT_Hello.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxTagHashInNetVersionTest, "Velox.Net.TagHashInNetVersion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxTagHashInNetVersionTest::RunTest(const FString& Parameters)
{
	UVeloxGameInstance::BindTagHashIntoNetworkVersion();
	TestTrue(TEXT("network version override is bound"), FNetworkVersion::GetLocalNetworkVersionOverride.IsBound());
	FNetworkVersion::InvalidateNetworkChecksum();
	const uint32 Base = FNetworkVersion::GetLocalNetworkVersion(false);
	FNetworkVersion::InvalidateNetworkChecksum();
	const uint32 Combined = FNetworkVersion::GetLocalNetworkVersion();
	TestTrue(TEXT("GetLocalNetworkVersion() differs from GetLocalNetworkVersion(false)"), Base != Combined);
	TestEqual(TEXT("combined = HashCombine(base, tag hash)"), Combined, HashCombine(Base, UGameplayTagsManager::Get().GetNetworkGameplayTagNodeIndexHash()));
	FNetworkVersion::InvalidateNetworkChecksum();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
