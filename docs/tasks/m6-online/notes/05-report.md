# M6-05 report: server browser

## Result
Done. Editor + Game Development build green, 130 automation tests green (127 before, +3), Shipping Game builds (cheat compiles out; artefacts removed), `settings-restart.sh --map L_CombatTest` write/verify PASS.

## Row layout
```
| Server                       | Map        | Mode | Players       | Phase | Version |
| Velox FFA [password]         | Praesidium | ffa  | 3/8 (+3 bots) | Live  | 0.6.0   |
| Velox Duel (full - join ...) | Praesidium | duel | 2/2           | Live  | 0.6.0   |
| Old release (other game ...) | ...                                           (greyed)
status line                                  [password box] [Refresh] [Connect] [Back]
```
Up/Down select, Enter connects, Escape closes. Column weights/headers/strings are in `UVeloxUISettings`.

## Compat rule (first match wins)
| Condition | Result | Row | Connect |
|---|---|---|---|
| `net_version` != local | NetVersionMismatch | greyed + reason | refused with reason |
| `version` != local | VersionMismatch | greyed + reason | refused with reason |
| `updated_s_ago` > 120 (`ServerStaleAfterSeconds`) | Stale | greyed + reason | tried |
| `max_players>0 && players>=max_players` | Full | normal + label | tried (spectator) |
| else | Ok | normal | yes |
Sort: Ok/Full first, then players desc, name (case-insensitive), address.

## Live check (headless `-game -nullrhi`, python http.server on 18820, fixture with 6 entries, local net 4065349793)
```
VeloxServers skipped malformed entries: entry 5: no address
VeloxServers dump count=5 error='' note='entry 5: no address' local_version=0.6.0 local_net=4065349793
row=0 name='Velox FFA' ... password=1 ... compat=Ok
row=1 name='Velox Duel' players=2/2 ... compat=Full
row=2 name='Old release' version=0.5.0 ... compat=VersionMismatch
row=3 name='Other net' net=1 ... compat=NetVersionMismatch
row=4 name='Gone' ... compat=Stale
```
(An earlier probe with the ADR golden file gave NetVersionMismatch for both rows, its net 2891746305 != 4065349793.)
Logs: scratchpad/t05/live.log, probe.log, build.log.

## Deviations
- `UVeloxGameInstance` (Core, not in scope) gained `ConnectToServerWithPassword`; `ConnectToServer` forwards to it. Necessary: `NormalizeConnectAddress` rejects `?`, so a password cannot ride in the address, and `LastConnectAddress` must not store it. `VeloxMenuMath::AppendPasswordOption` (+ test `Velox.Online.ConnectPassword`) validates the password (no whitespace, `? # " \`).
- Main menu also got an optional password box (ADR 5.2 wording) next to the direct-IP field, plus the "Servers" button.
- `ServerStaleAfterSeconds` and `ServerListTimeoutSeconds` live in `UVeloxUISettings`, not `UVeloxOnlineSettings` (Core not in scope).
- `IsClientProcess` rule: not dedicated, not commandlet, no `-ServerId=`. `VeloxServerKind::IsServerLike` needs a world, which does not exist at subsystem creation (TODO noted in header).
- Stale rows are greyed but connectable; only version mismatches refuse Connect (the engine would refuse them anyway).
- The engine itself logs the travel URL (incl. `?Password=`) in its own Browse line; our log prints `password=0|1` only.

## Schema gaps
None blocking. The widget would like a numeric `time_left_s` rendering (not shown) and phase as a display string (ADR sample shows lowercase "match", the game sends LexToString; shown verbatim).

## Not done / notes
- No widget automation test (no UMG test harness for the browser; parse/compat/password/subsystem rule are unit-tested). Visual check deferred (Mac cannot render, VM headless).
- build.sh's L_DM1 audit printed "RESULT at none" (non-fatal warning, unrelated to this task).
