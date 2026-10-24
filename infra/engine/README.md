# Unreal Engine 5.8 Linux install (streamed)

`install-ue-linux.sh` downloads the prebuilt Linux editor and extracts it directly
into `/opt/UnrealEngine-5.8` by piping `curl` into `bsdtar`. **The zip (39.8 GB for 5.8.4) is never
written to disk**, so only the extracted size is needed. Nothing is cached.

## 1. Get the download URL (human step)
1. Log in at https://www.unrealengine.com/en-US/linux with the Epic account.
2. Right-click the UE 5.8 Linux download button and choose "Copy link address".
3. The link is signed and expires after a few hours; start the script right away.
The link is signed for GET only (`x-id=GetObject`): a HEAD request returns 403, so the
script checks it with a one-byte ranged GET instead. The script never logs in or scrapes anything.

## 2. Run
```
infra/engine/install-ue-linux.sh '<download-url>' [/opt/UnrealEngine-5.8]
```
Quote the URL (it contains `&`). Takes an hour or more, so run it detached:
```
nohup infra/engine/install-ue-linux.sh '<url>' > ~/ue-nohup.out 2>&1 &
tail -f ~/ue-nohup.out        # or run inside tmux
```
Output is also logged to `$HOME/ue-install-<YYYY-MM-DD>.log`. If `pv` is installed
(`sudo apt-get install -y pv`, optional) it shows a progress bar; otherwise curl's bar is used.

Flags: `--force` (wipe non-empty dest), `--full` (no exclusions), `--check-only` (only run the
URL size check, exit 0 if it passes; useful to validate a link without touching disk),
`--trim-plugins` (after extraction delete the plugin list under "Lessons" below),
`--toolchain <name>` (clang toolchain dir; default `v26_clang-20.1.8-rockylinux8`).

By default these are excluded from extraction: `*.debug`, `*.sym`, `Templates/`, `FeaturePacks/`,
`Samples/` and `Engine/Documentation/`, all anchored to the archive root with bsdtar's `^`
(`^Templates/*` etc.; without `^` bsdtar matches at any depth and would delete
`Engine/Source/Runtime/Core/Public/Templates/`). Use `--full` for a complete install.
After extraction/trim the script fails loudly if `Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h`
or `Engine/Plugins/VirtualProduction/Takes/Takes.uplugin` is missing.

Pre-flight checks: curl and bsdtar present, dest writable, >= 45 GB free on dest's
filesystem (52 GB with `--full`), dest empty (else `--force`), ranged GET (`-r 0-0`) returns
200/206 and a total size >= 5 GB from `Content-Range` (fallback `Content-Length`); smaller means a
login/redirect page. It prints size and filename before the download.

## 3. Interruption
A pipe cannot resume (`curl -C -` does not work), so curl only retries transient errors
(`--retry 5`). If the run dies, re-run with `--force` (wipes dest) and, if the link expired,
a fresh URL:
```
infra/engine/install-ue-linux.sh --force '<new-url>'
```

## 4. After extraction
The script flattens a single top-level directory, checks `Engine/Binaries/Linux/UnrealEditor`
and `Engine/Build/BatchFiles/Linux/Build.sh`, restores exec bits (see below), ensures the clang
toolchain exists, prints `Build.version` and
whether `Engine/Build/InstalledBuild.txt` exists. Smoke check (no GPU):
```
/opt/UnrealEngine-5.8/Engine/Binaries/Linux/UnrealEditor-Cmd Velox.uproject -nullrhi -unattended -nopause -run=...
```
Then `infra/ci/build.sh` with `UE_ROOT=/opt/UnrealEngine-5.8`.

## Disk
Needs >= 45 GB free on the destination filesystem (52 GB with `--full`), checked. No temporary zip.

## Lessons from the 2026-10-24 install
- Actual zip: 39.8 GB. Installed size: 58 GB before trimming.
- **Exec bits are lost.** Zip stores Unix modes in the central directory, which a stream cannot read,
  so bsdtar extracts everything non-executable. The script afterwards runs `chmod +x` on all `*.sh`
  under `Engine`, every ELF file (magic check, not `.so`/`.a`/`.dll`) under `Engine/Binaries` and
  `Engine/Build`, and `Engine/Binaries/ThirdParty/DotNet/**/dotnet`.
