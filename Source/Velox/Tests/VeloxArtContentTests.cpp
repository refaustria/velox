// Velox - art content tests (ADR 0006 sections 4.6 and 8, task M5-13): the imported weapon and item meshes meet their budgets and are wired
// into the data assets, and the projectile light shares the movable-light budget with the muzzle flashes.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Core/VeloxAudioSettings.h"
#include "Core/VeloxWeaponCosmetics.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Items/VeloxItemDefinition.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"
#include "Weapons/VeloxMovableLights.h"

namespace VeloxArtContentTest
{
	constexpr int32 MaxWeaponTriangles = 1500;
	constexpr int32 MaxItemTriangles = 400;
	constexpr float MaxItemHeightCm = 60.f;

	const TCHAR* WeaponNames[] = { TEXT("MachineGun"), TEXT("Shotgun"), TEXT("PlasmaGun"), TEXT("RocketLauncher"), TEXT("LightningGun"), TEXT("Railgun") };
	// Item definition asset stems (DA_Item_<stem>), Tools/editor/create_item_content.py ITEMS.
	const TCHAR* ItemNames[] = {
		TEXT("Health_Small"), TEXT("Health_Medium"), TEXT("Health_Large"), TEXT("Health_Mega"), TEXT("Armor_Shard"), TEXT("Armor_Yellow"), TEXT("Armor_Red"),
		TEXT("Ammo_Bullets"), TEXT("Ammo_Shells"), TEXT("Ammo_Rockets"), TEXT("Ammo_Cells"), TEXT("Ammo_Lightning"), TEXT("Ammo_Slugs"),
		TEXT("Weapon_MachineGun"), TEXT("Weapon_Shotgun"), TEXT("Weapon_RocketLauncher"), TEXT("Weapon_LightningGun"), TEXT("Weapon_Railgun"), TEXT("Weapon_PlasmaGun"),
		TEXT("Powerup_Quad"), TEXT("Powerup_Haste"), TEXT("Powerup_Regen"), TEXT("Powerup_BattleSuit") };

	int32 TriangleCount(const UStaticMesh& Mesh)
	{
		const FStaticMeshRenderData* RenderData = Mesh.GetRenderData();
		return (RenderData && RenderData->LODResources.Num() > 0) ? RenderData->LODResources[0].GetNumTriangles() : 0;
	}

	/** Budgets shared by weapon and item meshes: one material, one UV set, no simple collision, no NoCollision-violating profile. */
	void CheckCommon(FAutomationTestBase& Test, const FString& Label, const UStaticMesh& Mesh, int32 MaxTriangles)
	{
		const int32 Tris = TriangleCount(Mesh);
		Test.TestTrue(*FString::Printf(TEXT("%s: 0 < %d triangles <= %d"), *Label, Tris, MaxTriangles), Tris > 0 && Tris <= MaxTriangles);
		Test.TestEqual(*FString::Printf(TEXT("%s: one material slot"), *Label), Mesh.GetStaticMaterials().Num(), 1);
		const FStaticMeshRenderData* RenderData = Mesh.GetRenderData();
		if (RenderData && RenderData->LODResources.Num() > 0)
		{
			// Dynamic objects take no lightmaps: the palette UV is the only channel (the engine's get_num_uv_channels is 0 for imports, render data is real).
			Test.TestEqual(*FString::Printf(TEXT("%s: UV channels"), *Label), (int32)RenderData->LODResources[0].GetNumTexCoords(), 1);
		}
		const UBodySetup* Body = Mesh.GetBodySetup();
		Test.TestTrue(*FString::Printf(TEXT("%s: no simple collision"), *Label), !Body || Body->AggGeom.GetElementCount() == 0);
	}
}

// Six weapon meshes: WorldMesh == ViewMesh on every DA_WeaponCosmetics_*, within 1500 triangles, a `muzzle` socket in front of the grip.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxWeaponMeshesTest, "Velox.Art.WeaponMeshes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxWeaponMeshesTest::RunTest(const FString& Parameters)
{
	using namespace VeloxArtContentTest;
	for (const TCHAR* Name : WeaponNames)
	{
		const FString Path = FString::Printf(TEXT("/Game/Weapons/DA_WeaponCosmetics_%s.DA_WeaponCosmetics_%s"), Name, Name);
		const UVeloxWeaponCosmetics* Data = LoadObject<UVeloxWeaponCosmetics>(nullptr, *Path);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), *Path), Data))
		{
			continue;
		}
		const UStaticMesh* World = Data->WorldMesh.LoadSynchronous();
		const UStaticMesh* View = Data->ViewMesh.LoadSynchronous();
		if (!TestNotNull(*FString::Printf(TEXT("%s WorldMesh"), Name), World) || !TestNotNull(*FString::Printf(TEXT("%s ViewMesh"), Name), View))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s WorldMesh == ViewMesh"), Name), World == View);
		TestTrue(*FString::Printf(TEXT("%s mesh is SM_Weapon_%s"), Name, Name), World->GetName() == FString::Printf(TEXT("SM_Weapon_%s"), Name));
		TestEqual(*FString::Printf(TEXT("%s ViewScale"), Name), Data->ViewScale, 1.f);
		TestTrue(*FString::Printf(TEXT("%s ViewOffset in front of and below the eye, to the right"), Name),
			Data->ViewOffset.GetLocation().X > 0.f && Data->ViewOffset.GetLocation().Y > 0.f && Data->ViewOffset.GetLocation().Z < 0.f);
		CheckCommon(*this, Name, *World, MaxWeaponTriangles);
		const UStaticMeshSocket* Muzzle = World->FindSocket(Data->MuzzleSocket);
		if (TestNotNull(*FString::Printf(TEXT("%s has socket %s"), Name, *Data->MuzzleSocket.ToString()), Muzzle))
		{
			const FBoxSphereBounds Bounds = World->GetBounds();
			TestTrue(*FString::Printf(TEXT("%s muzzle is at the barrel tip (+X)"), Name), Muzzle->RelativeLocation.X > 0.f && Muzzle->RelativeLocation.X >= Bounds.Origin.X + Bounds.BoxExtent.X - 1.0);
			TestEqual(*FString::Printf(TEXT("%s one socket"), Name), World->Sockets.Num(), 1);
		}
	}
	const UVeloxWeaponCosmetics* Rocket = LoadObject<UVeloxWeaponCosmetics>(nullptr, TEXT("/Game/Weapons/DA_WeaponCosmetics_RocketLauncher.DA_WeaponCosmetics_RocketLauncher"));
	if (TestNotNull(TEXT("rocket cosmetics"), Rocket))
	{
		TestTrue(TEXT("the rocket carries a flight light"), Rocket->ProjectileLightIntensity > 0.f && Rocket->ProjectileLightRadius > 0.f);
	}
	return true;
}

