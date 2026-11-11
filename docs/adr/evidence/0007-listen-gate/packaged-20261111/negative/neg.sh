#!/bin/bash
# 02b negative run: cooked listen server vs editor -game client started with one extra gameplay tag (different tag hash).
W=/home/bojan/velox/.claude/worktrees/agent-a7a746c921cbb02bd
G=/tmp/t02b-link/neg
mkdir -p $G
B=/tmp/t02b-link/srv/Velox/Binaries/Linux/Velox
"$B" "L_CombatTest?listen?SpectatorOnly=1" -nullrhi -nosound -unattended -nopause -nosplash -log -port=7843 -UserDir=$G/ud -LogCmds="LogNetVersion Log, LogVeloxNet Log, LogNet Log" -abslog=$G/server.log >/dev/null 2>&1 &
S=$!
for i in $(seq 1 60); do sleep 2; /usr/bin/grep -a -q 'listening on port' $G/server.log 2>/dev/null && break; done
/opt/UnrealEngine-5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$W/Velox.uproject" 127.0.0.1:7843 -game -nullrhi -nosound -unattended -nopause -nosplash -log -LogCmds="LogNetVersion Log, LogVeloxNet Log, LogNet Log" '-ini:GameplayTags:[/Script/GameplayTags.GameplayTagsSettings]:+GameplayTagList=(Tag="Velox.NegativeRun.Extra",DevComment="02b negative run")' -abslog=$G/client.log >/dev/null 2>&1 &
C=$!
sleep 45
kill $C $S; sleep 5; kill -9 $C $S 2>/dev/null
echo neg done
