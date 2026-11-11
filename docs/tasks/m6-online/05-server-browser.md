# 05 — Server browser: directory subsystem, list parsing, compatibility rule, widget

**Milestone:** M6 Online
**Agent:** dev
**Status:** done
**Depends on:** ADR 0007 §2; task 04's `UVeloxOnlineSettings` and the `servers.json` schema (dispatch after 04, or hand both the ADR schema verbatim)

## Goal
The main menu gets a "Servers" screen that fetches `https://games.inceptum.at/status/servers.json`, shows one row per server with map display name, mode, players, phase and version, greys out incompatible rows with the reason, and connects through the existing `UVeloxGameInstance::ConnectToServer`. No online subsystem, no ping column (ADR 0007 deferred list). C++ UMG per ADR 0004 §7, data through a view-model-like subsystem, parsing world-free and unit-tested.

## Scope — files/modules the agent may touch
- `Source/Velox/Net/VeloxServerList.h` (new, header-only, world-free): `FVeloxServerEntry` (all `servers.json` fields + `Address`), `VeloxServerList::Parse(const FString& Json, TArray<FVeloxServerEntry>& Out, FString& Error) → bool`, `EVeloxServerCompat { Ok, VersionMismatch, NetVersionMismatch, Stale, Full }`, `VeloxServerList::Compatibility(const FVeloxServerEntry&, const FString& LocalVersion, uint32 LocalNetVersion, double StaleAfterSeconds) → EVeloxServerCompat`, `VeloxServerList::CompatText(EVeloxServerCompat)`, sort (compatible first, then players desc, name)
- `Source/Velox/Net/VeloxServerDirectorySubsystem.h/.cpp` (new `UGameInstanceSubsystem`, client only): `Refresh()`, `GetServers()`, `OnServersUpdated` (dynamic multicast), `LastError`, auto-refresh timer while the browser is open (`ServerListRefreshSeconds`), `FHttpModule` GET with a 5 s timeout, `Accept: application/json`
- `Source/Velox/UI/VeloxServerBrowserWidget.h/.cpp` (new `UVeloxUserWidget` subclass, `RebuildWidget` pattern): header row, scroll box of rows (name, map display name, mode, `players/max (+bots)`, phase, version), Refresh, Connect (selected row), Back; a status line (fetching / error / N servers); rows with compat ≠ Ok greyed, tooltip-free (text suffix with `CompatText`)
- `Source/Velox/UI/VeloxMainMenuWidget.h/.cpp` — a "Servers" button opening the browser; the direct-IP field stays
- `Source/Velox/UI/VeloxUISettings.*` — `ServerBrowserWidgetClass` (config), strings for the compat texts and the status line
- `Source/Velox/UI/VeloxSettingsCheats.cpp` or a new `UI/VeloxOnlineCheats.cpp` — `velox.online.DumpServers [url]` (non-shipping): refreshes and logs the rows with their compat
- `Source/Velox/Tests/VeloxOnlineTests.cpp` — `Velox.Online.ServerListParse`, `Velox.Online.ServerCompat`
- `Content/`: none (programmatic widgets; a `WBP_` skin is the Mac's business later)
- `docs/tasks/m6-online/notes/05-report.md`

## Relevant ADRs / references
- ADR 0007 §2 (client side, versioning rule, no ping), §5.2 (password field on connect — task 07 adds the server side; add the optional password box now, pass `?Password=` only when non-empty)
- ADR 0004 §7 (C++ UMG rules, `UVeloxUserWidget`, settings for strings, `ConnectToServer`, failure texts in `VeloxMenuMath.h`)
- `Source/Velox/UI/VeloxMainMenuWidget.cpp` (existing connect flow, `LastConnectAddress`)

## Acceptance criteria
- [ ] `Velox.Online.ServerListParse`: the ADR §2 golden file parses to the expected entries (addresses, numbers, booleans); a missing `servers` array, a wrong `schema`, a malformed entry (skipped with the error listed, not fatal), an empty list, trailing unknown fields (ignored) are rows. `Velox.Online.ServerCompat`: equal version+net → Ok; different `version` same net → `VersionMismatch`; different `net_version` → `NetVersionMismatch` (takes precedence); `updated_s_ago` > `StaleAfterSeconds` (120) → Stale; `players >= max_players` → Full (still connectable as spectator — the row stays enabled but labelled); sort order row.
- [ ] `UVeloxServerDirectorySubsystem` never exists on a dedicated/headless server (`ShouldCreateSubsystem` false when `IsRunningDedicatedServer()` or `IsHeadlessHost` — use `VeloxServerKind` from task 02 if merged, else the command-line check and a TODO referencing it); `Refresh()` is idempotent while a request is in flight; HTTP errors become `LastError` text and an `OnServersUpdated` broadcast with the previous list retained.
- [ ] Widget: opens from the main menu, auto-refreshes every `ServerListRefreshSeconds` while visible, Connect calls `ConnectToServer(Entry.Address [+ "?Password=…"])` and the existing failure path returns to the menu with the reason; keyboard: Enter connects the selected row, Escape goes back. All strings from `UVeloxUISettings`.
- [ ] `velox.online.DumpServers http://127.0.0.1:8792/servers.json` on a headless `-game -nullrhi` client against `python3 -I -m http.server 8792` serving a fixture → log shows the rows and their compat; a fixture with a different `version` shows `VersionMismatch`.
- [ ] Build green (Editor + Game, incl. Shipping Game once: `Build.sh Velox Linux Shipping` — the cheat must compile out), tests green; `infra/ci/settings-restart.sh` still PASS (menu touched).

## Verification steps
1. `infra/ci/build.sh` → green; `Build.sh Velox Linux Shipping -Project=…` → Succeeded (then delete `Binaries/Linux/Velox-Linux-Shipping*` and the Shipping intermediates).
2. Fixture run as above; paste the `DumpServers` log lines.
3. `infra/ci/settings-restart.sh --map L_CombatTest` → PASS.

## Report back
Standard format plus: the row layout as a text sketch, the compat rule table, and any field the ADR schema lacks that the widget wanted (report; do not add to the schema without the lead).

## Notes / outcome
Done on wip/m6-05-browser, see notes/05-report.md. 130 tests green (+3: ServerListParse, ServerCompat, ConnectPassword), Shipping builds, settings-restart PASS, live DumpServers check logged.
