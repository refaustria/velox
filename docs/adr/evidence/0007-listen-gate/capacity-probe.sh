#!/usr/bin/env bash
# Task m6-online/02 host semantics (b) and (c), CapacityRun by log: a headless listen host (FFA, no bots, ?MaxPlayers=1)
# admits one active client (the spectator-only host is not counted toward MaxPlayers by AGameSession::AtCapacity), refuses a
# second active login ("Server full"), and has no UCheatManager ("God" is not recognised on the host).
# Usage: capacity-probe.sh <worktree> <out dir> [port]; run under flock -o <scratchpad>/soak.lock. Kills only its own PIDs.
set -uo pipefail
W="$1"; OUT="$2"; PORT="${3:-8412}"
UE=/opt/UnrealEngine-5.8/Engine/Binaries/Linux/UnrealEditor-Cmd
mkdir -p "$OUT"
pids=()
stop() { for p in "${pids[@]}"; do kill -TERM "$p" 2>/dev/null; done; sleep 3; for p in "${pids[@]}"; do kill -KILL "$p" 2>/dev/null; done; }
trap stop EXIT
"$UE" "$W/Velox.uproject" "L_ItemTest?game=FFA?Bots=0?MaxPlayers=1?listen?SpectatorOnly=1?Name=Server" -game -nullrhi -nosound -unattended -nopause \
  -nosplash -log -stdout -FullStdOutLogOutput -port="$PORT" -ExecCmds="t.MaxFPS 60, God" > "$OUT/capacity-host.log" 2>&1 &
pids+=($!)
for _ in $(seq 1 120); do /usr/bin/grep -aq "IpNetDriver listening on port $PORT" "$OUT/capacity-host.log" && break; sleep 1; done
for n in 1 2; do
  "$UE" "$W/Velox.uproject" "127.0.0.1:$PORT" -game -nullrhi -nosound -unattended -nopause -nosplash -log -stdout -FullStdOutLogOutput \
    -ExecCmds="t.MaxFPS 60" > "$OUT/capacity-client$n.log" 2>&1 &
  pids+=($!)
  sleep 25
done
stop; trap - EXIT
echo "== host"
/usr/bin/grep -a -E "VeloxMatch (join|spectator)|PreLogin failure|Server full|Command not recognized: God|CheatManager|Login request" "$OUT/capacity-host.log" | cut -c1-220
for n in 1 2; do echo "== client$n"; /usr/bin/grep -a -E "Server full|NetworkFailure|TravelFailure|Welcomed by server|ConnectionLost|PendingConnectionFailure|failure" "$OUT/capacity-client$n.log" | cut -c1-220 | head -5; done
