# 06 — Death, corpse, respawn, spawn selection

**Milestone:** M2 Combat
**Agent:** dev
**Status:** todo
**Depends on:** 01

## Goal
Implement ADR 0003 implementation-notes task **`06-death-respawn-gamemode`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `Source/Velox/GameModes/VeloxGameMode.*`, `VeloxGameState.*`, `VeloxPlayerState.*`, `VeloxPlayerController.*` (death camera), `Source/Velox/Character/VeloxCharacter.*` (death tag handling, corpse timer), `Config/DefaultGame.ini`

## Acceptance criteria
- [ ] ADR 0003 §10 `PlaceholderBody` + `PlayerColor`: bright per-player colour from a palette assigned by the game mode, replicated via PlayerState, applied to the placeholder mesh material (readability non-negotiable). Corpse collision must keep ignoring the `Weapon` channel (task 02 note).
- [ ] Death GE/tag → no ragdoll, corpse 3 s, death camera on corpse; respawn on press after 1.7 s, forced at 20 s; spawn = farthest-half random by nearest-enemy distance, telefrag only if all occupied; spawn protection mechanism (default 0 s); `OnKill` delegate.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
