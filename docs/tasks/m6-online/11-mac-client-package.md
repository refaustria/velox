# 11 — macOS client package (Apple Silicon), optional signing, upload

**Milestone:** M6 Online
**Agent:** dev (script) + human (runs it on the Apple Silicon Mac)
**Status:** todo
**Depends on:** ADR 0007 §4, question 47 (signing); task 01 (same version rule and manifest shape); task 06's `publish-download.sh`; the Apple Silicon Mac

## Goal
`infra/ci/build-mac.sh --package` produces `Velox-<ver>-macOS.zip` (Shipping, arm64) with the same version rule and manifest as the Linux package, ad-hoc signed by default (notarized only if the human chooses option 47 (b)), and the human uploads it with one command. The Mac is also the only machine that can **play** — so this task ends with the Mac client connecting to the public server through the browser.

## Scope — files/modules the agent may touch
- `infra/ci/build-mac.sh` — `--package [--version vX.Y.Z | --dev] [--sign adhoc|developer-id] [--notarize]` branch mirroring `package.sh` (shared logic moves to `infra/ci/package-common.sh` sourced by both, if that keeps `package.sh` readable; otherwise duplicate the few lines and say so)
- `infra/ci/package-common.sh` (new, optional), `infra/ci/README.md` (Mac package section), `docs/playtests/m6-online-checklist.md` — the Mac steps (task 13 owns the file; coordinate: this task writes the "download and connect" section)
- `docs/tasks/m6-online/notes/11-report.md`
(No `Source/`. The Mac has no coreutils `timeout`; reuse the existing shim.)

## Relevant ADRs / references
- ADR 0007 §4 (matrix, pipeline), §2 (versioning rule), question 47
- ADR 0001 §5 (Mac UAT invocation: `-platform=Mac -clientconfig=… -build -cook -stage -pak -archive`)
- `infra/ci/build-mac.sh` (`--bake` branch shows the pattern), STATUS q26 (Apple Silicon setup: Sonoma 14.5+, Xcode 26.0/26.1.1, Launcher UE 5.8, git-lfs)

## Acceptance criteria
- [ ] `build-mac.sh --package --dev` on the Apple Silicon Mac: `RunUAT.sh BuildCookRun -project=… -platform=Mac -clientconfig=Shipping -build -cook -stage -pak -compressed -archive -archivedirectory=<out>/Mac -unattended -utf8output -noP4 -nocompileeditor` (arm64 via the host default — no `-architecture` flag unless UAT asks), then `codesign --force --deep --sign - Velox.app` (ad-hoc) or `--sign "Developer ID Application: …"` + `xcrun notarytool submit … --wait` + `stapler` when `--notarize`; `ditto -c -k --keepParent Velox.app Velox-<ver>-macOS.zip`; `SHA256SUMS`, `MANIFEST.json` (same fields as Linux; `net_version` from a `-nullrhi` 20 s launch log of the packaged app); sizes printed.
- [ ] Version rule identical to task 01 (tag = `ProjectVersion`; `--dev` names `<ver>+<sha>` and prints the do-not-publish line).
- [ ] The packaged app launches on the Mac (windowed), reaches the main menu, the Servers screen lists the public server (task 06 phase B), Connect joins; a 5-minute FFA against the bots; the server journal shows the login/logout; `servers.json` showed `players 1`.
- [ ] Upload: `scp` the zip + sums to the VM, then `sudo infra/deploy/publish-download.sh <ver> Velox-<ver>-macOS.zip` → `https://games.inceptum.at/downloads/<ver>/Velox-<ver>-macOS.zip` downloadable, `latest.json` has `macos`.
- [ ] Gatekeeper path documented on the landing page per 47 (a): "right-click → Open" or `xattr -d com.apple.quarantine Velox.app`; verified once by the human on a second download.
- [ ] `infra/ci/README.md` documents the Mac package flow and the Developer-ID variant (env vars `VELOX_MAC_SIGN_IDENTITY`, `VELOX_NOTARY_PROFILE`, never credentials in the repo).

## Verification steps
1. On the Mac: `infra/ci/build-mac.sh --package --dev` → zip + sums; `codesign -dv --verbose=2 Velox.app` shows the ad-hoc (or Developer ID) signature; `spctl -a -vv Velox.app` result reported (will fail for ad-hoc — expected).
2. Launch the packaged app, Servers → Connect → play 5 min; screenshot the browser and the scoreboard for the checklist.
3. Upload and `curl -I https://games.inceptum.at/downloads/<ver>/Velox-<ver>-macOS.zip` → 200.

## Report back
Standard format plus: cook/stage times on the Mac, zip size, signing mode used, the Gatekeeper behaviour observed on first launch, and the human's play notes (feel vs the PIE checklists).

## Notes / outcome
(Filled by the agent or the lead after completion.)
