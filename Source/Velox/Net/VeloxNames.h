// Velox - player-name sanitation (ADR 0007 section 5.2, M6 task 07). Header-only and world-free (unit-tested in
// Velox.Net.NameSanitize). AVeloxGameMode applies it to every human name (InitNewPlayer and ChangeName); bot names are
// project data (ADR 0005) and never pass through here.
//
// Sanitize rules, in order, on Unicode code points (UTF-16 surrogate pairs are decoded; an unpaired surrogate is dropped):
//   1. strip control, format and bidi characters: U+0000-001F, U+007F-009F, U+00AD, U+200B-200F, U+2028-202E,
//      U+2060-206F (incl. the isolates U+2066-2069), U+FEFF, U+FFF9-FFFB, U+E0000-E007F (tags); the task's required set
//      (C0, DEL/C1, U+200B-200F, U+202A-202E, U+2066-2069) is a subset;
//   2. every whitespace character (ASCII, U+00A0, U+1680, U+2000-200A, U+205F, U+3000, ...) becomes one ' ', runs collapse;
//   3. trim both ends;
//   4. clamp to MaxLen code points (never splits a pair), trim again;
//   5. empty -> EmptyName ("Player").

#pragma once

#include "Containers/Array.h"
#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "Misc/Char.h"

namespace VeloxNames
{
	/** Fallback for a name that is empty after sanitation. */
	inline const TCHAR* const EmptyName = TEXT("Player");

	/** Code point is removed outright (rule 1). */
	inline bool IsStrippedCodePoint(uint32 C)
	{
		return C <= 0x1F
			|| (C >= 0x7F && C <= 0x9F)
			|| C == 0xAD
			|| (C >= 0x200B && C <= 0x200F)
			|| (C >= 0x2028 && C <= 0x202E)
			|| (C >= 0x2060 && C <= 0x206F)
			|| C == 0xFEFF
			|| (C >= 0xFFF9 && C <= 0xFFFB)
			|| (C >= 0xE0000 && C <= 0xE007F);
	}

	/** Code point counts as whitespace (rule 2). Checked after IsStrippedCodePoint, so tab/newline are stripped, not spaces. */
	inline bool IsSpaceCodePoint(uint32 C)
	{
		return C == 0x20 || C == 0xA0 || C == 0x1680 || (C >= 0x2000 && C <= 0x200A) || C == 0x205F || C == 0x3000;
	}

	/** Decodes In into code points; unpaired surrogates are dropped. */
	inline TArray<uint32> ToCodePoints(const FString& In)
	{
		TArray<uint32> Out;
		Out.Reserve(In.Len());
		const int32 Len = In.Len();
		for (int32 I = 0; I < Len; ++I)
		{
			const uint32 U = static_cast<uint32>(In[I]);
			if (sizeof(TCHAR) == 2 && U >= 0xD800 && U <= 0xDBFF)
			{
				const uint32 Low = I + 1 < Len ? static_cast<uint32>(In[I + 1]) : 0u;
				if (Low >= 0xDC00 && Low <= 0xDFFF)
				{
					Out.Add(0x10000u + ((U - 0xD800u) << 10) + (Low - 0xDC00u));
					++I;
				}
				continue;   // unpaired high surrogate
			}
			if (sizeof(TCHAR) == 2 && U >= 0xDC00 && U <= 0xDFFF)
			{
				continue;   // unpaired low surrogate
			}
			Out.Add(U);
		}
		return Out;
	}

	inline void AppendCodePoint(FString& Out, uint32 C)
	{
		if (sizeof(TCHAR) == 2 && C >= 0x10000)
		{
			const uint32 V = C - 0x10000u;
			Out.AppendChar(static_cast<TCHAR>(0xD800u + (V >> 10)));
			Out.AppendChar(static_cast<TCHAR>(0xDC00u + (V & 0x3FFu)));
		}
		else
		{
			Out.AppendChar(static_cast<TCHAR>(C));
		}
	}

	inline FString FromCodePoints(const TArray<uint32>& CodePoints)
	{
		FString Out;
		Out.Reserve(CodePoints.Num());
		for (const uint32 C : CodePoints)
		{
			AppendCodePoint(Out, C);
		}
		return Out;
	}

	/** Length of S in code points (a surrogate pair counts once). */
	inline int32 CodePointLen(const FString& S)
	{
		return ToCodePoints(S).Num();
	}

	/** Rules 1-5 above. MaxLen <= 0 is treated as 1. */
	inline FString Sanitize(const FString& In, int32 MaxLen)
	{
		const int32 Limit = FMath::Max(1, MaxLen);
		TArray<uint32> Kept;
		Kept.Reserve(In.Len());
		bool bPendingSpace = false;
		for (const uint32 C : ToCodePoints(In))
		{
			if (IsStrippedCodePoint(C))
			{
				continue;
			}
			if (IsSpaceCodePoint(C))
			{
				bPendingSpace = Kept.Num() > 0;   // leading whitespace is trimmed
				continue;
			}
			if (bPendingSpace)
			{
				Kept.Add(0x20);
				bPendingSpace = false;
			}
			Kept.Add(C);
		}
		if (Kept.Num() > Limit)
		{
			Kept.SetNum(Limit);
		}
		while (Kept.Num() > 0 && Kept.Last() == 0x20)
		{
			Kept.Pop();
		}
		return Kept.Num() > 0 ? FromCodePoints(Kept) : FString(EmptyName);
	}

	/**
	 * Name if no entry of ExistingNames equals it (case-insensitive), else the first free "Name (2)", "Name (3)", ...; the
	 * base is shortened (by code points, trailing space trimmed) so the result stays within MaxLen code points.
	 */
	inline FString Dedupe(const FString& Name, const TArray<FString>& ExistingNames, int32 MaxLen)
	{
		auto Taken = [&ExistingNames](const FString& Candidate)
		{
			return ExistingNames.ContainsByPredicate([&Candidate](const FString& Existing) { return Existing.Equals(Candidate, ESearchCase::IgnoreCase); });
		};
		if (!Taken(Name))
		{
			return Name;
		}
		const int32 Limit = FMath::Max(1, MaxLen);
		const TArray<uint32> Base = ToCodePoints(Name);
		for (int32 Suffix = 2; Suffix < 10000; ++Suffix)
		{
			const FString Tail = FString::Printf(TEXT(" (%d)"), Suffix);
			TArray<uint32> Head = Base;
			const int32 Room = FMath::Max(0, Limit - Tail.Len());
			if (Head.Num() > Room)
			{
				Head.SetNum(Room);
			}
			while (Head.Num() > 0 && Head.Last() == 0x20)
			{
				Head.Pop();
			}
			const FString Candidate = FromCodePoints(Head) + Tail;
			if (!Taken(Candidate))
			{
				return Candidate;
			}
		}
		return Name;
	}
}