- **Never remove `Engine/Extras/ThirdPartyNotUE/`.** `SDKs/HostLinux/Linux_x64/<toolchain>` holds the
  clang toolchain UnrealBuildTool requires (5.8: `v26_clang-20.1.8-rockylinux8`). The folder is
  ~3.4 GB and required; UBT fails without it. The default exclusions do not touch it. If it is
  missing after extraction, the script fetches Epic's public tarball
  `https://cdn.unrealengine.com/Toolchain_Linux/native-linux-<toolchain>.tar.gz` (1.59 GB, no login)
  and stream-extracts it there. The script tries to read the required name from the engine
  (`grep` for `v<N>_clang-...` in UnrealBuildTool); if nothing is found it uses the default, or
  pass `--toolchain <name>`.
- **Plugins deleted for disk space** (`--trim-plugins` does exactly this, under `Engine/Plugins/`):
  `MetaHuman`, `VirtualProduction/*` except `VirtualProduction/Takes`, `Media/PixelStreaming2`, `Experimental/TextToSpeech`,
  `Experimental/ProceduralVegetationEditor`, `Runtime/USDCore`, `Runtime/GeoReferencing`.
  `VirtualProduction/Takes` is kept: LiveLink, NiagaraSimCaching and ChaosCaching (default-enabled) depend on it.
  Velox uses none of the others; reason was purely disk. Re-extract with `--force` if one is ever needed.
- **bsdtar exclusions are unanchored (2026-10-24 incident).** `--exclude 'Templates/*'` and
  `'*/Templates/*'` matched `Engine/Source/Runtime/Core/Public/Templates/` (TSharedPtr etc.) and every
  other directory named Templates/Samples in the engine, which broke the build. libarchive patterns
  match at any depth unless prefixed with `^`; `*` also crosses `/`. Fixed with `^Templates/*`,
  `^FeaturePacks/*`, `^Samples/*`, `^Engine/Documentation/*`. Verified on a test zip: only the
  top-level dirs are dropped, `Engine/**/Templates` and `Engine/**/Samples` survive. The post-extraction
  sanity check guards against a repeat. Also: trimming all of `VirtualProduction` removed `Takes`,
  which other plugins need.

## trim-plugins.py: platform extensions and orphan check (2026-10-24 incident)
- **Incident.** `trim-plugins.py --apply` deleted the parent plugins RigLogic and OpenXR but left their
  platform-extension plugins under `Engine/Platforms/<Platform>/Plugins/**/<Name>_<Platform>.uplugin`
  (e.g. `RigLogic_Android.uplugin`). UBT then aborted: `Unable to find parent plugin RigLogic for
  platform extension plugin .../RigLogic_Android.uplugin`. Engine/Platforms/{Android,IOS,VisionOS} was
  removed by hand.
- **Fix.** The script now also scans `Engine/Platforms/*/Plugins/**/*.uplugin` (parent = file stem minus
  `_<Platform>`, else directory name). Children of any plugin in the deletion set are deleted with it and
  listed in the report under "Platform extensions removed with their parent".
- `--prune-platforms Android,IOS,VisionOS,TVOS` marks whole `Engine/Platforms/<X>` dirs (suggested on a
  Linux-only host; `Linux` and path-like names are refused). Only marks; deletion needs `--apply --yes`.
- `--check-orphans` lists platform-extension plugins whose parent no longer exists and exits 1 if any.
  It also runs automatically after `--apply`. Read-only; run it after any manual deletion too.

## Dependency verification (added after the 2026-10-24 post-mortem)

