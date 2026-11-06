# 02b — Packaged build: gameplay-tag table parity, bot presets and L_DM1 in the cook

**Milestone:** M6 Online
**Agent:** dev
**Status:** todo (acceptance amended by ADR 0007 F2, 2026-11-06: the tag hash is folded into the network version, P1–P3 on an idle host, JumpPad near-threshold rule)
**Depends on:** 02 (gate run 2026-11-06, findings B1/B2), ADR 0007 F2 (ii)–(iii), 01, 01b; decision 51 (cook and serve `L_DM1` now)

## Goal
A packaged LinuxServer accepts editor-binary clients and packaged clients alike with identical gameplay-tag net indices, every bot preset loads in packaged builds, and `L_DM1` is cooked. Then the gate's packaged part (P1 default list, P2 BotRun ×3, P3 BotDuel ×3) is rerun and passes.

## Findings (from `docs/tasks/m6-online/02-headless-listen-server-gate.md` Notes)
- **B1:** editor processes register 8 extra native tags (`Test`, `Test.StateTree.*`) from the engine's editor-only `StateTreeTestSuite`; with `FastReplication=True` every later tag index shifts by 8 (tag hash editor e71c34fb vs cooked 449a67bc); server assert "Received invalid tag net index 174"; silent misreads when in range; net checksum does not cover the tag table.
- **B2:** only `DA_Bot_Medium` is cooked; `?BotDifficulty=Hard` → `Failed to find object '/Game/Bots/DA_Bot_Hard'` → no Duel bots.
- **Decision 51:** `MapsToCook` lacks `/Game/Maps/DM1/L_DM1`.
- **Ruling (ADR 0007 F2 (ii)):** ADR 0007 §1 (b) (editor `-server` with packaged clients) is unsafe while the tables differ and is suspended; a connect-time refusal is **required** for M6 (step 2 below), parity (step 1) is still done so editor clients remain usable in the rig; `FastReplication` stays on. F2 (iii): after P1–P3 pass, the packaged listen server is the deploy target of task 06.

## Scope — files/modules the agent may touch
- `Velox.uproject` (plugin list), `Config/DefaultEngine.ini`, `Config/DefaultGame.ini` (`MapsToCook`, `DirectoriesToAlwaysCook` or `PrimaryAssetTypesToScan`), `Config/DefaultGameplayTags.ini` if needed
- `Source/Velox/**` only for a tag-parity guard (see below) and its test
- `infra/ci/package.sh` (smoke: assert every bot preset and `L_DM1` load), `infra/ci/nettest.sh` only if the packaged rerun needs a flag
- `docs/adr/evidence/0007-listen-gate/`, this file, `docs/tasks/m6-online/README.md` row

## Approach
1. B1: first try disabling `StateTree` (and any plugin that only drags `StateTreeTestSuite` in) in `Velox.uproject`; check nothing in Velox or its enabled plugins depends on it (`.uplugin` dependencies, `Build.cs`). Prove parity: dump the tag table (the gate's `tagprobe/` method) from the editor binary and the cooked server — identical lists and hash. If disabling is impossible, report with the dependency chain; do not switch `FastReplication` off without asking.
2. Guard (F2 (ii)-b, required): fold the tag hash into the network version. In `UVeloxGameInstance::Init()` bind `FNetworkVersion::GetLocalNetworkVersionOverride` to a static function returning `HashCombine(FNetworkVersion::GetLocalNetworkVersion(false), UGameplayTagsManager::Get().GetNetworkGameplayTagNodeIndexHash())`, then call `FNetworkVersion::InvalidateNetworkChecksum()`; bind `IGameplayTagsModule::OnGameplayTagTreeChanged` to `FNetworkVersion::InvalidateNetworkChecksum` (a late native-tag registration re-hashes); log once `VeloxNet netversion=<u32> taghash=<hex> tags=<n>` (`LogVeloxNet`, Log). The engine refuses a mismatched client at `NMT_Hello` with its standard version failure; `UVeloxServerHeartbeatSubsystem` (`net_version`) and `VeloxServerList::Compatibility` (`NetVersionMismatch`) pick the new value up unchanged — check the `Velox.Online.*` tests still pass. Unit row `Velox.Net.TagHashInNetVersion` (world-free): the override is bound and `GetLocalNetworkVersion()` differs from `GetLocalNetworkVersion(false)`. nettest: FAIL a round when the server's and any client's `VeloxNet taghash=` differ (rig check; with the fold in place the client is also refused). Bind in the game instance, not the module `StartupModule`, so the tag tree is complete when the first checksum is cached.
3. B2: cook `/Game/Bots` (all presets) — prefer a primary asset type if presets are already `UPrimaryDataAsset`, else `DirectoriesToAlwaysCook`.
4. Add `L_DM1` to `MapsToCook` (keep `L_DM1_Blockout` and the test maps already there).
5. `package.sh`, then the packaged rerun P1–P3 exactly as in the 02 notes (`gate-20261106/` runner scripts), **on an idle host** (F2 (iii)): load average < 2.0 at set start and no foreign UE process, both recorded in `host.txt`; a round the evaluator labels D47 (d) slow-throughout is rerun once, not argued. One negative run for the guard: the editor binary *before* the plugin change (or `StateTree` re-enabled on one side) against the cooked server — the client must be refused with the engine's version failure and the server log must not reach `Received invalid tag net index`.

## Acceptance criteria
- [ ] Tag lists and hash identical between editor binary and cooked server (evidence file: `tagprobe` dump of both, `diff` empty, `NetworkGameplayTagNodeIndexHash` equal).
- [ ] Tag hash folded into the network version (F2 (ii)-b): `Checksum from delegate` equal on editor `-game`, editor `-server` and the cooked binary; `Velox.Net.TagHashInNetVersion` green; the negative run (step 5) shows the refusal in the client log; `Velox.Online.*` tests still green.
- [ ] Packaged P1 default list 15/15, P2 BotRun ×3 set PASS, P3 BotDuel ×3 set PASS on an idle host (load < 2.0 at set start in `host.txt`; one D47 (d) round rerun once, said so). F2 (i) rule: a set whose **only** behaviour failure is JumpPad < 90 % on `L_ItemTest` with ≥ 30 attempts and every failure `reason=missed` on `VeloxNavLink_0`/`_4` is recorded "behaviour PASS (JumpPad near-threshold, F2 item)" with the link line quoted; anything else is a FAIL.
- [ ] `?BotDifficulty=Easy|Hard|Expert` load in the packaged server (log lines).
- [ ] The packaged server loads `L_DM1` (`?listen?SpectatorOnly=1`, 60 s, bots move).
- [ ] Editor build + all automation tests green.

## Constraints
Disk is tight (/ ≈ 12 GB free): delete your package staging/cooked output and archives after extracting; below 10 GB free pause and report. Soak lock for every UE step.

## Notes / outcome
