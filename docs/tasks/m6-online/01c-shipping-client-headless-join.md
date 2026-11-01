# 01c — Shipping client does not join headless

**Milestone:** M6 Online
**Agent:** dev
**Status:** todo
**Depends on:** 01, 01b

## Goal
`Velox-Linux-Shipping -game -nullrhi 127.0.0.1:<port>` stays alive 50 s but never connects to the packaged server (task 01 report); the join was only verified with a Development client. Find out why (Shipping has no log: use `-log`/`-LogCmds`, `bUseLoggingInShipping` is not set, or a Shipping-only path such as the null RHI, the travel URL parsing, `-nullrhi` in Shipping, missing `-NoSteam` etc.) and make the Shipping player archive join a packaged server headless, so the release smoke (task 13) can test the archive players download.

## Scope — files/modules the agent may touch
- `infra/ci/package.sh` (smoke), `Source/Velox.Target.cs` only if logging in Shipping is the fix and only for a test configuration, `Config/` (connection URL / net driver settings) — report before any `Source/` gameplay change.

## Acceptance criteria
- [ ] Cause stated.
- [ ] `package.sh --dev --smoke` runs the Shipping client against the packaged server and sees the join on the server side ("Join succeeded" / PostLogin line).
- [ ] No change to what the player archive contains apart from what the fix needs; sizes reported.
