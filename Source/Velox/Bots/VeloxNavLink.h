// Velox - authored nav link for bots (ADR 0005 section 4): jump pads, drops, gap jumps and rocket jumps.
//
// A simple point link (ANavLinkProxy::PointLinks[0]) built from the properties below; the smart-link machinery is not
// used (bSmartLinkIsRelevant = false): the Velox path follower (task 04) detects link segments itself through
// FNavMeshNodeFlags::IsNavLink() and UVeloxBotSubsystem::FindLinkByStart. Cost comes from the area of the link type
// (VeloxNavAreas.h), eligibility from the query filter (VeloxNavFilters.h). Placed by the map scripts
// (Tools/editor/create_*_map.py, conventions in Tools/editor/README.md). Server only: not loaded on clients.

#pragma once

#include "Navigation/NavLinkProxy.h"

#include "VeloxNavLink.generated.h"

class AVeloxJumpPad;
class UNavArea;

UENUM(BlueprintType)
enum class EVeloxNavLinkType : uint8
{
	/** Pad trigger centre (floor) -> landing point; the pad's own launch does the work. */
	JumpPad,
	/** Platform edge -> floor below: walk off. */
	Drop,
	/** Gap jump at ground speed. */
	Jump,
	/** Gap jump that needs strafe speed (RequiredSpeedCmS). */
	JumpFast,
	/** Rocket jump onto a ledge (RocketJumpPitchDeg, RunUpSeconds, MinHealthForRocketJump). */
	RocketJump,
};

UCLASS(ClassGroup = (Velox), HideCategories = (Input, Rendering, Replication, HLOD, Physics, Networking, SmartLink))
class VELOX_API AVeloxNavLink : public ANavLinkProxy
{
	GENERATED_BODY()

public:
	AVeloxNavLink(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Area class carrying the cost of a link type. */
	static TSubclassOf<UNavArea> AreaClassFor(EVeloxNavLinkType InType);
	/** Inverse of AreaClassFor; false for any other area (engine links, null area). */
	static bool LinkTypeForArea(const UClass* AreaClass, EVeloxNavLinkType& OutType);
	/** "JumpPad", "Drop", ... (log lines, DumpNav). */
	static const TCHAR* LinkTypeName(EVeloxNavLinkType InType);

	/** The point link these properties describe (actor-relative ends, as ANavLinkProxy stores them). */
	FNavigationLink BuildPointLink() const;

	/** Enables or disables the link at runtime: disabled = UNavArea_Null, the nav octree is updated (Dynamic regeneration). */
	UFUNCTION(BlueprintCallable, Category = "Velox|NavLink")
	void SetLinkEnabled(bool bInEnabled);

	UFUNCTION(BlueprintPure, Category = "Velox|NavLink")
	FVector GetStartWorld() const;

	UFUNCTION(BlueprintPure, Category = "Velox|NavLink")
	FVector GetEndWorld() const;

	EVeloxNavLinkType GetLinkType() const { return Type; }
	bool IsLinkEnabled() const { return bEnabled; }
	bool IsBothWays() const { return bBothWays; }
	AVeloxJumpPad* GetJumpPad() const { return JumpPad; }
	float GetSnapRadius() const { return LinkSnapRadius; }
	float GetSnapHeight() const { return LinkSnapHeight; }
	float GetRequiredSpeed() const { return RequiredSpeedCmS; }
	float GetRocketJumpPitch() const { return RocketJumpPitchDeg; }
	float GetRunUpSeconds() const { return RunUpSeconds; }
	int32 GetMinHealthForRocketJump() const { return MinHealthForRocketJump; }

protected:
	//~AActor interface
	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End of AActor interface

	/** Writes PointLinks[0] from the properties (and drops any other point link). */
	void RebuildPointLinks();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink")
	EVeloxNavLinkType Type = EVeloxNavLinkType::Drop;

	/** Link start, actor-relative (the map scripts place the actor at the start and leave this zero). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink", Meta = (MakeEditWidget = ""))
	FVector LinkStart = FVector::ZeroVector;

	/** Link end, actor-relative. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink", Meta = (MakeEditWidget = ""))
	FVector LinkEnd = FVector(100.0, 0.0, 0.0);

	/** BothWays instead of LeftToRight (symmetric Jump / JumpFast gaps only, authored per link). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink")
	bool bBothWays = false;

	/** JumpPad: the pad whose launch traverses the link. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink", Meta = (EditCondition = "Type == EVeloxNavLinkType::JumpPad"))
	TObjectPtr<AVeloxJumpPad> JumpPad;

	/** Jump / JumpFast: take-off speed the gap needs (informational, checked by the map script; the follower uses it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink", Meta = (ClampMin = "0", Units = "cm/s"))
	float RequiredSpeedCmS = 0.f;

	/** RocketJump: look-down pitch of the jump + fire tick (the rocket lands just behind the feet). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink", Meta = (ClampMin = "0", ClampMax = "90", Units = "deg"))
	float RocketJumpPitchDeg = 80.f;

	/** RocketJump: forward run-up before the jump + fire tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink", Meta = (ClampMin = "0", Units = "s"))
	float RunUpSeconds = 0.3f;

	/** RocketJump: the link is eligible only with at least this much health (self-damage ~33 after halving). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink", Meta = (ClampMin = "0"))
	int32 MinHealthForRocketJump = 70;

	/** Snap radius of both ends onto the nav mesh (FNavigationLinkBase::SnapRadius). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink", Meta = (ClampMin = "1", Units = "cm"))
	float LinkSnapRadius = 60.f;

	/** Snap height (bUseSnapHeight): platform tops are >= 300 cm apart, so snapping must not grab the floor below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink", Meta = (ClampMin = "1", Units = "cm"))
	float LinkSnapHeight = 100.f;

	/** Disabled links export UNavArea_Null (tests). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Velox|NavLink")
	bool bEnabled = true;
};
