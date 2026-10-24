// Velox - movement tunables (ADR 0002 section 1).

#include "Movement/VeloxMovementSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxMovementSettings)

FVeloxMoveParams UVeloxMovementSettings::BuildSimParams() const
{
	FVeloxMoveParams P;
	P.UnitScale = UnitScale;

	P.MaxGroundSpeed = MaxGroundSpeed;
	P.GroundAcceleration = GroundAcceleration;
	P.GroundFriction = GroundFriction;
	P.StopSpeed = StopSpeed;
	P.CrouchSpeedScale = CrouchSpeedScale;
	P.StepHeight = StepHeight;
	P.WalkableFloorAngle = WalkableFloorAngle;
	P.EdgePerchRadius = EdgePerchRadius;

	P.AirAcceleration = AirAcceleration;
	P.AirMaxWishSpeed = AirMaxWishSpeed;
	P.bAirControl = bAirControl;
	P.AirStopAcceleration = AirStopAcceleration;
	P.AirStrafeAcceleration = AirStrafeAcceleration;
	P.AirStrafeMaxWishSpeed = AirStrafeMaxWishSpeed;
	P.AirControl = AirControl;
	P.AirControlScale = AirControlScale;

	P.JumpVelocity = JumpVelocity;
	P.bAutoHop = bAutoHop;
	P.GravityZ = GravityZ;

	P.RampSlideMinNormalSpeed = RampSlideMinNormalSpeed;
	P.RampExitMinAngle = RampExitMinAngle;
	P.bRampExitCatchAir = bRampExitCatchAir;

	P.KnockbackGraceMin = KnockbackGraceMin;
	P.KnockbackGraceMax = KnockbackGraceMax;
	P.KnockbackGracePerSpeed = KnockbackGracePerSpeed;

	P.FixedStep = FixedStep;
	P.bFixedStepIntegration = bFixedStepIntegration;
	P.bCombineAirMoves = bCombineAirMoves;

	// TODO(M1 task 05): apply non-shipping velox.move.<FieldName> cvar overrides here (ADR 0002 section 6).
	return P;
}
