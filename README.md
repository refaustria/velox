# Velox

A fast arena FPS in the spirit of Quake 3 Arena, built on Unreal Engine 5.8. Strafe jumping, bunny hopping, rocket jumps, hitscan weapons, item timers, bots, dedicated servers.

This repository is driven by **Claude Code acting as project lead**. The lead plans milestones, writes tasks and delegates every task to a subagent; it does not write game code itself.

## How the repo is organised

| Path | Purpose |
|---|---|
| `CLAUDE.md` | Instructions for the Claude Code project lead: vision, environment, model routing, workflow, conventions. Read this first. |
| `ROADMAP.md` | Milestones M0–M7 with acceptance criteria. |
| `STATUS.md` | Living status: done, in progress, blocked, open questions for the human. Updated every session. |
| `.claude/agents/` | Subagent definitions. `dev` (Sonnet 5.5) for regular work, `dev-hard` (Opus 5.5) for hard implementation, `architect` (Fable 5.1) for architecture, reviews and root-causing. |
| `docs/adr/` | Architecture decision records written by `architect`. |
| `docs/tasks/` | One file per task, grouped by milestone. |
| `Source/`, `Content/`, `Config/` | The Unreal project (created in M0). |
| `infra/` | nginx vhost for `games.inceptum.at`, systemd units for dedicated servers, CI and build scripts. |

## Tech stack

- Unreal Engine 5.8, C++ for gameplay/netcode, Blueprints for UI and tuning data
- Gameplay Ability System; Epic's Lyra sample as reference
- Blender for assets, Git LFS for binaries
- nginx + Let's Encrypt on `games.inceptum.at` for downloads, status and later a server browser
- Rust for backend services, only when needed (M6+)

## Getting started

Requirements: a Linux machine with sudo, ~60 GB free disk, an Epic Games account (for the UE 5.8 Linux download), DNS for `games.inceptum.at` pointing at the machine, Claude Code installed and authenticated.

```bash
git clone <this repo> velox
cd velox
git lfs install
claude
```

Then paste the kickoff prompt from `docs/KICKOFF.md`. From that point on, start each session with `claude` in the repo root; the lead reads `CLAUDE.md`, `ROADMAP.md` and `STATUS.md` and continues where it left off.

Things the lead will ask you for (see "Open questions" in `STATUS.md`): the UE 5.8 Linux download, Steam/EOS credentials when M6 starts, and playtest feedback on movement feel at the end of M1.

## Milestones

M0 Infrastructure → M1 Movement → M2 Combat → M3 Game loop → M4 Bots → M5 First map & characters → M6 Online → M7 Polish. Details in `ROADMAP.md`.
