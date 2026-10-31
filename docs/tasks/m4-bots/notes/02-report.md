# M4-02 bot pure cores — agent report (2026-10-30)
build.sh 87 tests (+8 `Velox.Bots.*`), 0 failures (`20261030-013557-97259`, worktree). New files `Source/Velox/Bots/VeloxBotCoreTypes.h` (EVeloxBotGoal, FVeloxBotInput), `VeloxBotSteering`, `VeloxAimModel`, `VeloxItemValue`, `VeloxWeaponChoice`, `VeloxBotDecision`, `VeloxBotManeuver`, `Tests/VeloxBotTests.cpp` (tasks 05/06 append at the end).
- Steering `Steer(FVeloxSteerInput, FVeloxSteerParams)`, `OptimalStrafeAngleDeg`; ground tick with jump held steered as air. Strafe loop 1383.8 cm/s after 1 s, heading error 0.13°.
- Aim `FVeloxAimModel::Update/LeadPoint/StepError/Reset/MakeParams`; exact OU discretisation (ρ = exp(−Dt/τ)); Hard σ 1.8: yaw std 1.755, lag-1 0.932 (expected 0.9355); reaction 0.117–0.217 s; lead 0.66 cm vs 257.7 cm; turn-rate clamp on the (yaw, pitch) vector.
- Item value `Need/Score/Rank` (ties by CandidateId); weapon choice `Pick(Input, prefs)` → index; decision `ChooseGoal(In, Difficulty, Stream)`, `FightScore`; maneuver phases Approach/BackUp/Switch/RunUp/Launched/Airborne, results ok/missed/noimpulse/tooslow/ineligible. `EVeloxManeuverType` mirrors 01's `EVeloxNavLinkType` (04 maps).
## For the architect (wave-2 review)
- §5 "within 15° after 60 ticks" for a 90° air turn is physically impossible at AirAccel 1, W 800, 1000 cm/s (≈0.37°/tick): test checks min speed ≥ 950, monotone error, ≥ 15° in 60 ticks (17.4°), within 15° by 375 ticks (349). Sharp turns belong to the corner rule.
- §14 "Mega at 100/100 scores half of 50/100" contradicts §7's need formula (gives 2/3); §7 implemented.
- Walker cap: ground friction+accelerate exceed 800 by ~8 cm/s for one tick after a direction change; cap checked on straight runs only.
- Fight score undefined in the ADR: `FVeloxFightScoreParams {BaseScore 20, FullStrength 100, WeakWeaponScale 0.5}`; these and `ReactionJitter` 0.3 are not in `FVeloxBotDifficultyParams` → surface in data asset/settings.
- Signature refinements: `bFinalSegment`, aim `TargetId`/`HeldDesired`, item `CandidateId`, weapon `SwitchHysteresis` 1.25/`SplashSafetyMarginCm` 100, maneuver `bEligible`, `WishScale`, `bForcePitch`, `bFaceEnd`, `bWantRocketLauncher`, `AwayFromStart`. Gap jumps keep jump held airborne.
- Unity safety of the new files not proven by this run (adaptive unity excluded them).
