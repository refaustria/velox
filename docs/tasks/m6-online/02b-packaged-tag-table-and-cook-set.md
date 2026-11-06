# 02b — Packaged build: gameplay-tag table parity, bot presets and L_DM1 in the cook

**Milestone:** M6 Online
**Agent:** dev
**Status:** todo
**Depends on:** 02 (gate run 2026-11-06, findings B1/B2), 01, 01b; decision 51 (cook and serve `L_DM1` now)

## Goal
A packaged LinuxServer accepts editor-binary clients and packaged clients alike with identical gameplay-tag net indices, every bot preset loads in packaged builds, and `L_DM1` is cooked. Then the gate's packaged part (P1 default list, P2 BotRun ×3, P3 BotDuel ×3) is rerun and passes.

## Findings (from `docs/tasks/m6-online/02-headless-listen-server-gate.md` Notes)
- **B1:** editor processes register 8 extra native tags (`Test`, `Test.StateTree.*`) from the engine's editor-only `StateTreeTestSuite`; with `FastReplication=True` every later tag index shifts by 8 (tag hash editor e71c34fb vs cooked 449a67bc); server assert "Received invalid tag net index 174"; silent misreads when in range; net checksum does not cover the tag table.
- **B2:** only `DA_Bot_Medium` is cooked; `?BotDifficulty=Hard` → `Failed to find object '/Game/Bots/DA_Bot_Hard'` → no Duel bots.
- **Decision 51:** `MapsToCook` lacks `/Game/Maps/DM1/L_DM1`.

## Scope — files/modules the agent may touch
- `Velox.uproject` (plugin list), `Config/DefaultEngine.ini`, `Config/DefaultGame.ini` (`MapsToCook`, `DirectoriesToAlwaysCook` or `PrimaryAssetTypesToScan`), `Config/DefaultGameplayTags.ini` if needed
- `Source/Velox/**` only for a tag-parity guard (see below) and its test
- `infra/ci/package.sh` (smoke: assert every bot preset and `L_DM1` load), `infra/ci/nettest.sh` only if the packaged rerun needs a flag
- `docs/adr/evidence/0007-listen-gate/`, this file, `docs/tasks/m6-online/README.md` row

## Approach
1. B1: first try disabling `StateTree` (and any plugin that only drags `StateTreeTestSuite` in) in `Velox.uproject`; check nothing in Velox or its enabled plugins depends on it (`.uplugin` dependencies, `Build.cs`). Prove parity: dump the tag table (the gate's `tagprobe/` method) from the editor binary and the cooked server — identical lists and hash. If disabling is impossible, report with the dependency chain; do not switch `FastReplication` off without asking.
2. Guard: a cheap automation test or startup log line that prints the tag-table hash (`VeloxNet tags=<n> hash=<h>`) on server and client so nettest can compare them; nettest FAILs a round on mismatch. Keep it world-free where possible.
3. B2: cook `/Game/Bots` (all presets) — prefer a primary asset type if presets are already `UPrimaryDataAsset`, else `DirectoriesToAlwaysCook`.
4. Add `L_DM1` to `MapsToCook` (keep `L_DM1_Blockout` and the test maps already there).
5. `package.sh`, then the packaged rerun P1–P3 exactly as in the 02 notes (`gate-20261106/` runner scripts).

## Acceptance criteria
- [ ] Tag lists and hash identical between editor binary and cooked server (evidence file).
- [ ] Packaged P1 default list 15/15, P2 BotRun ×3 set PASS, P3 BotDuel ×3 set PASS (or a load-excluded round rerun once, said so).
- [ ] `?BotDifficulty=Easy|Hard|Expert` load in the packaged server (log lines).
- [ ] The packaged server loads `L_DM1` (`?listen?SpectatorOnly=1`, 60 s, bots move).
- [ ] Editor build + all automation tests green.

## Constraints
Disk is tight (/ ≈ 12 GB free): delete your package staging/cooked output and archives after extracting; below 10 GB free pause and report. Soak lock for every UE step.

## Notes / outcome
