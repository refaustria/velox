# 04b — Heartbeat only on real directory servers

**Milestone:** M6 Online
**Agent:** dev
**Status:** done
**Depends on:** 04 (`UVeloxServerHeartbeatSubsystem`, `UVeloxOnlineSettings`), report `notes/04-report.md`

## Goal
Since 04, every server without `-ServerId` (all soak/nettest/fts servers) tries to post and logs one `VeloxHeartbeat failed code=0` Warning per minute. A server posts only when it is a directory server: `-ServerId=<id>` on the command line (the systemd unit passes it, ADR 0007 §3). Without it the subsystem logs one `VeloxHeartbeat inactive (no -ServerId)` line at Log verbosity and posts nothing. `bHeartbeatEnabled` stays as the master switch.

## Scope — files/modules the agent may touch
- `Source/Velox/Net/VeloxServerHeartbeatSubsystem.*`, `Source/Velox/Core/VeloxOnlineSettings.*` (comment/default only)
- `Source/Velox/Tests/VeloxOnlineTests.cpp` (activation rule as a pure function test)
- `docs/tasks/m6-online/04-server-heartbeat-and-map-registry.md` notes

## Acceptance criteria
- [x] `infra/ci/build.sh` green (report the test count; this is also the first build of the merged head 01+03+04+12).
- [x] `nettest.sh --scripts FireRun` server log: 0 `VeloxHeartbeat failed` lines, one `inactive` line.
- [x] With `-ServerId=test` against `Tools/ci/heartbeat_stub.py` on loopback: posts as before.

## Notes (dev, 2026-11-01)
- Implemented: `IsHeartbeatActive(bEnabled, ServerId)` pure rule (enabled AND non-empty -ServerId); subsystem now always created but, when inactive, logs `VeloxHeartbeat inactive (no -ServerId)` once and registers no delegates/ticker. bHeartbeatEnabled=false now beats -ServerId. Test added to Velox.Online.HeartbeatJson.
- Verified: build.sh green, 125 tests, 0 not successful (lighting audit FAIL is the unbaked worktree, build exit 0). `nettest.sh --scripts FireRun --port 8461`: PASS, 0 `VeloxHeartbeat failed`, one `inactive` line.
- NOT verified: `-ServerId=test` against heartbeat_stub.py on loopback (run was queued behind the soak lock when the project paused). Command: UnrealEditor-Cmd Velox.uproject L_ItemTest -server -nullrhi -port=8463 -ServerId=test -ServerName=T "-ini:Game:[/Script/Velox.VeloxOnlineSettings]:HeartbeatUrl=http://127.0.0.1:8462/api/v1/heartbeat" with `python3 -I Tools/ci/heartbeat_stub.py 8462 --max 2`.

## Outcome (dev, 2026-11-06)
- Merged origin/m6-online (01b, 02 part A) cleanly. build.sh green on merged head: 127 tests, 0 not successful (lighting audit FAIL = unbaked worktree, exit 0).
- Stub run, ports 7791/18791, -ServerId=test: server log "VeloxHeartbeat hosting netmode=1 map=L_ItemTest", "VeloxHeartbeat ok code=204 reason=event"; stub received 2 posts (06:57:56, 06:58:26), body {"schema":1,"id":"test","name":"T","port":7791,"map":"L_ItemTest",...,"phase":"Warmup",...,"password":false}.
- Negative (no -ServerId, 100 s): one "VeloxHeartbeat inactive (no -ServerId)", 0 "VeloxHeartbeat failed", stub received 0 posts.
