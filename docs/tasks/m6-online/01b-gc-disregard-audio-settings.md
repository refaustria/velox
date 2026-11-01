# 01b — Packaged server dies at first GC: `VeloxAudioSettings` breaks Disregard-for-GC

**Milestone:** M6 Online
**Agent:** dev-hard
**Status:** done except the `nettest.sh` default-list run (not run: project paused 2026-11-01, see notes)
**Depends on:** 01 (`infra/ci/package.sh`), report `notes/01-report.md`

## Goal
The packaged Development server (`LinuxServer` archive from `package.sh --dev`) crashes about 61 s after start at its first periodic GC: fatal at `GarbageCollectionVerification.cpp:156`, "41 object(s) breaking Disregard for GC assumptions". The `UVeloxAudioSettings` CDO holds `LoadedAssets` that point at SoundWave/SoundAttenuation/SoundConcurrency/SoundMix assets outside the root set. Reproduced twice. Shipping compiles the check out but carries the same dangling-reference risk. After this task the packaged server and client run ≥ 10 min with several GCs and no fatal, and the audio assets still resolve on clients.

## Scope — files/modules the agent may touch
- `Source/Velox/Audio/` (settings, subsystem that loads/holds the assets); move the strong refs off the CDO into a game-instance/engine subsystem or root them correctly; no audio behaviour change
- Any other CDO holding loaded assets found by the same check (audit: `UDeveloperSettings`/config classes with hard `TObjectPtr`/`TArray<UObject*>` filled at runtime) — fix the same way, list them
- `Source/Velox/Tests/` — an automation test that fails if a settings CDO holds a runtime-loaded object (or a packaged-run check in `package.sh --smoke`)
- `infra/ci/package.sh` — extend the smoke to run the server ≥ 120 s with `gc.CollectGarbageEveryFrame 0` default and force 2 GCs (`obj gc` via `-ExecCmds`), then check no `Fatal error`

## Acceptance criteria
- [x] Root cause stated (why the CDO refs end up in the disregard set in a cooked build and not in the editor).
- [x] Packaged Development server: 10 min run, ≥ 3 GCs in the log, no fatal; a Development client joins and plays 2 min with sounds resolved (log line from the cue path).
- [ ] `infra/ci/build.sh` green, test count reported; `nettest.sh` default list PASS (editor path unchanged). — build green, 124 tests; nettest NOT run (wind-down).
- [x] UE steps under the soak lock, own processes killed.

## Notes / outcome (dev-hard, 2026-11-01)
**Root cause.** `FPlatformProperties::RequiresCookedData()` builds (packaged game/server) size a Disregard-for-GC pool from `gc.MaxObjectsNotConsideredByGC`; every UObject created before `FEngineLoop::PreInit` calls `GUObjectArray.CloseDisregardForGC()` (native classes and their CDOs, `UDeveloperSettings` CDOs included; log: "CloseDisregardForGC: 32907/32907 objects") is permanently alive and never traced by GC. `UVeloxAudioSettings::EnsureLoaded` filled the CDO's `Transient LoadedAssets` at runtime, so 41 assets (= "VeloxAudio preloaded 41 assets", the exact error count) were referenced only from a disregarded object: Development's `VerifyGCAssumptions` (on by default, every GC, non-editor) fatals at the first GC; Shipping would free the assets under the CDO. The editor sets no disregard pool (`RequiresCookedData()` false), so the CDO is traced like any object and PIE never shows it. It hit the "server" because the package's server is a *listen* host (`?listen`, NM_ListenServer), where `CanPlayAudio` is true; a true dedicated server never preloads.

**Fix.** Strong refs moved to `UVeloxAudioPreloadSubsystem` (`UEngineSubsystem`, created in `UEngine::Init` after the window closes, held by GEngine's subsystem collection): same once-per-process semantics. `UVeloxAudioSettings` keeps only the soft refs plus `GetPreloadPaths()`; `bLoggedServer` became a static; the CDO is never mutated (`GetMutableDefault` gone). Audit: no other Velox settings CDO (`UVeloxBotSettings`, `UVeloxBotCombatSettings`, `UVeloxUISettings`) or other CDO is written at runtime; `GetMutableDefault`/`const_cast` sites outside tests: only the audio one.

**Test.** `Velox.Core.SettingsCdoNoRuntimeRefs`: (1) reflection — no non-Config strong object property on any Velox `UDeveloperSettings` class (the old `LoadedAssets` fails it); (2) after a preload no settings CDO references a non-`/Script/` object, and the subsystem holds the assets.

**package.sh smoke.** Window `VELOX_SMOKE_SECONDS` (default 120), port `VELOX_SMOKE_PORT` (default 7797); server and Development client run `obj gc` at start and `gc.TimeBetweenPurgingPendingKillObjects 20` (`gc.CollectGarbageEveryFrame` untouched), `-LogCmds="LogGarbage Log"` (LogGarbage defaults to Warning, so the per-GC "ms for Verify GC Assumptions" line is otherwise silent); requires ≥ 3 verified GCs in both logs, no fatal in the dev client, and "VeloxAudio preloaded N>0". Run: server 8 / devclient 7 verified GCs, PACKAGE OK.

**10-min run** (packaged Development server, `L_ItemTest?listen?game=FFA?SpectatorOnly=1?Bots=3`, default 61 s GC, Development client joined the whole 601 s): both alive, 10 verified GCs each (9 after the preload), 0 fatal, 0 `Error:`/ensure in server log; client 11 866 `VeloxAudio play` cue-path lines (e.g. `GameplayCue.Weapon.MachineGun.Fire sound=SW_wpn_machinegun_fire` 2712, footsteps, pain, plasma/rocket/LG cues).

**Open.** `nettest.sh` default list not run (pause). `infra/ci/README.md` smoke paragraph still says 30 s (out of scope). Merge overlap with task 02 (`2b8f32b`, `CanPlayAudio`): this change does not touch `CanPlayAudio`; the new include sits after `Misc/CoreMisc.h` to stay off 02's include line.
