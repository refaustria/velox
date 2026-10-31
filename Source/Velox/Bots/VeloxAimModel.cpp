// Velox - world-free bot aim model (ADR 0005 section 8).

#include "Bots/VeloxAimModel.h"

#include "Bots/VeloxBotTypes.h"

namespace VeloxAimModelPrivate
{
	constexpr float MaxPitchDeg = 89.f;

	FRotator DesiredFor(const FVeloxAimInput& In, const FVeloxAimParams& Params, const FVector2D& ErrorDeg)
	{
		const bool bLead = Params.bLeadProjectiles && In.ProjectileSpeed > 0.f;
		const FVector Point = bLead
			? FVeloxAimModel::LeadPoint(In.Eye, In.TargetCentre, In.TargetVelocity, In.ProjectileSpeed, Params.LeadScale)
			: In.TargetCentre;
		FRotator Desired = (Point - In.Eye).Rotation();
		Desired.Yaw += ErrorDeg.X;
		Desired.Pitch += ErrorDeg.Y;
		Desired.Roll = 0.f;
		return Desired;
	}

	FRotator TurnToward(const FRotator& Aim, const FRotator& Desired, const FVeloxAimParams& Params, float H)
	{
		if (H <= 0.f)
		{
			return Aim;
		}
		const float DYaw = FRotator::NormalizeAxis(Desired.Yaw - Aim.Yaw);
		const float DPitch = FRotator::NormalizeAxis(FMath::Clamp(Desired.Pitch, -MaxPitchDeg, MaxPitchDeg) - Aim.Pitch);
		FVector2D Step(DYaw, DPitch);
		if (Params.TrackingSmoothingSeconds > 0.f)
		{
			Step *= 1.f - FMath::Exp(-H / Params.TrackingSmoothingSeconds);
		}
		if (Params.MaxTurnRateDegPerSec > 0.f)
		{
			const double MaxStep = static_cast<double>(Params.MaxTurnRateDegPerSec) * H;
			const double Len = Step.Size();
			if (Len > MaxStep)
			{
				Step *= MaxStep / Len;
			}
		}
		FRotator Out(FMath::Clamp(Aim.Pitch + static_cast<float>(Step.Y), -MaxPitchDeg, MaxPitchDeg), FRotator::NormalizeAxis(Aim.Yaw + static_cast<float>(Step.X)), 0.f);
		return Out;
	}
}

float FVeloxAimModel::Gaussian(FRandomStream& Stream)
{
	// Box-Muller; U1 in (0, 1] so the log is finite.
	const float U1 = 1.f - Stream.GetFraction();
	const float U2 = Stream.GetFraction();
	return FMath::Sqrt(-2.f * FMath::Loge(FMath::Max(U1, UE_SMALL_NUMBER))) * FMath::Cos(2.f * UE_PI * U2);
}

void FVeloxAimModel::StepError(FVector2D& Error, float Sigma, float Tau, float Dt, FRandomStream& Stream)
{
	if (Sigma <= 0.f)
	{
		Error = FVector2D::ZeroVector;
		return;
	}
	const double Rho = Tau > 0.f ? FMath::Exp(-static_cast<double>(Dt) / Tau) : 0.0;
	const double Noise = Sigma * FMath::Sqrt(FMath::Max(1.0 - Rho * Rho, 0.0));
	const float NX = Gaussian(Stream);
	const float NY = Gaussian(Stream);
	Error.X = Error.X * Rho + Noise * NX;
	Error.Y = Error.Y * Rho + Noise * NY;
}

FVector FVeloxAimModel::LeadPoint(const FVector& Eye, const FVector& Target, const FVector& TargetVel, float ProjectileSpeed, float LeadScale)
{
	if (ProjectileSpeed <= 0.f)
	{
		return Target;
	}
	FVector P = Target;
	for (int32 Iteration = 0; Iteration < 2; ++Iteration)
	{
		const double T = FVector::Dist(Eye, P) / ProjectileSpeed;
		P = Target + TargetVel * (T * LeadScale);
	}
	return P;
}

void FVeloxAimModel::Reset(FVeloxAimState& State, const FRotator& Aim)
{
	State = FVeloxAimState();
	State.Aim = FRotator(FMath::Clamp(Aim.Pitch, -VeloxAimModelPrivate::MaxPitchDeg, VeloxAimModelPrivate::MaxPitchDeg), FRotator::NormalizeAxis(Aim.Yaw), 0.f);
	State.HeldDesired = State.Aim;
}

FRotator FVeloxAimModel::Update(FVeloxAimState& State, const FVeloxAimInput& In, const FVeloxAimParams& Params, float Dt, FRandomStream& Stream)
{
	const bool bNewTarget = In.bHasTarget && (!State.bHadTarget || In.TargetId != State.TargetId);
	if (bNewTarget)
	{
		const float Jitter = Params.ReactionJitter * Stream.FRandRange(-1.f, 1.f);
		State.ReactionLeft = FMath::Max(Params.ReactionSeconds * (1.f + Jitter), 0.f);
	}
	if (!In.bHasTarget)
	{
		State.ReactionLeft = 0.f;
	}
	State.bHadTarget = In.bHasTarget;
	State.TargetId = In.bHasTarget ? In.TargetId : INDEX_NONE;

	StepError(State.ErrorDeg, Params.AimErrorDeg, Params.ErrorCorrelationSeconds, Dt, Stream);

	if (!In.bHasTarget)
	{
		State.HeldDesired = In.IdleAim;
		State.Aim = VeloxAimModelPrivate::TurnToward(State.Aim, In.IdleAim, Params, Dt);
		return State.Aim;
	}

	if (State.ReactionLeft >= Dt)
	{
		State.ReactionLeft -= Dt;
		State.Aim = VeloxAimModelPrivate::TurnToward(State.Aim, State.HeldDesired, Params, Dt);
		return State.Aim;
	}

	// Reaction over (possibly inside this tick): the held part, then the informed part.
	const float Held = FMath::Max(State.ReactionLeft, 0.f);
	State.ReactionLeft = 0.f;
	State.Aim = VeloxAimModelPrivate::TurnToward(State.Aim, State.HeldDesired, Params, Held);
	const FRotator Desired = VeloxAimModelPrivate::DesiredFor(In, Params, State.ErrorDeg);
	State.HeldDesired = Desired;
	State.Aim = VeloxAimModelPrivate::TurnToward(State.Aim, Desired, Params, Dt - Held);
	return State.Aim;
}

FVeloxAimParams FVeloxAimModel::MakeParams(const FVeloxBotDifficultyParams& Difficulty)
{
	FVeloxAimParams P;
	P.ReactionSeconds = Difficulty.ReactionSeconds;
	P.ReactionJitter = Difficulty.ReactionJitter;
	P.AimErrorDeg = Difficulty.AimErrorDeg;
	P.ErrorCorrelationSeconds = Difficulty.ErrorCorrelationSeconds;
	P.TrackingSmoothingSeconds = Difficulty.TrackingSmoothingSeconds;
	P.MaxTurnRateDegPerSec = Difficulty.MaxTurnRateDegPerSec;
	P.bLeadProjectiles = Difficulty.bLeadProjectiles;
	P.LeadScale = Difficulty.LeadScale;
	return P;
}
