// Velox - view model tests (ADR 0006 section 4.6, task M5-09).
//   Velox.Character.ViewModelBob       world-free: bob is zero airborne and below the speed floor, periodic in distance (not
//                                      time), recoil decays below 1 % within RecoilReturnSeconds, sway is clamped and
//                                      returns to rest, the raise/lower alpha follows the drop and the raise.
//   Velox.Core.WeaponCosmeticsSoftRefs every object reference of UVeloxWeaponCosmetics / UVeloxViewModelSettings and the
//                                      definition's Cosmetics field is soft (reflection over the class properties).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/VeloxViewModelMath.h"
#include "Core/VeloxViewModelSettings.h"
#include "Core/VeloxWeaponCosmetics.h"
#include "UObject/UnrealType.h"
#include "Weapons/VeloxWeaponDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxViewModelBobTest, "Velox.Character.ViewModelBob",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxViewModelBobTest::RunTest(const FString& Parameters)
{
	const UVeloxViewModelSettings& S = *GetDefault<UVeloxViewModelSettings>();

	// Airborne or slow: no bob, the phase stands still.
	TestEqual(TEXT("airborne bob is zero"), VeloxViewModelMath::BobOffset(0.25, 600.f, false, S), FVector::ZeroVector);
	TestEqual(TEXT("slow bob is zero"), VeloxViewModelMath::BobOffset(0.25, S.BobMinSpeed - 1.f, true, S), FVector::ZeroVector);
	TestEqual(TEXT("airborne phase stands still"), VeloxViewModelMath::AdvanceBobPhase(1.5, 600.f, false, 0.1f, S), 1.5);
	TestEqual(TEXT("slow phase stands still"), VeloxViewModelMath::AdvanceBobPhase(1.5, S.BobMinSpeed - 1.f, true, 0.1f, S), 1.5);

	// Distance-driven: the same distance gives the same phase at different speeds and frame times.
	double Fast = 0.0;
	double Slow = 0.0;
	for (int32 i = 0; i < 100; ++i)
	{
		Fast = VeloxViewModelMath::AdvanceBobPhase(Fast, 800.f, true, 0.01f, S); // 8 m
	}
	for (int32 i = 0; i < 100; ++i)
	{
		Slow = VeloxViewModelMath::AdvanceBobPhase(Slow, 400.f, true, 0.02f, S); // 8 m
	}
	TestNearlyEqual(TEXT("phase depends on distance only"), static_cast<float>(Fast), static_cast<float>(Slow), 1e-3f);
	TestNearlyEqual(TEXT("one cycle per BobCycleCm"), static_cast<float>(VeloxViewModelMath::AdvanceBobPhase(0.0, 520.f, true, 0.5f, S)), 260.f / S.BobCycleCm, 1e-4f);

	// Periodic: offset after one cycle of distance equals the start.
	const FVector A = VeloxViewModelMath::BobOffset(0.3, 600.f, true, S);
	const FVector B = VeloxViewModelMath::BobOffset(2.3, 600.f, true, S);
	TestTrue(TEXT("periodic over whole cycles"), A.Equals(B, 1e-3));
	TestTrue(TEXT("bob moves on the ground"), !VeloxViewModelMath::BobOffset(0.25, 600.f, true, S).IsNearlyZero());
	TestTrue(TEXT("bob stays within the amplitude"), FMath::Abs(VeloxViewModelMath::BobOffset(0.25, 600.f, true, S).Z) <= S.BobAmplitudeCm + 1e-4f);

	// Recoil: an impulse is below 1 % after RecoilReturnSeconds, also a stacked one.
	float Recoil = VeloxViewModelMath::AddRecoilImpulse(0.f);
	TestEqual(TEXT("impulse strength"), Recoil, 1.f);
	float Stacked = VeloxViewModelMath::AddRecoilImpulse(VeloxViewModelMath::AddRecoilImpulse(VeloxViewModelMath::AddRecoilImpulse(0.f)));
	TestTrue(TEXT("stacked impulses saturate"), Stacked <= VeloxViewModelMath::MaxRecoilStrength);
	const int32 Steps = 30;
	for (int32 i = 0; i < Steps; ++i)
	{
		Recoil = VeloxViewModelMath::DecayRecoil(Recoil, S.RecoilReturnSeconds / Steps, S);
		Stacked = VeloxViewModelMath::DecayRecoil(Stacked, S.RecoilReturnSeconds / Steps, S);
	}
	TestTrue(TEXT("recoil below 1 % after RecoilReturnSeconds"), Recoil < 0.01f);
	TestTrue(TEXT("stacked recoil below 1 % x peak after RecoilReturnSeconds"), Stacked < 0.01f * VeloxViewModelMath::MaxRecoilStrength);

	// Sway: opposes the turn, clamped, returns to rest.
	VeloxViewModelMath::FSwayState Sway;
	VeloxViewModelMath::StepSway(Sway, FVector2D(10.0, 0.0), 0.016f, S);
	TestTrue(TEXT("sway lags a right turn (negative yaw)"), Sway.Angles.X < 0.0);
	VeloxViewModelMath::StepSway(Sway, FVector2D(1000.0, -1000.0), 0.016f, S);
	TestTrue(TEXT("sway yaw clamped"), FMath::Abs(Sway.Angles.X) <= S.SwayMaxDeg + 1e-4);
	TestTrue(TEXT("sway pitch clamped"), FMath::Abs(Sway.Angles.Y) <= S.SwayMaxDeg + 1e-4);
	const FVector Offset = VeloxViewModelMath::SwayOffset(Sway, S);
	TestTrue(TEXT("sway offset clamped"), FMath::Abs(Offset.Y) <= S.SwayMaxCm + 1e-4 && FMath::Abs(Offset.Z) <= S.SwayMaxCm + 1e-4);
	for (int32 i = 0; i < 100; ++i)
	{
		VeloxViewModelMath::StepSway(Sway, FVector2D::ZeroVector, 0.016f, S);
	}
	TestTrue(TEXT("sway returns to rest"), Sway.Angles.Size() < 0.01);

	// Raise / lower: down over the drop, up over the raise, ready otherwise.
	TestEqual(TEXT("ready"), VeloxViewModelMath::LowerAlpha(false, 0.f, 0.2f, 0.f, 0.25f), 0.f);
	TestEqual(TEXT("drop start"), VeloxViewModelMath::LowerAlpha(true, 0.2f, 0.2f, 0.5f, 0.25f), 0.f);
	TestNearlyEqual(TEXT("drop half"), VeloxViewModelMath::LowerAlpha(true, 0.1f, 0.2f, 0.4f, 0.25f), 0.5f, 1e-4f);
	TestEqual(TEXT("drop end"), VeloxViewModelMath::LowerAlpha(true, 0.f, 0.2f, 0.45f, 0.25f), 1.f);
	TestEqual(TEXT("raise start"), VeloxViewModelMath::LowerAlpha(false, 0.f, 0.2f, 0.25f, 0.25f), 1.f);
	TestNearlyEqual(TEXT("raise half"), VeloxViewModelMath::LowerAlpha(false, 0.f, 0.2f, 0.125f, 0.25f), 0.5f, 1e-4f);
	TestEqual(TEXT("raise end"), VeloxViewModelMath::LowerAlpha(false, 0.f, 0.2f, 0.f, 0.25f), 0.f);

	// Shipped defaults of the task.
	TestEqual(TEXT("first person FOV"), S.FirstPersonFieldOfView, 70.f);
	TestEqual(TEXT("first person scale"), S.FirstPersonScale, 0.6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxWeaponCosmeticsSoftRefsTest, "Velox.Core.WeaponCosmeticsSoftRefs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxWeaponCosmeticsSoftRefsTest::RunTest(const FString& Parameters)
{
	const UClass* Classes[] = { UVeloxWeaponCosmetics::StaticClass(), UVeloxViewModelSettings::StaticClass() };
	for (const UClass* Class : Classes)
	{
		for (TFieldIterator<FProperty> It(Class, EFieldIterationFlags::None); It; ++It)
		{
			const bool bHardObject = It->IsA<FObjectProperty>() && !It->IsA<FSoftObjectProperty>() && !It->IsA<FClassProperty>();
			TestFalse(*FString::Printf(TEXT("%s.%s is not a hard object reference"), *Class->GetName(), *It->GetName()), bHardObject);
			TestFalse(*FString::Printf(TEXT("%s.%s is not a hard class reference"), *Class->GetName(), *It->GetName()), It->IsA<FClassProperty>());
		}
	}
	const FProperty* Cosmetics = UVeloxWeaponDefinition::StaticClass()->FindPropertyByName(TEXT("Cosmetics"));
	TestTrue(TEXT("UVeloxWeaponDefinition::Cosmetics is a soft object reference"), Cosmetics && Cosmetics->IsA<FSoftObjectProperty>());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
