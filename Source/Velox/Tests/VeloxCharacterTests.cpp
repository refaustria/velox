// Velox - character cosmetics and animation tests (ADR 0006 section 10).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/VeloxAnimState.h"
#include "Combat/VeloxCombatSettings.h"
#include "Core/VeloxCharacterCosmetics.h"
#include "Core/VeloxPawnData.h"
#include "Engine/DataAsset.h"
#include "UI/VeloxGameUserSettings.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

// ---------------------------------------------------------------- Task 08: leg states, soft references, skins

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxAnimStateTest, "Velox.Character.AnimState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxAnimStateTest::RunTest(const FString& Parameters)
{
	using namespace VeloxAnimState;
	const FVeloxAnimTuning Tuning;
	auto Ground = [](float Speed, float DirectionDeg = 0.f, bool bCrouching = false)
	{
		FVeloxAnimInput Input;
		Input.Speed2D = Speed;
		Input.DirectionDeg = DirectionDeg;
		Input.bCrouching = bCrouching;
		return Input;
	};
	auto Airborne = [](float VerticalSpeed, bool bCrouching = false)
	{
		FVeloxAnimInput Input;
		Input.Speed2D = 600.f;
		Input.VerticalSpeed = VerticalSpeed;
		Input.bAirborne = true;
		Input.bCrouching = bCrouching;
		return Input;
	};

	// Idle -> run at the threshold.
	{
		FVeloxAnimStateData S;
		TestTrue(TEXT("first step reports a change"), Step(Ground(0.f), S, 0.016f));
		TestEqual(TEXT("idle at rest"), S.LegState, EVeloxLegState::Idle);
		TestFalse(TEXT("no change while idle"), Step(Ground(99.f), S, 0.016f));
		TestEqual(TEXT("idle below threshold"), S.LegState, EVeloxLegState::Idle);
		TestTrue(TEXT("change to run"), Step(Ground(Tuning.MoveSpeedThreshold), S, 0.016f));
		TestEqual(TEXT("run at threshold"), S.LegState, EVeloxLegState::Run);
		TestEqual(TEXT("run back to idle"), (Step(Ground(50.f), S, 0.016f), S.LegState), EVeloxLegState::Idle);
	}

	// Run -> air (walked off a ledge) -> land -> run; jump start timing.
	{
		FVeloxAnimStateData S;
		Step(Ground(600.f), S, 0.016f);
		Step(Airborne(-10.f), S, 0.016f);
		TestEqual(TEXT("walk off a ledge: air"), S.LegState, EVeloxLegState::Air);
		Step(Ground(600.f), S, 0.016f);
		TestEqual(TEXT("touch down: land"), S.LegState, EVeloxLegState::Land);
		Step(Ground(600.f), S, 0.15f);
		TestEqual(TEXT("land holds 0.15 s"), S.LegState, EVeloxLegState::Land);
		Step(Ground(600.f), S, 0.1f);
		TestEqual(TEXT("land over after 0.2 s: run"), S.LegState, EVeloxLegState::Run);

		Step(Airborne(450.f), S, 0.016f);
		TestEqual(TEXT("jump: jump start"), S.LegState, EVeloxLegState::JumpStart);
		Step(Airborne(400.f), S, 0.1f);
		TestEqual(TEXT("jump start holds 0.1 s"), S.LegState, EVeloxLegState::JumpStart);
		Step(Airborne(350.f), S, 0.1f);
		TestEqual(TEXT("jump start over after 0.15 s: air"), S.LegState, EVeloxLegState::Air);
		Step(Ground(0.f), S, 0.016f);
		TestEqual(TEXT("land"), S.LegState, EVeloxLegState::Land);
		Step(Airborne(450.f), S, 0.016f);
		TestEqual(TEXT("bunny hop out of land: jump start"), S.LegState, EVeloxLegState::JumpStart);
		Step(Ground(0.f), S, 0.016f);
		Step(Ground(0.f), S, 0.25f);
		TestEqual(TEXT("land then idle at rest"), S.LegState, EVeloxLegState::Idle);
	}

	// Crouch variants.
	{
		FVeloxAnimStateData S;
		Step(Ground(0.f, 0.f, true), S, 0.016f);
		TestEqual(TEXT("crouch idle"), S.LegState, EVeloxLegState::CrouchIdle);
		Step(Ground(200.f, 0.f, true), S, 0.016f);
		TestEqual(TEXT("crouch walk"), S.LegState, EVeloxLegState::CrouchWalk);
		TestNearlyEqual(TEXT("crouch walk cadence"), S.PlayRate, ComputePlayRate(200.f));
		Step(Airborne(400.f, true), S, 0.016f);
		Step(Ground(200.f, 0.f, true), S, 0.016f);
		TestEqual(TEXT("crouched landing skips land"), S.LegState, EVeloxLegState::CrouchWalk);
		Step(Airborne(-5.f), S, 0.016f);
		Step(Ground(0.f), S, 0.016f);
		TestEqual(TEXT("land"), S.LegState, EVeloxLegState::Land);
		Step(Ground(0.f, 0.f, true), S, 0.016f);
		TestEqual(TEXT("crouching cuts land short"), S.LegState, EVeloxLegState::CrouchIdle);
	}

	// Dead is sticky.
	{
		FVeloxAnimStateData S;
		Step(Ground(600.f), S, 0.016f);
		FVeloxAnimInput Dead = Ground(600.f);
		Dead.bDead = true;
		TestTrue(TEXT("death reports a change"), Step(Dead, S, 0.016f));
		TestEqual(TEXT("dead"), S.LegState, EVeloxLegState::Dead);
		TestFalse(TEXT("dead: no change on movement"), Step(Ground(800.f), S, 0.016f));
		Step(Airborne(500.f), S, 1.f);
		TestEqual(TEXT("dead stays dead"), S.LegState, EVeloxLegState::Dead);
		TestFalse(TEXT("dead does not loop"), IsLooping(EVeloxLegState::Dead));
		TestTrue(TEXT("run loops"), IsLooping(EVeloxLegState::Run));
		TestFalse(TEXT("land plays once"), IsLooping(EVeloxLegState::Land));
	}

	// Play rate = clamp(Speed / 800, 0.5, 1.6).
	{
		TestNearlyEqual(TEXT("rate floor"), ComputePlayRate(100.f), 0.5f);
		TestNearlyEqual(TEXT("rate 400"), ComputePlayRate(400.f), 0.5f);
		TestNearlyEqual(TEXT("rate 800"), ComputePlayRate(800.f), 1.f);
		TestNearlyEqual(TEXT("rate 1000"), ComputePlayRate(1000.f), 1.25f);
		TestNearlyEqual(TEXT("rate ceiling"), ComputePlayRate(2000.f), 1.6f);
		FVeloxAnimStateData S;
		Step(Ground(1000.f), S, 0.016f);
		TestNearlyEqual(TEXT("run state carries the rate"), S.PlayRate, 1.25f);
		Step(Ground(0.f), S, 0.016f);
		TestNearlyEqual(TEXT("idle rate 1"), S.PlayRate, 1.f);
	}

	// Direction quadrants.
	{
		TestEqual(TEXT("0 fwd"), PickRunDirection(0.f), EVeloxRunDirection::Fwd);
		TestEqual(TEXT("45 fwd"), PickRunDirection(45.f), EVeloxRunDirection::Fwd);
		TestEqual(TEXT("-45 fwd"), PickRunDirection(-45.f), EVeloxRunDirection::Fwd);
		TestEqual(TEXT("46 right"), PickRunDirection(46.f), EVeloxRunDirection::Right);
		TestEqual(TEXT("90 right"), PickRunDirection(90.f), EVeloxRunDirection::Right);
		TestEqual(TEXT("134 right"), PickRunDirection(134.f), EVeloxRunDirection::Right);
		TestEqual(TEXT("135 back"), PickRunDirection(135.f), EVeloxRunDirection::Back);
		TestEqual(TEXT("180 back"), PickRunDirection(180.f), EVeloxRunDirection::Back);
		TestEqual(TEXT("-180 back"), PickRunDirection(-180.f), EVeloxRunDirection::Back);
		TestEqual(TEXT("-90 left"), PickRunDirection(-90.f), EVeloxRunDirection::Left);
		TestEqual(TEXT("270 wraps to left"), PickRunDirection(270.f), EVeloxRunDirection::Left);

		TestNearlyEqual(TEXT("+Y at yaw 0 is right"), ComputeDirectionDeg(FVector(0.0, 100.0, 0.0), 0.f), 90.f, 1e-3f);
		TestNearlyEqual(TEXT("+X at yaw 90 is left"), ComputeDirectionDeg(FVector(100.0, 0.0, 0.0), 90.f), -90.f, 1e-3f);
		TestNearlyEqual(TEXT("-X at yaw 0 is back"), FMath::Abs(ComputeDirectionDeg(FVector(-100.0, 0.0, 0.0), 0.f)), 180.f, 1e-3f);
		TestNearlyEqual(TEXT("at rest 0"), ComputeDirectionDeg(FVector(0.0, 0.0, -300.0), 37.f), 0.f);

		FVeloxAnimStateData S;
		Step(Ground(600.f, 0.f), S, 0.016f);
		TestEqual(TEXT("running forward"), S.RunDirection, EVeloxRunDirection::Fwd);
		TestFalse(TEXT("same quadrant: no change"), Step(Ground(600.f, 20.f), S, 0.016f));
		TestTrue(TEXT("quadrant change while running is a change"), Step(Ground(600.f, 100.f), S, 0.016f));
		TestEqual(TEXT("running right"), S.RunDirection, EVeloxRunDirection::Right);
	}
	return true;
}

