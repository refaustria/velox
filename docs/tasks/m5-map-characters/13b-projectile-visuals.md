# 13b — Projectile visuals (rocket, plasma) and hitscan tracers

**Milestone:** M5 First real map + characters
**Agent:** dev
**Status:** todo
**Depends on:** 13 (weapon/item models, rocket flight light), 18c (`UVeloxWeaponCosmetics`, `UVeloxAssetManager::LoadWeaponCosmetics`), ADR 0006 E1

## Goal
Task 13 found that `AVeloxProjectile` has no mesh: a flying rocket shows only its flight light and a plasma ball shows nothing. A player must see incoming rockets and plasma, and the rail/MG/shotgun shots need a readable trace (readability is non-negotiable 2). When this is done, every projectile has a low-poly, bright, unlit mesh with a short trail, and hitscan weapons draw a short-lived beam/tracer from the muzzle to the impact, all cosmetic, client-only, never loaded on a dedicated server.

## Scope — files/modules the agent may touch
- `Tools/blender/weapons_items.py` — projectile meshes (rocket, plasma ball), manifest entries
- `Tools/editor/import_art.py` (`--static`), `Tools/editor/create_cosmetics_content.py` — import and wire into the weapon cosmetics
- `Source/Velox/Core/VeloxWeaponCosmetics.h/.cpp` — soft fields for projectile mesh, trail/tracer material, tracer width/lifetime
- `Source/Velox/Weapons/VeloxProjectile.h/.cpp` — a cosmetic mesh component, set from the resident cosmetics (`Get()` only, no `LoadSynchronous`), real and fake (predicted) projectiles alike
- `Source/Velox/Weapons/VeloxCombatFeedbackComponent.h/.cpp` — hitscan tracer spawn on the existing Fire/impact cues
- `Source/Velox/Tests/VeloxArtContentTests.cpp` — rows for the new assets
- `Content/Weapons/**`, `ArtSource/**` — the new assets (LFS)
- this task file

## Relevant ADRs / references
- `docs/adr/0006-first-map-and-characters.md` (§4 readability, E1.3)
- `docs/adr/reviews/0006-cosmetics-review.md` (D4: one resident async load)
- `docs/adr/0003-*.md` (projectile prediction: fake projectiles must look identical)
- `docs/art/weapons-spec.md`, `docs/art/style-guide.md`

## Acceptance criteria
- [ ] Rocket and plasma meshes ≤ 150 tris each, unlit/emissive, bright against the concrete palette; both real and fake projectiles show the mesh (`VeloxProjectileMesh seq= fake=0|1 mesh=` Verbose line on the client).
- [ ] Rail, MG, shotgun, lightning: a tracer/beam from the muzzle socket to the impact point, lifetime from data (rail ~0.5 s, MG/SG ~0.05 s); LG keeps its existing beam if one exists, otherwise gets one.
- [ ] No `LoadSynchronous` in the projectile or tracer paths; the dedicated-server log has no projectile-mesh or tracer lines (the nettest server-log gate from 18b passes).
- [ ] No movable lights beyond `MaxMovableLights`.
- [ ] `infra/ci/build.sh` green (test count ≥ current + new rows), `fts.sh` OK, `nettest.sh --scripts FireRun,RocketJumpRun,BotRun --repeat 1` PASS.
- [ ] LFS growth ≤ 5 MB.

## Verification steps
1. Blender: `blender -b --factory-startup --python Tools/blender/weapons_items.py -- …` → manifest includes the projectile meshes.
2. `import_art.py --static ArtSource/Export --verify` exit 0.
3. `infra/ci/build.sh` → CI OK.
4. `nettest.sh --scripts FireRun,RocketJumpRun,BotRun --repeat 1` under the soak lock → NET SOAK OK; client log shows projectile-mesh and tracer lines, server log none.

## Report back
Standard format; note that nothing was viewed on a real render (Mac playtest item).

## Notes / outcome
(Filled by the agent.)