`trim-plugins.py --verify-deps` checks that every remaining plugin's hard dependencies (`Enabled:true`, not `Optional`) still exist, for the current tree and the planned
post-state; exit 1 on violations. `--apply` refuses to run when the planned post-state has violations and re-verifies afterwards. `.uplugin` parsing is strict about failures
(abort unless `--ignore-parse-errors`), name matching is case-insensitive, and a `"Enabled": false` in the `.uproject` never cuts a dependency edge (it is reported as an
"uproject conflict"). Always run `--verify-deps` before and after any deletion. See `TRIM-REPORT.md` "Post-mortem 2026-10-24".

### Module- and binary-level verification (`--verify-deps`, `--manifest DIR`)
Plugin-level checks are not enough: a kept plugin's `*.Build.cs` (also lowercase `.build.cs`) may list a module of a deleted plugin
(`IKRig/IKRigUAF -> UAFAnimGraph`), and precompiled `.so` files of kept plugins may `NEEDED` a `libUnrealEditor-<Module>.so` of a deleted plugin.
`--verify-deps` therefore runs three layers on the current tree and the planned post-state (and after `--apply`):
1. `.uplugin` hard dependencies; 2. dependency lists of kept plugins' Build.cs (`Public/PrivateDependencyModuleNames`, `DynamicallyLoadedModuleNames`,
`Public/PrivateIncludePathModuleNames`; `Add`, `AddRange(new string[]/new[])`, multi-line, comments ignored) against all module definitions;
3. `readelf -d` NEEDED of all kept `.so`. A missing module is a VIOLATION only if the manifest (`--manifest`, default
`/home/bojan/ue-5.8.4-uplugin-manifest`, the original `.uplugin` files) says a now-deleted plugin provided it; otherwise it is informational
("pre-existing/unknown"). Names in `ALLOWLIST` (top of the check section: GRDK, DiaSDK, Catch2, uLangCore, uLangJSON, LinuxTargetPlatformSettings) are
always informational-silent. Limitation: Build.cs parsing is textual, platform-conditional references (`if (Target.Platform == ...)`) are included. `--apply` is refused on any violation.

### Keep-closure over a unified dependency graph (computed, not only verified)

The deletion set is the complement of a keep-closure computed over three edge types, iterated to a fixed point from the roots
(EnabledByDefault + uproject-enabled + `EXTRA_ROOTS`/`PROTECTED_OPTIONAL` + plugins referenced from the project's Build.cs):

1. `uplugin`: `.uplugin` hard dependency (`Enabled:true`, not `Optional`).
2. `build.cs`: a module of plugin A lists module M in `Public/PrivateDependencyModuleNames`, `DynamicallyLoadedModuleNames` or
   `Public/PrivateIncludePathModuleNames`; M's owner is the plugin whose directory contains `M.Build.cs` (fallback: the `--manifest` provider).
   Modules defined only in `Engine/Source` create no edge.
3. `readelf`: a `.so` of plugin A has `NEEDED libUnrealEditor-M.so`; edge to the owner of M.

The report section "Why each non-root plugin is kept" lists, per kept plugin, the edge types and examples
(e.g. `UAF kept: IKRig/IKRigUAF.build.cs -> UAF`), and which plugins are kept only via build.cs/readelf edges. The aggressive (denylist)
blockers use the same graph. The verification layers above remain the final guard before `--apply`.

Platform-conditional heuristic: a Build.cs reference is ignored (reported as "platform-conditional (ignored)", not a violation and not an edge) when it
sits inside an `if (...)` block (braced or single statement; else-branches are treated as active) whose condition (a) mentions only non-Linux
platforms positively (`Win64, Win32, Windows, Mac, Android, IOS, TVOS, VisionOS, Apple, Microsoft, Switch, PS4, PS5, Sony, Xbox*, GDK, ...`) or `!= Linux`
(also `!IsInPlatformGroup(Unix)`), or (b) is a lone identifier declared `bool X = false;` in that file and never reassigned there
(a flag only platform subclasses flip, e.g. `bUseXbl` guarding `OnlineSubsystemGDK` in `Party.Build.cs`). Any positive Linux/Unix mention,
a negated non-Linux platform, or an unrecognised condition counts as active (conservative).
