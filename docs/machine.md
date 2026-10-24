# Machine inventory

Recorded 2026-10-24 (task M0/01).

| Item | Value |
|---|---|
| OS / kernel | Debian 13 (trixie), kernel 6.12.101+deb13-amd64 |
| CPU | 16 vCPU, QEMU Virtual CPU version 2.5+ |
| RAM / swap | 30 GiB / 479 MiB (swap nearly full) |
| Disk | `/` (/dev/vdb) 125G, 79G used, **41G free**; `/mnt/oldroot` (/dev/vda1) 7.4G, 1.7G free |
| GPU | QEMU stdvga (1234:1111). No real GPU: headless only, use `-nullrhi`. Vulkan packages installed but no usable device. |
| Docker | 29.9.2, active |
| nginx | 1.26.3 (`/usr/sbin/nginx`), active; holds TCP 80 and 443 (v4+v6) |
| Public IP | 77.244.242.34 |
| `dig +short games.inceptum.at` | `api.gitref.eu.` then `77.244.242.34` (CNAME to the public IP of this host) |
| Compilers / tools | gcc 14.2.0, clang 19.1.7, cmake 3.31.6, ninja 1.12.1, python 3.13.5, git-lfs 3.6.1 |

## Listening ports (at inventory time)
- TCP public: 22, 80, 443, 8765 (0.0.0.0), 3000 (*)
- TCP loopback only: 4923, 5432, 5433, 5435, 7681-7683, 8080-8082, 8090, 8095, 8580, 8787, 37765
- UDP: 5353 (mDNS), 57312, 42886 (dynamic). UDP 7777 (game) is free.

## Disk-space verdict
UE 5.8 Linux needs roughly 25 GB download plus ~50 GB installed (about 75 GB peak if the archive is kept until extracted). Free on `/`: 41 GB. **Missing: ~34 GB at peak** (~9 GB even if the archive is streamed/deleted so only the 50 GB install counts), and that leaves nothing for project files, DDC, cooked builds or the Git LFS store. The disk must be enlarged (or another volume attached) before the engine is installed; `/mnt/oldroot` is too small. Note also: swap is almost exhausted.

## Packages installed for UE (apt)
build-essential, clang, lld, cmake, ninja-build, git-lfs, unzip, p7zip-full, rsync, libsdl2-dev, libvulkan1, mesa-vulkan-drivers, vulkan-tools, libxcursor1, libxinerama1, libxrandr2, libxi6, libxss1, libgl1, libglu1-mesa, libssl-dev, libicu-dev, libatomic1, python3-pip. No dotnet (UE bundles its own).

## Engine
- Unreal Engine 5.8.4 (CL 58902052) at `/opt/UnrealEngine-5.8`, installed (prebuilt) build.
- Trimmed: default exclusions plus plugins MetaHuman, VirtualProduction, PixelStreaming2, TextToSpeech, ProceduralVegetationEditor, USDCore, GeoReferencing (disk). See `infra/engine/README.md`.
- Clang toolchain `v26_clang-20.1.8-rockylinux8` present under `Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/` (required, ~3.4 GB, do not delete).
- Swapfile 4 GB at `/swapfile` added 2026-10-24.
