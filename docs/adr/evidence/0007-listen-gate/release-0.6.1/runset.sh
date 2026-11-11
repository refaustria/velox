#!/bin/bash
# runset.sh <label> <nettest args...> (release 0.6.1 gate): records loadavg + foreign UE processes at start/end into host.txt, runs nettest.sh.
# Run under flock soak.lock. Env: S (scratch), W (worktree), both default to the 0.6.1 run.
S="${S:-/tmp/claude-1001/-home-bojan-velox/2d67b3e8-90f7-4bfb-b88f-343439b061f3/scratchpad/t13}"
W="${W:-/home/bojan/velox/.claude/worktrees/agent-ad5cb31d71d240545}"
label="$1"; shift
hoststate() {
  echo "== $label $1 $(date -u +%FT%TZ) loadavg: $(cat /proc/loadavg)"
  ps -eo pid=,user=,etime=,args= | /usr/bin/grep -a -E '(UnrealEditor|Binaries/Linux/Velox|UnrealTraceServer|ShaderCompileWorker)' | /usr/bin/grep -a -v grep | cut -c1-200 | sed 's/^/   /'
  echo "   foreign UE (not under $W / $S): $(ps -eo args= | /usr/bin/grep -a -E '^(/opt/UnrealEngine-5.8/Engine/Binaries/Linux/UnrealEditor|[^ ]*Binaries/Linux/Velox )' | /usr/bin/grep -a -v "$W" | /usr/bin/grep -a -v -c "$S")"
  echo "   df /: $(df -h / | tail -1)"
}
hoststate start >> "$S/host.txt"
export VELOX_OUT="$S/nt/$label"
mkdir -p "$VELOX_OUT"
"$W/infra/ci/nettest.sh" "$@" > "$S/$label.log" 2>&1
rc=$?
echo "   rc=$rc" >> "$S/host.txt"
hoststate end >> "$S/host.txt"
exit $rc
