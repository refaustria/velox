// Velox - weapon tests (ADR 0003 sections 4, 5, 9; task 04).
//   Velox.Combat.FireRateBucket      token bucket with one tolerance (world-free, B32): capacities 1 + 0.5 s / interval,
//                                    exact rate, 2x rate, receive jitter, rail burst and the 1.0 s rail cheat; honest MG
//                                    and RG with reliable-resend bursts at 100 / 250 ms RTT / 2 % loss never rejected (the
//                                    old 3-token bucket and B19's spacing rule do reject the same arrivals), a 2x MG
//                                    cheat rejected within 1 s.
//   Velox.Combat.FireTiming          Q3 one-frame carry of NextFireTime (no banked double tap) and the switch gate (B18,
//                                    B19; world-free); B37: the server's RemoteRefireWait and the 0.5 s gate rows.
//   Velox.Combat.ShotValidation      every server reject reason from its input, a valid shot normalised (world-free),
//                                    the projectile payload (B26), the fire time-stamp clamp (B25) and the shared B15
//                                    origin rule (a private world for ValidateOrigin; the occlusion row is in LagCompTrace).
//   Velox.Combat.WeaponTable         slot lookup, weapnext/prev cycling; the DA_WeaponTable asset carries the Q3 numbers;
//                                    DA_CombatSettings_Default carries the 04b timing fields.
//   Velox.Combat.TargetDataSerialize FGameplayAbilityTargetData_VeloxShot NetSerialize round trip (struct and handle);
//                                    FVeloxGameplayEffectContext round trip incl. PatternSeq (B21).
//   Velox.Combat.Inventory           loadout, AddWeapon, fire routing to the current weapon, switch drop/raise timing
//                                    and refire wait, the local SwitchEndTime gate (B18), through the ASC input path
//                                    (private game world); B33 supersede rows and the inventory-owned drop end (B37 iii:
//                                    a rejected supersede still lands at the drop end, a completing shot clears the timer,
//                                    a reset cancels it).
//   Velox.Combat.RemoteSwitchTimeline B37 on the server for a remote shooter: six bunched MG shots -> refire wait
//                                    <= one interval, rail wait 1.1 s at T + 0.4, the switch gate at 0.55 / 0.35 s left
//                                    with the 0.5 s resend tolerance, the completing rail clears the drop timer.
//   Velox.Combat.HitscanDummy        RG / MG / SG / LG FireTest at a dummy (AVeloxDummyController) 10 m away: the
//                                    Q3 damage reaches its health and armor through TraceShot and GE_Damage; one fire
//                                    and one impact cue per shot on the shooter's feedback component; the server pattern
//                                    counter advances once per accepted shot and seeds the SG pattern regardless of the
//                                    client ShotSeq (B21).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Bots/VeloxDummyController.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxDamageMath.h"
#include "Combat/VeloxGameplayEffectContext.h"
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
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "Weapons/VeloxCombatFeedbackComponent.h"
#include "GameModes/VeloxMatchEffects.h"
#include "Weapons/VeloxFireRateBucket.h"
#include "Weapons/VeloxFireTiming.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxGameplayAbility_FireHitscan.h"
#include "Weapons/VeloxGameplayAbility_FireProjectile.h"
#include "Weapons/VeloxGameplayAbility_WeaponSwitch.h"
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
	// B32: one tolerance, capacity 1 + Tol / Interval (no floor, no spacing rule).
	constexpr float JitterTolerance = 0.5f;
	TestEqual(TEXT("capacity RG 1.3333"), FVeloxFireRateBucket::CapacityFor(1.5f, JitterTolerance), 4.f / 3.f, 1e-4f);
	TestEqual(TEXT("capacity SG 1.5"), FVeloxFireRateBucket::CapacityFor(1.0f, JitterTolerance), 1.5f, 1e-4f);
	TestEqual(TEXT("capacity RL 1.625"), FVeloxFireRateBucket::CapacityFor(0.8f, JitterTolerance), 1.625f, 1e-4f);
	TestEqual(TEXT("capacity MG 6"), FVeloxFireRateBucket::CapacityFor(0.1f, JitterTolerance), 6.f, 1e-4f);
	TestEqual(TEXT("capacity LG 11"), FVeloxFireRateBucket::CapacityFor(0.05f, JitterTolerance), 11.f, 1e-4f);
	TestEqual(TEXT("capacity tolerance 0 -> 1"), FVeloxFireRateBucket::CapacityFor(0.1f, 0.f), 1.f, 1e-4f);
	TestEqual(TEXT("capacity interval 0 -> 1"), FVeloxFireRateBucket::CapacityFor(0.f, JitterTolerance), 1.f, 1e-4f);

	// Exact rate: LG 0.05 s, MG 0.1 s, rail 1.5 s, 1200 shots each; none rejected.
	for (const float Interval : { 0.05f, 0.1f, 1.5f })
	{
		FVeloxFireRateBucket Bucket;
		int32 Accepted = 0;
		const int32 Shots = 1200;
		const float Capacity = FVeloxFireRateBucket::CapacityFor(Interval, JitterTolerance);
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			Accepted += Bucket.TryConsume(100.0 + Index * static_cast<double>(Interval), Interval, Capacity) ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("exact rate %.2f s: all accepted"), Interval), Accepted, Shots);
	}

	// Twice the rate (LG): the burst of the capacity, then every other shot -> long-term rate exactly 1/Interval.
	{
		FVeloxFireRateBucket Bucket;
		const float Interval = 0.05f;
		const float Capacity = FVeloxFireRateBucket::CapacityFor(Interval, JitterTolerance);
		int32 Accepted = 0;
		const int32 Shots = 400;
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			Accepted += Bucket.TryConsume(Index * 0.025, Interval, Capacity) ? 1 : 0;
		}
		const int32 Expected = FMath::FloorToInt32(Capacity) + FMath::FloorToInt32((Shots - 1) * 0.025 / Interval);
		TestTrue(FString::Printf(TEXT("2x rate: %d accepted of %d (expected %d +- 1)"), Accepted, Shots, Expected), FMath::Abs(Accepted - Expected) <= 1);
	}

	// Receive jitter: LG sent every 50 ms, received with up to 40 ms of extra delay (spikes and clumps): all accepted.
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
			Accepted += Bucket.TryConsume(Receive, 0.05f, FVeloxFireRateBucket::CapacityFor(0.05f, JitterTolerance)) ? 1 : 0;
		}
		TestEqual(TEXT("40 ms jitter at 50 ms: all accepted"), Accepted, Shots);
	}

	// An explicit capacity of 3: three shots pass at once after a pause, the next is rejected (all over-rate shots are).
	{
		FVeloxFireRateBucket Bucket;
		TestTrue(TEXT("first shot"), Bucket.TryConsume(10.0, 1.0f, 3.f));
		TestTrue(TEXT("burst 2"), Bucket.TryConsume(10.0, 1.0f, 3.f));
		TestTrue(TEXT("burst 3"), Bucket.TryConsume(10.0, 1.0f, 3.f));
		TestTrue(TEXT("over the burst: rate"), Bucket.Consume(10.0, 1.0f, 3.f) == EVeloxFireRateResult::Rate);
		TestFalse(TEXT("still over at +0.5 s"), Bucket.TryConsume(10.5, 1.0f, 3.f));
		TestTrue(TEXT("refilled at +1 s"), Bucket.TryConsume(11.0, 1.0f, 3.f));
	}

	// B32: three rails in one frame after an idle -> one accepted (capacity 1.33), two rejected as rate (and free).
	const float RGCapacity = FVeloxFireRateBucket::CapacityFor(1.5f, JitterTolerance);
	{
		FVeloxFireRateBucket Bucket;
		TestTrue(TEXT("rail 1 at t accepted"), Bucket.TryConsume(10.0, 1.5f, RGCapacity));
		TestTrue(TEXT("rail 2 at t: rate"), Bucket.Consume(10.0, 1.5f, RGCapacity) == EVeloxFireRateResult::Rate);
		TestTrue(TEXT("rail 3 at t: rate"), Bucket.Consume(10.0, 1.5f, RGCapacity) == EVeloxFireRateResult::Rate);
		TestFalse(TEXT("rail at t + 0.99: rate (min spacing Interval - Tol = 1.0 s)"), Bucket.TryConsume(10.99, 1.5f, RGCapacity));
		TestTrue(TEXT("rail at t + 1.0 accepted"), Bucket.TryConsume(11.0, 1.5f, RGCapacity));
	}

	// B32 rail cheat: firing every 1.0 s from a full bucket. Shot 1 (t 0) and 2 (t 1.0) accepted, shot 3 (t 2.0) rejected,
	// a probe at 2.4 s still rejected, 2.5 s accepted (tokens after each: 0.333, 0.0, 0.667, 0.933, 1.0 -> 0.0).
	{
		FVeloxFireRateBucket Bucket;
		const double T0 = 10.0;
		TestTrue(TEXT("RG cheat shot 1 (t 0.0) accepted"), Bucket.TryConsume(T0, 1.5f, RGCapacity));
		TestEqual(TEXT("RG cheat tokens after shot 1"), Bucket.Tokens, 1.0 / 3.0, 1e-4);
		TestTrue(TEXT("RG cheat shot 2 (t 1.0) accepted"), Bucket.TryConsume(T0 + 1.0, 1.5f, RGCapacity));
		TestEqual(TEXT("RG cheat tokens after shot 2"), Bucket.Tokens, 0.0, 1e-4);
		TestFalse(TEXT("RG cheat shot 3 (t 2.0) rejected"), Bucket.TryConsume(T0 + 2.0, 1.5f, RGCapacity));
		TestEqual(TEXT("RG cheat tokens at shot 3"), Bucket.Tokens, 2.0 / 3.0, 1e-4);
		TestFalse(TEXT("RG cheat probe (t 2.4) rejected"), Bucket.TryConsume(T0 + 2.4, 1.5f, RGCapacity));
		TestTrue(TEXT("RG cheat next accepted at t 2.5"), Bucket.TryConsume(T0 + 2.5, 1.5f, RGCapacity));
	}
	// The same cheat for 60 s (61 shots, t 0..60): accepted <= 1 + floor(60 / 1.5) = 41 (pattern accept, accept, reject).
	{
		FVeloxFireRateBucket Bucket;
		int32 Accepted = 0;
		for (int32 Index = 0; Index <= 60; ++Index)
		{
			Accepted += Bucket.TryConsume(10.0 + Index, 1.5f, RGCapacity) ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("RG cheat 1.0 s for 60 s: %d accepted, 40..41"), Accepted), Accepted <= 41 && Accepted >= 40);
	}

	// Honest fire at Interval (client frames 120 Hz, Q3 carry: fire times on frame boundaries), one-way delay RTT/2 +- 20 ms,
	// 2 % of shots lost and resent RTT + 80 ms later (task 08 evidence: arrival 270-300 ms late at 200 ms RTT), arrivals
	// in order (a reliable resend holds back the later shots of the same channel). 600 s of sustained fire. B19MinGap
	// > 0 additionally applies B19's deleted spacing rule (reject below that receive-time gap after the last accepted
	// shot), so the test documents what the old rule did to the same arrivals.
	auto Honest = [](float Interval, double RttSeconds, float Capacity, uint32 Seed, double B19MinGap)
	{
		FVeloxFireRateBucket Bucket;
		FRandomStream Stream(Seed);
		constexpr double Frame = 1.0 / 120.0;
		const int32 Shots = FMath::RoundToInt32(600.0 / Interval);
		int32 Rejected = 0;
		double LastArrival = 0.0;
		double LastAccepted = -1e9;
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			const double Fire = FMath::CeilToDouble((10.0 + Index * static_cast<double>(Interval)) / Frame) * Frame;
			double Arrival = Fire + RttSeconds * 0.5 + Stream.FRandRange(-0.02, 0.02);
			if (Stream.FRand() < 0.02)
			{
				Arrival += RttSeconds + 0.08;
			}
			Arrival = FMath::Max(Arrival, LastArrival);
			LastArrival = Arrival;
			if (B19MinGap > 0.0 && Arrival - LastAccepted < B19MinGap - 1e-6)
			{
				++Rejected;
				continue;
			}
			if (Bucket.TryConsume(Arrival, Interval, Capacity))
			{
				LastAccepted = Arrival;
			}
			else
			{
				++Rejected;
			}
		}
		return Rejected;
	};
	const float MGCapacity = FVeloxFireRateBucket::CapacityFor(0.1f, JitterTolerance);
	TestEqual(TEXT("D3: honest MG, 100 ms RTT, 2 % resends: 0 rejected"), Honest(0.1f, 0.1, MGCapacity, 11, 0.0), 0);
	TestEqual(TEXT("D3: honest MG, 250 ms RTT, 2 % resends: 0 rejected"), Honest(0.1f, 0.25, MGCapacity, 12, 0.0), 0);
	TestTrue(TEXT("D3: the old 3-token bucket rejects the same honest MG at 200 ms RTT (the defect)"), Honest(0.1f, 0.2, 3.f, 13, 0.0) > 0);
	TestEqual(TEXT("B32: honest RG, 250 ms RTT, 2 % resends, 600 s: 0 rejected"), Honest(1.5f, 0.25, RGCapacity, 21, 0.0), 0);
	{
		// B19 (deleted): bucket floor 3 plus the minimum gap 1.5 - 0.25 = 1.25 s, on the same arrivals (same seed).
		const int32 LegacyRejected = Honest(1.5f, 0.25, 3.f, 21, 1.25);
		AddInfo(FString::Printf(TEXT("B32: B19's deleted 1.25 s minimum gap rejects %d of the same 400 honest rails"), LegacyRejected));
		TestTrue(FString::Printf(TEXT("B32: B19's deleted 1.25 s minimum gap rejects the same honest rails (%d, the defect)"), LegacyRejected), LegacyRejected >= 1);
	}

	// Resend burst row: shots 14-16 held back by one lost packet arrive together 270 ms late, 17 on time (task 08 round 3).
	{
		FVeloxFireRateBucket Bucket;
		constexpr float Interval = 0.1f;
		const double Late[] = { 0.0, 0.0, 0.0, 0.27, 0.17, 0.073, 0.022, 0.023, 0.024 };
		int32 Accepted = 0;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Late); ++Index)
		{
			Accepted += Bucket.TryConsume(10.0 + Index * Interval + Late[Index], Interval, MGCapacity) ? 1 : 0;
		}
		TestEqual(TEXT("D3: resend burst then an on-time shot: all accepted"), Accepted, static_cast<int32>(UE_ARRAY_COUNT(Late)));
	}

	// 2x cheat: MG every 50 ms after 5 s of honest fire. The first rejection comes within 1 s; long-term rate 1/interval.
	{
		FVeloxFireRateBucket Bucket;
		constexpr float Interval = 0.1f;
		for (int32 Index = 0; Index < 50; ++Index)
		{
			Bucket.TryConsume(10.0 + Index * Interval, Interval, MGCapacity);
		}
		const double CheatStart = 10.0 + 50 * Interval;
		double FirstReject = -1.0;
		int32 Accepted = 0;
		const int32 Shots = 400; // 20 s
		for (int32 Index = 0; Index < Shots; ++Index)
		{
			const double T = CheatStart + Index * 0.05;
			if (Bucket.TryConsume(T, Interval, MGCapacity))
			{
				++Accepted;
			}
			else if (FirstReject < 0.0)
			{
				FirstReject = T - CheatStart;
			}
		}
		TestTrue(FString::Printf(TEXT("D3: 2x MG cheat rejected within 1 s (first rejection after %.2f s)"), FirstReject), FirstReject >= 0.0 && FirstReject <= 1.0);
		const int32 MaxAccepted = FMath::CeilToInt32(Shots * 0.05 / Interval + MGCapacity);
		TestTrue(FString::Printf(TEXT("D3: 2x cheat long-term rate bounded: %d accepted <= %d"), Accepted, MaxAccepted), Accepted <= MaxAccepted);
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

	// B37 (ii): the server gate uses the one resend tolerance 0.5 s. A switch RPC delayed 0.33 s by a resend (fire RPC on
	// time): the rail meets the server's GE with 0.33 + 0.02 jitter left -> accepted; 0.55 left -> rejected.
	TestTrue(TEXT("B37: server, remaining 0.35 under the 0.5 tolerance -> allowed (rejected under the old 0.25)"), IsSwitchFireAllowed(false, 0.0, 0.0, 0.35, 0.5));
	TestFalse(TEXT("B37: server, remaining 0.55 -> blocked"), IsSwitchFireAllowed(false, 0.0, 0.0, 0.55, 0.5));

	// B37 (i): the server's refire wait for a remote shooter, from the last accepted receive time; no carry.
	{
		constexpr double MGInterval = 0.1;
		constexpr double T = 50.0;
		constexpr double Frame = 1.0 / 60.0;
		// Six bunched MG shots accepted in one server frame: B19's carry on receive time pushed NextFireTime +0.6 s
		// (08c: server wait 0.373 vs client 0.090); every accepted shot now sets the same receive time.
		double Carried = -1.0e9;
		double LastAccepted = -1.0e9;
		for (int32 Shot = 0; Shot < 6; ++Shot)
		{
			Carried = AdvanceNextFireTime(Carried, T, MGInterval, Frame);
			LastAccepted = T;
		}
		AddInfo(FString::Printf(TEXT("B37: six bunched MG shots: old carried wait %.3f s, new wait %.3f s"), FMath::Max(Carried - (T + Frame), 0.0),
			RemoteRefireWait(LastAccepted, MGInterval, T + Frame)));
		TestTrue(TEXT("B37: the old receive-time carry inflated the wait beyond one interval (the defect)"), Carried - (T + Frame) > MGInterval);
		const double Wait = RemoteRefireWait(LastAccepted, MGInterval, T + Frame);
		TestTrue(TEXT("B37: six bunched MG shots -> server wait in the next frame <= Interval (0.1)"), Wait <= MGInterval + 1e-9 && Wait > 0.0);
		TestEqual(TEXT("B37: ... = Interval - one frame"), Wait, MGInterval - Frame, 1e-9);
	}
	TestEqual(TEXT("B37: rail accepted at T -> wait at T + 0.4 = 1.1"), RemoteRefireWait(10.0, 1.5, 10.4), 1.1, 1e-9);
	TestEqual(TEXT("B37: rail accepted at T -> wait at T + 2 = 0"), RemoteRefireWait(10.0, 1.5, 12.0), 0.0, 1e-12);
	TestEqual(TEXT("B37: no shot accepted yet -> 0"), RemoteRefireWait(-1.0e9, 1.5, 3.0), 0.0, 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxWeaponsHasteFireRateTest, "Velox.Weapons.HasteFireRate", VeloxWeaponTest::Flags)

bool FVeloxWeaponsHasteFireRateTest::RunTest(const FString& Parameters)
{
	// ADR 0004 section 3 (task 03): Haste divides the fire interval by HasteFireRateScale on the shooter's machine (client
	// rule) and in the server's bucket (refill rate and capacity, B32: CapacityFor(Interval, Tol) = 1 + Tol / Interval).
	using namespace VeloxFireTiming;
	const float Scale = GetDefault<UVeloxCombatSettings>()->HasteFireRateScale;
	TestEqual(TEXT("HasteFireRateScale default 1.3 (Q3)"), Scale, 1.3f);
	TestEqual(TEXT("haste: interval / scale"), HasteFireInterval(0.1, true, Scale), 0.1 / Scale, 1e-12);
	TestEqual(TEXT("no haste: interval unchanged"), HasteFireInterval(0.1, false, Scale), 0.1, 1e-12);
	TestEqual(TEXT("scale <= 0 ignored"), HasteFireInterval(0.1, true, 0.0), 0.1, 1e-12);

	// Client rule: held fire for 10 s at 120 Hz frames. MG 0.1 s -> 100 shots, hasted 130 (Q3 addTime / 1.3).
	auto CountShots = [](double Interval, double Seconds, double FrameHz)
	{
		const double Dt = 1.0 / FrameHz;
		double Next = -1.0e9;
		int32 Shots = 0;
		for (int64 Frame = 0; static_cast<double>(Frame) * Dt < Seconds - 1e-9; ++Frame)
		{
			const double Now = 50.0 + static_cast<double>(Frame) * Dt;
			if (Now >= Next)
			{
				Next = AdvanceNextFireTime(Next, Now, Interval, Dt);
				++Shots;
			}
		}
		return Shots;
	};
	for (const double Interval : { 0.1, 1.5 })
	{
		const int32 Plain = CountShots(HasteFireInterval(Interval, false, Scale), 30.0, 120.0);
		const int32 Hasted = CountShots(HasteFireInterval(Interval, true, Scale), 30.0, 120.0);
		const double Want = 30.0 / Interval;
		TestTrue(FString::Printf(TEXT("client %.2f s: %d plain shots in 30 s (want %.0f +- 1)"), Interval, Plain, Want), FMath::Abs(Plain - Want) <= 1.0);
		TestTrue(FString::Printf(TEXT("client %.2f s: %d hasted shots in 30 s (want %.0f +- 1)"), Interval, Hasted, Want * Scale), FMath::Abs(Hasted - Want * Scale) <= 1.0);
	}

	// Server rule: a remote shooter firing at the hasted rate (receive times exact, 30 s). The bucket with the hasted
	// interval accepts every shot; the plain interval rejects the excess (Rate) once the burst is spent.
	const UVeloxCombatSettings& Settings = *GetDefault<UVeloxCombatSettings>();
	for (const float Interval : { 0.1f, 1.5f })
	{
		const float Hasted = static_cast<float>(HasteFireInterval(Interval, true, Scale));
		auto Run = [&Settings](float BucketInterval, float ShotInterval)
		{
			FVeloxFireRateBucket Bucket;
			const float Capacity = FVeloxFireRateBucket::CapacityFor(BucketInterval, Settings.GetFireRateJitterToleranceSeconds());
			int32 Accepted = 0;
			const int32 N = FMath::CeilToInt32(30.f / ShotInterval);
			for (int32 I = 0; I < N; ++I)
			{
				Accepted += Bucket.TryConsume(10.0 + static_cast<double>(I) * ShotInterval, BucketInterval, Capacity) ? 1 : 0;
			}
			return TPair<int32, int32>(Accepted, N);
		};
		const TPair<int32, int32> WithHaste = Run(Hasted, Hasted);
		const TPair<int32, int32> WithoutHaste = Run(Interval, Hasted);
		TestEqual(FString::Printf(TEXT("server %.2f s: hasted stream, hasted bucket accepts all"), Interval), WithHaste.Key, WithHaste.Value);
		TestTrue(FString::Printf(TEXT("server %.2f s: hasted stream, plain bucket rejects the excess (%d of %d accepted)"), Interval, WithoutHaste.Key, WithoutHaste.Value),
			WithoutHaste.Key < WithHaste.Value - 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatShotValidationTest, "Velox.Combat.ShotValidation", VeloxWeaponTest::Flags)

bool FVeloxCombatShotValidationTest::RunTest(const FString& Parameters)
{
	constexpr float Capacity = 3.f;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const float FInf = std::numeric_limits<float>::infinity();
	const float FNaN = std::numeric_limits<float>::quiet_NaN();

	FGameplayAbilityTargetData_VeloxShot Good;
	Good.Origin = FVector(100.0, 200.0, 64.0);
	Good.Dir = FVector(3.0, 4.0, 0.0); // not normalised on purpose
	Good.ViewServerTime = 12.5f;
	Good.ShotSeq = 42;
	Good.HintTargetStamp = 7.f; // no hint bit: dropped

	// Each payload case with a fresh MG bucket (0.1 s, capacity 3): the reason comes from the payload alone.
	auto Check = [&](const TCHAR* Name, const FGameplayAbilityTargetData* Raw, EVeloxShotReject Expected)
	{
		FVeloxFireRateBucket Bucket;
		FVeloxValidatedShot Out;
		const EVeloxShotReject Got = VeloxShotValidation::Validate(Raw, Bucket, 10.0, 0.1f, Capacity, Out);
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
		// B36: the hitscan payload carries FireMoveTimeStamp too.
		Bad.HintTargetStamp = 7.f;
		Bad.FireMoveTimeStamp = FNaN;
		Check(TEXT("hitscan: NaN fire stamp"), &Bad, EVeloxShotReject::Time);
		Bad.FireMoveTimeStamp = FInf;
		Check(TEXT("hitscan: +inf fire stamp"), &Bad, EVeloxShotReject::Time);
		Bad.FireMoveTimeStamp = -FInf;
		Check(TEXT("hitscan: -inf fire stamp"), &Bad, EVeloxShotReject::Time);
		Bad.FireMoveTimeStamp = 41.5f;
		TestEqual(TEXT("hitscan: fire stamp carried into the validated shot"), Check(TEXT("hitscan: finite fire stamp"), &Bad, EVeloxShotReject::None).FireMoveTimeStamp, 41.5f);
	}

	// Over-rate: MG (0.1 s, capacity 3) four shots in one frame -> the fourth is rate.
	{
		FVeloxFireRateBucket Bucket;
		FVeloxValidatedShot Out;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TestTrue(FString::Printf(TEXT("MG burst %d accepted"), Index + 1), VeloxShotValidation::Validate(&Good, Bucket, 10.0, 0.1f, Capacity, Out) == EVeloxShotReject::None);
		}
		TestTrue(TEXT("MG fourth in one frame: rate"), VeloxShotValidation::Validate(&Good, Bucket, 10.0, 0.1f, Capacity, Out) == EVeloxShotReject::Rate);
		TestEqual(TEXT("rejected shot still names its seq"), static_cast<int32>(Out.ShotSeq), 42);
	}
	// B32: rails at the default capacity 1.33: 0.9 s after the first -> rate (no separate spacing reason), 1.0 s -> accepted.
	{
		FVeloxFireRateBucket Bucket;
		FVeloxValidatedShot Out;
		const float RGCapacity = FVeloxFireRateBucket::CapacityFor(1.5f, 0.5f);
		TestTrue(TEXT("rail 1 accepted"), VeloxShotValidation::Validate(&Good, Bucket, 10.0, 1.5f, RGCapacity, Out) == EVeloxShotReject::None);
		TestTrue(TEXT("rail 2 after 0.9 s: rate"), VeloxShotValidation::Validate(&Good, Bucket, 10.9, 1.5f, RGCapacity, Out) == EVeloxShotReject::Rate);
		TestTrue(TEXT("rail 2 after 1.0 s: accepted"), VeloxShotValidation::Validate(&Good, Bucket, 11.0, 1.5f, RGCapacity, Out) == EVeloxShotReject::None);
	}
	// The bucket is consumed before the payload: three malformed shots empty it, the valid fourth is rate.
	{
		FVeloxFireRateBucket Bucket;
		FVeloxValidatedShot Out;
		FGameplayAbilityTargetData_SingleTargetHit Wrong;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TestTrue(FString::Printf(TEXT("wrong type %d: type"), Index + 1), VeloxShotValidation::Validate(&Wrong, Bucket, 10.0, 1.0f, Capacity, Out) == EVeloxShotReject::Type);
		}
		TestTrue(TEXT("valid shot after three malformed ones: rate (they cost tokens)"), VeloxShotValidation::Validate(&Good, Bucket, 10.0, 1.0f, Capacity, Out) == EVeloxShotReject::Rate);
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
		TestEqual(TEXT("FireRateJitterToleranceSeconds 0.5 (B32, also the switch tolerance B37)"), Settings->FireRateJitterToleranceSeconds, 0.5f);
		TestEqual(TEXT("MaxRewindSeconds 0.35 (B34, q19)"), Settings->MaxRewindSeconds, 0.35f);
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
	In.FireMoveTimeStamp = 183.0258331f;

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
	TestEqual(TEXT("fire move time stamp exact float (B36)"), Out.FireMoveTimeStamp, In.FireMoveTimeStamp);
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

	// B21: the Velox effect context carries the server's pattern seed (one byte) next to the shot sequence.
	{
		FVeloxGameplayEffectContext ContextIn;
		ContextIn.AddOrigin(FVector(10.0, 20.0, 30.0));
		ContextIn.HitDirection = FVector(0.0, 0.6, 0.8);
		ContextIn.HitLocation = FVector(512.25, -64.5, 99.0);
		ContextIn.KnockbackScale = 0.5f;
		ContextIn.ShotSeq = 17;
		ContextIn.PatternSeq = 203;
		ContextIn.AddFlag(EVeloxDamageFlags::Radius);

		FBitWriter ContextWriter(1024, true);
		bool bContextOk = false;
		ContextIn.NetSerialize(ContextWriter, nullptr, bContextOk);
		TestTrue(TEXT("context: write ok"), bContextOk && !ContextWriter.IsError());

		// PatternSeq is a fixed byte: a context with PatternSeq 0 has the same size.
		FVeloxGameplayEffectContext ContextZero = ContextIn;
		ContextZero.PatternSeq = 0;
		FBitWriter ZeroWriter(1024, true);
		ContextZero.NetSerialize(ZeroWriter, nullptr, bContextOk);
		TestEqual(TEXT("context: PatternSeq is a fixed byte"), ContextWriter.GetNumBits(), ZeroWriter.GetNumBits());

		FBitReader ContextReader(ContextWriter.GetData(), ContextWriter.GetNumBits());
		FVeloxGameplayEffectContext ContextOut;
		ContextOut.NetSerialize(ContextReader, nullptr, bContextOk);
		TestTrue(TEXT("context: read ok"), bContextOk && !ContextReader.IsError());
		TestEqual(TEXT("context: pattern seq"), static_cast<int32>(ContextOut.PatternSeq), 203);
		TestEqual(TEXT("context: shot seq"), static_cast<int32>(ContextOut.ShotSeq), 17);
		TestTrue(TEXT("context: origin"), ContextOut.HasOrigin() && ContextOut.GetOrigin().Equals(ContextIn.GetOrigin(), 0.01));
		TestTrue(TEXT("context: hit direction"), FVector(ContextOut.HitDirection).Equals(FVector(ContextIn.HitDirection), 1e-3));
		TestTrue(TEXT("context: radius flag"), ContextOut.HasFlag(EVeloxDamageFlags::Radius));

		// Duplicate (cue parameters and specs copy contexts) keeps it too.
		TUniquePtr<FGameplayEffectContext> Copy(ContextIn.Duplicate());
		const FVeloxGameplayEffectContext* VeloxCopy = static_cast<const FVeloxGameplayEffectContext*>(Copy.Get());
		TestEqual(TEXT("context: Duplicate keeps PatternSeq"), static_cast<int32>(VeloxCopy->PatternSeq), 203);
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

		/**
		 * Pawn possessed by an AVeloxPlayerController without a local player: in a world without a net driver it is not a
		 * local controller, so the ASC treats it as a remote client's (the server-for-remote-shooter path; as in
		 * VeloxProjectileTest).
		 */
		AVeloxCharacter* SpawnRemoteShooter(const FVector& Location, AVeloxPlayerController*& OutController)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVeloxCharacter* Pawn = World->SpawnActor<AVeloxCharacter>(AVeloxCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
			AVeloxPlayerState* PlayerState = World->SpawnActor<AVeloxPlayerState>();
			OutController = World->SpawnActor<AVeloxPlayerController>(AVeloxPlayerController::StaticClass(), FTransform::Identity, Params);
			if (!Pawn || !PlayerState || !OutController)
			{
				return nullptr;
			}
			OutController->PlayerState = PlayerState;
			PlayerState->SetOwner(OutController);
			OutController->Possess(Pawn);
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

		// ADR 0003 B36: the overload against the eye at the fire stamp. The pawn has moved 180 cm since that eye (the
		// round-4 shape: a late reliable RPC meets the eye of a later move).
		const FVector StampedEye = Eye + FVector(0.0, -180.0, 0.0);
		Test.TestTrue(TEXT("stamped eye: origin = stamped eye -> unchanged"),
			VeloxShotValidation::ValidateOrigin(*W.World, *Shooter, StampedEye, StampedEye, MaxErrorCm, Reason).Equals(StampedEye, 1e-6) && !Reason);
		Test.TestTrue(TEXT("live eye (pre-B36): the same origin -> snapped to the live eye"),
			VeloxShotValidation::ValidateOrigin(*W.World, *Shooter, StampedEye, MaxErrorCm, Reason).Equals(Eye, 1e-6));
		Test.TestTrue(TEXT("live eye (pre-B36): reason distance"), Reason && FCString::Strcmp(Reason, TEXT("distance")) == 0);
		const FVector Behind = StampedEye + FVector(0.0, -30.0, 0.0);
		Test.TestTrue(TEXT("stamped eye: 30 cm off, nothing in between -> unchanged"),
			VeloxShotValidation::ValidateOrigin(*W.World, *Shooter, StampedEye, Behind, MaxErrorCm, Reason).Equals(Behind, 1e-6) && !Reason);
		// The occlusion trace runs from the stamped eye: a 10 cm wall between it and the origin 30 cm away.
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (Test.TestNotNull(TEXT("origin: cube mesh"), Cube))
		{
			AStaticMeshActor* Wall = W.World->SpawnActor<AStaticMeshActor>(StampedEye + FVector(0.0, -15.0, 0.0), FRotator::ZeroRotator);
			Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Wall->SetActorScale3D(FVector(4.0, 0.1, 4.0)); // 10 cm thick, 400 cm wide, between y = -10 and -20 of the stamped eye
			Test.TestTrue(TEXT("stamped eye: wall between it and the origin -> snapped to the stamped eye"),
				VeloxShotValidation::ValidateOrigin(*W.World, *Shooter, StampedEye, Behind, MaxErrorCm, Reason).Equals(StampedEye, 1e-6));
			Test.TestTrue(TEXT("stamped eye: reason occluded"), Reason && FCString::Strcmp(Reason, TEXT("occluded")) == 0);
			const FVector Ahead = StampedEye + FVector(30.0, 0.0, 0.0);
			Test.TestTrue(TEXT("stamped eye: 30 cm the other way, wall not in between -> unchanged"),
				VeloxShotValidation::ValidateOrigin(*W.World, *Shooter, StampedEye, Ahead, MaxErrorCm, Reason).Equals(Ahead, 1e-6) && !Reason);
			Wall->Destroy();
		}

		// ResolveShotEye without a ring entry or a stamp: the live eye, late only with a stamp on the authority.
		const FVeloxShotEye NoStamp = VeloxShotValidation::ResolveShotEye(*Shooter, false, 0.f, 0.3f);
		Test.TestTrue(TEXT("resolve: no stamp -> live eye, no late"), NoStamp.Eye.Equals(Eye, 1e-6) && !NoStamp.bHasLate && !NoStamp.bFromRing);
		const FVeloxShotEye EmptyRing = VeloxShotValidation::ResolveShotEye(*Shooter, true, 0.f, 0.3f);
		Test.TestTrue(TEXT("resolve: empty ring -> live eye"), EmptyRing.Eye.Equals(Eye, 1e-6) && !EmptyRing.bFromRing);
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

	// M3-02b: a match-frozen pawn cannot fire (ActivationBlockedTags), and fires again once unfrozen.
	ASC->AddLooseGameplayTag(VeloxMatchTags::State_MatchFrozen);
	ASC->AbilityInputTagPressed(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	TestEqual(TEXT("frozen pawn cannot fire: no bullet spent"), PlayerState->GetAmmoSet()->GetBullets(), BulletsBefore);
	ASC->RemoveLooseGameplayTag(VeloxMatchTags::State_MatchFrozen);

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

	// Task 08b D4: an accepted shot of the pending weapon completes the switch (CompletePendingSwitch, called by
	// FinalisePendingSwitch on the server) before its drop end; a switch in the same frame must then activate. RG -> RL
	// now waits for the rail's refire (1.5 s). B42: the switch instance ends at activation, the inventory owns the drop.
	FGameplayAbilitySpec* SwitchSpec = ASC->FindAbilitySpecFromClass(UVeloxGameplayAbility_WeaponSwitch::StaticClass());
	if (!TestNotNull(TEXT("D4: switch spec"), SwitchSpec))
	{
		return false;
	}
	const FGameplayAbilitySpecHandle SwitchHandle = SwitchSpec->Handle;
	auto SwitchActive = [&]() { const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(SwitchHandle); return Spec && Spec->IsActive(); };
	ASC->AbilityInputTagPressed(InputTag_Weapon_Slot5);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Weapon_Slot5);
	W.Advance(1.f / 60.f);
	TestTrue(TEXT("D4: RG -> RL running (tag, pending RL, drop timer)"), ASC->HasMatchingGameplayTag(State_WeaponSwitching) && Inventory->GetPendingIndex() == RL
		&& Inventory->IsPendingDropTimerActive());
	TestFalse(TEXT("B42: the switch instance ended at activation"), SwitchActive());
	Inventory->CompletePendingSwitch();
	TestEqual(TEXT("D4: completed -> RL current"), Inventory->GetCurrentIndex(), RL);
	TestEqual(TEXT("D4: completed -> no pending index"), Inventory->GetPendingIndex(), static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("D4: completed -> switch instance ended"), SwitchActive());
	TestFalse(TEXT("D4: completed -> GE_WeaponSwitching removed"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));
	TestEqual(TEXT("D4: completed -> SwitchEndTime cleared"), Inventory->GetSwitchEndTime(), 0.0);
	// Same frame: RL -> MG activates (pre-08b: rejected, the instance was still running).
	ASC->AbilityInputTagPressed(InputTag_Weapon_Slot2);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Weapon_Slot2);
	TestEqual(TEXT("D4: switch in the same frame as the completion activates"), Inventory->GetPendingIndex(), MG);
	// B42: a local press during the own drop retargets the switch (pre-B42: ignored); drop end and switch end stay.
	const double SwitchEndMG = Inventory->GetSwitchEndTime();
	ASC->AbilityInputTagPressed(InputTag_Weapon_Slot5);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Weapon_Slot5);
	TestEqual(TEXT("B42: local press during the drop retargets (pending MG -> RL, the weapon being lowered)"), Inventory->GetPendingIndex(), RL);
	TestEqual(TEXT("B42: ... switch end unchanged"), Inventory->GetSwitchEndTime(), SwitchEndMG, 1e-9);
	// The server receiving a new (non-retarget) switch while the previous one is pending (latency-shifted drop end): it
	// supersedes (B33: the switch has 0.45 s <= Raise + Tol left, so it completes first, RL stays current; then RG is a
	// real change whose wait is the superseded switch's remaining 0.45 s, B42).
	{
		FGameplayEventData Event;
		Event.EventTag = InputTag_Weapon_Slot7;
		Event.EventMagnitude = static_cast<float>(RG);
		TestTrue(TEXT("D4: server activation during a running switch supersedes it"), ASC->InternalTryActivateAbility(SwitchHandle, FPredictionKey(), nullptr, nullptr, &Event));
	}
	TestEqual(TEXT("D4: superseded -> pending RG"), Inventory->GetPendingIndex(), RG);
	TestEqual(TEXT("D4: superseded -> current RL"), Inventory->GetCurrentIndex(), RL);
	TestEqual(TEXT("B42: superseding switch end = old remaining 0.45 + drop + raise"), Inventory->GetSwitchEndTime(), SwitchEndMG + 0.45, 2e-3);
	W.Advance(0.7f);
	TestEqual(TEXT("D4: the superseding switch lands on RG"), Inventory->GetCurrentIndex(), RG);
	TestFalse(TEXT("D4: no switch running after its drop"), SwitchActive());
	W.Advance(0.3f);

	// Task 08c (ADR 0003 B33) with B37: a superseding server activation first completes the pending switch if its
	// remaining time is <= Raise + FireRateJitterTolerance (the superseded drop has ended within the one resend
	// tolerance), else it is rejected and the pending switch completes at its own drop end through the inventory's timer
	// (B37 iii). Defaults: Raise 0.25 + Tol 0.5 = 0.75 s; drop 0.2 s.
	{
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory->GetCombatSettings());
		const double Raise = Settings.WeaponRaiseTime;
		const double Drop = Settings.WeaponDropTime;
		const double Limit = Raise + Settings.GetFireRateJitterToleranceSeconds();
		auto ServerSwitch = [&](int32 Target)
		{
			FGameplayEventData Event;
			Event.EventTag = InputTag_Weapon_Slot2;
			Event.EventMagnitude = static_cast<float>(Target);
			ASC->InternalTryActivateAbility(SwitchHandle, FPredictionKey(), nullptr, nullptr, &Event);
		};
		auto ClearSwitch = [&]()
		{
			Inventory->SetCurrentIndex(MG);
			ASC->RemoveActiveGameplayEffectBySourceEffect(UVeloxGE_WeaponSwitching::StaticClass(), nullptr);
			W.Advance(1.6f); // every refire wait (rail 1.5 s) over
		};
		auto FirePress = [&]()
		{
			ASC->AbilityInputTagPressed(InputTag_Fire);
			ASC->ProcessAbilityInput(1.f / 60.f, false);
			ASC->AbilityInputTagReleased(InputTag_Fire);
			ASC->ProcessAbilityInput(1.f / 60.f, false);
		};

		// (a) MG -> RG on the server, 0.1 s later (its drop still running on the server, i.e. the client's drop end has
		// passed and the raise runs there) the server receives RG -> MG: accepted, the RG switch completes first.
		ClearSwitch();
		ServerSwitch(RG);
		TestTrue(TEXT("B33 (a): MG -> RG running, pending RG"), Inventory->GetPendingIndex() == RG && Inventory->GetCurrentIndex() == MG);
		TestTrue(TEXT("B37: SetPendingIndex armed the inventory's drop timer"), Inventory->IsPendingDropTimerActive());
		W.Advance(0.1f);
		const double RemainingA = UVeloxGameplayAbility_Fire::GetSwitchRemaining(ASC);
		AddInfo(FString::Printf(TEXT("B33 (a): remaining %.3f s at the switch-back (limit %.3f, raise %.3f)"), RemainingA, Limit, Raise));
		TestTrue(TEXT("B33 (a): server drop still running (raise < remaining <= raise + tol)"), Inventory->GetPendingIndex() == RG && RemainingA > Raise && RemainingA <= Limit);
		ServerSwitch(MG);
		TestEqual(TEXT("B33 (a): switch-back accepted -> pending MG"), Inventory->GetPendingIndex(), MG);
		TestEqual(TEXT("B33 (a): the superseded switch completed -> current RG"), Inventory->GetCurrentIndex(), RG);
		TestTrue(TEXT("B33 (a): the switch-back runs (tag, drop timer)"), Inventory->IsPendingDropTimerActive() && ASC->HasMatchingGameplayTag(State_WeaponSwitching));
		TestTrue(TEXT("B33 (a): SwitchEndTime of the switch-back"), Inventory->GetSwitchEndTime() > W.World->GetTimeSeconds());
		W.Advance(1.6f);
		TestEqual(TEXT("B33 (a): the switch-back lands on MG"), Inventory->GetCurrentIndex(), MG);
		TestFalse(TEXT("B33 (a): no switch running after it"), SwitchActive());

		// (b) RG current, rail fired (refire wait 1.5 s), RG -> MG on the server: remaining 1.95 s > 0.75 s; a press in
		// the same frame (during the drop) is rejected, back to RG and to a third weapon alike; current, pending and GE
		// unchanged.
		ClearSwitch();
		Inventory->SetCurrentIndex(RG);
		const float SlugsB = PlayerState->GetAmmoSet()->GetSlugs();
		FirePress();
		TestEqual(TEXT("B33 (b): rail fired (refire wait 1.5 s)"), PlayerState->GetAmmoSet()->GetSlugs(), SlugsB - 1.f);
		const double TB = W.World->GetTimeSeconds();
		ServerSwitch(MG);
		const double RemainingB = UVeloxGameplayAbility_Fire::GetSwitchRemaining(ASC);
		AddInfo(FString::Printf(TEXT("B33 (b): remaining %.3f s at the press (limit %.3f)"), RemainingB, Limit));
		TestTrue(TEXT("B33 (b): RG -> MG running, remaining > raise + tol"), Inventory->GetPendingIndex() == MG && RemainingB > Limit);
		ServerSwitch(RG);
		TestEqual(TEXT("B33 (b): MG -> RG during the drop rejected -> current RG"), Inventory->GetCurrentIndex(), RG);
		TestEqual(TEXT("B33 (b): pending MG untouched"), Inventory->GetPendingIndex(), MG);
		TestTrue(TEXT("B33 (b): GE_WeaponSwitching still active, same remaining"), ASC->HasMatchingGameplayTag(State_WeaponSwitching)
			&& FMath::IsNearlyEqual(UVeloxGameplayAbility_Fire::GetSwitchRemaining(ASC), RemainingB, 1e-3));
		ServerSwitch(RL);
		TestTrue(TEXT("B33 (b): a third weapon during the drop rejected too (current RG, pending MG)"), Inventory->GetCurrentIndex() == RG && Inventory->GetPendingIndex() == MG);
		TestFalse(TEXT("B33 (b): no switch instance running after the rejection"), SwitchActive());

		// (c) B37 iii (08e item 2): no switch instance waits for the drop (B42: it ended at activation), the inventory's own timer
		// still completes the switch at its drop end (refire 1.5 + drop 0.2) without any further RPC; the GE keeps
		// running through the raise.
		TestTrue(TEXT("B37 (c): the superseded switch's drop timer is still armed after the rejection"), Inventory->IsPendingDropTimerActive());
		const double DropEndC = TB + 1.5 + Drop;
		W.AdvanceTo(DropEndC - 0.05);
		TestTrue(TEXT("B37 (c): before the drop end: current RG, pending MG"), Inventory->GetCurrentIndex() == RG && Inventory->GetPendingIndex() == MG);
		W.AdvanceTo(DropEndC + 1.0 / 60.0);
		TestEqual(TEXT("B37 (c): at the drop end, no RPC -> current MG"), Inventory->GetCurrentIndex(), MG);
		TestEqual(TEXT("B37 (c): ... pending none"), Inventory->GetPendingIndex(), static_cast<int32>(INDEX_NONE));
		TestFalse(TEXT("B37 (c): ... timer done"), Inventory->IsPendingDropTimerActive());
		const double RemainingC = UVeloxGameplayAbility_Fire::GetSwitchRemaining(ASC);
		AddInfo(FString::Printf(TEXT("B37 (c): GE remaining %.3f s after the drop end (raise %.3f)"), RemainingC, Raise));
		TestTrue(TEXT("B37 (c): ... GE_WeaponSwitching still active through the raise"), ASC->HasMatchingGameplayTag(State_WeaponSwitching) && RemainingC > 0.0 && RemainingC <= Raise);
		W.Advance(0.3f);
		TestFalse(TEXT("B37 (c): GE gone after the raise"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));

		// (d) B37 iii: an accepted shot of the pending weapon before the drop end (FinalisePendingSwitch ->
		// CompletePendingSwitch) completes the switch and clears the timer: exactly one weapon change, none at the old
		// drop end.
		ClearSwitch();
		int32 Changes = 0;
		const FDelegateHandle ChangeHandle = Inventory->OnCurrentWeaponChanged.AddLambda([&Changes](int32, int32) { ++Changes; });
		ServerSwitch(RG);
		TestTrue(TEXT("B37 (d): MG -> RG running, timer armed"), Inventory->GetPendingIndex() == RG && Inventory->IsPendingDropTimerActive());
		W.Advance(0.1f);
		Inventory->CompletePendingSwitch();
		TestTrue(TEXT("B37 (d): the shot completed it -> current RG, nothing pending"), Inventory->GetCurrentIndex() == RG && Inventory->GetPendingIndex() == INDEX_NONE);
		TestFalse(TEXT("B37 (d): ... and cleared the drop timer"), Inventory->IsPendingDropTimerActive());
		W.Advance(0.3f); // past the old drop end
		TestEqual(TEXT("B37 (d): one weapon change only (no second SetCurrentIndex at the old drop end)"), Changes, 1);
		TestEqual(TEXT("B37 (d): still RG"), Inventory->GetCurrentIndex(), RG);
		Inventory->OnCurrentWeaponChanged.Remove(ChangeHandle);
		W.Advance(0.3f);
	}

	// Respawn semantics: the loadout again, RG/RL taken away, a running switch's local end cleared and its drop end
	// cancelled (B37: the inventory's timer must not land a weapon of the old life).
	ASC->AbilityInputTagPressed(InputTag_Weapon_Slot5);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Weapon_Slot5);
	TestTrue(TEXT("switch to RL running"), Inventory->GetSwitchEndTime() > W.World->GetTimeSeconds() && Inventory->GetPendingIndex() == RL);
	Inventory->ResetToLoadout();
	TestEqual(TEXT("reset clears SwitchEndTime"), Inventory->GetSwitchEndTime(), 0.0);
	TestFalse(TEXT("reset clears the pending drop timer (B37)"), Inventory->IsPendingDropTimerActive());
	// B25: the shot counter restarts per life (an MG and a rail shot were fired above).
	TestEqual(TEXT("reset: next shot sequence is 1"), static_cast<int32>(Inventory->AllocateShotSeq()), 1);
	TestEqual(TEXT("reset: MG only"), static_cast<int32>(Inventory->GetOwnedMask()), 1 << MG);
	TestNull(TEXT("reset: RG spec removed"), Inventory->FindFireSpec(RG));
	TestNotNull(TEXT("reset: MG spec kept"), Inventory->FindFireSpec(MG));
	TestEqual(TEXT("reset: MG selected"), Inventory->GetCurrentIndex(), MG);
	W.Advance(0.5f);
	TestEqual(TEXT("reset: the pre-reset switch to RL never lands"), Inventory->GetCurrentIndex(), MG);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatRemoteSwitchTimelineTest, "Velox.Combat.RemoteSwitchTimeline", VeloxWeaponTest::Flags)

