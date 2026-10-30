// Velox - authored nav link for bots (ADR 0005 section 4).

#include "Bots/VeloxNavLink.h"

#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxNavAreas.h"
#include "Engine/World.h"
#include "Movement/VeloxJumpPad.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationSystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxNavLink)

AVeloxNavLink::AVeloxNavLink(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	// Bots and nav data exist only on the server (ADR 0005 section 3: bAllowClientSideNavigation=False).
	bNetLoadOnClient = false;
	SetReplicates(false);
	bSmartLinkIsRelevant = false;
	RebuildPointLinks();
}

TSubclassOf<UNavArea> AVeloxNavLink::AreaClassFor(EVeloxNavLinkType InType)
{
	switch (InType)
	{
	case EVeloxNavLinkType::JumpPad:
		return UVeloxNavArea_JumpPad::StaticClass();
	case EVeloxNavLinkType::Drop:
		return UVeloxNavArea_Drop::StaticClass();
	case EVeloxNavLinkType::Jump:
		return UVeloxNavArea_Jump::StaticClass();
	case EVeloxNavLinkType::JumpFast:
		return UVeloxNavArea_JumpFast::StaticClass();
	case EVeloxNavLinkType::RocketJump:
		return UVeloxNavArea_RocketJump::StaticClass();
	default:
		return UVeloxNavArea_Drop::StaticClass();
	}
}

bool AVeloxNavLink::LinkTypeForArea(const UClass* AreaClass, EVeloxNavLinkType& OutType)
{
	static const EVeloxNavLinkType AllTypes[] = { EVeloxNavLinkType::JumpPad, EVeloxNavLinkType::Drop, EVeloxNavLinkType::Jump, EVeloxNavLinkType::JumpFast, EVeloxNavLinkType::RocketJump };
	for (const EVeloxNavLinkType Candidate : AllTypes)
	{
		if (AreaClass && AreaClass == AreaClassFor(Candidate).Get())
		{
			OutType = Candidate;
			return true;
		}
	}
	return false;
}

const TCHAR* AVeloxNavLink::LinkTypeName(EVeloxNavLinkType InType)
{
	switch (InType)
	{
	case EVeloxNavLinkType::JumpPad:
		return TEXT("JumpPad");
	case EVeloxNavLinkType::Drop:
		return TEXT("Drop");
	case EVeloxNavLinkType::Jump:
		return TEXT("Jump");
	case EVeloxNavLinkType::JumpFast:
		return TEXT("JumpFast");
	case EVeloxNavLinkType::RocketJump:
		return TEXT("RocketJump");
	default:
		return TEXT("Unknown");
	}
}

FNavigationLink AVeloxNavLink::BuildPointLink() const
{
	FNavigationLink Link(LinkStart, LinkEnd);
	Link.Direction = bBothWays ? ENavLinkDirection::BothWays : ENavLinkDirection::LeftToRight;
	Link.SnapRadius = LinkSnapRadius;
	Link.bUseSnapHeight = true;
	Link.SnapHeight = LinkSnapHeight;
	Link.SetAreaClass(bEnabled ? AreaClassFor(Type).Get() : UNavArea_Null::StaticClass());
	// SupportedAgents: default selector (every agent); Velox has one (ADR 0005 section 3).
	return Link;
}

void AVeloxNavLink::RebuildPointLinks()
{
	PointLinks.Reset(1);
	PointLinks.Add(BuildPointLink());
}

void AVeloxNavLink::PostRegisterAllComponents()
{
	// Before Super: AActor::PostRegisterAllComponents registers the actor with the nav octree, which reads PointLinks
	// through GetNavigationData (the engine does the same for SmartLinkComp relevancy).
	RebuildPointLinks();
	Super::PostRegisterAllComponents();
}

void AVeloxNavLink::BeginPlay()
{
	Super::BeginPlay();
	if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld()))
	{
		Bots->RegisterNavLink(this);
	}
}

void AVeloxNavLink::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(GetWorld()))
	{
		Bots->UnregisterNavLink(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AVeloxNavLink::SetLinkEnabled(bool bInEnabled)
{
	if (bEnabled == bInEnabled)
	{
		return;
	}
	bEnabled = bInEnabled;
	RebuildPointLinks();
	UNavigationSystemV1::UpdateActorInNavOctree(*this);
}

FVector AVeloxNavLink::GetStartWorld() const
{
	return GetActorTransform().TransformPosition(LinkStart);
}

FVector AVeloxNavLink::GetEndWorld() const
{
	return GetActorTransform().TransformPosition(LinkEnd);
}

#if WITH_EDITOR
void AVeloxNavLink::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	RebuildPointLinks();
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (GetWorld())
	{
		FNavigationSystem::UpdateActorData(*this);
	}
}
#endif
