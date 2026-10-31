// Velox - third-person character cosmetics (ADR 0006 sections 1, 4.3-4.5). Data only.
//
// Every reference is soft (ADR 0006 section 1): the dedicated server never loads a skeletal mesh, material or animation.
// UVeloxAssetManager::LoadCharacterCosmetics resolves them on non-server worlds only. Velox.Core.CosmeticsSoftRefs walks
// this class by reflection and fails on any hard object pointer.

#pragma once

#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"

#include "VeloxCharacterCosmetics.generated.h"

class UAnimInstance;
class UAnimSequence;
class UMaterialInterface;
class USkeletalMesh;
class UVeloxViewModelSettings;

/** Leg state of the third-person body (ADR 0006 section 4.5). The AnimGraph state machine (ABP_Velox) mirrors it. */
UENUM(BlueprintType)
enum class EVeloxLegState : uint8
{
	Idle,
	Run,
	JumpStart,
	Air,
	Land,
	CrouchIdle,
	CrouchWalk,
	Dead
};

/** 4-way directional pick for the run cycle (Run_Fwd / Run_Back / Run_Left / Run_Right). */
UENUM(BlueprintType)
enum class EVeloxRunDirection : uint8
{
	Fwd,
	Back,
	Left,
	Right
};

/** Cadence and thresholds of the leg states (ADR 0006 section 4.4). Lives in UVeloxCharacterCosmetics::AnimTuning. */
USTRUCT(BlueprintType)
struct FVeloxAnimTuning
{
	GENERATED_BODY()

	/** Horizontal speed at and above which the legs run (cm/s); below it they idle. Same rule crouched (walk/idle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Anim", Meta = (ClampMin = "0", Units = "cm/s"))
	float MoveSpeedThreshold = 100.f;

	/** Jump_Start length (s) before the legs switch to Air_Loop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Anim", Meta = (ClampMin = "0", Units = "s"))
	float JumpStartSeconds = 0.15f;

	/** Land length (s) before the legs return to idle/run. Crouching cuts it short. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Anim", Meta = (ClampMin = "0", Units = "s"))
	float LandSeconds = 0.2f;

	/** A take-off with more upward speed than this (cm/s) is a jump (Jump_Start); otherwise the pawn walked off a ledge (Air). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Anim", Meta = (Units = "cm/s"))
	float JumpStartMinUpSpeed = 0.f;

	/** Speed at which the run cycle plays at rate 1 (cm/s): play rate = Speed2D / this, clamped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Anim", Meta = (ClampMin = "1", Units = "cm/s"))
	float ReferenceRunSpeed = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Anim", Meta = (ClampMin = "0"))
	float MinPlayRate = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Anim", Meta = (ClampMin = "0"))
	float MaxPlayRate = 1.6f;

	/** Half-width of the forward and back quadrants (deg): |direction| <= this is Fwd, >= 180 - this is Back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Anim", Meta = (ClampMin = "0", ClampMax = "90", Units = "deg"))
	float ForwardHalfAngleDeg = 45.f;

	/** Simulated proxies: time constant (s) of the first-order smoothing of the replicated velocity; 0 = raw. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Anim", Meta = (ClampMin = "0", Units = "s"))
	float ProxyVelocitySmoothingSeconds = 0.1f;
};

/** One skin: a material instance of M_Character_Master (ADR 0006 section 4.3) and its body colour (HUD, scoreboard, tint). */
USTRUCT(BlueprintType)
struct FVeloxSkin
{
	GENERATED_BODY()

	/** Skin name (ForcedEnemySkin in the user settings refers to it), e.g. Red. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Cosmetics")
	FName Name;

	/** MI_Skin_<Name>; may be empty until the art exists (the mesh then keeps its own material, tinted). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Cosmetics")
	TSoftObjectPtr<UMaterialInterface> Material;

	/** Linear body colour. The first entries equal UVeloxCombatSettings::PlayerColorPalette so HUD colours stay stable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|Cosmetics")
	FLinearColor BodyColor = FLinearColor::White;
};

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Character Cosmetics"))
class VELOX_API UVeloxCharacterCosmetics : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** SK_Velox (UE5 Mannequin bone names, ADR 0006 section 4.2). Empty: the pawn keeps PlaceholderBody. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** ABP_Velox (parent UVeloxAnimInstance); may be empty, then bUseSequencePlayback decides. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	TSoftClassPtr<UAnimInstance> AnimClass;

	/** Skins in SkinIndex order (= the old palette order, ADR 0006 section 4.3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	TArray<FVeloxSkin> Skins;

	/** Vector parameter of the skin material that takes the body colour (tint for SkinIndex past the skin list, dark corpses). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	FName BodyColorParameter = TEXT("BodyColor");

	/** Sequence per leg state for bUseSequencePlayback (Run is the fallback of RunSequences). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	TMap<EVeloxLegState, TSoftObjectPtr<UAnimSequence>> Sequences;

	/** Run_Fwd / Run_Back / Run_Left / Run_Right for bUseSequencePlayback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	TMap<EVeloxRunDirection, TSoftObjectPtr<UAnimSequence>> RunSequences;

	/** Leg-state cadence (ADR 0006 section 4.4), read by UVeloxAnimInstance and the sequence-playback driver. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	FVeloxAnimTuning AnimTuning;

	/** Mesh component relative Z (cm): feet at the capsule bottom (ADR 0006 section 4.1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics", Meta = (Units = "cm"))
	float MeshOffsetZ = -70.f;

	/** Mesh component relative yaw (deg): FBX forward convention (ADR 0006 section 4.1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics", Meta = (Units = "deg"))
	float MeshYawDeg = -90.f;

	/** Without an AnimClass: play Sequences directly on the mesh on leg-state changes (hard cuts, no fire slot). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	bool bUseSequencePlayback = true;

	/** First-person view model tunables (task 09's DA_ViewModelSettings_Default). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cosmetics")
	TSoftObjectPtr<UVeloxViewModelSettings> ViewModelSettings;

	/** Index of the skin named Name, INDEX_NONE if there is none. */
	int32 FindSkin(FName Name) const;

	/** The sequence for a leg state (Run: the direction's entry, else Sequences[Run]); null when unset or not loaded. */
	UAnimSequence* GetLoadedSequence(EVeloxLegState State, EVeloxRunDirection Direction) const;

	/** Every soft reference of this asset (mesh, anim class, skin materials, sequences, view model settings). */
	void GetSoftReferences(TArray<FSoftObjectPath>& OutPaths) const;

	/**
	 * Skin slot for a pawn: the forced enemy skin when bForceEnemySkin and the local player does not own the pawn (and
	 * the name exists), else SkinIndex wrapped onto the skin list. INDEX_NONE without skins or without either choice.
	 */
	static int32 ChooseSkinSlot(const UVeloxCharacterCosmetics* Cosmetics, bool bOwnedByLocalPlayer, bool bForceEnemySkin, FName ForcedEnemySkin, uint8 SkinIndex, uint8 NoSkin);

	/** Body colour for SkinIndex: the skin's BodyColor (sRGB) when the list has it, else the palette entry, else white. */
	static FColor ResolveBodyColor(const UVeloxCharacterCosmetics* Cosmetics, uint8 SkinIndex, const TArray<FColor>& Palette);
};
