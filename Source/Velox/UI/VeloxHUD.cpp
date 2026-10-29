// Velox - HUD.

#include "UI/VeloxHUD.h"

#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Movement/VeloxMoveDebug.h"
#include "Movement/VeloxMovementComponent.h"
#include "Character/VeloxCharacter.h"
#include "Core/VeloxLog.h"
#include "UI/VeloxHUDViewModel.h"
#include "UI/VeloxHUDWidget.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Canvas.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxHUD)

#if !UE_BUILD_SHIPPING
namespace VeloxHUDPrivate
{
	TAutoConsoleVariable<int32> CVarHudMovement(TEXT("velox.hud.Movement"), 1,
		TEXT("1 shows the movement debug block on the HUD (speed, ground/air, grace, latch, corrections). Non-shipping."), ECVF_Default);

	// Layout of the debug block (screen pixels at scale 1).
	constexpr float BlockX = 24.f;
	constexpr float BlockY = 24.f;
	constexpr float LineHeight = 16.f;
}
#endif

AVeloxHUD::AVeloxHUD(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	HUDWidgetClass = UVeloxHUDWidget::StaticClass();
}

void AVeloxHUD::BeginPlay()
{
	Super::BeginPlay();

	// The HUD actor exists only for local players; a dedicated server never builds UI.
	APlayerController* Controller = GetOwningPlayerController();
	if (GetNetMode() == NM_DedicatedServer || !Controller || !Controller->IsLocalController() || !HUDWidgetClass)
	{
		return;
	}
	ViewModel = NewObject<UVeloxHUDViewModel>(this);
	HUDWidget = CreateWidget<UVeloxHUDWidget>(Controller, HUDWidgetClass);
	if (HUDWidget)
	{
		HUDWidget->SetViewModel(ViewModel);
		HUDWidget->AddToViewport(HUDWidgetZOrder);
	}
	UE_LOG(LogVelox, Log, TEXT("VeloxHUD widget class=%s created=%d in_viewport=%d"), *GetNameSafe(HUDWidgetClass), HUDWidget ? 1 : 0, HUDWidget && HUDWidget->IsInViewport() ? 1 : 0);
}

void AVeloxHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HUDWidget)
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}
	ViewModel = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AVeloxHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const AVeloxPlayerController* Controller = Cast<AVeloxPlayerController>(GetOwningPlayerController());
	if (!ViewModel || !HUDWidget || !Controller)
	{
		return;
	}
	ViewModel->UpdateFromGame(*Controller);
	if (!bLoggedWidget && ViewModel->bHasPlayer)
	{
		// Headless check (-game -nullrhi): the widget tree exists and carries the first real values.
		bLoggedWidget = true;
		HUDWidget->RefreshFromViewModel();
		UE_LOG(LogVelox, Log, TEXT("VeloxHUD bound %s"), *HUDWidget->DescribeBoundValues());
	}
}

void AVeloxHUD::DrawHUD()
{
	Super::DrawHUD();

#if !UE_BUILD_SHIPPING
	using namespace VeloxHUDPrivate;
	if (!CVarHudMovement.GetValueOnGameThread() || !Canvas)
	{
		return;
	}
	const AVeloxCharacter* Character = PlayerOwner ? Cast<AVeloxCharacter>(PlayerOwner->GetViewTarget()) : nullptr;
	const UVeloxMovementComponent* Movement = Character ? Character->GetVeloxMovementComponent() : nullptr;
	if (!Movement)
	{
		return;
	}

	TArray<FString> Lines;
	VeloxMoveDebug::BuildStatusLines(*Movement, false, Lines);
	const float DeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const float Ping = (PlayerOwner && PlayerOwner->PlayerState) ? PlayerOwner->PlayerState->GetPingInMilliseconds() : 0.f;
	Lines.Add(FString::Printf(TEXT("fps %.0f   ping %.0f ms"), DeltaSeconds > 0.f ? 1.f / DeltaSeconds : 0.f, Ping));

	float Y = BlockY;
	for (const FString& Line : Lines)
	{
		DrawText(Line, FLinearColor::Yellow, BlockX, Y, GEngine ? GEngine->GetSmallFont() : nullptr);
		Y += LineHeight;
	}
#endif
}
