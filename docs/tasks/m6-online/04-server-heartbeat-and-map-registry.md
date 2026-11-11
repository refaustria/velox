# 04 — Server heartbeat subsystem, online settings, map registry, project version

**Milestone:** M6 Online
**Agent:** dev
**Status:** done (see notes/04-report.md)
**Depends on:** ADR 0007 §2; schema shared with tasks 03 and 05 (this task owns `Net/VeloxServerStatus.h`)

## Goal
The game server reports itself to `veloxmaster` every 30 s and on every phase/join/leave change, from data (`UVeloxOnlineSettings`), with the map's display name from a new `UVeloxMapRegistry` ("Praesidium" for `L_DM1`, the binding ADR 0006 E0.5 deferred to M6), and the project gets a real `ProjectVersion` so the engine's network version separates releases. Everything that formats JSON is world-free and unit-tested.

## Scope — files/modules the agent may touch
- `Source/Velox/Velox.Build.cs` — add `HTTP`, `Json`, `JsonUtilities`
- `Source/Velox/Core/VeloxOnlineSettings.h/.cpp` (new `UDeveloperSettings`, `Config=Game`): `ServerListUrl` (`https://games.inceptum.at/status/servers.json`), `HeartbeatUrl` (`http://127.0.0.1:8790/api/v1/heartbeat`), `HeartbeatIntervalSeconds 30`, `HeartbeatDebounceSeconds 2`, `HeartbeatTimeoutSeconds 5`, `bHeartbeatEnabled true`, `MaxPlayerNameLength 24` (used by task 07), `ServerListRefreshSeconds 10` (task 05)
- `Source/Velox/Core/VeloxMapRegistry.h/.cpp` (new `UDeveloperSettings`): `TArray<FVeloxMapInfo { FSoftObjectPath Map; FString ShortName; FText DisplayName; TArray<FGameplayTag> Modes; bool bPublic; }>`, `FindByWorld(const UWorld*)`, `FindByShortName`; `Config/DefaultGame.ini` entries for `L_DM1` ("Praesidium", FFA+Duel, public), `L_DM1_Blockout`, `L_ItemTest`, `L_CombatTest`, `L_MovementTest` (not public)
- `Source/Velox/Net/VeloxServerStatus.h` (new, header-only, world-free): `FVeloxServerSnapshot` (every heartbeat field), `VeloxServerStatus::BuildHeartbeat(const FVeloxServerSnapshot&) → FString` (JSON per ADR 0007 §2), `VeloxServerStatus::ParseServerList(...)` is **task 05's** — do not add it here
- `Source/Velox/Net/VeloxServerHeartbeatSubsystem.h/.cpp` (new `UGameInstanceSubsystem`)
- `Source/Velox/GameModes/VeloxGameState.*` — only a read-only accessor if one is missing (phase, `PhaseEndServerTime`, counts); `VeloxPlayerState.h` — only an `IsABot()`-style accessor if missing
- `Config/DefaultGame.ini` — `[/Script/EngineSettings.GeneralProjectSettings] ProjectVersion=0.6.0`, the two new settings sections
- `Source/Velox/Tests/VeloxOnlineTests.cpp` (new): `Velox.Online.HeartbeatJson`, `Velox.Online.MapRegistry`
- `Tools/ci/heartbeat_stub.py` (new, stdlib only): prints received JSON, answers 204
- `docs/tasks/m6-online/notes/04-report.md`

## Relevant ADRs / references
- ADR 0007 §2 (heartbeat body and when it is sent; `players`/`bots`/`spectators` definitions; no player names), §5.7, fact 8 (`FNetworkVersion` hashes `ProjectVersion`)
- ADR 0004 §5 (phase machine, `AVeloxGameState` fields), §6 (`AVeloxPlayerState`)
- ADR 0006 E0.5 (display name "Praesidium", `DISPLAY_NAME` in `dm1_layout.py` — the registry is the runtime home; keep the script's constant in sync or have the script read nothing — report which)

## Acceptance criteria
- [ ] `UVeloxServerHeartbeatSubsystem::ShouldCreateSubsystem` is true only when the process will host: `-ServerId=` present on the command line (the unit passes it) **or** `UVeloxOnlineSettings::bHeartbeatEnabled` and the world is a listen/dedicated server at first tick (re-checked in `OnWorldBeginPlay`; a client or standalone instance never posts, pinned by a log assertion in the headless run).
- [ ] Posts `BuildHeartbeat(snapshot)` with `FHttpModule` (`POST`, `Content-Type: application/json`, timeout from settings) every `HeartbeatIntervalSeconds`, plus one debounced post ≤ 2 s after `OnPhaseChanged`, `PostLogin`, `Logout` (bind to the GameState/GameMode delegates that exist; do not add replication). Failures log `VeloxHeartbeat failed code= url=` at Warning, rate-limited 1/min; success at Verbose with the body.
- [ ] Snapshot fields: `id` (`-ServerId=`), `name` (`-ServerName=`, fallback `UVeloxOnlineSettings::DefaultServerName` "Velox"), `port` (the net driver's listen port), `map` (short name), `map_display_name` (registry; fallback = short name), `mode` (`GameModeDefinition->ModeTag` leaf: `FFA`/`Duel`), `phase` (enum name), `time_left_s` (ADR 0004 §6 clock formula, integer, 0 when open-ended), `players` (active humans: `!IsOnlyASpectator() && !IsABot()`), `max_players` (`MaxActivePlayers` from the definition), `bots`, `spectators` (minus the host's own spectator PlayerState when `IsHeadlessHost`), `version` (`ProjectVersion`), `net_version` (`FNetworkVersion::GetLocalNetworkVersion()`), `password` (task 07 sets it; false until then).
- [ ] `Velox.Online.HeartbeatJson`: a fixed snapshot produces the golden JSON (field order irrelevant, parsed and compared); strings are escaped; `time_left_s` never negative. `Velox.Online.MapRegistry`: `L_DM1` resolves to "Praesidium" with FFA and Duel; a test map is not public; an unknown map falls back to its short name.
- [ ] `ProjectVersion=0.6.0` set; the server log's `LogNetVersion` line changes versus `main` (report both values).
- [ ] Headless: `UnrealEditor-Cmd … L_ItemTest?listen?game=FFA?SpectatorOnly=1?Bots=2 -game -nullrhi -ServerId=test -ServerName=T -ini:Game:[/Script/Velox.VeloxOnlineSettings]:HeartbeatUrl=http://127.0.0.1:8791/api/v1/heartbeat` against `python3 -I Tools/ci/heartbeat_stub.py 8791` for 90 s → ≥ 3 posts; the stub shows `players 0`, `bots 2`, `spectators 0` (host excluded), `phase` Warmup, `map_display_name` for `L_ItemTest` = its short name; the same run with `-game` as a connecting client (no `-ServerId`, `127.0.0.1:7777` URL) posts nothing.
- [ ] Build green (Editor + Game), all tests green; `nettest.sh --scripts FireRun` PASS (the HTTP module adds nothing to the move stream — confirm no new log noise).

## Verification steps
1. `infra/ci/build.sh` → green; the two new tests listed.
2. The headless run above under the soak lock; paste the stub's output tail.
3. `grep LogNetVersion Saved/Logs/*.log | tail -1` → the new network version; compare with a `main` log.

## Report back
Standard format plus: the golden heartbeat JSON, both `LogNetVersion` values, how `IsABot()`/`IsOnlyASpectator()` were resolved, and the exact delegates used for the debounced posts.

## Notes / outcome
(Filled by the agent or the lead after completion.)
