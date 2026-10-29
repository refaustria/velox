// Velox - weapon tests (ADR 0003 sections 4, 5, 9; task 04).
//   Velox.Combat.FireRateBucket      token bucket + minimum spacing: exact rate, 2x rate, receive jitter, rail burst
//                                    (world-free, B19).
//   Velox.Combat.FireTiming          Q3 one-frame carry of NextFireTime (no banked double tap) and the switch gate (B18,
//                                    B19; world-free).
//   Velox.Combat.ShotValidation      every server reject reason from its input, a valid shot normalised (world-free),
//                                    the projectile payload (B26), the fire time-stamp clamp (B25) and the shared B15
//                                    origin rule (a private world for ValidateOrigin; the occlusion row is in LagCompTrace).
//   Velox.Combat.WeaponTable         slot lookup, weapnext/prev cycling; the DA_WeaponTable asset carries the Q3 numbers;
//                                    DA_CombatSettings_Default carries the 04b timing fields.
//   Velox.Combat.TargetDataSerialize FGameplayAbilityTargetData_VeloxShot NetSerialize round trip (struct and handle).
//   Velox.Combat.Inventory           loadout, AddWeapon, fire routing to the current weapon, switch drop/raise timing
//                                    and refire wait, the local SwitchEndTime gate (B18), through the ASC input path
//                                    (private game world).
//   Velox.Combat.HitscanDummy        RG / MG / SG / LG FireTest at a dummy (AVeloxDummyController) 10 m away: the
//                                    Q3 damage reaches its health and armor through TraceShot and GE_Damage.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Bots/VeloxDummyController.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxDamageMath.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/VeloxPlayerState.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "Weapons/VeloxCombatFeedbackComponent.h"
#include "Weapons/VeloxFireRateBucket.h"
#include "Weapons/VeloxFireTiming.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxGameplayAbility_FireHitscan.h"
#include "Weapons/VeloxGameplayAbility_FireProjectile.h"
#include "Weapons/VeloxShotTargetData.h"
#include "Weapons/VeloxShotValidation.h"
#include "Weapons/VeloxWeaponCheats.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"
#include "Weapons/VeloxWeaponTable.h"

namespace VeloxWeaponTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	const UVeloxWeaponTable* LoadTable()
	{
		return LoadObject<UVeloxWeaponTable>(nullptr, TEXT("/Game/Weapons/DA_WeaponTable.DA_WeaponTable"));
	}

	/** ValidateOrigin rows of Velox.Combat.ShotValidation (need a world; defined after FWeaponWorld). */
	void TestValidateOrigin(FAutomationTestBase& Test);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatFireRateBucketTest, "Velox.Combat.FireRateBucket", VeloxWeaponTest::Flags)

bool FVeloxCombatFireRateBucketTest::RunTest(const FString& Parameters)
{
	// B19 spacing at the default tolerance 0.25: MG/LG 0 (bucket only), rail 1.25 s.
	constexpr float SpacingTolerance = 0.25f;
	TestEqual(TEXT("spacing LG 0"), FVeloxFireRateBucket::MinSpacingFor(0.05f, SpacingTolerance), 0.f);
	TestEqual(TEXT("spacing MG 0"), FVeloxFireRateBucket::MinSpacingFor(0.1f, SpacingTolerance), 0.f);
	TestEqual(TEXT("spacing SG 0.75"), FVeloxFireRateBucket::MinSpacingFor(1.0f, SpacingTolerance), 0.75f, 1e-6f);
	TestEqual(TEXT("spacing RL 0.55"), FVeloxFireRateBucket::MinSpacingFor(0.8f, SpacingTolerance), 0.55f, 1e-6f);
	TestEqual(TEXT("spacing RG 1.25"), FVeloxFireRateBucket::MinSpacingFor(1.5f, SpacingTolerance), 1.25f, 1e-6f);

	// Exact rate: LG 0.05 s for 60 s, MG 0.1 s, rail 1.5 s (with its spacing); none rejected.
	for (const float Interval : { 0.05f, 0.1f, 1.5f })
	{
		FVeloxFireRateBucket Bucket;
		int32 Accepted = 0;
		const int32 Shots = 1200;
		const float MinSpacing = FVeloxFireRateBucket::MinSpacingFor(Interval, SpacingTolerance);
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			Accepted += Bucket.TryConsume(100.0 + Index * static_cast<double>(Interval), Interval, 3.f, MinSpacing) ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("exact rate %.2f s: all accepted"), Interval), Accepted, Shots);
	}

	// Twice the rate: the burst of 3, then every other shot -> half rejected; long-term rate exactly 1/Interval.
	{
		FVeloxFireRateBucket Bucket;
		const float Interval = 0.05f;
		int32 Accepted = 0;
		const int32 Shots = 400;
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			Accepted += Bucket.TryConsume(Index * 0.025, Interval, 3.f, FVeloxFireRateBucket::MinSpacingFor(Interval, SpacingTolerance)) ? 1 : 0;
		}
		const int32 Expected = 3 + FMath::FloorToInt32((Shots - 1) * 0.025 / Interval);
		TestTrue(FString::Printf(TEXT("2x rate: %d accepted of %d (expected %d +- 1)"), Accepted, Shots, Expected), FMath::Abs(Accepted - Expected) <= 1);
	}

	// Receive jitter: sent every 50 ms, received with up to 40 ms of extra delay (spikes and clumps): all accepted.
	{
		FVeloxFireRateBucket Bucket;
		FRandomStream Stream(1234);
		int32 Accepted = 0;
		const int32 Shots = 2000;
		double LastReceive = 0.0;
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			const double Receive = FMath::Max(Index * 0.05 + Stream.FRandRange(0.0, 0.04), LastReceive);
			LastReceive = Receive;
			Accepted += Bucket.TryConsume(Receive, 0.05f, 3.f, FVeloxFireRateBucket::MinSpacingFor(0.05f, SpacingTolerance)) ? 1 : 0;
		}
		TestEqual(TEXT("40 ms jitter at 50 ms: all accepted"), Accepted, Shots);
	}

	// The bucket alone (spacing 0): capacity shots pass at once after a pause, the next is rejected (all over-rate shots are).
	{
		FVeloxFireRateBucket Bucket;
		TestTrue(TEXT("first shot"), Bucket.TryConsume(10.0, 1.0f, 3.f, 0.f));
		TestTrue(TEXT("burst 2"), Bucket.TryConsume(10.0, 1.0f, 3.f, 0.f));
		TestTrue(TEXT("burst 3"), Bucket.TryConsume(10.0, 1.0f, 3.f, 0.f));
		TestTrue(TEXT("over the burst: rate"), Bucket.Consume(10.0, 1.0f, 3.f, 0.f) == EVeloxFireRateResult::Rate);
		TestFalse(TEXT("still over at +0.5 s"), Bucket.TryConsume(10.5, 1.0f, 3.f, 0.f));
		TestTrue(TEXT("refilled at +1 s"), Bucket.TryConsume(11.0, 1.0f, 3.f, 0.f));
	}

	// B19: with the spacing, three rails in one frame after a pause -> one accepted, two rejected as spacing (and free).
	{
		FVeloxFireRateBucket Bucket;
		const float Interval = 1.5f;
		const float MinSpacing = FVeloxFireRateBucket::MinSpacingFor(Interval, SpacingTolerance);
		const double T = 10.0;
		TestTrue(TEXT("rail 1 at t accepted"), Bucket.Consume(T, Interval, 3.f, MinSpacing) == EVeloxFireRateResult::Accepted);
		TestTrue(TEXT("rail 2 at t: spacing"), Bucket.Consume(T, Interval, 3.f, MinSpacing) == EVeloxFireRateResult::Spacing);
		TestTrue(TEXT("rail 3 at t: spacing"), Bucket.Consume(T, Interval, 3.f, MinSpacing) == EVeloxFireRateResult::Spacing);
		TestTrue(TEXT("rail at t + 1.2: spacing"), Bucket.Consume(T + 1.2, Interval, 3.f, MinSpacing) == EVeloxFireRateResult::Spacing);
		TestTrue(TEXT("rail at t + 1.3 accepted (resend compression inside the tolerance)"), Bucket.Consume(T + 1.3, Interval, 3.f, MinSpacing) == EVeloxFireRateResult::Accepted);
		TestTrue(TEXT("rail at t + 2.6 accepted"), Bucket.Consume(T + 2.6, Interval, 3.f, MinSpacing) == EVeloxFireRateResult::Accepted);
	}

	// Rails sent every 1.5 s, received with up to 0.25 s of jitter (resend compression): all accepted, long-term rate kept.
	{
		FVeloxFireRateBucket Bucket;
		FRandomStream Stream(4321);
		const float Interval = 1.5f;
		const float MinSpacing = FVeloxFireRateBucket::MinSpacingFor(Interval, SpacingTolerance);
		int32 Accepted = 0;
		const int32 Shots = 400;
		double LastReceive = 0.0;
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			const double Receive = FMath::Max(Index * 1.5 + Stream.FRandRange(0.0, 0.2499), LastReceive);
			LastReceive = Receive;
			Accepted += Bucket.TryConsume(Receive, Interval, 3.f, MinSpacing) ? 1 : 0;
		}
		TestEqual(TEXT("0.25 s jitter at 1.5 s: all accepted"), Accepted, Shots);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatFireTimingTest, "Velox.Combat.FireTiming", VeloxWeaponTest::Flags)

