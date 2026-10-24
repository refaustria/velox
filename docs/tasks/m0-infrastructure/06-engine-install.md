# 06 — Unreal Engine 5.8 install on this VM (human-dependent)

**Milestone:** M0 Infrastructure
**Agent:** dev (after the human provides disk and the download)
**Status:** done (2026-10-24) — UE 5.8.4 installed build at /opt/UnrealEngine-5.8, trimmed; see infra/engine/TRIM-REPORT.md
**Depends on:** human decisions in STATUS.md open questions

## Goal
UE 5.8 Linux is installed at `$UE_ROOT` (proposed `/opt/UnrealEngine-5.8`), owned by an appropriate user, and `UnrealEditor-Cmd -nullrhi` runs the `Velox.Core.Smoke` test on this VM. `docs/machine.md` records installed-vs-source status.

## Scope
- `/opt/UnrealEngine-5.8` (extraction only), `infra/env.example`, `docs/machine.md`
- Optional: swapfile creation (`/swapfile`, 16 GB) if the lead approves — state before doing it.

## Acceptance criteria
- [ ] ≥ 100 GB free before starting (checked with `df -h`).
- [ ] Archive from the human (`Linux_Unreal_Engine_5.8.x.zip` from Epic's site) extracted to `$UE_ROOT`; `Engine/Build/InstalledBuild.txt` present (installed build) — or a source build if the human chose that (then follow Epic's `Setup.sh && GenerateProjectFiles.sh && make` path, several hours).
- [ ] `infra/ci/build.sh` succeeds end-to-end on this VM.

## Verification steps
1. `infra/ci/build.sh` exit 0 with the smoke test in the summary.

## Notes
- Tooling: `infra/engine/install-ue-linux.sh` streams the zip via curl into bsdtar (no ~25 GB zip on disk; ~52 GB free needed). Instructions in `infra/engine/README.md`.
- Human provides the signed URL from unrealengine.com/en-US/linux; then run the script (detached) with `/opt/UnrealEngine-5.8` as dest.