// Every item definition shows a mesh: <= 400 triangles, base origin, <= 60 cm (weapon pickups reuse the weapon mesh).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxItemMeshesTest, "Velox.Art.ItemMeshes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxItemMeshesTest::RunTest(const FString& Parameters)
{
	using namespace VeloxArtContentTest;
	for (const TCHAR* Name : ItemNames)
	{
		const FString Path = FString::Printf(TEXT("/Game/Items/DA_Item_%s.DA_Item_%s"), Name, Name);
		const UVeloxItemDefinition* Item = LoadObject<UVeloxItemDefinition>(nullptr, *Path);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), *Path), Item))
		{
			continue;
		}
		const UStaticMesh* Mesh = Item->Mesh.LoadSynchronous();
		if (!TestNotNull(*FString::Printf(TEXT("%s Mesh"), Name), Mesh))
		{
			continue;
		}
		const bool bWeapon = Item->Type == EVeloxItemType::Weapon;
		CheckCommon(*this, Name, *Mesh, bWeapon ? MaxWeaponTriangles : MaxItemTriangles);
		if (bWeapon)
		{
			TestTrue(*FString::Printf(TEXT("%s shows the weapon's own mesh"), Name), Mesh->GetName().StartsWith(TEXT("SM_Weapon_")));
		}
		else
		{
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			const double Bottom = Bounds.Origin.Z - Bounds.BoxExtent.Z;
			const double Top = Bounds.Origin.Z + Bounds.BoxExtent.Z;
			TestTrue(*FString::Printf(TEXT("%s: origin at the base (z %.1f..%.1f)"), Name, Bottom, Top), FMath::Abs(Bottom) <= 0.5);
			TestTrue(*FString::Printf(TEXT("%s: height %.1f <= %.0f cm"), Name, Top, MaxItemHeightCm), Top <= MaxItemHeightCm + 0.5);
			TestTrue(*FString::Printf(TEXT("%s: no sockets"), Name), Mesh->Sockets.IsEmpty());
		}
	}
	return true;
}

// Review 0006 E1.3: the flight light is an attached light counted in the same budget as the muzzle flashes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxFlightLightBudgetTest, "Velox.Weapons.FlightLightBudget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxFlightLightBudgetTest::RunTest(const FString& Parameters)
{
	const int32 Max = UVeloxAudioSettings::Get().MaxMovableLights;
	if (!TestTrue(TEXT("a movable-light budget exists"), Max > 0) || !GEngine)
	{
		return true;
	}
	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxFlightLightTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("test world"), World))
	{
		GEngine->DestroyWorldContext(World);
		return true;
	}
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();

	AActor* Rocket = World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Rocket, TEXT("Root"));
	Rocket->SetRootComponent(Root);
	Root->RegisterComponent();

	const int32 Before = VeloxMovableLights::NumActive();
	TArray<UPointLightComponent*> Lights;
	// Fill the budget (other tests may hold lights: only the free slots are ours).
	for (int32 Slot = Before; Slot < Max; ++Slot)
	{
		UPointLightComponent* Light = VeloxMovableLights::AcquireAttached(Rocket, Root, FLinearColor::White, 1000.f, 300.f);
		if (TestNotNull(TEXT("a light is granted while the budget has room"), Light))
		{
			Lights.Add(Light);
			TestTrue(TEXT("it rides the projectile root"), Light->GetAttachParent() == Root);
			TestFalse(TEXT("it casts no shadow"), Light->CastShadows);
			TestTrue(TEXT("it is movable"), Light->Mobility == EComponentMobility::Movable);
		}
	}
	TestEqual(TEXT("the budget is full"), VeloxMovableLights::NumActive(), Max);
	TestNull(TEXT("an attached light is refused at the cap"), VeloxMovableLights::AcquireAttached(Rocket, Root, FLinearColor::White, 1000.f, 300.f));
	TestFalse(TEXT("a muzzle flash is refused at the cap, too"), VeloxMovableLights::Acquire(World, Rocket, FVector::ZeroVector, FLinearColor::White, 1000.f, 300.f, 0.1f));
	TestNull(TEXT("zero intensity is refused"), VeloxMovableLights::AcquireAttached(Rocket, Root, FLinearColor::White, 0.f, 300.f));

	for (UPointLightComponent* Light : Lights)
	{
		Light->DestroyComponent();
	}
	World->EndPlay(EEndPlayReason::LevelTransition);
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