bool FVeloxCombatFireTimingTest::RunTest(const FString& Parameters)
{
	using namespace VeloxFireTiming;

	// Held fire, one check per frame (the shooter's CanActivateAbility + AdvanceNextFireTime): 1200 shots span exactly
	// 1199 intervals (within one frame), at 60 Hz and at a frame rate that does not divide the intervals (144 Hz).
	for (const double FrameHz : { 60.0, 144.0 })
	{
		const double Dt = 1.0 / FrameHz;
		for (const double Interval : { 0.05, 0.1, 1.5 })
		{
			double Next = -1.0e9;
			double First = 0.0;
			double Last = 0.0;
			int32 Shots = 0;
			for (int64 Frame = 0; Shots < 1200; ++Frame)
			{
				const double Now = 100.0 + static_cast<double>(Frame) * Dt;
				if (Now >= Next)
				{
					First = Shots == 0 ? Now : First;
					Last = Now;
					Next = AdvanceNextFireTime(Next, Now, Interval, Dt);
					++Shots;
				}
			}
			const double Span = Last - First;
			const double Exact = 1199.0 * Interval;
			TestTrue(FString::Printf(TEXT("%.0f Hz, %.2f s: 1200 shots span %.4f s = 1199 intervals %.4f (+ <= 1 frame)"), FrameHz, Interval, Span, Exact),
				Span >= Exact - 1e-6 && Span <= Exact + Dt + 1e-6);
		}
	}

	// No bank: a 1.5 s rail fired 1.49 s after its schedule restarts; the following shot is a full interval later.
	{
		const double Next = AdvanceNextFireTime(10.0, 11.49, 1.5, 1.0 / 60.0);
		TestEqual(TEXT("late shot restarts: Next = Now + 1.5"), Next, 11.49 + 1.5, 1e-9);
		TestTrue(TEXT("no second rail 10 ms later"), 11.50 < Next);
	}
	// A shot inside the crossing frame keeps the schedule (Q3 weaponTime carry of < 1 frame).
	TestEqual(TEXT("crossing frame carries: Next + Interval"), AdvanceNextFireTime(10.0, 10.004, 1.5, 1.0 / 60.0), 11.5, 1e-9);
	TestEqual(TEXT("exactly one frame late still carries"), AdvanceNextFireTime(10.0, 10.0 + 1.0 / 60.0, 1.5, 1.0 / 60.0), 11.5, 1e-9);
	TestEqual(TEXT("first shot (no schedule) starts from now"), AdvanceNextFireTime(-1.0e9, 5.0, 0.1, 1.0 / 60.0), 5.1, 1e-9);

	// Switch gate (B18).
	const double End = 20.0;
	TestFalse(TEXT("local: blocked before SwitchEndTime"), IsSwitchFireAllowed(true, End - 1.0 / 60.0, End, 0.0, 0.25));
	TestTrue(TEXT("local: allowed at SwitchEndTime"), IsSwitchFireAllowed(true, End, End, 0.0, 0.25));
	TestTrue(TEXT("local: no switch (0) allowed"), IsSwitchFireAllowed(true, 1.0, 0.0, 0.0, 0.25));
	TestTrue(TEXT("local: ignores the server-side remaining time"), IsSwitchFireAllowed(true, End, End, 5.0, 0.25));
	TestTrue(TEXT("server: remaining 0.25 allowed"), IsSwitchFireAllowed(false, 0.0, 0.0, 0.25, 0.25));
	TestFalse(TEXT("server: remaining 0.26 blocked"), IsSwitchFireAllowed(false, 0.0, 0.0, 0.26, 0.25));
	TestTrue(TEXT("server: no GE (remaining 0) allowed"), IsSwitchFireAllowed(false, 0.0, 0.0, 0.0, 0.25));
	TestTrue(TEXT("server: ignores the local switch end"), IsSwitchFireAllowed(false, 0.0, 1.0e9, 0.1, 0.25));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatShotValidationTest, "Velox.Combat.ShotValidation", VeloxWeaponTest::Flags)

