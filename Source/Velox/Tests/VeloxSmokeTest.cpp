// Velox - core smoke test (ADR 0001 section 4).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/VeloxGameInstance.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCoreSmokeTest, "Velox.Core.Smoke",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxCoreSmokeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Velox module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("Velox")));
	TestNotNull(TEXT("UVeloxGameInstance class is registered"), UVeloxGameInstance::StaticClass());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
