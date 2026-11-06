// Velox - non-shipping server browser console command (task M6-05).
//   velox.online.DumpServers [url] [quit]   refreshes the server list (UVeloxOnlineSettings::ServerListUrl, or url) and logs
//                                           `VeloxServers dump ...` plus one `VeloxServer row=...` line per entry with its
//                                           compat; `quit` exits the process afterwards (live checks).

#include "Core/VeloxLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Net/VeloxServerDirectorySubsystem.h"

#if !UE_BUILD_SHIPPING
namespace VeloxOnlineCheats
{
	void DumpServers(const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		UVeloxServerDirectorySubsystem* Directory = GameInstance ? GameInstance->GetSubsystem<UVeloxServerDirectorySubsystem>() : nullptr;
		if (!Directory)
		{
			UE_LOG(LogVelox, Warning, TEXT("VeloxServers dump: no server directory subsystem (dedicated server or no game instance)"));
			return;
		}
		FString Url;
		bool bQuit = false;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				bQuit = true;
			}
			else
			{
				Url = Arg;
			}
		}
		// One-shot: log when this fetch completes. A fetch already in flight (the browser's) finishes first; then ask again.
		const TSharedRef<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
		const TWeakObjectPtr<UVeloxServerDirectorySubsystem> WeakDirectory = Directory;
		*Handle = Directory->OnServersUpdatedNative.AddLambda([WeakDirectory, Handle, bQuit]()
		{
			if (UVeloxServerDirectorySubsystem* Subsystem = WeakDirectory.Get())
			{
				Subsystem->OnServersUpdatedNative.Remove(*Handle);
				Subsystem->LogServers(TEXT("dump"));
			}
			if (bQuit)
			{
				FPlatformMisc::RequestExit(false);
			}
		});
		if (!Directory->Refresh(Url) && !Directory->IsRefreshing())
		{
			UE_LOG(LogVelox, Warning, TEXT("VeloxServers dump: the request could not be started"));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DumpServersCmd(
		TEXT("velox.online.DumpServers"),
		TEXT("velox.online.DumpServers [url] [quit]: fetches the server list and logs the rows with their compatibility. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpServers));
}
#endif
