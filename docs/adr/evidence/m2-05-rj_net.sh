#!/usr/bin/env bash
# Rocket jump online: dedicated server (L_CombatTest, 60 Hz) + one -nullrhi client at 120 Hz with the nettest packet
# simulation (100 ms / 2 % loss each way). Usage: rj_net.sh <outdir> [port] [extra client exec]
set -u
OUT="$1"; PORT="${2:-7805}"; EXTRA="${3:-}"
P=/home/bojan/velox/.claude/worktrees/agent-a6369e19685ea9b88/Velox.uproject
ED=/opt/UnrealEngine-5.8/Engine/Binaries/Linux/UnrealEditor-Cmd
mkdir -p "$OUT"
"$ED" "$P" L_CombatTest -server -log -stdout -FullStdOutLogOutput -nullrhi -unattended -nopause -nosplash -port="$PORT" \
  -ExecCmds="velox.combat.GiveAllWeapons" >"$OUT/server.log" 2>&1 &
SPID=$!
for i in $(seq 1 180); do /usr/bin/grep -aq "Game Engine Initialized" "$OUT/server.log" && break; sleep 1; done
sleep 2
timeout 120 "$ED" "$P" "127.0.0.1:$PORT" -game -log -stdout -FullStdOutLogOutput -nullrhi -nosound -unattended -nopause -nosplash \
  -ExecCmds="t.MaxFPS 120, log LogVeloxMovement Verbose, velox.move.Record $OUT/rj.csv, velox.combat.RocketJumpTest quit${EXTRA:+, $EXTRA}" \
  -WINDOWED -ResX=320 -ResY=240 -PktLag=100 -PktLagVariance=20 -PktLoss=2 -PktIncomingLagMin=80 -PktIncomingLagMax=120 -PktIncomingLoss=2 \
  >"$OUT/client.log" 2>&1
echo "client rc=$?"
kill -TERM "$SPID" 2>/dev/null; sleep 3; kill -KILL "$SPID" 2>/dev/null
/usr/bin/grep -a "VeloxRocketJump" "$OUT/client.log"
