// Velox - server RPC gate and its non-shipping test tooling (ADR 0007 sections 5.1-5.3, M6 task 07).
//   velox.net.AllowTestRpcs 0|1         server cvar (default 0): the project's test RPCs pass _Validate only when 1
//                                       (infra/ci/nettest.sh sets it on its servers; the public server never does).
//                                       Plain ECVF_Default so -ExecCmds can set it on a Development server; compiled out
//                                       in Shipping, where every test RPC is refused.
//   velox.move.DebugTimeScale <s>       client-only speed-hack proxy (task 07 report row): every locally controlled Velox
//                                       character of a client world gets CustomTimeDilation = s, so its movement
//                                       component ticks, and stamps its moves, s times faster than real time. Default 1.
//   velox.move.DebugTimeScaleLog 0|1    with 1 (or a scale != 1) logs `VeloxTimeScale` once per real second: scale, real
//                                       time, world time, position, 2-D speed of the local character.
//   velox.net.ConnectProbe [timeout]    client: waits for the connect to finish; logs `VeloxConnectProbe result=joined` or
//                                       `result=failed failure= error="..."` (the server's PreLogin text) and quits 0;
//                                       `result=timeout` quits 1 (nettest PasswordRun).
//   velox.net.RpcGateProbe exec|kill [timeout]
//                                       client: once joined (kill: with a live pawn, 1 s settled) sends `ServerExec stat fps`
//                                       through the console (exec) or UVeloxScriptedInputComponent::ServerKillSelfTest
//                                       (kill), then expects the server to drop the connection: `VeloxRpcGate probe= result=
//                                       disconnected dt= failure= error=` quits 0, `result=connected` after the timeout
//                                       quits 1 (nettest RpcGateRun).

#include "Net/VeloxNetCheats.h"

#include "Character/VeloxCharacter.h"
#include "Containers/Ticker.h"
#include "Core/VeloxLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Movement/VeloxScriptedInputComponent.h"

namespace VeloxNetCheatsPrivate
{
#if !UE_BUILD_SHIPPING
	int32 AllowTestRpcs = 0;
	FAutoConsoleVariableRef CVarAllowTestRpcs(
		TEXT("velox.net.AllowTestRpcs"),
		AllowTestRpcs,
		TEXT("Server: 1 accepts the project's test RPCs (scripted-input teleport/knockback/haste/kill, AddBot/KickBot); 0 (default) refuses them and the engine disconnects the sender (ADR 0007 section 5.1)."),
		ECVF_Default);
#endif

	/** The player controller that owns Context: the actor itself, a component's owner, a pawn's controller, an owner chain. */
	APlayerController* FindOwningPlayerController(const UObject* Context)
	{
		const AActor* Actor = Cast<AActor>(Context);
		if (!Actor)
		{
			if (const UActorComponent* Component = Cast<UActorComponent>(Context))
			{
				Actor = Component->GetOwner();
			}
		}
		for (int32 Depth = 0; Actor && Depth < 8; ++Depth)
		{
			if (const APlayerController* PC = Cast<APlayerController>(Actor))
			{
				return const_cast<APlayerController*>(PC);
			}
			const APawn* Pawn = Cast<APawn>(Actor);
			const AActor* Next = Pawn && Pawn->GetController() ? Pawn->GetController() : Actor->GetOwner();
			Actor = Next;
		}
		return nullptr;
	}
}

bool VeloxNet::TestRpcsAllowed()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return VeloxNetCheatsPrivate::AllowTestRpcs != 0;
#endif
}

bool VeloxNet::ValidateTestRpc(const UObject* Context, const TCHAR* RpcName)
{
	if (TestRpcsAllowed())
	{
		return true;
	}
	const TCHAR* Reason = UE_BUILD_SHIPPING ? TEXT("shipping") : TEXT("test_rpcs_off");
	if (AVeloxPlayerController* PC = Cast<AVeloxPlayerController>(VeloxNetCheatsPrivate::FindOwningPlayerController(Context)))
	{
		PC->NotifyRpcRefused(FName(RpcName), Reason);
	}
	else
	{
		UE_LOG(LogVeloxNet, Warning, TEXT("VeloxRpcRefused controller=none context=%s rpc=%s reason=%s"), *GetNameSafe(Context), RpcName, Reason);
	}
	return false;
}

