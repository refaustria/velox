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
#include "Combat/VeloxCombatSettings.h"
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

namespace VeloxHUDPrivate
{
	// Hit marker geometry, relative to HitMarkerSize: gap from the centre, line thickness, kill marker scale.
	constexpr float HitMarkerGap = 0.5f;
	constexpr float HitMarkerThickness = 2.f;
	constexpr float KillMarkerScale = 1.6f;
	constexpr float DeathTextScale = 2.f;
	constexpr float DeathTextY = 0.35f;
	constexpr float DeathPromptGap = 36.f;
	const FLinearColor HitColor(1.f, 1.f, 1.f, 1.f);
	const FLinearColor KillColor(1.f, 0.1f, 0.1f, 1.f);
}

void AVeloxHUD::DrawHitMarker()
{
	using namespace VeloxHUDPrivate;
	const AVeloxPlayerController* PC = Cast<AVeloxPlayerController>(PlayerOwner);
	const AVeloxPlayerState* PS = PC ? PC->GetPlayerState<AVeloxPlayerState>() : nullptr;
	const UWorld* World = GetWorld();
	if (!PC || !World || !Canvas || PC->GetLastHitConfirmTime() < 0.0)
	{
		return;
	}
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PS ? PS->GetCombatSettings() : nullptr);
	const double Age = World->GetTimeSeconds() - PC->GetLastHitConfirmTime();
	if (Age < 0.0 || Age > Settings.HitMarkerSeconds)
	{
		return;
	}
	const bool bKill = PC->WasLastHitConfirmKill();
	const float Arm = Settings.HitMarkerSize * (Canvas->ClipY / 1080.f) * (bKill ? KillMarkerScale : 1.f);
	const float Gap = Arm * HitMarkerGap;
	const FLinearColor Color = bKill ? KillColor : HitColor;
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	for (const FVector2D Sign : { FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(-1, -1) })
	{
		DrawLine(CX + Sign.X * Gap, CY + Sign.Y * Gap, CX + Sign.X * Arm, CY + Sign.Y * Arm, Color, HitMarkerThickness);
	}
}

void AVeloxHUD::DrawDeathOverlay()
{
	using namespace VeloxHUDPrivate;
	const AVeloxPlayerController* PC = Cast<AVeloxPlayerController>(PlayerOwner);
	const UWorld* World = GetWorld();
	if (!PC || !World || !Canvas || !PC->IsWaitingForRespawn())
	{
		return;
	}
	const AVeloxPlayerState* PS = PC->GetPlayerState<AVeloxPlayerState>();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(PS ? PS->GetCombatSettings() : nullptr);
	const double Waited = World->GetTimeSeconds() - PC->GetPawnLostTime();
	const FString Title = TEXT("YOU WERE FRAGGED");
	const FString Prompt = Waited < Settings.MinRespawnDelay ? FString::Printf(TEXT("Respawn in %.1f"), Settings.MinRespawnDelay - Waited) : FString(TEXT("Press FIRE to respawn"));
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	float W = 0.f;
	float H = 0.f;
	GetTextSize(Title, W, H, Font, DeathTextScale);
	const float Y = Canvas->ClipY * DeathTextY;
	DrawText(Title, KillColor, (Canvas->ClipX - W) * 0.5f, Y, Font, DeathTextScale);
	GetTextSize(Prompt, W, H, Font, 1.f);
	DrawText(Prompt, HitColor, (Canvas->ClipX - W) * 0.5f, Y + H * DeathTextScale + DeathPromptGap, Font, 1.f);
}

AVeloxHUD::AVeloxHUD(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AVeloxHUD::DrawHUD()
{
	Super::DrawHUD();

	DrawHitMarker();
	DrawDeathOverlay();

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
