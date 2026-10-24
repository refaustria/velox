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
