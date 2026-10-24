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

## Review notes from task 01 (architect, 2026-10-25; ADR 0003 amendments B1–B9 are binding)
- **B8 (binding order in `HandleDeath`, server):** inside the `OnOutOfHealth` callback, synchronously: apply `GE_Dead` first (so a second damage GE in the same frame sees `Velox.State.Dead` and yields nothing), then `ASC->CancelAbilities()`, `ClearAbilityInput()`, `RemoveAllGameplayCues()`, then movement/collision/hitbox/corpse handling; `UnPossess` last. Do not defer any of this to a timer.
- `UVeloxHealthComponent::StartDeath/FinishDeath` exist; `OnDeathStarted` → HUD/cue, `OnDeathFinished` → corpse destroyed. On clients, uninitialise the corpse's health component when the pawn stops being the avatar (so a corpse never shows the next life's values).
- Respawn path is already in place: `AVeloxCharacter::PossessedBy` → `InitAbilityActorInfo` → `AVeloxPlayerState::OnPawnPossessed` (grant-once, `GE_SpawnInit`). Add `Inventory->ResetToLoadout()` (task 04) before `ApplySpawnInit`, then `GE_SpawnProtection` via `VeloxEffects::MakeDurationSpec` when `SpawnProtectionSeconds > 0`. `velox.combat.TestRespawn` (`Combat/VeloxCombatCheats.cpp`) exercises it headless.
- Dead-input gate: `ProcessAbilityInput` returns early under `Velox.State.Dead` (01b); the controller's respawn request on fire/jump press is native input, not an ability.
- `FellOutOfWorld` → `VeloxEffects::MakeDamageSpec(VictimASC, {Damage 1000, DamageTag Velox.Damage.Environment, bNoKnockback})` on the victim's own ASC with `bSelf = false` (the cheat command shows the pattern).

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
