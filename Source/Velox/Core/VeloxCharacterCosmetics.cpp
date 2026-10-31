// Velox - third-person character cosmetics.

#include "Core/VeloxCharacterCosmetics.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxCharacterCosmetics)

int32 UVeloxCharacterCosmetics::FindSkin(FName Name) const
{
	return Skins.IndexOfByPredicate([Name](const FVeloxSkin& Skin) { return Skin.Name == Name; });
}

UAnimSequence* UVeloxCharacterCosmetics::GetLoadedSequence(EVeloxLegState State, EVeloxRunDirection Direction) const
{
	if (State == EVeloxLegState::Run)
	{
		if (const TSoftObjectPtr<UAnimSequence>* Directional = RunSequences.Find(Direction))
		{
			if (UAnimSequence* Sequence = Directional->Get())
			{
				return Sequence;
			}
		}
	}
	const TSoftObjectPtr<UAnimSequence>* Entry = Sequences.Find(State);
	return Entry ? Entry->Get() : nullptr;
}

void UVeloxCharacterCosmetics::GetSoftReferences(TArray<FSoftObjectPath>& OutPaths) const
{
	auto Add = [&OutPaths](const FSoftObjectPath& Path)
	{
		if (!Path.IsNull())
		{
			OutPaths.AddUnique(Path);
		}
	};
	Add(SkeletalMesh.ToSoftObjectPath());
	Add(AnimClass.ToSoftObjectPath());
	for (const FVeloxSkin& Skin : Skins)
	{
		Add(Skin.Material.ToSoftObjectPath());
	}
	for (const TPair<EVeloxLegState, TSoftObjectPtr<UAnimSequence>>& Pair : Sequences)
	{
		Add(Pair.Value.ToSoftObjectPath());
	}
	for (const TPair<EVeloxRunDirection, TSoftObjectPtr<UAnimSequence>>& Pair : RunSequences)
	{
		Add(Pair.Value.ToSoftObjectPath());
	}
	Add(ViewModelSettings.ToSoftObjectPath());
}

int32 UVeloxCharacterCosmetics::ChooseSkinSlot(const UVeloxCharacterCosmetics* Cosmetics, bool bOwnedByLocalPlayer, bool bForceEnemySkin, FName ForcedEnemySkin, uint8 SkinIndex, uint8 NoSkin)
{
	if (!Cosmetics || Cosmetics->Skins.IsEmpty())
	{
		return INDEX_NONE;
	}
	if (bForceEnemySkin && !bOwnedByLocalPlayer)
	{
		const int32 Forced = Cosmetics->FindSkin(ForcedEnemySkin);
		if (Forced != INDEX_NONE)
		{
			return Forced;
		}
	}
	return SkinIndex == NoSkin ? INDEX_NONE : static_cast<int32>(SkinIndex) % Cosmetics->Skins.Num();
}

FColor UVeloxCharacterCosmetics::ResolveBodyColor(const UVeloxCharacterCosmetics* Cosmetics, uint8 SkinIndex, const TArray<FColor>& Palette)
{
	if (Cosmetics && Cosmetics->Skins.IsValidIndex(SkinIndex))
	{
		return Cosmetics->Skins[SkinIndex].BodyColor.ToFColorSRGB();
	}
	return Palette.IsValidIndex(SkinIndex) ? Palette[SkinIndex] : FColor::White;
}
