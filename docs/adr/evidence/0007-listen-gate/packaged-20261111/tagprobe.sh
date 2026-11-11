#!/bin/bash
# tagprobe.sh (02b): tag net index table + net version of editor -game, editor -server and the cooked Development server.
# Usage: tagprobe.sh <worktree> <packaged-root> <outdir>. Run under flock -o soak.lock. Kills only PIDs it started.
W="$1"; PK="$2"; G="$3"
B=$PK/Velox/Binaries/Linux/Velox
mkdir -p $G/tagprobe
CMDS='-ExecCmds=GameplayTags.PrintNetIndices, GameplayTags.DumpSources'
LC='-LogCmds=LogNetVersion Log, LogVeloxNet Log'
ED=/opt/UnrealEngine-5.8/Engine/Binaries/Linux/UnrealEditor-Cmd
"$ED" "$W/Velox.uproject" "L_CombatTest?listen?SpectatorOnly=1" -game -nullrhi -nosound -unattended -nopause -nosplash -log -port=7840 "$CMDS" "$LC" -abslog=$G/tagprobe/editor-game.log > /dev/null 2>&1 &
P1=$!
"$ED" "$W/Velox.uproject" "L_CombatTest?game=FFA" -server -nullrhi -nosound -unattended -nopause -nosplash -log -port=7841 "$CMDS" "$LC" -abslog=$G/tagprobe/editor-server.log > /dev/null 2>&1 &
P2=$!
"$B" "L_CombatTest?listen?SpectatorOnly=1" -nullrhi -nosound -unattended -nopause -nosplash -log -port=7842 -UserDir=$G/userdir-probe "$CMDS" "$LC" -abslog=$G/tagprobe/packaged.log > /dev/null 2>&1 &
P3=$!
for i in $(seq 1 90); do
  sleep 2
  n=0; for f in editor-game editor-server packaged; do /usr/bin/grep -a -q 'listening on port' $G/tagprobe/$f.log 2>/dev/null && n=$((n+1)); done
  [ $n -eq 3 ] && { sleep 15; break; }
done
kill $P1 $P2 $P3; sleep 5; kill -9 $P1 $P2 $P3 2>/dev/null
echo probe done