bool FVeloxCombatRemoteSwitchTimelineTest::RunTest(const FString& Parameters)
{
	// ADR 0003 B37 on the real abilities, server for a remote shooter (activation and target data RPCs run on this
	// authority): (i) bunched accepted MG shots do not inflate the switch's refire wait (no receive-time NextFireTime);
	// (ii) the switch gate uses the 0.5 s resend tolerance; (iii) an accepted shot of the pending weapon clears the drop
	// timer.
	using namespace VeloxWeaponTest;
	using namespace VeloxGameplayTags;

	FWeaponWorld W;
	AVeloxPlayerController* ShooterPC = nullptr;
	AVeloxCharacter* Shooter = W.SpawnRemoteShooter(FVector(0, 0, 100), ShooterPC);
	if (!TestNotNull(TEXT("remote shooter"), Shooter) || !PS(Shooter))
	{
		return false;
	}
	W.Advance(0.3f);
	TestTrue(TEXT("give all"), VeloxWeaponCheats::GiveAllWeapons(*PS(Shooter)));
	UVeloxAbilitySystemComponent* ASC = PS(Shooter)->GetVeloxAbilitySystemComponent();
	UVeloxWeaponInventoryComponent* Inventory = PS(Shooter)->GetWeaponInventory();
	const UVeloxWeaponTable* Table = Inventory ? Inventory->GetWeaponTable() : nullptr;
	if (!TestNotNull(TEXT("inventory"), Inventory) || !TestNotNull(TEXT("table"), Table) || !ASC)
	{
		return false;
	}
	const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();
	if (!TestTrue(TEXT("remote shooter path"), UVeloxGameplayAbility_Fire::IsServerForRemoteShooter(ActorInfo)))
	{
		return false;
	}
	const int32 MG = Table->FindBySlot(2);
	const int32 RG = Table->FindBySlot(7);
	const int32 RL = Table->FindBySlot(5);
	FGameplayAbilitySpec* MGSpec = Inventory->FindFireSpec(MG);
	FGameplayAbilitySpec* RGSpec = Inventory->FindFireSpec(RG);
	const FGameplayAbilitySpec* SwitchSpec = ASC->FindAbilitySpecFromClass(UVeloxGameplayAbility_WeaponSwitch::StaticClass());
	if (!TestNotNull(TEXT("MG spec"), MGSpec) || !TestNotNull(TEXT("RG spec"), RGSpec) || !TestNotNull(TEXT("switch spec"), SwitchSpec))
	{
		return false;
	}
	const FGameplayAbilitySpecHandle MGHandle = MGSpec->Handle;
	const FGameplayAbilitySpecHandle RGHandle = RGSpec->Handle;
	const FGameplayAbilitySpecHandle SwitchHandle = SwitchSpec->Handle;
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory->GetCombatSettings());
	const double Tol = Settings.GetFireRateJitterToleranceSeconds();
	int16 NextKey = 100;

	// One remote hitscan shot: the client's activation RPC, then its target data (into empty space, +Y).
	auto RemoteShot = [&](FGameplayAbilitySpecHandle Handle, uint8 Seq)
	{
		FPredictionKey Key;
		Key.Current = NextKey++;
		ASC->CallServerTryActivateAbility(Handle, true, Key);
		FGameplayAbilityTargetData_VeloxShot* Data = new FGameplayAbilityTargetData_VeloxShot();
		Data->Origin = Shooter->GetPawnViewLocation();
		Data->Dir = FVector(0.0, 1.0, 0.0);
		Data->ViewServerTime = static_cast<float>(W.World->GetTimeSeconds());
		Data->ShotSeq = Seq;
		ASC->CallServerSetReplicatedTargetData(Handle, Key, FGameplayAbilityTargetDataHandle(Data), FGameplayTag(), Key);
	};
	// The client's switch press arriving at the server (LocalPredicted, valid prediction key).
	auto RemoteSwitch = [&](int32 Target)
	{
		FPredictionKey Key;
		Key.Current = NextKey++;
		FGameplayEventData Event;
		Event.EventTag = InputTag_Weapon_Slot7;
		Event.EventMagnitude = static_cast<float>(Target);
		return ASC->InternalTryActivateAbility(SwitchHandle, Key, nullptr, nullptr, &Event);
	};
	auto Refire = [&]() { return UVeloxGameplayAbility_WeaponSwitch::RefireWait(ActorInfo, true); };
	const UVeloxGameplayAbility_Fire* MGFire = Cast<UVeloxGameplayAbility_Fire>(MGSpec->GetPrimaryInstance());
	const UVeloxGameplayAbility_Fire* RGFire = Cast<UVeloxGameplayAbility_Fire>(RGSpec->GetPrimaryInstance());

	// No shot of the current weapon yet -> 0.
	Inventory->SetCurrentIndex(RL);
	TestEqual(TEXT("B37: no shot accepted -> refire wait 0"), Refire(), 0.0, 1e-9);

	// (i) Six MG shots accepted in one server frame (a bunched resend; the bucket holds 6).
	Inventory->SetCurrentIndex(MG);
	const float Bullets0 = PS(Shooter)->GetAmmoSet()->GetBullets();
	const double TMG = W.World->GetTimeSeconds();
	for (uint8 Seq = 1; Seq <= 6; ++Seq)
	{
		RemoteShot(MGHandle, Seq);
	}
	MGFire = Cast<UVeloxGameplayAbility_Fire>(Inventory->FindFireSpec(MG)->GetPrimaryInstance());
	TestEqual(TEXT("B37 (i): six bunched MG shots accepted (six bullets)"), PS(Shooter)->GetAmmoSet()->GetBullets(), Bullets0 - 6.f);
	TestTrue(TEXT("B37 (i): last accepted receive time = this frame"), MGFire && FMath::IsNearlyEqual(MGFire->GetLastAcceptedReceiveTime(), TMG, 1e-9));
	TestTrue(TEXT("B37 (i): no NextFireTime carried on the server"), MGFire && MGFire->GetNextFireTime() < 0.0);
	W.Advance(1.f / 60.f);
	const double WaitBunched = Refire();
	AddInfo(FString::Printf(TEXT("B37 (i): refire wait one frame after six bunched MG shots: %.4f s (08c: 0.373 under the carry)"), WaitBunched));
	TestTrue(TEXT("B37 (i): refire wait in the next frame <= Interval (0.1), not 0.6"), WaitBunched > 0.0 && WaitBunched <= 0.1 + 1e-6);

	// (ii) Switch gate with the 0.5 s resend tolerance. One MG shot, the switch MG -> RG in the same frame: wait 0.1 +
	// drop 0.2 + raise 0.25 = 0.55 s left > 0.5 -> a rail now is rejected.
	W.Advance(0.5f);
	RemoteShot(MGHandle, 7);
	TestEqual(TEXT("B37: single MG shot -> refire wait = Interval"), Refire(), 0.1, 1e-6);
	const double TS = W.World->GetTimeSeconds();
	TestTrue(TEXT("B37 (ii): server switch MG -> RG activates"), RemoteSwitch(RG));
	const double Remaining055 = UVeloxGameplayAbility_Fire::GetSwitchRemaining(ASC);
	TestTrue(TEXT("B37 (ii): GE remaining 0.55, pending RG, timer armed"), FMath::IsNearlyEqual(Remaining055, 0.55, 1e-3) && Inventory->GetPendingIndex() == RG
		&& Inventory->IsPendingDropTimerActive());
	const float Slugs0 = PS(Shooter)->GetAmmoSet()->GetSlugs();
	RemoteShot(RGHandle, 8);
	TestEqual(TEXT("B37 (ii): rail at remaining 0.55 > 0.5 rejected (no slug)"), PS(Shooter)->GetAmmoSet()->GetSlugs(), Slugs0);
	TestEqual(TEXT("B37 (ii): ... switch still pending"), Inventory->GetPendingIndex(), RG);
	// The switch RPC was 0.33 s late relative to the client's: the rail meets the GE with 0.33 + 0.02 left -> accepted
	// (rejected under the former 0.25). The shot of the pending weapon completes the switch before the drop end (0.3 s).
	W.AdvanceTo(TS + 0.55 - (0.33 + 0.02));
	const double Remaining035 = UVeloxGameplayAbility_Fire::GetSwitchRemaining(ASC);
	AddInfo(FString::Printf(TEXT("B37 (ii): remaining %.4f at the late rail (tolerance %.2f)"), Remaining035, Tol));
	TestTrue(TEXT("B37 (ii): remaining ~0.35, still pending (drop end at 0.3)"), FMath::IsNearlyEqual(Remaining035, 0.35, 2e-3) && Inventory->GetPendingIndex() == RG);
	const double TR = W.World->GetTimeSeconds();
	RemoteShot(RGHandle, 9);
	RGFire = Cast<UVeloxGameplayAbility_Fire>(Inventory->FindFireSpec(RG)->GetPrimaryInstance());
	TestEqual(TEXT("B37 (ii): rail at remaining 0.35 <= 0.5 accepted"), PS(Shooter)->GetAmmoSet()->GetSlugs(), Slugs0 - 1.f);
	TestTrue(TEXT("B37 (iii): the accepted rail completed the switch (current RG, nothing pending)"), Inventory->GetCurrentIndex() == RG && Inventory->GetPendingIndex() == INDEX_NONE);
	TestFalse(TEXT("B37 (iii): ... and cleared the drop timer"), Inventory->IsPendingDropTimerActive());

	// (i) Rail accepted at T -> server refire wait at T + 0.4 = 1.1 s.
	TestTrue(TEXT("B37: rail receive time recorded"), RGFire && FMath::IsNearlyEqual(RGFire->GetLastAcceptedReceiveTime(), TR, 1e-9));
	W.AdvanceTo(TR + 0.4);
	const double WaitRail = Refire();
	AddInfo(FString::Printf(TEXT("B37: rail refire wait 0.4 s after the shot: %.4f"), WaitRail));
	TestEqual(TEXT("B37: rail accepted at T -> wait at T + 0.4 = 1.1"), WaitRail, 1.1, 2e-3);
	W.Advance(0.3f);
	TestEqual(TEXT("B37: still RG after the old drop end (no second index change)"), Inventory->GetCurrentIndex(), RG);

	// B41 / ADR 0004 section 3: the server's refire wait of a remote shooter uses the Haste interval (the server tag).
	// Haste on, one MG shot accepted at T -> one frame later the wait is 0.1 / 1.3 - 1/60; tag removed, a new shot -> 0.1 - 1/60.
	Inventory->SetCurrentIndex(MG);
	W.Advance(1.0f); // the MG bucket is full again, no recent shot
	ASC->AddLooseGameplayTag(Powerup_Haste);
	const double THaste = W.World->GetTimeSeconds();
	RemoteShot(MGHandle, 10);
	W.AdvanceTo(THaste + 1.0 / 60.0);
	const double WaitHaste = Refire();
	AddInfo(FString::Printf(TEXT("B41: MG refire wait one frame after a shot under Haste: %.4f (want 0.0603)"), WaitHaste));
	TestEqual(TEXT("B41: Haste: refire wait at T + 1/60 = 0.1 / 1.3 - 1/60"), WaitHaste, 0.1 / 1.3 - 1.0 / 60.0, 1e-3);
	ASC->RemoveLooseGameplayTag(Powerup_Haste);
	W.Advance(1.0f);
	const double TNoHaste = W.World->GetTimeSeconds();
	RemoteShot(MGHandle, 11);
	W.AdvanceTo(TNoHaste + 1.0 / 60.0);
	const double WaitNoHaste = Refire();
	AddInfo(FString::Printf(TEXT("B41: MG refire wait one frame after a shot without Haste: %.4f (want 0.0833)"), WaitNoHaste));
	TestEqual(TEXT("B41: no Haste: refire wait at T' + 1/60 = 0.1 - 1/60"), WaitNoHaste, 0.1 - 1.0 / 60.0, 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatSwitchRetargetRulesTest, "Velox.Combat.SwitchRetargetRules", VeloxWeaponTest::Flags)

