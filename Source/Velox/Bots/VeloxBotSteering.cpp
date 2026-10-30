// Velox - world-free bot steering (ADR 0005 section 5).

#include "Bots/VeloxBotSteering.h"

#include "Bots/VeloxBotTypes.h"

namespace VeloxBotSteeringPrivate
{
	FVector2D RotateDeg(const FVector2D& V, float Deg)
	{
		const double R = FMath::DegreesToRadians(static_cast<double>(Deg));
		const double C = FMath::Cos(R), S = FMath::Sin(R);
		return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
	}
}

namespace VeloxBotSteering
{
	float OptimalStrafeAngleDeg(float Speed, float WishSpeed, float AirAccel, float Dt)
	{
		const float A = AirAccel * Dt * WishSpeed;
		const float Threshold = WishSpeed - A;
		if (Speed <= Threshold || Speed <= UE_SMALL_NUMBER)
		{
			return 0.f;
		}
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Threshold / Speed, -1.f, 1.f)));
	}

	float SignedAngleDeg(const FVector2D& A, const FVector2D& B)
	{
		if (A.IsNearlyZero() || B.IsNearlyZero())
		{
			return 0.f;
		}
		const double Cross = A.X * B.Y - A.Y * B.X;
		const double Dot = A.X * B.X + A.Y * B.Y;
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot)));
	}

	FVeloxSteerOutput Steer(const FVeloxSteerInput& In, const FVeloxSteerParams& Params)
	{
		FVeloxSteerOutput Out;
		const FVector2D Dir = In.ToTarget.GetSafeNormal();
		if (Dir.IsZero())
		{
			return Out;
		}

		Out.WishScale = 1.f;
		if (In.bFinalSegment && Params.ArriveSlowdownCm > 0.f)
		{
			Out.WishScale = FMath::Clamp(In.SegmentRemainingCm / Params.ArriveSlowdownCm, 0.f, 1.f);
		}

		const bool bCornerOk = In.bFinalSegment || In.NextCornerDeg < Params.MaxHopCornerDeg;
		Out.bJumpHeld = Params.bBunnyHop && In.SegmentRemainingCm > Params.MinHopDistanceCm && bCornerOk;

		Out.WishDir = Dir;
		const bool bAirMoveThisTick = !In.bOnGround || Out.bJumpHeld;
		if (!bAirMoveThisTick || !Params.bStrafeJump)
		{
			return Out;
		}

		const float Speed = static_cast<float>(In.Velocity.Size());
		const float Theta = OptimalStrafeAngleDeg(Speed, In.WishSpeed, In.AirAccel, In.FixedStep);
		if (Theta <= 0.f)
		{
			return Out;
		}

		const FVector2D VelDir = In.Velocity / Speed;
		const float Error = SignedAngleDeg(VelDir, Dir);
		const float AbsError = FMath::Abs(Error);
		const float Angle = AbsError > Theta ? FMath::Max(Theta, FMath::Min(Params.StrafeMaxAngleDeg, AbsError)) : Theta;
		const float Sign = Error >= 0.f ? 1.f : -1.f;
		Out.WishDir = VeloxBotSteeringPrivate::RotateDeg(VelDir, Sign * Angle).GetSafeNormal();
		return Out;
	}

	FVeloxSteerParams MakeParams(const FVeloxBotDifficultyParams& Difficulty)
	{
		FVeloxSteerParams P;
		P.bBunnyHop = Difficulty.bBunnyHop;
		P.bStrafeJump = Difficulty.bStrafeJump;
		return P;
	}
}
