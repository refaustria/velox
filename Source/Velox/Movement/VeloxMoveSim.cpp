// Velox - world-free movement simulation core (ADR 0002 sections 1 and 3).

#include "Movement/VeloxMoveSim.h"

namespace
{
	// Numerical guards only (not tuning): below these the quantity is treated as zero.
	constexpr double SpeedEpsilon = 1e-3;     // cm/s
	constexpr float GraceEpsilon = 1e-5f;     // s, absorbs float drift of repeated Dt subtraction
	constexpr int32 MaxSubsteps = 1024;       // safety bound for pathological Dt; CMC dt is <= MaxSimulationTimeStep

	FVector SanitizeWishDir(const FVector& WishDir)
	{
		return FVector(WishDir.X, WishDir.Y, 0.0).GetSafeNormal();
	}
}

namespace VeloxMoveSim
{
	int32 NumSubsteps(float Dt, const FVeloxMoveParams& P)
	{
		if (!P.bFixedStepIntegration || P.FixedStep <= 0.f || Dt <= 0.f)
		{
			return 1;
		}
		// Tolerance so that Dt == k * FixedStep (up to float error) yields exactly k substeps.
		const double Ratio = static_cast<double>(Dt) / static_cast<double>(P.FixedStep);
		return FMath::Clamp(FMath::CeilToInt32(Ratio - 1e-4), 1, MaxSubsteps);
	}

	FVector ApplyFriction(const FVector& V, float Dt, const FVeloxMoveParams& P)
	{
		const double Speed = FVector(V.X, V.Y, 0.0).Size();
		if (Speed < SpeedEpsilon)
		{
			return FVector(0.0, 0.0, V.Z);
		}
		const double Control = FMath::Max(Speed, static_cast<double>(P.StopSpeed));
		const double Drop = Control * P.GroundFriction * Dt;
		const double Scale = FMath::Max(Speed - Drop, 0.0) / Speed;
		return FVector(V.X * Scale, V.Y * Scale, V.Z);
	}

	FVector Accelerate(const FVector& V, const FVector& WishDir, float WishSpeed, float Accel, float Dt)
	{
		const double CurrentSpeed = V | WishDir;
		const double AddSpeed = WishSpeed - CurrentSpeed;
		if (AddSpeed <= 0.0)
		{
			return V;
		}
		const double AccelSpeed = FMath::Min(static_cast<double>(Accel) * Dt * WishSpeed, AddSpeed);
		return V + WishDir * AccelSpeed;
	}

	FVector AirControl(const FVector& V, const FVector& WishDir, float Dt, const FVeloxMoveParams& P)
	{
		FVector Horizontal(V.X, V.Y, 0.0);
		const double Speed = Horizontal.Size();
		if (Speed < SpeedEpsilon)
		{
			return V;
		}
		Horizontal /= Speed;
		const double Dot = Horizontal | WishDir;
		if (Dot <= 0.0)
		{
			return V;
		}
		const double K = static_cast<double>(P.AirControlScale) * P.AirControl * Dot * Dot * Dt;
		const FVector Turned = (Horizontal * Speed + WishDir * K).GetSafeNormal() * Speed;
		return FVector(Turned.X, Turned.Y, V.Z);
	}

	FVector GroundMove(const FVector& V, const FVeloxMoveInput& In, float Dt, float KnockbackGrace, const FVeloxMoveParams& P)
	{
		return GroundMove(V, In, Dt, KnockbackGrace, P, nullptr);
	}

	FVector GroundMove(const FVector& V, const FVeloxMoveInput& In, float Dt, float KnockbackGrace, const FVeloxMoveParams& P, FVector* OutMeanVelocity)
	{
		const int32 N = NumSubsteps(Dt, P);
		const float H = Dt / static_cast<float>(N);
		const FVector WishDir = SanitizeWishDir(In.WishDir);
		const float WishSpeed = FMath::Clamp(In.WishSpeed, 0.f, P.MaxGroundSpeed * EffectiveSpeedScale(In));

		FVector Result = V;
		FVector Displacement = FVector::ZeroVector;   // A31: trapezoid sum over the substeps
		float Grace = KnockbackGrace;
		auto SubstepBody = [&Result, &Displacement, &WishDir, WishSpeed, &P](float StepDt, bool bInGrace)
		{
			const FVector Start = Result;
			// Q3 PM_WalkMove under PMF_TIME_KNOCKBACK: no friction and pm_airaccelerate instead of pm_accelerate.
			if (!bInGrace)
			{
				Result = ApplyFriction(Result, StepDt, P);
			}
			if (WishSpeed > 0.f)
			{
				Result = Accelerate(Result, WishDir, WishSpeed, bInGrace ? P.AirAcceleration : P.GroundAcceleration, StepDt);
			}
			Displacement += 0.5 * (Start + Result) * static_cast<double>(StepDt);
		};
		for (int32 i = 0; i < N; ++i)
		{
			if (Grace > GraceEpsilon && Grace < H - GraceEpsilon)
			{
				// A27: the grace expires inside this substep. Split it at the expiry so friction starts at the same
				// simulated time for every move partition (a server move that merges lost client moves has other substeps).
				SubstepBody(Grace, true);
				SubstepBody(H - Grace, false);
			}
			else
			{
				SubstepBody(H, Grace > GraceEpsilon);
			}
			Grace -= H;
		}
		if (OutMeanVelocity)
		{
			*OutMeanVelocity = Dt > 0.f ? Displacement / static_cast<double>(Dt) : Result;
		}
		return Result;
	}

