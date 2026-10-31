// Velox - audio tests (ADR 0006 section 9, task M5-10): footstep cadence, landing and pain bands, announcer rules, settings defaults.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/VeloxFootstepMath.h"
#include "Core/VeloxAudioSettings.h"
#include "UI/VeloxAnnouncerComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxFootstepCadenceTest, "Velox.Audio.FootstepCadence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxFootstepCadenceTest::RunTest(const FString& Parameters)
{
	using namespace VeloxFootstepMath;
	const float Spacing = 170.f;
	const float MinSpeed = 100.f;
	const float Speed = 320.f;

	// One step per 170 cm of ground travel, however the travel is chopped into frames; the feet alternate L, R, L, R.
	{
		FState State;
		int32 Steps = 0;
		TArray<bool> Feet;
		for (int32 Frame = 0; Frame < 1000; ++Frame)
		{
			bool bFirstRight = false;
			const int32 N = Advance(State, 5.f, IsAudible(true, false, Speed, MinSpeed), Spacing, bFirstRight);
			for (int32 i = 0; i < N; ++i)
			{
				Feet.Add(bFirstRight != ((i & 1) != 0));
			}
			Steps += N;
		}
		// 1000 frames x 5 cm = 5000 cm -> floor(5000 / 170) = 29 steps.
		TestEqual(TEXT("5000 cm gives 29 steps"), Steps, 29);
		bool bAlternates = Feet.Num() == 29 && !Feet[0];
		for (int32 i = 1; i < Feet.Num(); ++i)
		{
			bAlternates &= Feet[i] != Feet[i - 1];
		}
		TestTrue(TEXT("left first, then alternating"), bAlternates);
	}

	// Frame spacing does not change the count (distance-driven, not time-driven).
	{
		FState Fine;
		FState Coarse;
		int32 FineSteps = 0;
		int32 CoarseSteps = 0;
		bool bFirst = false;
		for (int32 i = 0; i < 4000; ++i)
		{
			FineSteps += Advance(Fine, 1.f, true, Spacing, bFirst);
		}
		for (int32 i = 0; i < 40; ++i)
		{
			CoarseSteps += Advance(Coarse, 100.f, true, Spacing, bFirst);
		}
		TestEqual(TEXT("fine and coarse frames give the same steps"), FineSteps, CoarseSteps);
		TestEqual(TEXT("4000 cm = 23 steps"), FineSteps, 23);
	}

	// A big frame can complete two steps; the second is the other foot.
	{
		FState State;
		bool bFirstRight = true;
		TestEqual(TEXT("two steps in one frame"), Advance(State, 350.f, true, Spacing, bFirstRight), 2);
		TestFalse(TEXT("the first of them is the left foot"), bFirstRight);
		TestTrue(TEXT("the next step is the left foot again"), !State.bNextRight);
		TestNearlyEqual(TEXT("remainder kept"), State.AccumulatedCm, 10.f, 1e-3f);
	}

	// Silent: airborne, crouched, slower than 100 cm/s.
	TestFalse(TEXT("airborne is silent"), IsAudible(false, false, Speed, MinSpeed));
	TestFalse(TEXT("crouched is silent"), IsAudible(true, true, Speed, MinSpeed));
	TestFalse(TEXT("99 cm/s is silent"), IsAudible(true, false, 99.f, MinSpeed));
	TestTrue(TEXT("100 cm/s is audible"), IsAudible(true, false, 100.f, MinSpeed));
	{
		FState State;
		bool bFirstRight = false;
		int32 Steps = 0;
		for (int32 i = 0; i < 100; ++i)
		{
			Steps += Advance(State, 20.f, IsAudible(true, true, Speed, MinSpeed), Spacing, bFirstRight);
			Steps += Advance(State, 20.f, IsAudible(false, false, Speed, MinSpeed), Spacing, bFirstRight);
			Steps += Advance(State, 20.f, IsAudible(true, false, 50.f, MinSpeed), Spacing, bFirstRight);
		}
		TestEqual(TEXT("no steps while crouched, airborne or slow"), Steps, 0);
		TestEqual(TEXT("and no distance banked"), State.AccumulatedCm, 0.f);
	}

	// Landing (|Vz| 400 soft, 1000 hard) and pain bands (< 25 heavy, < 50 medium).
	TestEqual(TEXT("399 is silent"), LandingKind(-399.f, 400.f, 1000.f), 0);
	TestEqual(TEXT("-400 is soft"), LandingKind(-400.f, 400.f, 1000.f), 1);
	TestEqual(TEXT("-999 is soft"), LandingKind(-999.f, 400.f, 1000.f), 1);
	TestEqual(TEXT("-1000 is hard"), LandingKind(-1000.f, 400.f, 1000.f), 2);
	TestEqual(TEXT("health 80 is light pain"), PainBand(80.f, 25.f, 50.f), 0);
	TestEqual(TEXT("health 49 is medium pain"), PainBand(49.f, 25.f, 50.f), 1);
	TestEqual(TEXT("health 24 is heavy pain"), PainBand(24.f, 25.f, 50.f), 2);

	// The defaults of the settings object are the ADR numbers.
	const UVeloxAudioSettings& Settings = UVeloxAudioSettings::Get();
	TestEqual(TEXT("FootstepDistanceCm default"), Settings.FootstepDistanceCm, 170.f);
	TestEqual(TEXT("FootstepMinSpeed"), Settings.FootstepMinSpeed, 100.f);
	TestEqual(TEXT("LandSoftVz"), Settings.LandSoftVz, 400.f);
	TestEqual(TEXT("LandHardVz"), Settings.LandHardVz, 1000.f);
	TestEqual(TEXT("AnnouncerQueueDepth"), Settings.AnnouncerQueueDepth, 2);
	TestEqual(TEXT("LeadLineCooldown"), Settings.LeadLineCooldown, 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxAnnouncerRulesTest, "Velox.Audio.AnnouncerRules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVeloxAnnouncerRulesTest::RunTest(const FString& Parameters)
{
	using namespace VeloxAnnouncerMath;

	TestEqual(TEXT("3.0 s left is three"), CountdownNumber(3.0), 3);
	TestEqual(TEXT("2.4 s left is three"), CountdownNumber(2.4), 3);
	TestEqual(TEXT("1.5 s left is two"), CountdownNumber(1.5), 2);
	TestEqual(TEXT("0.2 s left is one"), CountdownNumber(0.2), 1);
	TestEqual(TEXT("over three seconds is silent"), CountdownNumber(3.5), 0);
	TestEqual(TEXT("zero is silent"), CountdownNumber(0.0), 0);

	TestTrue(TEXT("ahead"), ComputeLead(5.f, 3.f) == ELead::Leading);
	TestTrue(TEXT("behind"), ComputeLead(2.f, 3.f) == ELead::Behind);
	TestTrue(TEXT("tied"), ComputeLead(3.f, 3.f) == ELead::Tied);
	TestEqual(TEXT("tied -> leading"), LeadLine(ELead::Tied, ELead::Leading), FName(TEXT("lead_taken")));
	TestEqual(TEXT("behind -> leading"), LeadLine(ELead::Behind, ELead::Leading), FName(TEXT("lead_taken")));
	TestEqual(TEXT("leading -> behind"), LeadLine(ELead::Leading, ELead::Behind), FName(TEXT("lead_lost")));
	TestEqual(TEXT("leading -> tied"), LeadLine(ELead::Leading, ELead::Tied), FName(TEXT("lead_tied")));
	TestEqual(TEXT("behind -> tied"), LeadLine(ELead::Behind, ELead::Tied), FName(TEXT("lead_tied")));
	TestTrue(TEXT("tied -> behind is silent"), LeadLine(ELead::Tied, ELead::Behind).IsNone());
	TestTrue(TEXT("no change is silent"), LeadLine(ELead::Leading, ELead::Leading).IsNone());

	// Queue depth 2: a third waiting line pushes the oldest out.
	FLineQueue Queue;
	Queue.Push(TEXT("a"), 2);
	Queue.Push(TEXT("b"), 2);
	Queue.Push(TEXT("c"), 2);
	TestEqual(TEXT("two waiting"), Queue.Waiting.Num(), 2);
	TestEqual(TEXT("oldest dropped"), Queue.Pop(), FName(TEXT("b")));
	TestEqual(TEXT("then the newest"), Queue.Pop(), FName(TEXT("c")));
	TestTrue(TEXT("empty"), Queue.Pop().IsNone());
	Queue.Push(TEXT("x"), 0);
	TestTrue(TEXT("depth 0 queues nothing"), Queue.Waiting.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