#if !UE_BUILD_SHIPPING
namespace VeloxNetCheatsPrivate
{
	// ------------------------------------------------------------------------------------------- speed-hack proxy

	float DebugTimeScale = 1.f;
	int32 DebugTimeScaleLog = 0;
	FTSTicker::FDelegateHandle TimeScaleTicker;
	double TimeScaleStart = -1.0;
	double TimeScaleLastLog = -1e9;

	bool TickTimeScale(float)
	{
		const float Scale = FMath::Clamp(DebugTimeScale, 0.1f, 4.f);
		const double Now = FPlatformTime::Seconds();
		const bool bLog = DebugTimeScaleLog != 0 || !FMath::IsNearlyEqual(Scale, 1.f);
		if (!GEngine)
		{
			return true;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || World->GetNetMode() != NM_Client)
			{
				continue;   // client-side only: the server and standalone worlds are never scaled
			}
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* PC = It->Get();
				AVeloxCharacter* Character = PC && PC->IsLocalController() ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
				if (!Character)
				{
					continue;
				}
				if (Character->CustomTimeDilation != Scale)
				{
					Character->CustomTimeDilation = Scale;
					UE_LOG(LogVeloxNet, Log, TEXT("VeloxTimeScale applied scale=%.3f pawn=%s"), Scale, *Character->GetName());
				}
				if (bLog && Now - TimeScaleLastLog >= 1.0)
				{
					if (TimeScaleStart < 0.0)
					{
						TimeScaleStart = Now;
					}
					TimeScaleLastLog = Now;
					UE_LOG(LogVeloxNet, Log, TEXT("VeloxTimeScale scale=%.3f real_t=%.3f world_t=%.3f pos=%s speed2d=%.1f"), Scale, Now - TimeScaleStart, World->GetTimeSeconds(),
						*Character->GetActorLocation().ToCompactString(), Character->GetVelocity().Size2D());
				}
			}
		}
		return true;
	}

	void EnsureTimeScaleTicker(IConsoleVariable*)
	{
		if (!TimeScaleTicker.IsValid())
		{
			TimeScaleTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickTimeScale), 0.f);
		}
	}

	FAutoConsoleVariableRef CVarDebugTimeScale(
		TEXT("velox.move.DebugTimeScale"),
		DebugTimeScale,
		TEXT("Client only, non-shipping speed-hack proxy (ADR 0007 section 5.3): CustomTimeDilation of the locally controlled Velox character (default 1)."),
		FConsoleVariableDelegate::CreateStatic(&EnsureTimeScaleTicker),
		ECVF_Default);

	FAutoConsoleVariableRef CVarDebugTimeScaleLog(
		TEXT("velox.move.DebugTimeScaleLog"),
		DebugTimeScaleLog,
		TEXT("1: log VeloxTimeScale (position, speed) once per real second for the local Velox character of a client world."),
		FConsoleVariableDelegate::CreateStatic(&EnsureTimeScaleTicker),
		ECVF_Default);

	// ------------------------------------------------------------------------------------------- connect / RPC gate probes

	struct FProbe
	{
		enum class EMode : uint8 { Connect, Exec, Kill };
		EMode Mode = EMode::Connect;
		double Deadline = 0.0;
		double ReadySince = -1.0;
		double SentTime = -1.0;
		bool bDone = false;
		FDelegateHandle NetFailureHandle;
		FDelegateHandle TravelFailureHandle;
		FTSTicker::FDelegateHandle Ticker;

		const TCHAR* ModeName() const { return Mode == EMode::Connect ? TEXT("connect") : Mode == EMode::Exec ? TEXT("exec") : TEXT("kill"); }
	};
	TSharedPtr<FProbe> ActiveProbe;

	void FinishProbe(int32 ExitCode)
	{
		if (!ActiveProbe.IsValid() || ActiveProbe->bDone)
		{
			return;
		}
		ActiveProbe->bDone = true;
		if (GEngine)
		{
			GEngine->OnNetworkFailure().Remove(ActiveProbe->NetFailureHandle);
			GEngine->OnTravelFailure().Remove(ActiveProbe->TravelFailureHandle);
		}
		GLog->Flush();
		FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(ExitCode));
	}

	void OnProbeFailure(const TCHAR* Kind, const FString& Error)
	{
		if (!ActiveProbe.IsValid() || ActiveProbe->bDone)
		{
			return;
		}
		const FProbe& Probe = *ActiveProbe;
		if (Probe.Mode == FProbe::EMode::Connect)
		{
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxConnectProbe result=failed failure=%s error=\"%s\""), Kind, *Error);
			FinishProbe(0);
			return;
		}
		if (Probe.SentTime >= 0.0)
		{
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxRpcGate probe=%s result=disconnected dt=%.3f failure=%s error=\"%s\""), Probe.ModeName(), FPlatformTime::Seconds() - Probe.SentTime, Kind,
				*Error);
			FinishProbe(0);
		}
		else
		{
			UE_LOG(LogVeloxNet, Warning, TEXT("VeloxRpcGate probe=%s result=failed_before_send failure=%s error=\"%s\""), Probe.ModeName(), Kind, *Error);
			FinishProbe(1);
		}
	}

	/** The local Velox player controller of a connected client world (with its PlayerState), or null. */
	AVeloxPlayerController* FindConnectedClientController()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->GetNetMode() == NM_Client)
			{
				AVeloxPlayerController* PC = Cast<AVeloxPlayerController>(World->GetFirstPlayerController());
				if (PC && PC->IsLocalController() && PC->PlayerState)
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	bool TickProbe(float)
	{
		if (!ActiveProbe.IsValid() || ActiveProbe->bDone)
		{
			return false;
		}
		FProbe& Probe = *ActiveProbe;
		const double Now = FPlatformTime::Seconds();
		AVeloxPlayerController* PC = FindConnectedClientController();
		if (Probe.Mode == FProbe::EMode::Connect)
		{
			if (PC)
			{
				UE_LOG(LogVeloxNet, Log, TEXT("VeloxConnectProbe result=joined player=%s"), *AVeloxPlayerState::VeloxPlayerLabel(PC->PlayerState));
				FinishProbe(0);
				return false;
			}
		}
		else if (Probe.SentTime < 0.0)
		{
			AVeloxCharacter* Character = PC ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
			const bool bReady = PC && (Probe.Mode == FProbe::EMode::Exec || (Character && !Character->IsCorpse()));
			Probe.ReadySince = bReady ? (Probe.ReadySince < 0.0 ? Now : Probe.ReadySince) : -1.0;
			if (bReady && Now - Probe.ReadySince >= 1.0)
			{
				Probe.SentTime = Now;
				Probe.Deadline = FMath::Max(Probe.Deadline, Now + 5.0);
				if (Probe.Mode == FProbe::EMode::Exec)
				{
					UE_LOG(LogVeloxNet, Log, TEXT("VeloxRpcGate probe=exec sent cmd=\"ServerExec stat fps\" player=%s"), *AVeloxPlayerState::VeloxPlayerLabel(PC->PlayerState));
					PC->ConsoleCommand(TEXT("ServerExec stat fps"));
				}
				else if (UVeloxScriptedInputComponent* Scripted = Character->FindComponentByClass<UVeloxScriptedInputComponent>())
				{
					UE_LOG(LogVeloxNet, Log, TEXT("VeloxRpcGate probe=kill sent rpc=ServerKillSelfTest player=%s"), *AVeloxPlayerState::VeloxPlayerLabel(PC->PlayerState));
					Scripted->ServerKillSelfTest();
				}
				else
				{
					UE_LOG(LogVeloxNet, Warning, TEXT("VeloxRpcGate probe=kill result=no_component"));
					FinishProbe(1);
					return false;
				}
			}
		}
		if (Now > Probe.Deadline)
		{
			if (Probe.Mode == FProbe::EMode::Connect)
			{
				UE_LOG(LogVeloxNet, Warning, TEXT("VeloxConnectProbe result=timeout"));
			}
			else
			{
				UE_LOG(LogVeloxNet, Warning, TEXT("VeloxRpcGate probe=%s result=%s"), Probe.ModeName(), Probe.SentTime >= 0.0 ? TEXT("connected") : TEXT("timeout_before_send"));
			}
			FinishProbe(1);
			return false;
		}
		return true;
	}

	void StartProbe(FProbe::EMode Mode, double TimeoutSeconds)
	{
		if (ActiveProbe.IsValid() && !ActiveProbe->bDone)
		{
			UE_LOG(LogVeloxNet, Warning, TEXT("velox.net probe already running (%s)"), ActiveProbe->ModeName());
			return;
		}
		ActiveProbe = MakeShared<FProbe>();
		ActiveProbe->Mode = Mode;
		ActiveProbe->Deadline = FPlatformTime::Seconds() + FMath::Max(1.0, TimeoutSeconds);
		if (GEngine)
		{
			ActiveProbe->NetFailureHandle = GEngine->OnNetworkFailure().AddLambda([](UWorld*, UNetDriver*, ENetworkFailure::Type Type, const FString& Error)
			{
				OnProbeFailure(ENetworkFailure::ToString(Type), Error);
			});
			ActiveProbe->TravelFailureHandle = GEngine->OnTravelFailure().AddLambda([](UWorld*, ETravelFailure::Type Type, const FString& Error)
			{
				OnProbeFailure(ETravelFailure::ToString(Type), Error);
			});
		}
		ActiveProbe->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickProbe), 0.05f);
		UE_LOG(LogVeloxNet, Log, TEXT("VeloxProbe started mode=%s timeout=%.0f"), ActiveProbe->ModeName(), TimeoutSeconds);
	}

	double ParseTimeout(const TArray<FString>& Args, int32 Index, double Default)
	{
		return Args.IsValidIndex(Index) && Args[Index].IsNumeric() ? FCString::Atod(*Args[Index]) : Default;
	}

	FAutoConsoleCommand ConnectProbeCommand(
		TEXT("velox.net.ConnectProbe"),
		TEXT("velox.net.ConnectProbe [timeout_s=60]: client; logs VeloxConnectProbe result=joined|failed (with the server's error text)|timeout and quits (nettest PasswordRun)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			StartProbe(FProbe::EMode::Connect, ParseTimeout(Args, 0, 60.0));
		}));

	FAutoConsoleCommand RpcGateProbeCommand(
		TEXT("velox.net.RpcGateProbe"),
		TEXT("velox.net.RpcGateProbe exec|kill [timeout_s=60]: client; sends ServerExec / ServerKillSelfTest once joined and expects the server to disconnect it (nettest RpcGateRun)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const FString Mode = Args.IsValidIndex(0) ? Args[0] : FString(TEXT("exec"));
			if (!Mode.Equals(TEXT("exec"), ESearchCase::IgnoreCase) && !Mode.Equals(TEXT("kill"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogVeloxNet, Warning, TEXT("velox.net.RpcGateProbe: mode must be exec or kill, got '%s'"), *Mode);
				return;
			}
			StartProbe(Mode.Equals(TEXT("kill"), ESearchCase::IgnoreCase) ? FProbe::EMode::Kill : FProbe::EMode::Exec, ParseTimeout(Args, 1, 60.0));
		}));
}
#endif // !UE_BUILD_SHIPPING
