# Task 04c agent report (2026-10-29)

build.sh 47 tests, 0 failures (`Saved/CI/20261029-115058-3158558`; existing tests extended, no new names). Headless FireTest RG/MG/SG/LG hit, one `.Fire` + one `.Impact` cue each, `VeloxShotPattern seq=1 pattern=1 rays=11` for SG. nettest full list 1/1 (`Saved/CI/20261029-120049-3176582`); an earlier run failed LatchRun (C=2 max 26.39, "expected >= 2 landing observations, got 1", `Saved/CI/20261029-115612-3168251`) under load (loadavg ~3.5); LatchRun alone 3/3 afterwards.

- Server alone picks the seed: `ResolveServerShot` → `Inventory->AllocateServerPatternSeq()` after the lag-comp check (rejected shots never consume one); seed `VeloxShotDirs::MakeSeed(PatternSeq, PlayerId)`. Counter `ServerPatternSeqCounter` (uint8, not replicated) on the inventory. Client `ShotSeq` names the shot and matches fake/real projectile only.
- `FVeloxGameplayEffectContext` gains `uint8 PatternSeq` (one byte in NetSerialize, Duplicate, ToString `pattern=`); server writes `PatternSeq` + `HitDirection` (unit centre dir) into both hitscan cues and the LG beam cue. M5 FX rebuild: `VeloxShotDirs::Build(Def, HitDirection, MakeSeed(PatternSeq, PlayerId))` (not bit-exact, NetQuantizeNormal).
- Cue policy: fire cue keyed (predicted); impact cue in `FScopedPredictionWindow(ASC, FPredictionKey(), false)`, one per shot (SG: nearest hit or centre-ray world end); owner gets it ~1 RTT late. Owner cosmetics: centre ray only, one tracer, `PatternSeq = 0`; MG tracer follows the centre ray.
- Not unit-testable: counter not advancing on a remote-shooter server rejection (structural only); 08's FireRun can assert `pattern=` on accepted shots.
- Open: stale comment `Combat/VeloxHitMath.h` ~l.156 ("ShotSeq ^ PlayerId"); review hand-off: projectile `MakeCueParameters` should write `PatternSeq = 0` (05b or later).
- For 08: exactly one `.Impact` per accepted hitscan shot on the owner, ~1 RTT late; `log LogVeloxCombat Verbose` shows `VeloxShotPattern` and `pattern=` on `VeloxCue`.
- Watch: LatchRun under load (see above), soak only on an idle machine.
