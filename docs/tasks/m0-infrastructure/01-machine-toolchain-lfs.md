# 01 — Machine inventory, Linux toolchain, Git LFS and repo hygiene

**Milestone:** M0 Infrastructure
**Agent:** dev
**Status:** in progress
**Depends on:** —

## Goal
The machine is inventoried, every Linux package Unreal Engine 5.8 needs for headless compile/cook/dedicated-server is installed, Git LFS is installed and configured, and the repository has a correct `.gitattributes`/`.gitignore` for an Unreal C++ project.

## Scope — files/modules the agent may touch
- `docs/machine.md` (new) — inventory
- `.gitattributes`, `.gitignore` (new)
- System packages via `sudo apt-get` (install only; do not remove anything, do not change nginx, firewall or docker)
- `infra/env.example` (new) — documented env vars (`UE_ROOT`, etc.), no secrets

## Relevant ADRs / references
- `CLAUDE.md` section 2 (environment) and 6 (conventions)
- Epic docs "Linux Development Requirements" for UE 5.x

## Acceptance criteria
- [ ] `docs/machine.md` records: OS/kernel, CPU cores, RAM, swap, disk free per mount, GPU (note: QEMU stdvga, no GPU → headless only, `-nullrhi`), docker present, nginx present and which ports it holds, listening TCP/UDP ports, public IP, result of `dig +short games.inceptum.at`, installed compiler/cmake/python versions, and an explicit disk-space verdict: UE 5.8 Linux needs roughly 25 GB download + ~50 GB installed; state how much is free and how much is missing.
- [ ] Installed (or confirmed present): `build-essential`, `clang`, `lld`, `cmake`, `ninja-build`, `git-lfs`, `unzip`, `p7zip-full`, `rsync`, `libsdl2-dev`, `libvulkan1`, `mesa-vulkan-drivers`, `vulkan-tools`, `libxcursor1 libxinerama1 libxrandr2 libxi6 libxss1 libgl1 libglu1-mesa` (UE editor runtime libs), `libssl-dev`, `libicu-dev`, `libatomic1`, `python3-pip`. UE 5.x bundles its own dotnet, so do not install dotnet unless the engine later complains.
- [ ] `git lfs install` run for the repo user; `.gitattributes` tracks binaries: `*.uasset *.umap *.fbx *.blend *.png *.jpg *.tga *.psd *.wav *.ogg *.mp4 *.ttf *.otf *.exr *.hdr *.zip` with `filter=lfs diff=lfs merge=lfs -text`; text line endings normalised (`* text=auto eol=lf`, `*.sh eol=lf`, `*.bat eol=crlf`).
- [ ] `.gitignore` covers UE: `Binaries/ Intermediate/ DerivedDataCache/ Saved/ Build/ .vs/ .vscode/ .idea/ *.xcworkspace *.xcodeproj *.sln *.suo *.opensdf *.sdf *.VC.db *.VC.opendb *.code-workspace .env` (keep `Content/` and `Config/` tracked; keep `Plugins/*/Binaries` and `Plugins/*/Intermediate` ignored; do not ignore `infra/`).
- [ ] `infra/env.example` lists `UE_ROOT=/opt/UnrealEngine-5.8` (proposed install path; do not create it) and `VELOX_PROJECT=$PWD/Velox.uproject`.

## Verification steps
1. `git lfs version` prints a version; `git check-attr filter -- Content/Foo.uasset` prints `lfs`.
2. `dpkg -l | grep -E 'git-lfs|libsdl2-dev|libvulkan1'` shows installed.
3. `cat docs/machine.md` contains the disk-space verdict.

## Report back
Standard format. Include the apt commands run and the disk verdict verbatim.
