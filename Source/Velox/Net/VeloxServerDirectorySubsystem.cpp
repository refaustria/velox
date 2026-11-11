// Velox - client side of the server directory (ADR 0007 section 2, task M6-05).

#include "Net/VeloxServerDirectorySubsystem.h"

#include "Core/VeloxLog.h"
#include "Core/VeloxOnlineSettings.h"
#include "Http.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreMisc.h"
#include "Misc/NetworkVersion.h"
#include "Misc/Parse.h"
#include "Net/VeloxServerHeartbeatSubsystem.h"
#include "UI/VeloxUISettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxServerDirectorySubsystem)

bool UVeloxServerDirectorySubsystem::IsClientProcess(bool bDedicatedServer, bool bCommandlet, bool bHasServerId)
{
	return !bDedicatedServer && !bCommandlet && !bHasServerId;
}

bool UVeloxServerDirectorySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString ServerId;
	FParse::Value(FCommandLine::Get(), TEXT("ServerId="), ServerId);
	return IsClientProcess(IsRunningDedicatedServer(), IsRunningCommandlet(), !ServerId.IsEmpty()) && Super::ShouldCreateSubsystem(Outer);
}

void UVeloxServerDirectorySubsystem::Deinitialize()
{
	StopAutoRefresh();
	Super::Deinitialize();
}

EVeloxServerCompat UVeloxServerDirectorySubsystem::Classify(const FVeloxServerEntry& Entry) const
{
	return VeloxServerList::Compatibility(Entry, FNetworkVersion::GetProjectVersion(), FNetworkVersion::GetLocalNetworkVersion(), UVeloxUISettings::Get().ServerStaleAfterSeconds);
}

bool UVeloxServerDirectorySubsystem::Refresh(const FString& UrlOverride)
{
	if (bInFlight)
	{
		return false;
	}
	RequestUrl = UrlOverride.IsEmpty() ? UVeloxOnlineSettings::Get().ServerListUrl : UrlOverride;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(RequestUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetTimeout(UVeloxUISettings::Get().ServerListTimeoutSeconds);
	Request->OnProcessRequestComplete().BindWeakLambda(this, [this](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		ApplyResponse(bConnected && Response.IsValid(), Response.IsValid() ? Response->GetResponseCode() : 0, Response.IsValid() ? Response->GetContentAsString() : FString());
	});
	bInFlight = true;
	if (!Request->ProcessRequest())
	{
		ApplyResponse(false, 0, FString());
		return false;
	}
	UE_LOG(LogVelox, Verbose, TEXT("VeloxServers refresh url=%s"), *RequestUrl);
	return true;
}

void UVeloxServerDirectorySubsystem::ApplyResponse(bool bConnected, int32 ResponseCode, const FString& Body)
{
	bInFlight = false;
	bCompletedFetch = true;
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	if (!bConnected)
	{
		LastError = UI.ServerListUnreachableText;
	}
	else if (ResponseCode < 200 || ResponseCode >= 300)
	{
		LastError = FString::Printf(TEXT("%s (HTTP %d)"), *UI.ServerListHttpErrorText, ResponseCode);
	}
	else
	{
		TArray<FVeloxServerEntry> Parsed;
		FString ParseError;
		if (VeloxServerList::Parse(Body, Parsed, ParseError))
		{
			VeloxServerList::Sort(Parsed, FNetworkVersion::GetProjectVersion(), FNetworkVersion::GetLocalNetworkVersion(), UI.ServerStaleAfterSeconds);
			Servers = MoveTemp(Parsed);
			LastError.Reset();
			LastParseNote = ParseError;
			if (!ParseError.IsEmpty())
			{
				UE_LOG(LogVelox, Warning, TEXT("VeloxServers skipped malformed entries: %s"), *ParseError);
			}
		}
		else
		{
			LastError = ParseError;
		}
	}
	if (!LastError.IsEmpty())
	{
		UE_LOG(LogVelox, Warning, TEXT("VeloxServers refresh failed url=%s error='%s' keeping=%d rows"), *RequestUrl, *LastError, Servers.Num());
	}
	OnServersUpdated.Broadcast();
	OnServersUpdatedNative.Broadcast();
}

void UVeloxServerDirectorySubsystem::StartAutoRefresh()
{
	if (AutoRefreshHandle.IsValid())
	{
		return;
	}
	const float Interval = FMath::Max(UVeloxOnlineSettings::Get().ServerListRefreshSeconds, 1.f);
	AutoRefreshHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UVeloxServerDirectorySubsystem::TickAutoRefresh), Interval);
}

void UVeloxServerDirectorySubsystem::StopAutoRefresh()
{
	if (AutoRefreshHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AutoRefreshHandle);
		AutoRefreshHandle.Reset();
	}
}

bool UVeloxServerDirectorySubsystem::TickAutoRefresh(float DeltaTime)
{
	Refresh();
	return true;
}

void UVeloxServerDirectorySubsystem::LogServers(const TCHAR* Context) const
{
	UE_LOG(LogVelox, Log, TEXT("VeloxServers %s count=%d error='%s' note='%s' local_version=%s local_net=%u"), Context, Servers.Num(), *LastError, *LastParseNote,
		*FNetworkVersion::GetProjectVersion(), FNetworkVersion::GetLocalNetworkVersion());
	for (int32 Index = 0; Index < Servers.Num(); ++Index)
	{
		const FVeloxServerEntry& E = Servers[Index];
		UE_LOG(LogVelox, Log, TEXT("VeloxServer row=%d name='%s' map='%s' mode=%s players=%d/%d bots=%d phase=%s version=%s net=%u password=%d address=%s compat=%s"), Index, *E.Name,
			*E.MapDisplayName, *E.Mode, E.Players, E.MaxPlayers, E.Bots, *E.Phase, *E.Version, E.NetVersion, E.bPassword ? 1 : 0, *E.Address, VeloxServerList::CompatName(Classify(E)));
	}
}
