# 01b — Packaged server dies at first GC: `VeloxAudioSettings` breaks Disregard-for-GC

**Milestone:** M6 Online
**Agent:** dev-hard
**Status:** todo
**Depends on:** 01 (`infra/ci/package.sh`), report `notes/01-report.md`

## Goal
The packaged Development server (`LinuxServer` archive from `package.sh --dev`) crashes about 61 s after start at its first periodic GC: fatal at `GarbageCollectionVerification.cpp:156`, "41 object(s) breaking Disregard for GC assumptions". The `UVeloxAudioSettings` CDO holds `LoadedAssets` that point at SoundWave/SoundAttenuation/SoundConcurrency/SoundMix assets outside the root set. Reproduced twice. Shipping compiles the check out but carries the same dangling-reference risk. After this task the packaged server and client run ≥ 10 min with several GCs and no fatal, and the audio assets still resolve on clients.

## Scope — files/modules the agent may touch
- `Source/Velox/Audio/` (settings, subsystem that loads/holds the assets); move the strong refs off the CDO into a game-instance/engine subsystem or root them correctly; no audio behaviour change
- Any other CDO holding loaded assets found by the same check (audit: `UDeveloperSettings`/config classes with hard `TObjectPtr`/`TArray<UObject*>` filled at runtime) — fix the same way, list them
- `Source/Velox/Tests/` — an automation test that fails if a settings CDO holds a runtime-loaded object (or a packaged-run check in `package.sh --smoke`)
- `infra/ci/package.sh` — extend the smoke to run the server ≥ 120 s with `gc.CollectGarbageEveryFrame 0` default and force 2 GCs (`obj gc` via `-ExecCmds`), then check no `Fatal error`

## Acceptance criteria
- [ ] Root cause stated (why the CDO refs end up in the disregard set in a cooked build and not in the editor).
- [ ] Packaged Development server: 10 min run, ≥ 3 GCs in the log, no fatal; a Development client joins and plays 2 min with sounds resolved (log line from the cue path).
- [ ] `infra/ci/build.sh` green, test count reported; `nettest.sh` default list PASS (editor path unchanged).
- [ ] UE steps under the soak lock, own processes killed.