	FVector AirMove(const FVector& V, const FVeloxMoveInput& In, float Dt, const FVeloxMoveParams& P)
	{
		const FVector WishDir = SanitizeWishDir(In.WishDir);
		const float WishSpeed = FMath::Clamp(In.WishSpeed, 0.f, P.AirMaxWishSpeed * EffectiveSpeedScale(In));
		if (WishSpeed <= 0.f || WishDir.IsZero())
		{
			return V;
		}

		const bool bSideOnly = In.bHasSideInput && !In.bHasForwardInput;
		const bool bForwardOnly = In.bHasForwardInput && !In.bHasSideInput;
		const int32 N = NumSubsteps(Dt, P);
		const float H = Dt / static_cast<float>(N);

		FVector Result = V;
		for (int32 i = 0; i < N; ++i)
		{
			if (!P.bAirControl)
			{
				Result = Accelerate(Result, WishDir, WishSpeed, P.AirAcceleration, H);
				continue;
			}

			// CPM PM_AirMove.
			float Accel = ((Result | WishDir) < 0.0) ? P.AirStopAcceleration : P.AirAcceleration;
			float StepWishSpeed = WishSpeed;
			if (bSideOnly)
			{
				StepWishSpeed = FMath::Min(StepWishSpeed, P.AirStrafeMaxWishSpeed);
				Accel = P.AirStrafeAcceleration;
			}
			Result = Accelerate(Result, WishDir, StepWishSpeed, Accel, H);
			if (bForwardOnly)
			{
				Result = AirControl(Result, WishDir, H, P);
			}
		}
		return Result;
	}

	FVector ApplyGravity(const FVector& V, float Dt, const FVeloxMoveParams& P)
	{
		return FVector(V.X, V.Y, V.Z + static_cast<double>(P.GravityZ) * Dt);
	}

	FVector Jump(const FVector& V, const FVeloxMoveParams& P)
	{
		return FVector(V.X, V.Y, P.JumpVelocity);
	}

	FVector ClipVelocity(const FVector& V, const FVector& Normal)
	{
		return FVector::VectorPlaneProject(V, Normal);
	}

	FVector ApplyKnockback(const FVector& V, const FVector& Impulse, bool bOverride)
	{
		return bOverride ? Impulse : V + Impulse;
	}

	float KnockbackGraceFor(const FVector& Impulse, const FVeloxMoveParams& P)
	{
		const double Magnitude = Impulse.Size();
		if (Magnitude < SpeedEpsilon)
		{
			return 0.f;
		}
		return FMath::Clamp(static_cast<float>(Magnitude * P.KnockbackGracePerSpeed), P.KnockbackGraceMin, P.KnockbackGraceMax);
	}

	FVector JumpPadLaunchVelocity(const FVector& From, const FVector& Target, float GravityZ)
	{
		const double G = -static_cast<double>(GravityZ);
		if (G <= 0.0)
		{
			return FVector::ZeroVector;
		}
		const FVector Delta = Target - From;
		const FVector Horizontal(Delta.X, Delta.Y, 0.0);
		const double Dist = Horizontal.Size();
		const double Height = Delta.Z;
		const FVector Dir = Dist > SpeedEpsilon ? Horizontal / Dist : FVector::ZeroVector;

		if (Height > SpeedEpsilon)
		{
			// Q3 AimAtTarget: rise to the target height and reach it at the apex.
			const double Time = FMath::Sqrt(2.0 * Height / G);
			return Dir * (Dist / Time) + FVector(0.0, 0.0, G * Time);
		}

		if (Dist <= SpeedEpsilon)
		{
			return FVector::ZeroVector;
		}
		// Minimum-speed ballistic solution: with R = |Delta|, flight time sqrt(2R/G), Vxy = D*sqrt(G/2R), Vz = (H+R)*sqrt(G/2R).
		const double R = FMath::Sqrt(Height * Height + Dist * Dist);
		const double S = FMath::Sqrt(G / (2.0 * R));
		return Dir * (Dist * S) + FVector(0.0, 0.0, (Height + R) * S);
	}

	void StepFrame(FVeloxMoveState& S, const FVeloxMoveInput& In, float Dt, const FVeloxMoveParams& P)
	{
		if (S.bOnGround && In.bJumpHeld)
		{
			S.Velocity = Jump(S.Velocity, P);
			S.bOnGround = false;
		}

		if (S.bOnGround)
		{
			S.Velocity = GroundMove(S.Velocity, In, Dt, S.KnockbackGrace, P);
			S.Velocity.Z = 0.0;
		}
		else
		{
			S.Velocity = AirMove(S.Velocity, In, Dt, P);
			S.Velocity = ApplyGravity(S.Velocity, Dt, P);
		}

		S.KnockbackGrace -= Dt;
		if (S.KnockbackGrace <= GraceEpsilon)
		{
			S.KnockbackGrace = 0.f;
		}
	}
}