namespace VeloxCosmeticsTestPrivate
{
	/** Appends "<path> (<type>)" for every object reference in Property that is not a soft pointer. */
	void FindHardReferences(const FProperty* Property, const FString& Path, TArray<FString>& Out)
	{
		if (CastField<FSoftObjectProperty>(Property))
		{
			return; // TSoftObjectPtr and TSoftClassPtr (FSoftClassProperty derives from it)
		}
		if (CastField<FObjectPropertyBase>(Property) || CastField<FInterfaceProperty>(Property))
		{
			Out.Add(FString::Printf(TEXT("%s (%s)"), *Path, *Property->GetCPPType()));
			return;
		}
		if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			FindHardReferences(Array->Inner, Path + TEXT("[]"), Out);
		}
		else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
		{
			FindHardReferences(Set->ElementProp, Path + TEXT("{}"), Out);
		}
		else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
		{
			FindHardReferences(Map->KeyProp, Path + TEXT(".Key"), Out);
			FindHardReferences(Map->ValueProp, Path + TEXT(".Value"), Out);
		}
		else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				FindHardReferences(*It, Path + TEXT(".") + It->GetName(), Out);
			}
		}
	}

	/** Every property the class declares below UPrimaryDataAsset (engine base properties are not ours to judge). */
	TArray<FString> FindHardReferences(const UClass* Class)
	{
		TArray<FString> Out;
		for (const UClass* Level = Class; Level && Level != UPrimaryDataAsset::StaticClass() && Level != UDataAsset::StaticClass(); Level = Level->GetSuperClass())
		{
			for (TFieldIterator<FProperty> It(Level, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				FindHardReferences(*It, Level->GetName() + TEXT(".") + It->GetName(), Out);
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCosmeticsSoftRefsTest, "Velox.Core.CosmeticsSoftRefs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxCosmeticsSoftRefsTest::RunTest(const FString& Parameters)
{
	using namespace VeloxCosmeticsTestPrivate;

	// The walker must see a hard pointer where there is one (pawn data holds TObjectPtr settings).
	TestTrue(TEXT("walker finds hard pointers (UVeloxPawnData)"), FindHardReferences(UVeloxPawnData::StaticClass()).Num() > 0);
	const FProperty* PawnCosmetics = UVeloxPawnData::StaticClass()->FindPropertyByName(TEXT("Cosmetics"));
	TestTrue(TEXT("UVeloxPawnData::Cosmetics is soft"), PawnCosmetics && CastField<FSoftObjectProperty>(PawnCosmetics) != nullptr);

	// Every Velox cosmetics data class (UVeloxCharacterCosmetics, UVeloxWeaponCosmetics, and any later *Cosmetics class).
	int32 NumClasses = 0;
	bool bSawCharacter = false;
	bool bSawWeapon = false;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		const UClass* Class = *It;
		if (!Class->IsChildOf(UDataAsset::StaticClass()) || !Class->GetName().StartsWith(TEXT("Velox")) || !Class->GetName().EndsWith(TEXT("Cosmetics"))
			|| Class->HasAnyClassFlags(CLASS_NewerVersionExists) || Class->GetOutermost() != UVeloxCharacterCosmetics::StaticClass()->GetOutermost())
		{
			continue;
		}
		++NumClasses;
		bSawCharacter |= Class == UVeloxCharacterCosmetics::StaticClass();
		bSawWeapon |= Class->GetName() == TEXT("VeloxWeaponCosmetics");
		for (const FString& Hard : FindHardReferences(Class))
		{
			AddError(FString::Printf(TEXT("hard object reference in cosmetics data: %s"), *Hard));
		}
		AddInfo(FString::Printf(TEXT("checked %s"), *Class->GetName()));
	}
	TestTrue(TEXT("UVeloxCharacterCosmetics checked"), bSawCharacter);
	TestTrue(TEXT("UVeloxWeaponCosmetics checked"), bSawWeapon);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxSkinChoiceTest, "Velox.Character.SkinChoice",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxSkinChoiceTest::RunTest(const FString& Parameters)
{
	const UVeloxCombatSettings& Combat = *GetDefault<UVeloxCombatSettings>();
	const TArray<FColor>& Palette = Combat.PlayerColorPalette;
	if (!TestTrue(TEXT("palette has at least six colours"), Palette.Num() >= 6))
	{
		return false;
	}
	for (const FColor& Color : Palette)
	{
		TestEqual(TEXT("sRGB round trip of the palette is exact"), FLinearColor::FromSRGBColor(Color).ToFColorSRGB(), Color);
	}

	UVeloxCharacterCosmetics* Cosmetics = NewObject<UVeloxCharacterCosmetics>(GetTransientPackage());
	const TCHAR* Names[] = { TEXT("Red"), TEXT("Blue"), TEXT("Green"), TEXT("Yellow"), TEXT("Magenta"), TEXT("Cyan") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		FVeloxSkin& Skin = Cosmetics->Skins.AddDefaulted_GetRef();
		Skin.Name = Names[Index];
		Skin.BodyColor = FLinearColor::FromSRGBColor(Palette[Index]);
	}
	constexpr uint8 NoSkin = 255;
	const FName Green(TEXT("Green"));
	TestEqual(TEXT("own pawn: own skin"), UVeloxCharacterCosmetics::ChooseSkinSlot(Cosmetics, true, true, Green, 3, NoSkin), 3);
	TestEqual(TEXT("enemy, forcing on: Green"), UVeloxCharacterCosmetics::ChooseSkinSlot(Cosmetics, false, true, Green, 3, NoSkin), 2);
	TestEqual(TEXT("enemy, forcing on, no skin index yet: Green"), UVeloxCharacterCosmetics::ChooseSkinSlot(Cosmetics, false, true, Green, NoSkin, NoSkin), 2);
	TestEqual(TEXT("enemy, forcing off: own skin"), UVeloxCharacterCosmetics::ChooseSkinSlot(Cosmetics, false, false, Green, 4, NoSkin), 4);
	TestEqual(TEXT("unknown forced name: own skin"), UVeloxCharacterCosmetics::ChooseSkinSlot(Cosmetics, false, true, TEXT("Plaid"), 1, NoSkin), 1);
	TestEqual(TEXT("index past the list wraps"), UVeloxCharacterCosmetics::ChooseSkinSlot(Cosmetics, true, true, Green, 7, NoSkin), 1);
	TestEqual(TEXT("no index, not forced: none"), UVeloxCharacterCosmetics::ChooseSkinSlot(Cosmetics, true, true, Green, NoSkin, NoSkin), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("no cosmetics: none"), UVeloxCharacterCosmetics::ChooseSkinSlot(nullptr, false, true, Green, 1, NoSkin), static_cast<int32>(INDEX_NONE));

	for (int32 Index = 0; Index < Palette.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("body colour %d equals the palette (HUD colours stable)"), Index),
			UVeloxCharacterCosmetics::ResolveBodyColor(Cosmetics, static_cast<uint8>(Index), Palette), Palette[Index]);
	}
	TestEqual(TEXT("no cosmetics: palette"), UVeloxCharacterCosmetics::ResolveBodyColor(nullptr, 2, Palette), Palette[2]);
	TestEqual(TEXT("unassigned: white"), UVeloxCharacterCosmetics::ResolveBodyColor(Cosmetics, NoSkin, Palette), FColor::White);

	const UVeloxGameUserSettings& UserDefaults = *GetDefault<UVeloxGameUserSettings>();
	TestTrue(TEXT("enemy skin forcing on by default"), UserDefaults.bForceEnemySkin);
	TestEqual(TEXT("forced enemy skin defaults to Green"), UVeloxGameUserSettings::DefaultForcedEnemySkin(), Green);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCosmeticsContentTest, "Velox.Character.CosmeticsContent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxCosmeticsContentTest::RunTest(const FString& Parameters)
{
	// Content written by Tools/editor/create_cosmetics_content.py.
	const UVeloxPawnData* PawnData = LoadObject<UVeloxPawnData>(nullptr, TEXT("/Game/Data/DA_PawnData_Default.DA_PawnData_Default"));
	const UVeloxCharacterCosmetics* Cosmetics = LoadObject<UVeloxCharacterCosmetics>(nullptr, TEXT("/Game/Characters/Velox/DA_Cosmetics_Velox.DA_Cosmetics_Velox"));
	if (!TestNotNull(TEXT("DA_PawnData_Default"), PawnData) || !TestNotNull(TEXT("DA_Cosmetics_Velox"), Cosmetics))
	{
		return false;
	}
	TestEqual(TEXT("pawn data points at DA_Cosmetics_Velox"), PawnData->Cosmetics.ToSoftObjectPath(), FSoftObjectPath(Cosmetics));
	const TArray<FColor>& Palette = GetDefault<UVeloxCombatSettings>()->PlayerColorPalette;
	const TCHAR* Names[] = { TEXT("Red"), TEXT("Blue"), TEXT("Green"), TEXT("Yellow"), TEXT("Magenta"), TEXT("Cyan") };
	if (TestEqual(TEXT("six skins"), Cosmetics->Skins.Num(), static_cast<int32>(UE_ARRAY_COUNT(Names))))
	{
		for (int32 Index = 0; Index < Cosmetics->Skins.Num(); ++Index)
		{
			TestEqual(TEXT("skin name"), Cosmetics->Skins[Index].Name, FName(Names[Index]));
			TestEqual(*FString::Printf(TEXT("skin %d colour = palette %d"), Index, Index), Cosmetics->Skins[Index].BodyColor.ToFColorSRGB(), Palette[Index]);
		}
	}
	TestTrue(TEXT("forced enemy skin exists"), Cosmetics->FindSkin(UVeloxGameUserSettings::DefaultForcedEnemySkin()) != INDEX_NONE);
	TestEqual(TEXT("mesh offset -70"), Cosmetics->MeshOffsetZ, -70.f);
	TestEqual(TEXT("mesh yaw -90"), Cosmetics->MeshYawDeg, -90.f);
	TestTrue(TEXT("sequence playback until ABP_Velox exists"), Cosmetics->bUseSequencePlayback);
	TestEqual(TEXT("view model settings = task 09's default asset"), Cosmetics->ViewModelSettings.ToSoftObjectPath(),
		FSoftObjectPath(TEXT("/Game/Characters/Velox/DA_ViewModelSettings_Default.DA_ViewModelSettings_Default")));
	return true;
}

// ---------------------------------------------------------------- end task 08

#endif // WITH_DEV_AUTOMATION_TESTS
