# 04b — Heartbeat only on real directory servers

**Milestone:** M6 Online
**Agent:** dev
**Status:** todo
**Depends on:** 04 (`UVeloxServerHeartbeatSubsystem`, `UVeloxOnlineSettings`), report `notes/04-report.md`

## Goal
Since 04, every server without `-ServerId` (all soak/nettest/fts servers) tries to post and logs one `VeloxHeartbeat failed code=0` Warning per minute. A server posts only when it is a directory server: `-ServerId=<id>` on the command line (the systemd unit passes it, ADR 0007 §3). Without it the subsystem logs one `VeloxHeartbeat inactive (no -ServerId)` line at Log verbosity and posts nothing. `bHeartbeatEnabled` stays as the master switch.

## Scope — files/modules the agent may touch
- `Source/Velox/Net/VeloxServerHeartbeatSubsystem.*`, `Source/Velox/Core/VeloxOnlineSettings.*` (comment/default only)
- `Source/Velox/Tests/VeloxOnlineTests.cpp` (activation rule as a pure function test)
- `docs/tasks/m6-online/04-server-heartbeat-and-map-registry.md` notes

## Acceptance criteria
- [ ] `infra/ci/build.sh` green (report the test count; this is also the first build of the merged head 01+03+04+12).
- [ ] `nettest.sh --scripts FireRun` server log: 0 `VeloxHeartbeat failed` lines, one `inactive` line.
- [ ] With `-ServerId=test` against `Tools/ci/heartbeat_stub.py` on loopback: posts as before.
