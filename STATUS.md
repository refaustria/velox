# Velox Status

_Last updated: 2026-10-24 (session 1, project lead). Branch: `m0-infrastructure`._

## Done
- Repo bootstrapped: `ROADMAP.md`, `STATUS.md`, `docs/tasks/TEMPLATE.md`, subagents `dev` / `dev-hard` / `architect` in `.claude/agents/`.
- **ADR 0001** project & module structure (`docs/adr/0001-project-and-module-structure.md`): one `Velox` module with domain folders, four targets, GAS on PlayerState, CMC saved-move prediction, server-side lag-compensation subsystem, forward renderer + MSAA + static lighting, 60 Hz server tick, test strategy, build-host strategy (Linux headless, Mac client on the human's Mac).
- **M0-01** machine inventory (`docs/machine.md`), Linux toolchain installed, Git LFS + `.gitattributes` + `.gitignore`, `infra/env.example`.
- **M0-02** `https://games.inceptum.at` live: nginx vhost, Let's Encrypt (valid to 2027-01-22, auto-renew), placeholder landing page, `/downloads/` and `/status/` locations prepared. Config in `infra/nginx/`.

## In progress
- **M0-03** Unreal project skeleton (dev-hard) — all classes/targets/config from ADR 0001; cannot be compiled on this VM (no engine), so it gets a two-reviewer static pass (ADR conformance + compile-error hunt) before commit.
- **M0-05** CI scripts `infra/ci/build.sh` / `build-mac.sh` (dev) — verified via `--dry-run` against a fake engine tree.

## Blocked
- **M0-06** UE 5.8 install on this VM: needs ~100 GB free (41 GB available) and the Epic download — see open questions.
- **M0-04** content bootstrap (`L_Empty`, input assets, data assets): needs a running editor (this VM after 06, or the Mac).
- First real compile of the skeleton: needs UE 5.8 on the Mac or on this VM.

## Next
- Merge M0-03/05 once reviewed → human compiles on the Mac (`infra/ci/build-mac.sh`) → fix anything the real compiler finds → M0-04 → merge `main` → M1 (movement) ADR + tasks.

## Open questions for the human
1. **Disk space.** UE 5.8 Linux needs ~25 GB download + ~50 GB installed; `/` has 41 GB free. Please enlarge the disk or attach a volume (≥ 100 GB free recommended, mounted e.g. at `/opt`). Also: swap is 479 MiB and nearly full; may I create a 16 GB `/swapfile`?
2. **UE 5.8 Linux download.** Log in at https://www.unrealengine.com/en-US/linux with your Epic account, download `Linux_Unreal_Engine_5.8.x.zip`, and place it at `/opt/ue-download/` (I will extract it to `/opt/UnrealEngine-5.8`). Alternatively give me the direct download URL (it is short-lived) and I fetch it. I will not try to work around the Epic login.
3. **Installed vs source engine.** Installed engines (Launcher, Linux zip) cannot build the `VeloxServer`/`VeloxClient` targets; the dedicated server is then `Velox -server`, which is fine until M6. A source build (needs EpicGames GitHub org linked to your Epic account, ~150 GB disk, hours of compile) is needed for M6 shipping. Recommendation: installed engine now, source build before M6. OK?
4. **Mac toolchain.** Please confirm: UE 5.8 installed via Epic Games Launcher (path `/Users/Shared/Epic Games/UE_5.8`), Xcode version, Apple Silicon or Intel. First build command will be `infra/ci/build-mac.sh` from the repo root.
5. **Renderer decision (veto window).** ADR 0001 picks forward renderer + MSAA + baked static lighting (Q3-style readability, max fps). This shapes all M5 art. Veto now if you prefer deferred + TAA.
6. **Scale.** Netcode defaults assume ≤ 16 players per server until M6. Confirm.
