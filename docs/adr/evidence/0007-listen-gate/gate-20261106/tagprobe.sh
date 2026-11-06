#!/bin/bash
# tagprobe.sh: dump the gameplay tag net index table of the editor -game process and of the packaged Development binary.
# Run under flock -o soak.lock. Kills only the PIDs it started.
G=/tmp/claude-1001/-home-bojan-velox/2d67b3e8-90f7-4bfb-b88f-343439b061f3/scratchpad/gate02
W=/home/bojan/velox/.claude/worktrees/agent-af959418f54947a3a
B=$G/srv/Velox/Binaries/Linux/Velox
mkdir -p $G/tagprobe
CMDS='-ExecCmds=GameplayTags.PrintNetIndices, GameplayTags.DumpSources'
/opt/UnrealEngine-5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$W/Velox.uproject" "L_CombatTest?listen?SpectatorOnly=1" -game -nullrhi -nosound -unattended -nopause -nosplash -log -port=7786 "$CMDS" -abslog=$G/tagprobe/editor.log > /dev/null 2>&1 &
P1=$!
"$B" "L_CombatTest?listen?SpectatorOnly=1" -nullrhi -nosound -unattended -nopause -nosplash -log -port=7787 -UserDir=$G/srv-userdir/probe "$CMDS" -abslog=$G/tagprobe/packaged.log > /dev/null 2>&1 &
P2=$!
for i in $(seq 1 90); do
  sleep 2
  a=$(/usr/bin/grep -a -c 'NetIndex\|Tag Source\|Source' $G/tagprobe/editor.log 2>/dev/null); b=$(/usr/bin/grep -a -c 'listening on port' $G/tagprobe/packaged.log 2>/dev/null)
  if /usr/bin/grep -a -q 'listening on port' $G/tagprobe/editor.log 2>/dev/null && [ "${b:-0}" -gt 0 ]; then sleep 15; break; fi
done
kill $P1 $P2; sleep 5; kill -9 $P1 $P2 2>/dev/null
echo probe done
