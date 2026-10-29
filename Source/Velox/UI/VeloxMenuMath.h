// Velox - world-free menu logic (ADR 0004 section 7): connect address normalisation and failure texts. Unit-tested
// (Velox.UI.MenuMath).

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"

namespace VeloxMenuMath
{
	/**
	 * Trims Input and checks it is a plain `host`, `host:port`, `[v6]:port` or bare IPv6 address (no travel options, no
	 * map names, no spaces). A missing port gets DefaultPort (not for a bare IPv6 address). False and OutError when unusable.
	 */
	inline bool NormalizeConnectAddress(const FString& Input, int32 DefaultPort, FString& OutAddress, FString& OutError)
	{
		FString Address = Input;
		Address.TrimStartAndEndInline();
		if (Address.IsEmpty())
		{
			OutError = TEXT("Enter a server address (host or host:port).");
			return false;
		}
		int32 ColonCount = 0;
		for (const TCHAR C : Address)
		{
			if (C <= 32 || C == TEXT('?') || C == TEXT('#') || C == TEXT('"') || C == TEXT('\\') || C == TEXT('/') || C == 127)
			{
				OutError = FString::Printf(TEXT("Invalid character '%c' in the address."), C);
				return false;
			}
			ColonCount += C == TEXT(':') ? 1 : 0;
		}

		int32 ColonIndex = INDEX_NONE;
		if (Address.StartsWith(TEXT("[")))
		{
			const int32 Close = Address.Find(TEXT("]"));
			if (Close == INDEX_NONE)
			{
				OutError = TEXT("Unterminated '[' in the address.");
				return false;
			}
			if (Close + 1 < Address.Len())
			{
				if (Address[Close + 1] != TEXT(':'))
				{
					OutError = TEXT("Invalid address.");
					return false;
				}
				ColonIndex = Close + 1;
			}
		}
		else if (ColonCount == 1)
		{
			Address.FindChar(TEXT(':'), ColonIndex);
		}
		else if (ColonCount > 1)
		{
			OutAddress = Address;
			return true;
		}

		if (ColonIndex != INDEX_NONE)
		{
			const FString PortText = Address.Mid(ColonIndex + 1);
			const FString Host = Address.Left(ColonIndex);
			bool bDigits = !PortText.IsEmpty();
			for (const TCHAR C : PortText)
			{
				bDigits &= FChar::IsDigit(C);
			}
			if (Host.IsEmpty() || !bDigits || PortText.Len() > 5)
			{
				OutError = TEXT("Invalid host or port.");
				return false;
			}
			const int32 Port = FCString::Atoi(*PortText);
			if (Port < 1 || Port > 65535)
			{
				OutError = TEXT("The port must be between 1 and 65535.");
				return false;
			}
			OutAddress = Address;
			return true;
		}
		OutAddress = FString::Printf(TEXT("%s:%d"), *Address, DefaultPort);
		return true;
	}

	/** One line for the menu after a failed connect (the engine's error string is appended when it has one). */
	inline FString NetworkFailureMessage(ENetworkFailure::Type Type, const FString& Error)
	{
		const TCHAR* Headline = TEXT("Network error");
		switch (Type)
		{
		// The engine's text for these is a multi-line transport diagnostic: the headline alone is for the player (it is in the log).
		case ENetworkFailure::ConnectionLost:
			return TEXT("Connection lost");
		case ENetworkFailure::ConnectionTimeout:
			return TEXT("Connection timed out");
		case ENetworkFailure::PendingConnectionFailure:
			Headline = TEXT("Could not connect to the server");
			break;
		case ENetworkFailure::FailureReceived:
			Headline = TEXT("The server refused the connection");
			break;
		case ENetworkFailure::OutdatedClient:
			Headline = TEXT("Your game version is older than the server's");
			break;
		case ENetworkFailure::OutdatedServer:
			Headline = TEXT("The server's game version is older than yours");
			break;
		default:
			break;
		}
		return Error.IsEmpty() ? FString(Headline) : FString::Printf(TEXT("%s: %s"), Headline, *Error);
	}

	inline FString TravelFailureMessage(ETravelFailure::Type Type, const FString& Error)
	{
		const TCHAR* Headline = TEXT("Travel failed");
		switch (Type)
		{
		case ETravelFailure::NoLevel:
		case ETravelFailure::LoadMapFailure:
			Headline = TEXT("The map could not be loaded");
			break;
		case ETravelFailure::InvalidURL:
			Headline = TEXT("Invalid server address");
			break;
		case ETravelFailure::PackageMissing:
			Headline = TEXT("Content missing");
			break;
		case ETravelFailure::PackageVersion:
			Headline = TEXT("Content version mismatch");
			break;
		case ETravelFailure::NoDownload:
			Headline = TEXT("Content cannot be downloaded");
			break;
		default:
			break;
		}
		return Error.IsEmpty() ? FString(Headline) : FString::Printf(TEXT("%s: %s"), Headline, *Error);
	}
}