bool FVeloxCombatSwitchRetargetRulesTest::RunTest(const FString& Parameters)
{
	// ADR 0003 B42, world-free: the new switch's wait, the server's retarget classification, the event encoding.
	using namespace VeloxFireTiming;
	TestEqual(TEXT("wait: no switch running -> refire wait"), SwitchStartWait(0.3, 0.0), 0.3, 1e-12);
	TestEqual(TEXT("wait: in a raise -> the remaining raise (Q3 weaponTime)"), SwitchStartWait(0.0, 0.15), 0.15, 1e-12);
	TestEqual(TEXT("wait: the larger of both, never a sum"), SwitchStartWait(0.1, 0.15), 0.15, 1e-12);
	TestEqual(TEXT("wait: negatives clamp to 0"), SwitchStartWait(-1.0, -2.0), 0.0, 1e-12);

	TestTrue(TEXT("classify: pending drop -> Pending"), ClassifyRemoteRetarget(true, 10.0, -1.0, 0.5) == ERemoteRetarget::Pending);
	TestTrue(TEXT("classify: pending wins over a recent drop end"), ClassifyRemoteRetarget(true, 10.0, 9.9, 0.5) == ERemoteRetarget::Pending);
	TestTrue(TEXT("classify: drop ended 0.1 s ago -> Late"), ClassifyRemoteRetarget(false, 10.0, 9.9, 0.5) == ERemoteRetarget::Late);
	TestTrue(TEXT("classify: drop ended exactly Tol ago -> Late"), ClassifyRemoteRetarget(false, 10.5, 10.0, 0.5) == ERemoteRetarget::Late);
	TestTrue(TEXT("classify: drop ended > Tol ago -> Fresh"), ClassifyRemoteRetarget(false, 10.51, 10.0, 0.5) == ERemoteRetarget::Fresh);
	TestTrue(TEXT("classify: no drop this life -> Fresh"), ClassifyRemoteRetarget(false, 10.0, -1.0, 0.5) == ERemoteRetarget::Fresh);
	TestTrue(TEXT("classify: drop end in the future (bad input) -> Fresh"), ClassifyRemoteRetarget(false, 10.0, 10.2, 0.5) == ERemoteRetarget::Fresh);

	using Switch = UVeloxGameplayAbility_WeaponSwitch;
	int32 Index = 0;
	bool bRetarget = false;
	for (const int32 I : { 0, 3, 15 })
	{
		for (const bool bR : { false, true })
		{
			const bool bOk = Switch::DecodeSwitchEvent(Switch::EncodeSwitchEvent(I, bR), Index, bRetarget);
			TestTrue(*FString::Printf(TEXT("event round trip index %d retarget %d"), I, bR ? 1 : 0), bOk && Index == I && bRetarget == bR);
		}
	}
	TestFalse(TEXT("event: negative rejected"), Switch::DecodeSwitchEvent(-1.f, Index, bRetarget));
	TestFalse(TEXT("event: 32 rejected"), Switch::DecodeSwitchEvent(32.f, Index, bRetarget));
	TestFalse(TEXT("event: NaN rejected"), Switch::DecodeSwitchEvent(std::numeric_limits<float>::quiet_NaN(), Index, bRetarget));
	TestFalse(TEXT("event: inf rejected"), Switch::DecodeSwitchEvent(std::numeric_limits<float>::infinity(), Index, bRetarget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatSwitchRetargetLocalTest, "Velox.Combat.SwitchRetargetLocal", VeloxWeaponTest::Flags)

bool FVeloxCombatSwitchRetargetLocalTest::RunTest(const FString& Parameters)
{
	// ADR 0003 B42 on the shooter's machine (an AI-controlled authority: the bot path; the owning client runs the same
	// PrepareInputActivation / ActivateAbility / inventory drop, predicting the index instead of setting it), through the
	// real input path: retarget in the drop (to a third weapon, back to the weapon being lowered, the same target again,
	// an unowned slot), a press in the raise (wait = remaining raise), the current weapon in the raise (no-op).
	using namespace VeloxWeaponTest;
	using namespace VeloxGameplayTags;

	FWeaponWorld W;
	AVeloxCharacter* Pawn = W.Spawn(FVector(0, 0, 100), AAIController::StaticClass());
	AVeloxPlayerState* PlayerState = PS(Pawn);
	UVeloxWeaponInventoryComponent* Inventory = PlayerState ? PlayerState->GetWeaponInventory() : nullptr;
	UVeloxAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetVeloxAbilitySystemComponent() : nullptr;
	const UVeloxWeaponTable* Table = Inventory ? Inventory->GetWeaponTable() : nullptr;
	if (!TestNotNull(TEXT("inventory"), Inventory) || !TestNotNull(TEXT("table"), Table) || !ASC)
	{
		return false;
	}
	const int32 MG = Table->FindBySlot(2);
	const int32 SG = Table->FindBySlot(3);
	const int32 RL = Table->FindBySlot(5);
	const int32 RG = Table->FindBySlot(7);
	const int32 PG = Table->FindBySlot(8);
	TestTrue(TEXT("add SG, RL, RG (PG stays unowned)"), Inventory->AddWeapon(Table->Get(SG), 10) && Inventory->AddWeapon(Table->Get(RL), 5) && Inventory->AddWeapon(Table->Get(RG), 5));
	TestFalse(TEXT("PG unowned"), Inventory->OwnsIndex(PG));
	TestFalse(TEXT("local shooter path"), UVeloxGameplayAbility_Fire::IsServerForRemoteShooter(ASC->AbilityActorInfo.Get()));
	W.Advance(0.5f);
	TestEqual(TEXT("MG current"), Inventory->GetCurrentIndex(), MG);

	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory->GetCombatSettings());
	const double Drop = Settings.WeaponDropTime;
	const double Raise = Settings.WeaponRaiseTime;
	const FGameplayAbilitySpec* SwitchSpec = ASC->FindAbilitySpecFromClass(UVeloxGameplayAbility_WeaponSwitch::StaticClass());
	if (!TestNotNull(TEXT("switch spec"), SwitchSpec))
	{
		return false;
	}
	const FGameplayAbilitySpecHandle SwitchHandle = SwitchSpec->Handle;
	auto SwitchActive = [&]() { const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(SwitchHandle); return Spec && Spec->IsActive(); };
	auto Press = [&](const FGameplayTag& Tag)
	{
		ASC->AbilityInputTagPressed(Tag);
		ASC->ProcessAbilityInput(1.f / 60.f, false);
		ASC->AbilityInputTagReleased(Tag);
	};
	int32 Changes = 0;
	const FDelegateHandle ChangeHandle = Inventory->OnCurrentWeaponChanged.AddLambda([&Changes](int32, int32) { ++Changes; });

	// MG -> RG (no refire wait): drop end T + Drop, switch end T + Drop + Raise.
	const double T = W.World->GetTimeSeconds();
	Press(InputTag_Weapon_Slot7);
	TestTrue(TEXT("MG -> RG: pending RG, drop timer"), Inventory->GetPendingIndex() == RG && Inventory->IsPendingDropTimerActive());
	TestFalse(TEXT("B42: no switch instance stays active"), SwitchActive());
	const double SwitchEnd = Inventory->GetSwitchEndTime();
	TestEqual(TEXT("switch end = T + Drop + Raise"), SwitchEnd, T + Drop + Raise, 1e-3);

	// Retarget in the drop (0.1 s in): RG -> SG; drop end and switch end stay.
	W.AdvanceTo(T + 0.1);
	Press(InputTag_Weapon_Slot3);
	TestEqual(TEXT("drop: retarget to SG"), Inventory->GetPendingIndex(), SG);
	TestEqual(TEXT("drop: switch end unchanged"), Inventory->GetSwitchEndTime(), SwitchEnd, 1e-9);
	TestEqual(TEXT("drop: drop end unchanged (remaining)"), Inventory->GetPendingDropRemaining(), T + Drop - W.World->GetTimeSeconds(), 2e-3);
	TestEqual(TEXT("drop: still MG"), Inventory->GetCurrentIndex(), MG);
	// The same target again: no-op. An unowned slot: ignored. Next cycles from the pending target (SG -> RL).
	Press(InputTag_Weapon_Slot3);
	TestTrue(TEXT("drop: same target again -> no-op"), Inventory->GetPendingIndex() == SG && FMath::IsNearlyEqual(Inventory->GetSwitchEndTime(), SwitchEnd, 1e-9));
	Press(InputTag_Weapon_Slot8);
	TestTrue(TEXT("drop: unowned PG ignored"), Inventory->GetPendingIndex() == SG && FMath::IsNearlyEqual(Inventory->GetSwitchEndTime(), SwitchEnd, 1e-9));
	TestEqual(TEXT("drop: Next resolves from the pending target (SG -> RL)"), Inventory->ResolveSelectInput(InputTag_Weapon_Next), RL);
	// Back to the weapon being lowered: Q3 raises it again at the same drop end (no shortcut).
	Press(InputTag_Weapon_Slot2);
	TestEqual(TEXT("drop: retarget back to MG"), Inventory->GetPendingIndex(), MG);
	TestEqual(TEXT("drop: switch end still unchanged"), Inventory->GetSwitchEndTime(), SwitchEnd, 1e-9);
	W.AdvanceTo(T + Drop + 0.02);
	TestTrue(TEXT("drop end: MG current, nothing pending"), Inventory->GetCurrentIndex() == MG && Inventory->GetPendingIndex() == INDEX_NONE);
	TestEqual(TEXT("drop end: no weapon change (MG -> MG)"), Changes, 0);
	TestTrue(TEXT("raise: still switching"), ASC->HasMatchingGameplayTag(State_WeaponSwitching) && W.World->GetTimeSeconds() < SwitchEnd);
	// The fire gate holds through the raise of the re-raised MG.
	const float Bullets0 = PlayerState->GetAmmoSet()->GetBullets();
	ASC->AbilityInputTagPressed(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	TestEqual(TEXT("raise: no MG shot before the switch end"), PlayerState->GetAmmoSet()->GetBullets(), Bullets0);

	// Raise phase (0.1 s before the switch end): the current weapon again is a no-op; RL is a new switch whose wait is the
	// remaining raise: switch end = Now + remaining + Drop + Raise, drop end = Now + remaining + Drop.
	W.AdvanceTo(SwitchEnd - 0.1);
	Press(InputTag_Weapon_Slot2);
	TestTrue(TEXT("raise: the current weapon again -> no-op"), Inventory->GetPendingIndex() == INDEX_NONE && FMath::IsNearlyEqual(Inventory->GetSwitchEndTime(), SwitchEnd, 1e-9));
	const double TR = W.World->GetTimeSeconds();
	const double Remaining = SwitchEnd - TR;
	Press(InputTag_Weapon_Slot5);
	AddInfo(FString::Printf(TEXT("raise press: remaining raise %.4f s"), Remaining));
	TestEqual(TEXT("raise: RL pending"), Inventory->GetPendingIndex(), RL);
	TestEqual(TEXT("raise: switch end = now + remaining raise + drop + raise"), Inventory->GetSwitchEndTime(), TR + Remaining + Drop + Raise, 2e-3);
	TestEqual(TEXT("raise: drop remaining = remaining raise + drop"), Inventory->GetPendingDropRemaining(), Remaining + Drop, 2e-3);
	TestEqual(TEXT("raise: GE covers the whole new switch"), UVeloxGameplayAbility_Fire::GetSwitchRemaining(ASC), Remaining + Drop + Raise, 2e-3);
	const double SwitchEndRL = Inventory->GetSwitchEndTime();
	W.AdvanceTo(TR + Remaining + Drop - 0.02);
	TestEqual(TEXT("raise: still MG before the new drop end"), Inventory->GetCurrentIndex(), MG);
	W.AdvanceTo(TR + Remaining + Drop + 0.02);
	TestEqual(TEXT("raise: RL after the new drop end"), Inventory->GetCurrentIndex(), RL);
	// In RL's raise: RL again is a no-op (current).
	Press(InputTag_Weapon_Slot5);
	TestTrue(TEXT("RL raise: RL again -> no-op"), Inventory->GetPendingIndex() == INDEX_NONE && FMath::IsNearlyEqual(Inventory->GetSwitchEndTime(), SwitchEndRL, 1e-9));
	W.AdvanceTo(SwitchEndRL + 0.02);
	TestFalse(TEXT("switch over: tag gone"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));
	TestEqual(TEXT("one weapon change in all (MG -> RL)"), Changes, 1);

	// Retarget during the refire wait (part of the wait/drop): fire RL (0.8 s refire), switch to RG, retarget to SG.
	ASC->AbilityInputTagPressed(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	ASC->AbilityInputTagReleased(InputTag_Fire);
	ASC->ProcessAbilityInput(1.f / 60.f, false);
	W.Advance(1.f / 60.f);
	Press(InputTag_Weapon_Slot7);
	const double SwitchEndW = Inventory->GetSwitchEndTime();
	const double DropLeftW = Inventory->GetPendingDropRemaining();
	AddInfo(FString::Printf(TEXT("refire-wait switch: drop remaining %.4f s"), DropLeftW));
	TestTrue(TEXT("refire wait: RG pending, drop end after the refire wait"), Inventory->GetPendingIndex() == RG && DropLeftW > Drop + 0.5);
	W.Advance(0.2f);
	Press(InputTag_Weapon_Slot3);
	TestTrue(TEXT("refire wait: retarget to SG keeps the switch end"), Inventory->GetPendingIndex() == SG && FMath::IsNearlyEqual(Inventory->GetSwitchEndTime(), SwitchEndW, 1e-9));
	W.AdvanceTo(SwitchEndW + 0.02);
	TestEqual(TEXT("refire wait: lands on SG"), Inventory->GetCurrentIndex(), SG);
	Inventory->OnCurrentWeaponChanged.Remove(ChangeHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCombatSwitchRetargetRemoteTest, "Velox.Combat.SwitchRetargetRemote", VeloxWeaponTest::Flags)

bool FVeloxCombatSwitchRetargetRemoteTest::RunTest(const FString& Parameters)
{
	// ADR 0003 B42 on the server for a remote shooter (authoritative): a retarget RPC applied to the pending switch, to a
	// drop that ended within the resend tolerance (late), outside it (fresh), the same target, an unowned weapon, and the
	// raise-phase wait from the server's GE.
	using namespace VeloxWeaponTest;
	using namespace VeloxGameplayTags;

	FWeaponWorld W;
	AVeloxPlayerController* ShooterPC = nullptr;
	AVeloxCharacter* Shooter = W.SpawnRemoteShooter(FVector(0, 0, 100), ShooterPC);
	if (!TestNotNull(TEXT("remote shooter"), Shooter) || !PS(Shooter))
	{
		return false;
	}
	W.Advance(0.3f);
	UVeloxAbilitySystemComponent* ASC = PS(Shooter)->GetVeloxAbilitySystemComponent();
	UVeloxWeaponInventoryComponent* Inventory = PS(Shooter)->GetWeaponInventory();
	const UVeloxWeaponTable* Table = Inventory ? Inventory->GetWeaponTable() : nullptr;
	if (!TestNotNull(TEXT("inventory"), Inventory) || !TestNotNull(TEXT("table"), Table) || !ASC)
	{
		return false;
	}
	if (!TestTrue(TEXT("remote shooter path"), UVeloxGameplayAbility_Fire::IsServerForRemoteShooter(ASC->AbilityActorInfo.Get())))
	{
		return false;
	}
	const int32 MG = Table->FindBySlot(2);
	const int32 SG = Table->FindBySlot(3);
	const int32 RL = Table->FindBySlot(5);
	const int32 RG = Table->FindBySlot(7);
	const int32 PG = Table->FindBySlot(8);
	TestTrue(TEXT("add SG, RL, RG (PG stays unowned)"), Inventory->AddWeapon(Table->Get(SG), 10) && Inventory->AddWeapon(Table->Get(RL), 5) && Inventory->AddWeapon(Table->Get(RG), 5));
	Inventory->SetCurrentIndex(MG);
	const FGameplayAbilitySpec* SwitchSpec = ASC->FindAbilitySpecFromClass(UVeloxGameplayAbility_WeaponSwitch::StaticClass());
	if (!TestNotNull(TEXT("switch spec"), SwitchSpec))
	{
		return false;
	}
	const FGameplayAbilitySpecHandle SwitchHandle = SwitchSpec->Handle;
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory->GetCombatSettings());
	const double Drop = Settings.WeaponDropTime;
	const double Raise = Settings.WeaponRaiseTime;
	const double Tol = Settings.GetFireRateJitterToleranceSeconds();
	int16 NextKey = 200;
	auto RemoteSwitch = [&](int32 Target, bool bRetarget)
	{
		FPredictionKey Key;
		Key.Current = NextKey++;
		FGameplayEventData Event;
		Event.EventTag = InputTag_Weapon_Slot2;
		Event.EventMagnitude = UVeloxGameplayAbility_WeaponSwitch::EncodeSwitchEvent(Target, bRetarget);
		return ASC->InternalTryActivateAbility(SwitchHandle, Key, nullptr, nullptr, &Event);
	};
	auto GERemaining = [&]() { return UVeloxGameplayAbility_Fire::GetSwitchRemaining(ASC); };

	// (1) Pending: MG -> RG, 0.1 s later retarget SG, then MG (the weapon being lowered); drop end and GE stay.
	const double T = W.World->GetTimeSeconds();
	TestTrue(TEXT("MG -> RG activates"), RemoteSwitch(RG, false));
	TestEqual(TEXT("GE = drop + raise"), GERemaining(), Drop + Raise, 1e-3);
	W.AdvanceTo(T + 0.1);
	RemoteSwitch(SG, true);
	TestEqual(TEXT("(1) retarget pending -> SG"), Inventory->GetPendingIndex(), SG);
	TestEqual(TEXT("(1) drop end unchanged"), Inventory->GetPendingDropRemaining(), T + Drop - W.World->GetTimeSeconds(), 2e-3);
	TestEqual(TEXT("(1) GE unchanged"), GERemaining(), T + Drop + Raise - W.World->GetTimeSeconds(), 2e-3);
	RemoteSwitch(SG, true);
	TestEqual(TEXT("(1) same target again -> no-op"), Inventory->GetPendingIndex(), SG);
	RemoteSwitch(PG, true);
	TestEqual(TEXT("(1) unowned PG rejected -> pending SG"), Inventory->GetPendingIndex(), SG);
	RemoteSwitch(MG, true);
	TestEqual(TEXT("(1) back to the weapon being lowered -> pending MG"), Inventory->GetPendingIndex(), MG);
	W.AdvanceTo(T + Drop + 0.02);
	TestTrue(TEXT("(1) drop end: MG current, nothing pending"), Inventory->GetCurrentIndex() == MG && Inventory->GetPendingIndex() == INDEX_NONE);
	TestEqual(TEXT("(1) last drop end recorded"), Inventory->GetLastDropEndTime(), T + Drop, 1.0 / 60.0 + 1e-3);

	// (2) Late: the retarget arrives after the server's drop end (jitter): within the tolerance the target becomes current
	// at once, the raise (GE) runs on.
	W.Advance(1.0f);
	const double T2 = W.World->GetTimeSeconds();
	RemoteSwitch(RG, false);
	W.AdvanceTo(T2 + Drop + 0.05);
	TestEqual(TEXT("(2) RG after the server's drop end"), Inventory->GetCurrentIndex(), RG);
	const double GE2 = GERemaining();
	RemoteSwitch(SG, true);
	TestEqual(TEXT("(2) late retarget -> SG current at once"), Inventory->GetCurrentIndex(), SG);
	TestEqual(TEXT("(2) ... nothing pending"), Inventory->GetPendingIndex(), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("(2) ... GE (raise) unchanged"), GERemaining(), GE2, 1e-3);

	// (3) Fresh: a retarget long after any drop end (> Tol) runs as a fresh switch on the server's timeline.
	W.Advance(static_cast<float>(Tol) + 0.5f);
	TestFalse(TEXT("(3) no switch running"), ASC->HasMatchingGameplayTag(State_WeaponSwitching));
	RemoteSwitch(RL, true);
	TestEqual(TEXT("(3) fresh -> pending RL"), Inventory->GetPendingIndex(), RL);
	TestEqual(TEXT("(3) fresh -> full GE"), GERemaining(), Drop + Raise, 1e-3);
	// A fresh retarget onto the weapon the server holds is a no-op, not a rejection.
	W.Advance(1.0f);
	RemoteSwitch(RL, true);
	TestTrue(TEXT("(3) fresh retarget onto the current weapon -> no-op"), Inventory->GetCurrentIndex() == RL && Inventory->GetPendingIndex() == INDEX_NONE
		&& !ASC->HasMatchingGameplayTag(State_WeaponSwitching));

	// (4) Raise-phase press on the server: wait = the server GE's remaining raise.
	W.Advance(1.0f);
	const double T4 = W.World->GetTimeSeconds();
	RemoteSwitch(MG, false);
	W.AdvanceTo(T4 + Drop + 0.1);
	TestEqual(TEXT("(4) MG current in its raise"), Inventory->GetCurrentIndex(), MG);
	const double Remaining4 = GERemaining();
	AddInfo(FString::Printf(TEXT("(4) server remaining raise %.4f"), Remaining4));
	RemoteSwitch(RG, false);
	TestEqual(TEXT("(4) raise press: pending RG"), Inventory->GetPendingIndex(), RG);
	TestEqual(TEXT("(4) raise press: drop remaining = remaining raise + drop"), Inventory->GetPendingDropRemaining(), Remaining4 + Drop, 2e-3);
	TestEqual(TEXT("(4) raise press: GE = remaining raise + drop + raise"), GERemaining(), Remaining4 + Drop + Raise, 2e-3);
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
	const UVeloxCombatFeedbackComponent* Feedback = Shooter->GetCombatFeedbackComponent();
	TestNotNull(TEXT("shooter feedback component"), Feedback);

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
		const int32 FireCuesBefore = Feedback ? Feedback->GetNumFireCues() : 0;
		const int32 ImpactCuesBefore = Feedback ? Feedback->GetNumImpactCues() : 0;
		const uint8 PatternBefore = Inventory->GetServerPatternSeq();
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
		// B21: one fire and exactly one impact cue per shot reach the shooter's own feedback component (not one per pellet,
		// not a predicted plus a server copy).
		const int32 FireCuesNow = Feedback ? Feedback->GetNumFireCues() : 0;
		const int32 ImpactCuesNow = Feedback ? Feedback->GetNumImpactCues() : 0;
		TestEqual(FString::Printf(TEXT("%s: one fire cue on the shooter"), Case.Weapon), FireCuesNow - FireCuesBefore, Def->FireCueTag.IsValid() ? 1 : 0);
		TestEqual(FString::Printf(TEXT("%s: exactly one impact cue on the shooter"), Case.Weapon), ImpactCuesNow - ImpactCuesBefore, Def->ImpactCueTag.IsValid() ? 1 : 0);
		if (Case.Slot == 7)
		{
			TestEqual(TEXT("rail: impact cue count == 1"), ImpactCuesNow - ImpactCuesBefore, 1);
		}
		// B21: the server's pattern counter advances by one per accepted shot and is the seed the shot used.
		const UVeloxGameplayAbility_FireHitscan* Hitscan = Cast<UVeloxGameplayAbility_FireHitscan>(Fire);
		const uint8 PatternExpected = static_cast<uint8>(PatternBefore + 1);
		TestEqual(FString::Printf(TEXT("%s: server pattern counter +1"), Case.Weapon), Inventory->GetServerPatternSeq(), PatternExpected);
		TestTrue(FString::Printf(TEXT("%s: the shot used the server counter"), Case.Weapon), Hitscan && Hitscan->GetLastServerPatternSeq() == PatternExpected);
		TestEqual(FString::Printf(TEXT("%s: one server ray per pellet"), Case.Weapon), Hitscan ? Hitscan->GetLastServerDirs().Num() : 0, FMath::Max(Def->PelletCount, 1));
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
	const uint8 PatternBeforeRefused = Inventory->GetServerPatternSeq();
	TestFalse(TEXT("rail refire blocked within 1.5 s"), VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("RG"), Dummy, 0.f, Error));
	TestEqual(TEXT("refused shot: pattern counter unchanged"), Inventory->GetServerPatternSeq(), PatternBeforeRefused);

	// LG range 1920 cm: a dummy at 25 m is out of reach.
	W.Advance(1.6f);
	DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), 100.f);
	Dummy->SetActorLocation(FVector(2500, 0, Dummy->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
	Dummy->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	TestTrue(TEXT("LG fired at 25 m"), VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("LG"), Dummy, 0.f, Error));
	TestEqual(TEXT("LG out of range: no damage"), DummyHealth->GetHealth(), 100.f);

	// B21: the SG pattern is the server's. Two shots with the same server counter state but different client ShotSeq give
	// identical rays (= VeloxShotDirs::Build with MakeSeed(counter + 1, PlayerId)); the next counter value gives another.
	{
		const int32 SGIndex = Table->FindBySlot(3);
		const UVeloxWeaponDefinition* SGDef = Table->Get(SGIndex);
		const AController* ShooterController = Shooter->GetController();
		const int32 ShooterPlayerId = PS(Shooter)->GetPlayerId();
		constexpr uint8 CounterState = 41;
		struct FSGShot
		{
			bool bFired = false;
			uint8 ShotSeq = 0;
			uint8 PatternSeq = 0;
			FVector CentreDir = FVector::ZeroVector;
			TArray<FVector, TInlineAllocator<1>> Dirs;
		};
		auto FireSG = [&](uint8 Counter)
		{
			FSGShot Out;
			W.Advance(SGDef->GetSafeFireInterval() + 0.1f);
			DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetHealthAttribute(), 100.f);
			DummyASC->SetNumericAttributeBase(UVeloxHealthSet::GetArmorAttribute(), 100.f);
			Dummy->SetActorLocation(FVector(1000, 0, Dummy->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			Dummy->GetCharacterMovement()->Velocity = FVector::ZeroVector;
			Inventory->SetServerPatternSeqForTest(Counter);
			FString SGError;
			Out.bFired = VeloxWeaponCheats::FireTestShot(*Shooter, TEXT("SG"), Dummy, 0.f, SGError);
			const FGameplayAbilitySpec* SGSpec = Inventory->FindFireSpec(SGIndex);
			const UVeloxGameplayAbility_FireHitscan* SG = SGSpec ? Cast<UVeloxGameplayAbility_FireHitscan>(SGSpec->GetPrimaryInstance()) : nullptr;
			if (Out.bFired && SG && ShooterController)
			{
				Out.ShotSeq = SG->GetLastServerShot().ShotSeq;
				Out.PatternSeq = SG->GetLastServerPatternSeq();
				Out.CentreDir = ShooterController->GetControlRotation().Vector();
				Out.Dirs = SG->GetLastServerDirs();
			}
			return Out;
		};
		auto SameDirs = [](const TArray<FVector, TInlineAllocator<1>>& A, const TArray<FVector, TInlineAllocator<1>>& B)
		{
			if (A.Num() != B.Num() || A.Num() == 0)
			{
				return false;
			}
			for (int32 RayIndex = 0; RayIndex < A.Num(); ++RayIndex)
			{
				if (!A[RayIndex].Equals(B[RayIndex], 1e-9))
				{
					return false;
				}
			}
			return true;
		};

		const FSGShot ShotA = FireSG(CounterState);
		const FSGShot ShotB = FireSG(CounterState);
		const FSGShot ShotC = FireSG(CounterState + 1);
		if (TestTrue(TEXT("SG fired three times"), ShotA.bFired && ShotB.bFired && ShotC.bFired))
		{
			TestNotEqual(TEXT("SG: different client ShotSeq"), ShotA.ShotSeq, ShotB.ShotSeq);
			TestEqual(TEXT("SG: pattern seq = counter + 1"), static_cast<int32>(ShotA.PatternSeq), CounterState + 1);
			TestTrue(TEXT("SG: same centre ray"), ShotA.CentreDir.Equals(ShotB.CentreDir, 1e-9));
			TestEqual(TEXT("SG: one server ray per pellet"), ShotA.Dirs.Num(), SGDef->PelletCount);
			TestTrue(TEXT("SG: same server counter, different ShotSeq -> identical Request.Dirs"), SameDirs(ShotA.Dirs, ShotB.Dirs));
			TArray<FVector, TInlineAllocator<1>> Expected;
			VeloxShotDirs::Build(*SGDef, ShotA.CentreDir, VeloxShotDirs::MakeSeed(CounterState + 1, ShooterPlayerId), Expected);
			TestTrue(TEXT("SG: rays = Build(MakeSeed(server counter, PlayerId))"), SameDirs(ShotA.Dirs, Expected));
			TestFalse(TEXT("SG: next server counter -> another pattern"), SameDirs(ShotA.Dirs, ShotC.Dirs));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
