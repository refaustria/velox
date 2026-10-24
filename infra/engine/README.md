# Unreal Engine 5.8 Linux install (streamed)

`install-ue-linux.sh` downloads the prebuilt Linux editor and extracts it directly
into `/opt/UnrealEngine-5.8` by piping `curl` into `bsdtar`. **The ~25 GB zip is never
written to disk**, so only the extracted size (~50 GB) is needed. Nothing is cached.

## 1. Get the download URL (human step)
1. Log in at https://www.unrealengine.com/en-US/linux with the Epic account.
2. Right-click the UE 5.8 Linux download button and choose "Copy link address".
3. The link is signed and expires after a few hours; start the script right away.
The script never logs in or scrapes anything.

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

Pre-flight checks: curl and bsdtar present, dest writable, >= 52 GB free on dest's
filesystem, dest empty (else `--force`), HEAD request returns 2xx and a size >= 5 GB
(smaller means a login/redirect page). It prints size and filename before the download.

## 3. Interruption
A pipe cannot resume (`curl -C -` does not work), so curl only retries transient errors
(`--retry 5`). If the run dies, re-run with `--force` (wipes dest) and, if the link expired,
a fresh URL:
```
infra/engine/install-ue-linux.sh --force '<new-url>'
```

## 4. After extraction
The script flattens a single top-level directory, checks `Engine/Binaries/Linux/UnrealEditor`
and `Engine/Build/BatchFiles/Linux/Build.sh`, fixes exec bits, prints `Build.version` and
whether `Engine/Build/InstalledBuild.txt` exists. Smoke check (no GPU):
```
/opt/UnrealEngine-5.8/Engine/Binaries/Linux/UnrealEditor-Cmd Velox.uproject -nullrhi -unattended -nopause -run=...
```
Then `infra/ci/build.sh` with `UE_ROOT=/opt/UnrealEngine-5.8`.

## Disk
Needs >= 52 GB free on the destination filesystem (checked). No temporary zip.