bool FVeloxCombatShotValidationTest::RunTest(const FString& Parameters)
{
	constexpr float Capacity = 3.f;
	constexpr float Tolerance = 0.25f;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const float FInf = std::numeric_limits<float>::infinity();
	const float FNaN = std::numeric_limits<float>::quiet_NaN();

	FGameplayAbilityTargetData_VeloxShot Good;
	Good.Origin = FVector(100.0, 200.0, 64.0);
	Good.Dir = FVector(3.0, 4.0, 0.0); // not normalised on purpose
	Good.ViewServerTime = 12.5f;
	Good.ShotSeq = 42;
	Good.HintTargetStamp = 7.f; // no hint bit: dropped

	// Each payload case with a fresh MG bucket (0.1 s, spacing 0): the reason comes from the payload alone.
	auto Check = [&](const TCHAR* Name, const FGameplayAbilityTargetData* Raw, EVeloxShotReject Expected)
	{
		FVeloxFireRateBucket Bucket;
		FVeloxValidatedShot Out;
		const EVeloxShotReject Got = VeloxShotValidation::Validate(Raw, Bucket, 10.0, 0.1f, Capacity, Tolerance, Out);
		TestEqual(FString::Printf(TEXT("%s -> %s (got %s)"), Name, LexToString(Expected), LexToString(Got)), static_cast<int32>(Got), static_cast<int32>(Expected));
		return Out;
	};

	{
		const FVeloxValidatedShot Out = Check(TEXT("valid"), &Good, EVeloxShotReject::None);
		TestTrue(TEXT("valid: dir normalised"), Out.Dir.Equals(FVector(0.6, 0.8, 0.0), 1e-6));
		TestTrue(TEXT("valid: origin"), Out.Origin.Equals(Good.Origin, 1e-6));
		TestEqual(TEXT("valid: view time"), Out.ViewServerTime, 12.5);
		TestEqual(TEXT("valid: seq"), static_cast<int32>(Out.ShotSeq), 42);
		TestEqual(TEXT("valid: no hint, no stamp"), Out.HintTargetStamp, 0.f);
	}
	Check(TEXT("null data"), nullptr, EVeloxShotReject::Type);
	{
		FGameplayAbilityTargetData_SingleTargetHit Wrong;
		Check(TEXT("wrong struct type"), &Wrong, EVeloxShotReject::Type);
	}
	{
		FGameplayAbilityTargetData_VeloxShot Bad = Good;
		Bad.Origin = FVector(NaN, 0.0, 0.0);
		Check(TEXT("NaN origin"), &Bad, EVeloxShotReject::Origin);
	}
	{
		FGameplayAbilityTargetData_VeloxShot Bad = Good;
		Bad.Dir = FVector::ZeroVector;
		Check(TEXT("zero dir"), &Bad, EVeloxShotReject::Dir);
		Bad.Dir = FVector(0.0, NaN, 0.0);
		Check(TEXT("NaN dir"), &Bad, EVeloxShotReject::Dir);
	}
	{
		FGameplayAbilityTargetData_VeloxShot Bad = Good;
		Bad.ViewServerTime = FInf;
		Check(TEXT("+inf view time"), &Bad, EVeloxShotReject::Time);
		Bad.ViewServerTime = -FInf;
		Check(TEXT("-inf view time"), &Bad, EVeloxShotReject::Time);
		Bad.ViewServerTime = FNaN;
		Check(TEXT("NaN view time"), &Bad, EVeloxShotReject::Time);
		Bad.ViewServerTime = 12.5f;
		Bad.HintTargetStamp = FNaN;
		Check(TEXT("NaN hint stamp"), &Bad, EVeloxShotReject::Time);
	}

	// Over-rate: MG (0.1 s, spacing 0) four shots in one frame -> the fourth is rate.
	{
		FVeloxFireRateBucket Bucket;
		FVeloxValidatedShot Out;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TestTrue(FString::Printf(TEXT("MG burst %d accepted"), Index + 1), VeloxShotValidation::Validate(&Good, Bucket, 10.0, 0.1f, Capacity, Tolerance, Out) == EVeloxShotReject::None);
		}
		TestTrue(TEXT("MG fourth in one frame: rate"), VeloxShotValidation::Validate(&Good, Bucket, 10.0, 0.1f, Capacity, Tolerance, Out) == EVeloxShotReject::Rate);
		TestEqual(TEXT("rejected shot still names its seq"), static_cast<int32>(Out.ShotSeq), 42);
	}
	// Under-spacing: two rails 1.0 s apart (spacing 1.25) -> spacing.
	{
		FVeloxFireRateBucket Bucket;
		FVeloxValidatedShot Out;
		TestTrue(TEXT("rail 1 accepted"), VeloxShotValidation::Validate(&Good, Bucket, 10.0, 1.5f, Capacity, Tolerance, Out) == EVeloxShotReject::None);
		TestTrue(TEXT("rail 2 after 1.0 s: spacing"), VeloxShotValidation::Validate(&Good, Bucket, 11.0, 1.5f, Capacity, Tolerance, Out) == EVeloxShotReject::Spacing);
	}
	// The bucket is consumed before the payload: three malformed shots empty it, the valid fourth is rate.
	{
		FVeloxFireRateBucket Bucket;
		FVeloxValidatedShot Out;
		FGameplayAbilityTargetData_SingleTargetHit Wrong;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TestTrue(FString::Printf(TEXT("wrong type %d: type"), Index + 1), VeloxShotValidation::Validate(&Wrong, Bucket, 10.0, 1.0f, Capacity, 1.0f, Out) == EVeloxShotReject::Type);
		}
		TestTrue(TEXT("valid shot after three malformed ones: rate (they cost tokens)"), VeloxShotValidation::Validate(&Good, Bucket, 10.0, 1.0f, Capacity, 1.0f, Out) == EVeloxShotReject::Rate);
	}

	// B26: the projectile payload (subclass) through the same checks, plus a finite fire move time stamp.
	{
		FGameplayAbilityTargetData_VeloxProjectileShot Rocket;
		static_cast<FGameplayAbilityTargetData_VeloxShot&>(Rocket) = Good;
		Rocket.FireMoveTimeStamp = 37.25f;
		const FVeloxValidatedShot Out = Check(TEXT("projectile: valid"), &Rocket, EVeloxShotReject::None);
		TestTrue(TEXT("projectile: flagged"), Out.bProjectileShot);
		TestEqual(TEXT("projectile: fire move time stamp"), Out.FireMoveTimeStamp, 37.25f);
		TestEqual(TEXT("projectile: seq"), static_cast<int32>(Out.ShotSeq), 42);
		TestFalse(TEXT("hitscan payload: not flagged"), Check(TEXT("hitscan: valid"), &Good, EVeloxShotReject::None).bProjectileShot);
		Rocket.FireMoveTimeStamp = FNaN;
		Check(TEXT("projectile: NaN fire stamp"), &Rocket, EVeloxShotReject::Time);
		Rocket.FireMoveTimeStamp = FInf;
		Check(TEXT("projectile: +inf fire stamp"), &Rocket, EVeloxShotReject::Time);
		Rocket.FireMoveTimeStamp = -FInf;
		Check(TEXT("projectile: -inf fire stamp"), &Rocket, EVeloxShotReject::Time);
		Rocket.FireMoveTimeStamp = 37.25f;
		Rocket.Origin = FVector(NaN, 0.0, 0.0);
		Check(TEXT("projectile: NaN origin rejected like hitscan"), &Rocket, EVeloxShotReject::Origin);
	}

	// B25 / review of task 05 defect 6: the fire time stamp clamp (window 0.25 s, reset period 240 s).
	{
		constexpr float Window = 0.25f;
		constexpr float Period = 240.f;
		EVeloxFireStampClamp Result = EVeloxFireStampClamp::Clamped;
		TestEqual(TEXT("stamp: inside the window kept"), VeloxShotValidation::ClampFireMoveTimeStamp(100.1f, 100.f, Window, Period, Result), 100.1f);
		TestTrue(TEXT("stamp: inside -> none"), Result == EVeloxFireStampClamp::None);
		TestEqual(TEXT("stamp: ahead clamped"), VeloxShotValidation::ClampFireMoveTimeStamp(101.f, 100.f, Window, Period, Result), 100.25f);
		TestTrue(TEXT("stamp: ahead -> clamped"), Result == EVeloxFireStampClamp::Clamped);
		TestEqual(TEXT("stamp: behind clamped"), VeloxShotValidation::ClampFireMoveTimeStamp(99.f, 100.f, Window, Period, Result), 99.75f);
		TestTrue(TEXT("stamp: behind -> clamped"), Result == EVeloxFireStampClamp::Clamped);
		TestEqual(TEXT("stamp: client reset just before the shot, server not yet: kept"), VeloxShotValidation::ClampFireMoveTimeStamp(0.02f, 239.95f, Window, Period, Result), 0.02f);
		TestTrue(TEXT("stamp: reset (client side) -> reset"), Result == EVeloxFireStampClamp::Reset);
		TestEqual(TEXT("stamp: server reset, client stamp from before: kept"), VeloxShotValidation::ClampFireMoveTimeStamp(239.95f, 0.02f, Window, Period, Result), 239.95f);
		TestTrue(TEXT("stamp: reset (server side) -> reset"), Result == EVeloxFireStampClamp::Reset);
		VeloxShotValidation::ClampFireMoveTimeStamp(5.f, 125.1f, Window, Period, Result);
		TestTrue(TEXT("stamp: just over half a period apart -> reset"), Result == EVeloxFireStampClamp::Reset);
		VeloxShotValidation::ClampFireMoveTimeStamp(5.f, 124.9f, Window, Period, Result);
		TestTrue(TEXT("stamp: just under half a period apart -> clamped"), Result == EVeloxFireStampClamp::Clamped);
	}

	VeloxWeaponTest::TestValidateOrigin(*this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatWeaponTableTest, "Velox.Combat.WeaponTable", VeloxWeaponTest::Flags)

