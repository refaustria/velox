// Velox - FVeloxMoveSim unit tests (ADR 0001 section 4).
//
// TODO(M1): FVeloxMoveSim (Movement/VeloxMoveSim.h) is the first M1 deliverable; ADR 0001 names its functions
// (ApplyFriction, Accelerate, AirAccelerate, ClipVelocity, each taking const UVeloxMovementSettings& and float Dt)
// but not their exact signatures. Add here, under WITH_DEV_AUTOMATION_TESTS:
//   Velox.Movement.Friction       friction decay over N ticks
//   Velox.Movement.AirAccel       air-accel speed gain for a scripted strafe sequence
//   Velox.Movement.BunnyHop       speed retention across a landing
//   Velox.Movement.JumpPad        jump-pad launch vector
//   Velox.Movement.Determinism    identical inputs and Dt -> bitwise-equal end state

#include "Misc/AutomationTest.h"
