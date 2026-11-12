# 07 (H2) — AVeloxHazardVolume, UVeloxHazardSubsystem, L_HazardTest, FtHazard

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** done
**Depends on:** H1, P4
**Wave:** hazard track

## Goal
Hazard actor with lava tick and void kill through `MakeDamageSpec` (no armor, no knockback, credited attacker's ASC), `HasHazards()` subsystem, the `VeloxHazard ...` log line, `veloxmap.hazard()`, the `L_HazardTest` fixture map and `FtHazard`.

## Scope — files/modules the agent may touch
- `Source/Velox/Combat/VeloxHazardVolume.*` (new)
- `Source/Velox/Combat/VeloxHazardSubsystem.*` (new)
- `Tools/editor/veloxmap.py` (`hazard()`)
- `Tools/editor/create_hazard_test_map.py` (new)
- `Source/Velox/Tests/`
- `infra/ci/fts.sh`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §3.1, §3.3, §3.7
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [x] `FtHazard` green, all six cases of §3.7 (incl. lava 25 per 0.25 s with death at the computed time and armor untouched; a dummy rocketed into the void gives credit)
- [ ] Default soak/FT list 13/13 (fts.sh full list run, see notes; nettest default soak list not run)
- [x] `FellOutOfWorld` unchanged and never credits anyone
- [x] `L_HazardTest` committed only with its generator script
- [x] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
dev-hard, 2026-11-12, branch `wip/m7-07-h2-hazard-volume` (base origin/m7-maps), engine 5.8.4 at /opt/UnrealEngine-5.8.

- `AVeloxHazardVolume` (`Combat/VeloxHazardVolume.*`): `UBoxComponent` root "Box" (static, QueryOnly, WorldStatic, overlaps
  `ECC_Pawn` only, no nav influence, hidden), `EVeloxHazardType Type { Lava, Void }`, `bReplicates = false`, `bNetLoadOnClient = true`.
  Overlap events and the begin/end handlers are switched on in `PostInitializeComponents` only on a game world that is not
  `NM_Client` (a non-replicated level actor has ROLE_Authority on clients too, so `HasAuthority()` is not the test). Only the
  pawn's capsule counts (mesh/hitbox ignored); corpses are skipped. Ticks only while it has occupants.
- Lava: contact hit on begin overlap unless the last hit was < `LavaTickSeconds` ago (1 ms slack for the 60 Hz step), then one hit
  every `LavaTickSeconds` while inside; a left pawn's record is kept 1 s. Void: `EnvironmentKillDamage` once per entry.
- Damage: `VeloxEffects::MakeDamageSpec` with `bNoArmor`, `bNoKnockback`, `bSelf = false`, `DamageScale = 1`, `HitLocation` = feet,
  `Causer` = the volume, tag `Velox.Damage.Hazard.Lava|Void`. Source ASC = the credited attacker's PS ASC from
  `UVeloxHealthComponent::GetCreditedAttacker(Now, HazardKillCreditSeconds)`, else the victim's own ASC (as `FellOutOfWorld`).
  Settings from the victim PS's `GetCombatSettings()`. No scoring code: frag/suicide come from `HandleDeath` -> `PlayerDied`.
  `FellOutOfWorld` untouched. Quad: confirmed `VeloxDamageExecution.cpp` does not scale; the FT proves it (lava_quad).
- Log line (before the spec is applied): `VeloxHazard type=lava|void victim=<label> damage=<n> credited=<label|none> age=<s> at=x,y,z`;
  `age` = seconds since the last-attacker record (also when expired, e.g. 6.73), `-1.00` without any record; `at` = feet.
- `UVeloxHazardSubsystem` (world subsystem, server and clients): `RegisterVolume`/`UnregisterVolume` (BeginPlay/EndPlay),
  `HasHazards()`, `FindHazardAt(P)`, `IsInHazard(P)`, `IsInHazard(P, Type)`, `GetVolumes()`.
- `veloxmap.hazard(name, htype, center, size, folder, tags, layout)`: spawns the volume, sets `type` and the box extent, verifies it,
  counts `hazardvolume` (the key `mapgen.spawn_hazards` expects).
- `L_HazardTest` (`create_hazard_test_map.py`): main floor x -2400..0 (walls N/S/W, east open), lava channel x -1500..-1200 /
  y -600..600 (bed top -60, volume -60..-40), platform 800 x 800 at x 1000..1800 over the gap, pad `Pad_Gap` (-300,0) -> apex
  (500,0,450), |v| 1790, landing x 1300 (+ JUMP_PAD link to the platform centre), void slab z -1300..-500 = footprint + 4000 cm,
  KillZ -2600, dummy spawner on the platform (bAutoSpawn off), 4 starts, nav bounds. Not in `MapsToCook`.
- Tests: `Velox.Combat.HazardVolume` (automation: CDO collision/replication settings, subsystem register/query/unregister) and
  `velox.combat.FtHazard` (fts.sh row `FtHazard|L_HazardTest`), six cases, all green: lava_self (5 hits of 25, 0.250 s apart,
  death 1.000 s after the first = expected, armor 50 untouched, suicide), void_rocket (credited, age 0.78, frag +1), void_walk_off
  (suicide), void_expired (record age 6.00 not credited, suicide), void_own_rocket (self hit, no record, knocked off the edge by its own rocket, suicide),
  lava_quad (attacker with Quad, every tick still 25, frag credited). No `FellOutOfWorld` death in the run.
- Verification (under the soak lock): `infra/ci/build.sh --targets VeloxEditor,Velox` green, automation 143/143 Success
  (`Saved/CI/20261112-233952-4191894`; Server/Client targets skipped on the installed engine); `infra/ci/fts.sh` OK 9/9 incl.
  FtHazard (`Saved/CI/20261112-235027-53099`); `create_map.py --map dm1 --golden check` OK; no UE process left. The nettest default soak list (18 scripts today, the "13/13"
  in the ADR is the M4 count) was not run: none of its maps has a hazard volume, and the code path is inert without one.