bool FVeloxCombatWeaponTableTest::RunTest(const FString& Parameters)
{
	// Cycling (Q3 weapnext/weapprev) over owned bits, wrapping.
	TestEqual(TEXT("next from 0 over {0,2,4}"), UVeloxWeaponTable::CycleOwned(0b10101, 6, 0, +1), 2);
	TestEqual(TEXT("next from 4 wraps"), UVeloxWeaponTable::CycleOwned(0b10101, 6, 4, +1), 0);
	TestEqual(TEXT("prev from 0 wraps"), UVeloxWeaponTable::CycleOwned(0b10101, 6, 0, -1), 4);
	TestEqual(TEXT("only the current owned: itself"), UVeloxWeaponTable::CycleOwned(0b00100, 6, 2, +1), 2);
	TestEqual(TEXT("nothing owned"), UVeloxWeaponTable::CycleOwned(0, 6, 0, +1), INDEX_NONE);
	TestEqual(TEXT("no current: next is the first"), UVeloxWeaponTable::CycleOwned(0b10100, 6, INDEX_NONE, +1), 2);
	TestEqual(TEXT("bits beyond the table ignored"), UVeloxWeaponTable::CycleOwned(0b1000000, 6, 0, +1), INDEX_NONE);

	const UVeloxWeaponTable* Table = VeloxWeaponTest::LoadTable();
	if (!TestNotNull(TEXT("DA_WeaponTable (bootstrap script)"), Table))
	{
		return false;
	}
	TestEqual(TEXT("six weapons"), Table->Num(), 6);
	struct FRow { const TCHAR* Name; int32 Slot; float Interval; int32 Damage; int32 Pellets; float RangeCm; bool bHitscan; };
	using namespace VeloxGameplayTags;
	const FRow Rows[] = {
		{ TEXT("MachineGun"), 2, 0.1f, 7, 1, 20480.f, true },
		{ TEXT("Shotgun"), 3, 1.0f, 10, 11, 20480.f, true },
		{ TEXT("RocketLauncher"), 5, 0.8f, 100, 1, 20480.f, false },
		{ TEXT("LightningGun"), 6, 0.05f, 8, 1, 1920.f, true },
		{ TEXT("Railgun"), 7, 1.5f, 100, 1, 20480.f, true },
		{ TEXT("PlasmaGun"), 8, 0.1f, 20, 1, 20480.f, false },
	};
	int32 LastSlot = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rows) && Index < Table->Num(); ++Index)
	{
		const FRow& Row = Rows[Index];
		const UVeloxWeaponDefinition* Def = Table->Get(Index);
		if (!TestNotNull(FString::Printf(TEXT("%s definition"), Row.Name), Def))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s tag"), Row.Name), Def->WeaponTag.ToString().EndsWith(Row.Name));
		TestEqual(FString::Printf(TEXT("%s slot"), Row.Name), Def->Slot, Row.Slot);
		TestTrue(FString::Printf(TEXT("%s in slot order"), Row.Name), Def->Slot > LastSlot);
		LastSlot = Def->Slot;
		TestEqual(FString::Printf(TEXT("%s interval"), Row.Name), Def->FireInterval, Row.Interval, 1e-5f);
		TestEqual(FString::Printf(TEXT("%s damage"), Row.Name), Def->Damage, Row.Damage);
		TestEqual(FString::Printf(TEXT("%s pellets"), Row.Name), Def->PelletCount, Row.Pellets);
		TestEqual(FString::Printf(TEXT("%s range"), Row.Name), Def->RangeCm, Row.RangeCm, 0.01f);
		TestEqual(FString::Printf(TEXT("%s hitscan ability"), Row.Name), Def->FireAbility.Get() == UVeloxGameplayAbility_FireHitscan::StaticClass(), Row.bHitscan);
		TestTrue(FString::Printf(TEXT("%s damage/ammo/cue tags"), Row.Name), Def->DamageTag.IsValid() && Def->AmmoTag.IsValid() && Def->FireCueTag.IsValid() && Def->ImpactCueTag.IsValid());
		TestEqual(FString::Printf(TEXT("%s slot lookup"), Row.Name), Table->FindBySlot(Row.Slot), Index);
		TestEqual(FString::Printf(TEXT("%s index"), Row.Name), Table->IndexOf(Def), Index);
	}
	if (Table->Num() == 6)
	{
		TestEqual(TEXT("MG spread atan(200/8192)"), Table->Get(0)->SpreadAngleDeg, 1.3987f, 1e-3f);
		TestEqual(TEXT("SG spread atan(700/8192)"), Table->Get(1)->SpreadAngleDeg, 4.8838f, 1e-3f);
		TestTrue(TEXT("RG penetrates up to 4"), Table->Get(4)->bPenetratesPawns && Table->Get(4)->MaxPenetrations == 4);
		TestTrue(TEXT("LG beam cue"), Table->Get(3)->bBeam && Table->Get(3)->BeamCueTag == GameplayCue_Weapon_LightningGun_Beam);
		TestEqual(TEXT("RL splash 100 / 300 cm, 2250 cm/s"), FVector(Table->Get(2)->SplashDamage, Table->Get(2)->SplashRadiusCm, Table->Get(2)->ProjectileSpeed), FVector(100, 300, 2250));
		TestEqual(TEXT("PG splash 15 / 50 cm, 5000 cm/s"), FVector(Table->Get(5)->SplashDamage, Table->Get(5)->SplashRadiusCm, Table->Get(5)->ProjectileSpeed), FVector(15, 50, 5000));
		TestEqual(TEXT("slot 1 is empty"), Table->FindBySlot(1), INDEX_NONE);
	}

	// Task 04b fields (B18, B19, B23) in the bootstrap asset.
	const UVeloxCombatSettings* Settings = LoadObject<UVeloxCombatSettings>(nullptr, TEXT("/Game/Data/DA_CombatSettings_Default.DA_CombatSettings_Default"));
	if (TestNotNull(TEXT("DA_CombatSettings_Default"), Settings))
	{
		TestEqual(TEXT("SwitchTimingToleranceSeconds 0.25"), Settings->SwitchTimingToleranceSeconds, 0.25f);
		TestEqual(TEXT("FireRateSpacingTolerance 0.25"), Settings->FireRateSpacingTolerance, 0.25f);
		TestEqual(TEXT("BeamKeepAliveSeconds 0.25"), Settings->BeamKeepAliveSeconds, 0.25f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatTargetDataSerializeTest, "Velox.Combat.TargetDataSerialize", VeloxWeaponTest::Flags)

bool FVeloxCombatTargetDataSerializeTest::RunTest(const FString& Parameters)
{
	FGameplayAbilityTargetData_VeloxShot In;
	In.Origin = FVector(1234.567, -89.012, 155.0);
	In.Dir = FVector(0.6, 0.64, -0.48);
	In.ViewServerTime = 1834.21875f;
	In.ShotSeq = 251;
	In.HintTargetStamp = 77.123456f;

	// Struct NetSerialize without a package map: the hint cannot be named and is dropped (one bit).
	FBitWriter Writer(1024, true);
	bool bOk = false;
	In.NetSerialize(Writer, nullptr, bOk);
	TestTrue(TEXT("write ok"), bOk && !Writer.IsError());
	TestTrue(FString::Printf(TEXT("compact: %lld bits"), Writer.GetNumBits()), Writer.GetNumBits() <= 200);

	FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
	FGameplayAbilityTargetData_VeloxShot Out;
	Out.NetSerialize(Reader, nullptr, bOk);
	TestTrue(TEXT("read ok"), bOk && !Reader.IsError());
	TestTrue(TEXT("origin to 1/100 cm"), Out.Origin.Equals(In.Origin, 0.01));
	TestTrue(TEXT("dir to 1e-4"), Out.Dir.Equals(In.Dir, 1e-4));
	TestEqual(TEXT("view time exact float"), Out.ViewServerTime, In.ViewServerTime);
	TestEqual(TEXT("shot seq"), static_cast<int32>(Out.ShotSeq), 251);
	TestFalse(TEXT("no hint"), Out.bHasHint);

	// The hint bit with a stamp round-trips exactly as a float (B12): checked on the stamp field directly.
	{
		FBitWriter StampWriter(64, true);
		float Stamp = In.HintTargetStamp;
		StampWriter << Stamp;
		FBitReader StampReader(StampWriter.GetData(), StampWriter.GetNumBits());
		float StampOut = 0.f;
		StampReader << StampOut;
		TestEqual(TEXT("stamp exact float"), StampOut, In.HintTargetStamp);
	}

	// Through the handle (script-struct cache of UAbilitySystemGlobals): the type survives.
	FGameplayAbilityTargetDataHandle HandleIn(new FGameplayAbilityTargetData_VeloxShot(In));
	FBitWriter HandleWriter(2048, true);
	HandleIn.NetSerialize(HandleWriter, nullptr, bOk);
	FBitReader HandleReader(HandleWriter.GetData(), HandleWriter.GetNumBits());
	FGameplayAbilityTargetDataHandle HandleOut;
	HandleOut.NetSerialize(HandleReader, nullptr, bOk);
	const FGameplayAbilityTargetData* Raw = HandleOut.Get(0);
	if (TestTrue(TEXT("handle carries one VeloxShot"), HandleOut.Num() == 1 && Raw && Raw->GetScriptStruct() == FGameplayAbilityTargetData_VeloxShot::StaticStruct()))
	{
		const FGameplayAbilityTargetData_VeloxShot* Shot = static_cast<const FGameplayAbilityTargetData_VeloxShot*>(Raw);
		TestEqual(TEXT("handle: shot seq"), static_cast<int32>(Shot->ShotSeq), 251);
		TestTrue(TEXT("handle: origin"), Shot->Origin.Equals(In.Origin, 0.01));
	}
	return true;
}

namespace VeloxWeaponTest
{
	/** Private game world with a floor; combatants are pawns possessed by controllers that own an AVeloxPlayerState. */
	class FWeaponWorld
	{
	public:
		FWeaponWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("VeloxWeaponTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay();
			if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
			{
				AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -50), FRotator::ZeroRotator);
				Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
				Floor->SetActorScale3D(FVector(60, 60, 1));
			}
		}

		~FWeaponWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::LevelTransition);
				GEngine->ShutdownWorldNetDriver(World);
				World->DestroyWorld(true);
				World->SetPhysicsScene(nullptr);
				GEngine->DestroyWorldContext(World);
				World->RemoveFromRoot();
				World = nullptr;
			}
		}

		/** Pawn at Location possessed by a new controller of ControllerClass with its own Velox PlayerState. */
		AVeloxCharacter* Spawn(const FVector& Location, TSubclassOf<AAIController> ControllerClass, AAIController** OutController = nullptr)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVeloxCharacter* Pawn = World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
			AVeloxPlayerState* PlayerState = World->SpawnActor<AVeloxPlayerState>();
			AAIController* Controller = World->SpawnActor<AAIController>(ControllerClass, FTransform::Identity, Params);
			if (!Pawn || !PlayerState || !Controller)
			{
				return nullptr;
			}
			// No game mode in a private world: the controller gets its PlayerState by hand before possession.
			Controller->PlayerState = PlayerState;
			PlayerState->SetOwner(Controller);
			Controller->Possess(Pawn);
			if (OutController)
			{
				*OutController = Controller;
			}
			return Pawn;
		}

		void Advance(float Seconds, float Dt = 1.f / 60.f)
		{
			for (float T = 0.f; T < Seconds - 1e-4f; T += Dt)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Dt);
			}
		}

		/** Ticks 60 Hz frames, then one partial frame, so the world time ends at Target (within float rounding, >= Target). */
		void AdvanceTo(double Target)
		{
			constexpr float Dt = 1.f / 60.f;
			while (World->GetTimeSeconds() + Dt < Target)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Dt);
			}
			// A partial frame (rounded up by 1 us so float rounding cannot leave the world time just short of Target).
			const double Rest = Target - World->GetTimeSeconds();
			if (Rest > 0.0)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, static_cast<float>(Rest + 1e-6));
			}
		}

		UWorld* World = nullptr;
	};

	AVeloxPlayerState* PS(const AVeloxCharacter* Pawn) { return Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr; }

	void TestValidateOrigin(FAutomationTestBase& Test)
	{
		constexpr float MaxErrorCm = 75.f;
		FWeaponWorld W;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const AVeloxCharacter* Shooter = W.World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, Params);
		if (!Test.TestNotNull(TEXT("origin: shooter"), Shooter))
		{
			return;
		}
		const FVector Eye = Shooter->GetPawnViewLocation();
		const TCHAR* Reason = TEXT("unset");
		const FVector Near = Eye + FVector(0.0, 30.0, -10.0);
		Test.TestTrue(TEXT("origin: within tolerance, nothing in between -> unchanged"), VeloxShotValidation::ValidateOrigin(*W.World, *Shooter, Near, MaxErrorCm, Reason).Equals(Near, 1e-6));
		Test.TestNull(TEXT("origin: within tolerance -> no reason"), Reason);
		const FVector Tiny = Eye + FVector(0.5, 0.0, 0.0);
		Test.TestTrue(TEXT("origin: within 1 cm -> unchanged"), VeloxShotValidation::ValidateOrigin(*W.World, *Shooter, Tiny, MaxErrorCm, Reason).Equals(Tiny, 1e-6) && !Reason);
		const FVector NaNOrigin(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
		Test.TestTrue(TEXT("origin: NaN -> eye"), VeloxShotValidation::ValidateOrigin(*W.World, *Shooter, NaNOrigin, MaxErrorCm, Reason).Equals(Eye, 1e-6));
		Test.TestTrue(TEXT("origin: NaN -> reason nan"), Reason && FCString::Strcmp(Reason, TEXT("nan")) == 0);
		const FVector Far = Eye + FVector(0.0, MaxErrorCm + 25.0, 0.0);
		Test.TestTrue(TEXT("origin: beyond MaxOriginErrorCm -> eye"), VeloxShotValidation::ValidateOrigin(*W.World, *Shooter, Far, MaxErrorCm, Reason).Equals(Eye, 1e-6));
		Test.TestTrue(TEXT("origin: beyond -> reason distance"), Reason && FCString::Strcmp(Reason, TEXT("distance")) == 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatInventoryTest, "Velox.Combat.Inventory", VeloxWeaponTest::Flags)

bool FVeloxCombatInventoryTest::RunTest(const FString& Parameters)
{
	using namespace VeloxWeaponTest;
	using namespace VeloxGameplayTags;

	FWeaponWorld W;
	AVeloxCharacter* Pawn = W.Spawn(FVector(0, 0, 100), AAIController::StaticClass());
	AVeloxPlayerState* PlayerState = PS(Pawn);
	if (!TestNotNull(TEXT("pawn"), Pawn) || !TestNotNull(TEXT("player state"), PlayerState))
	{
		return false;
	}
	UVeloxWeaponInventoryComponent* Inventory = PlayerState->GetWeaponInventory();
	UVeloxAbilitySystemComponent* ASC = PlayerState->GetVeloxAbilitySystemComponent();
	const UVeloxWeaponTable* Table = Inventory ? Inventory->GetWeaponTable() : nullptr;
	if (!TestNotNull(TEXT("inventory"), Inventory) || !TestNotNull(TEXT("weapon table from the combat settings"), Table) || Table->Num() != 6)
	{
		return false;
	}
	const int32 MG = Table->FindBySlot(2);
	const int32 RG = Table->FindBySlot(7);
	const int32 RL = Table->FindBySlot(5);

	// Spawn: the loadout (MG, selected) with 100 bullets from GE_SpawnInit; one fire spec routed by InputTag.Fire, one switch spec.
	TestEqual(TEXT("loadout: MG only"), static_cast<int32>(Inventory->GetOwnedMask()), 1 << MG);
	TestEqual(TEXT("loadout: MG selected"), Inventory->GetCurrentIndex(), MG);
	TestEqual(TEXT("loadout: 100 bullets"), PlayerState->GetAmmoSet()->GetBullets(), 100.f);
	FGameplayAbilitySpec* MGSpec = Inventory->FindFireSpec(MG);
	if (!TestNotNull(TEXT("MG fire spec"), MGSpec))
	{
		return false;
	}
	TestTrue(TEXT("MG spec: SourceObject = definition, InputTag.Fire"), MGSpec->SourceObject.Get() == Table->Get(MG) && MGSpec->GetDynamicSpecSourceTags().HasTagExact(InputTag_Fire));
	const int32 NumSpecsAtSpawn = ASC->GetActivatableAbilities().Num();

	// AddWeapon grants once and adds ammo; routing passes only the current weapon's fire spec.
	TestTrue(TEXT("add RG"), Inventory->AddWeapon(Table->Get(RG), 10));
	TestTrue(TEXT("add RG again (ammo only)"), Inventory->AddWeapon(Table->Get(RG), 5));
	TestTrue(TEXT("owns RG"), Inventory->OwnsIndex(RG));
	TestEqual(TEXT("one spec more"), ASC->GetActivatableAbilities().Num(), NumSpecsAtSpawn + 1);	TestEqual(TEXT("slugs 15"), PlayerState->GetAmmoSet()->GetSlugs(), 15.f);
	TestTrue(TEXT("add RL"), Inventory->AddWeapon(Table->Get(RL), 5));
	TestNotNull(TEXT("RL has its projectile fire spec (task 05)"), Inventory->FindFireSpec(RL));
	MGSpec = Inventory->FindFireSpec(MG);
	FGameplayAbilitySpec* RGSpec = Inventory->FindFireSpec(RG);
	if (!TestNotNull(TEXT("RG fire spec"), RGSpec) || !MGSpec)
	{
		return false;
	}
	TestTrue(TEXT("route: MG (current)"), ASC->CanRouteInputToSpec(*MGSpec));
	TestFalse(TEXT("route: not RG"), ASC->CanRouteInputToSpec(*RGSpec));
	TestEqual(TEXT("select slot 7 -> RG"), Inventory->ResolveSelectInput(InputTag_Weapon_Slot7), RG);
	TestEqual(TEXT("select slot 3 (not owned) -> none"), Inventory->ResolveSelectInput(InputTag_Weapon_Slot3), INDEX_NONE);
	TestEqual(TEXT("next from MG -> RL (slot order)"), Inventory->ResolveSelectInput(InputTag_Weapon_Next), RL);
	TestEqual(TEXT("prev from MG -> RG (wraps)"), Inventory->ResolveSelectInput(InputTag_Weapon_Prev), RG);

	// Fire the MG through the input path (WhileInputActive), then switch to RG by slot key: refire wait 0.1 s, drop
	// 0.2 s (index changes), raise 0.25 s (fire blocked by the local SwitchEndTime, B18; the tag is still granted).
	W.Advance(0.1f);
	const float BulletsBefore = PlayerState->GetAmmoSet()->GetBullets();
	ASC->AbilityInputTagPressed(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	TestEqual(TEXT("MG fired through input: one bullet"), PlayerState->GetAmmoSet()->GetBullets(), BulletsBefore - 1.f);
	const UVeloxGameplayAbility_Fire* MGFire = Cast<UVeloxGameplayAbility_Fire>(MGSpec->GetPrimaryInstance());
	const double T0 = W.World->GetTimeSeconds();
	TestTrue(TEXT("MG next fire time = now + 0.1"), MGFire && FMath::IsNearlyEqual(MGFire->GetNextFireTime(), T0 + 0.1, 1e-3));

	ASC->AbilityInputTagPressed(InputTag_Weapon_Slot7);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Weapon_Slot7);
	TestTrue(TEXT("switching tag at once"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));
	const double SwitchEnd = Inventory->GetSwitchEndTime();
	TestEqual(TEXT("SwitchEndTime = now + refire 0.1 + drop 0.2 + raise 0.25"), SwitchEnd, T0 + 0.55, 1e-3);
	TestEqual(TEXT("still MG during refire wait + drop"), Inventory->GetCurrentIndex(), MG);
	W.Advance(0.25f); // t = 0.25 < 0.1 + 0.2
	TestEqual(TEXT("still MG at 0.25 s (refire 0.1 + drop 0.2)"), Inventory->GetCurrentIndex(), MG);
	W.Advance(0.1f); // t = 0.35
	TestEqual(TEXT("RG after refire + drop"), Inventory->GetCurrentIndex(), RG);
	TestTrue(TEXT("raising: still switching"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));
	ASC->AbilityInputTagPressed(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	TestEqual(TEXT("no rail during the raise"), PlayerState->GetAmmoSet()->GetSlugs(), 15.f);
	ASC->AbilityInputTagReleased(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);

	// One frame before the local switch end: the press does not fire; the tag is still there (GE unchanged).
	W.AdvanceTo(SwitchEnd - 1.0 / 60.0);
	TestTrue(TEXT("tag still granted one frame before the switch end"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));
	ASC->AbilityInputTagPressed(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	TestEqual(TEXT("no rail at SwitchEndTime - 1/60"), PlayerState->GetAmmoSet()->GetSlugs(), 15.f);

	// At the switch end the press fires, independent of Velox.State.WeaponSwitching (a 5 s copy of the GE is granted
	// first: the local gate is SwitchEndTime only).
	W.AdvanceTo(SwitchEnd);
	const FActiveGameplayEffectHandle ExtraSwitching = ASC->ApplyGameplayEffectSpecToSelf(*VeloxEffects::MakeDurationSpec(ASC, UVeloxGE_WeaponSwitching::StaticClass(), 5.f).Data.Get());
	TestTrue(TEXT("tag present at the switch end (extra GE)"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));
	ASC->AbilityInputTagPressed(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	TestEqual(TEXT("rail fires at SwitchEndTime despite the tag"), PlayerState->GetAmmoSet()->GetSlugs(), 14.f);
	ASC->AbilityInputTagReleased(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->RemoveActiveGameplayEffect(ExtraSwitching);

	// Respawn semantics: the loadout again, RG/RL taken away, a running switch's local end cleared.
	ASC->AbilityInputTagPressed(InputTag_Weapon_Slot2);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Weapon_Slot2);
	TestTrue(TEXT("switch back to MG running"), Inventory->GetSwitchEndTime() > W.World->GetTimeSeconds());
	Inventory->ResetToLoadout();
	TestEqual(TEXT("reset clears SwitchEndTime"), Inventory->GetSwitchEndTime(), 0.0);
	// B25: the shot counter restarts per life (an MG and a rail shot were fired above).
	TestEqual(TEXT("reset: next shot sequence is 1"), static_cast<int32>(Inventory->AllocateShotSeq()), 1);
	TestEqual(TEXT("reset: MG only"), static_cast<int32>(Inventory->GetOwnedMask()), 1 << MG);
	TestNull(TEXT("reset: RG spec removed"), Inventory->FindFireSpec(RG));
	TestNotNull(TEXT("reset: MG spec kept"), Inventory->FindFireSpec(MG));
	TestEqual(TEXT("reset: MG selected"), Inventory->GetCurrentIndex(), MG);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatHitscanDummyTest, "Velox.Combat.HitscanDummy", VeloxWeaponTest::Flags)

bool FVeloxCombatHitscanDummyTest::RunTest(const FString& Parameters)
{
	using namespace VeloxWeaponTest;

	FWeaponWorld W;
	AAIController* DummyController = nullptr;
	AVeloxCharacter* Shooter = W.Spawn(FVector(0, 0, 100), AAIController::StaticClass());
	AVeloxCharacter* Dummy = W.Spawn(FVector(1000, 0, 100), AVeloxDummyController::StaticClass(), &DummyController);
	if (!TestNotNull(TEXT("shooter"), Shooter) || !TestNotNull(TEXT("dummy"), Dummy) || !PS(Shooter) || !PS(Dummy))
	{
		return false;
	}
	if (AVeloxDummyController* Strafer = Cast<AVeloxDummyController>(DummyController))
	{
		Strafer->StrafeSpeedScale = 0.f; // a standing dummy: the numbers, not the aim, are under test
	}
	W.Advance(0.2f); // land, record lag-comp samples
	UVeloxLagCompensationSubsystem* LagComp = UVeloxLagCompensationSubsystem::Get(W.World);
	TestTrue(TEXT("both pawns registered for lag compensation"), LagComp && LagComp->IsRegistered(Shooter) && LagComp->IsRegistered(Dummy));
	TestTrue(TEXT("give all"), VeloxWeaponCheats::GiveAllWeapons(*PS(Shooter)));

	UVeloxAbilitySystemComponent* DummyASC = PS(Dummy)->GetVeloxAbilitySystemComponent();
	const UVeloxHealthSet* DummyHealth = PS(Dummy)->GetHealthSet();
	UVeloxWeaponInventoryComponent* Inventory = PS(Shooter)->GetWeaponInventory();
	const UVeloxWeaponTable* Table = Inventory->GetWeaponTable();

	struct FCase { const TCHAR* Weapon; int32 Slot; };
	const FCase Cases[] = { { TEXT("RG"), 7 }, { TEXT("MG"), 2 }, { TEXT("SG"), 3 }, { TEXT("LG"), 6 } };
	for (const FCase& Case : Cases)
	{
		// Let the previous knockback play out, then reset the dummy: 100 health / 100 armor, back in place, at rest.
		W.Advance(0.1f);
		DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), 100.f);
		DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), 100.f);
		Dummy->SetActorLocation(FVector(1000, 0, Dummy->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		Dummy->GetCharacterMovement()->Velocity = FVector::ZeroVector;

		const int32 Index = Table->FindBySlot(Case.Slot);
		const UVeloxWeaponDefinition* Def = Table->Get(Index);
		FString Error;
		const bool bFired = VeloxWeaponCheats::FireTestShot(*Shooter, Case.Weapon, Dummy, 0.f, Error);
		if (!TestTrue(FString::Printf(TEXT("%s fired (%s)"), Case.Weapon, *Error), bFired))
		{
			continue;
		}
		const FGameplayAbilitySpec* Spec = Inventory->FindFireSpec(Index);
		const UVeloxGameplayAbility_Fire* Fire = Spec ? Cast<UVeloxGameplayAbility_Fire>(Spec->GetPrimaryInstance()) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s fire instance"), Case.Weapon), Fire))
		{
			continue;
		}
		const FVeloxServerShotInfo& Shot = Fire->GetLastServerShot();
		TestFalse(FString::Printf(TEXT("%s not rejected"), Case.Weapon), Shot.bRejected);
		TestEqual(FString::Printf(TEXT("%s hit the dummy"), Case.Weapon), Shot.NumHits, 1);
		TestTrue(FString::Printf(TEXT("%s pellets hit: %d"), Case.Weapon, Shot.PelletHits), Shot.PelletHits >= 1 && Shot.PelletHits <= Def->PelletCount);

		FVeloxDamageInput In;
		In.Damage = Def->Damage;
		In.Armor = 100;
		const FVeloxDamageResult Expected = FVeloxDamageMath::ApplyPellets(In, FMath::Max(Shot.PelletHits, 1), PS(Shooter)->GetCombatSettings()->BuildCombatParams());
		TestEqual(FString::Printf(TEXT("%s health 100 - %d"), Case.Weapon, Expected.HealthLoss), DummyHealth->GetHealth(), 100.f - Expected.HealthLoss);
		TestEqual(FString::Printf(TEXT("%s armor 100 - %d"), Case.Weapon, Expected.ArmorLoss), DummyHealth->GetArmor(), 100.f - Expected.ArmorLoss);
		TestEqual(FString::Printf(TEXT("%s total damage reported"), Case.Weapon), Shot.TotalDamage, Expected.HealthLoss + Expected.ArmorLoss);
		if (Case.Slot == 7)
		{
			TestEqual(TEXT("rail: Q3 34/66 vs 100 armor"), FVector2D(Expected.HealthLoss, Expected.ArmorLoss), FVector2D(34, 66));
		}
	}

	// The LG (last case) keeps its looping beam cue alive on the server; with no further shot it stops after
	// BeamKeepAliveSeconds (B23): advance that plus one frame.
	UVeloxAbilitySystemComponent* ShooterASC = PS(Shooter)->GetVeloxAbilitySystemComponent();
	TestTrue(TEXT("LG beam cue active after the shot"), ShooterASC->IsGameplayCueActive(VeloxGameplayTags::GameplayCue_Weapon_LightningGun_Beam));
	TestTrue(TEXT("beam reached the pawn's feedback component"), Shooter->GetCombatFeedbackComponent() && Shooter->GetCombatFeedbackComponent()->IsBeamActive());
	const float KeepAlive = UVeloxCombatSettings::GetOrDefault(PS(Shooter)->GetCombatSettings()).GetBeamKeepAliveSeconds();
	W.Advance(0.5f * KeepAlive);
	TestTrue(TEXT("LG beam still alive at half the keep-alive"), ShooterASC->IsGameplayCueActive(VeloxGameplayTags::GameplayCue_Weapon_LightningGun_Beam));
	W.Advance(0.5f * KeepAlive + 1.f / 60.f + 1e-3f);
	TestFalse(TEXT("LG beam cue removed after release"), ShooterASC->IsGameplayCueActive(VeloxGameplayTags::GameplayCue_Weapon_LightningGun_Beam));
	TestFalse(TEXT("feedback beam off"), Shooter->GetCombatFeedbackComponent() && Shooter->GetCombatFeedbackComponent()->IsBeamActive());

	// Same weapon again within its interval: the shooter's own NextFireTime refuses (one rail per 1.5 s).
	FString Error;
	TestFalse(TEXT("rail refire blocked within 1.5 s"), VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("RG"), Dummy, 0.f, Error));

	// LG range 1920 cm: a dummy at 25 m is out of reach.
	W.Advance(1.6f);
	DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), 100.f);
	Dummy->SetActorLocation(FVector(2500, 0, Dummy->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
	Dummy->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	TestTrue(TEXT("LG fired at 25 m"), VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("LG"), Dummy, 0.f, Error));
	TestEqual(TEXT("LG out of range: no damage"), DummyHealth->GetHealth(), 100.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
