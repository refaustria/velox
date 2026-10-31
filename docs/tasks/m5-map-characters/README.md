# M5 Map + characters — task index

Spec: `docs/adr/0006-first-map-and-characters.md` (ADR 0006). Template: `docs/tasks/TEMPLATE.md`. Evidence goes to `docs/adr/evidence/0006-*/`, reports to `notes/`.

| # | Task | Agent | Est. | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| 01 | [headless-lighting-probe](01-headless-lighting-probe.md) | dev | 2 h | ADR §5 | 0 | done — PATH A FAILS; bake on the Apple Silicon Mac (E0.1) |
| 02 | [map-library-refactor](02-map-library-refactor.md) | dev | 2–3 h | ADR §2 | 0 | done (maps committed by 03b) |
| 03 | [nav-coverage-lint](03-nav-coverage-lint.md) | dev-hard | 3 h | ADR §6, ADR 0005 D41 | 0 | done (deviations accepted, E0.2) |
| 03b | [test-maps-lint-baseline-and-regen](03b-test-maps-lint-baseline-and-regen.md) | dev | 2–3 h | 02, 03, E0.2 (e), E0.4 | 0b (parallel with 05) | todo |
| 04 | [soak-runner-map-override-and-bands](04-soak-runner-map-override-and-bands.md) | dev | 2–3 h | ADR §7 | 0 | done (findings ruled, E0.3) |
| 04b | [runner-per-instance-coverage-and-link-definition](04b-runner-per-instance-coverage-and-link-definition.md) | dev | 1–2 h | 04, E0.3 | 0b (before 06's final sets) | done |
| 04c | [runner-dwell-timed-wait-exclusion](04c-runner-dwell-timed-wait-exclusion.md) | dev | 2 h | 04b, E2, `notes/dwell-report.md` | 0b (before 06's final sets) | todo |
| 05 | [dm1-blockout](05-dm1-blockout.md) | dev | 3 h (may split: layout+map / FT+gating) | 02, 03, E0.2/E0.5 (final lint with 03b) | 1 | todo |
| 06 | [dm1-bot-iteration](06-dm1-bot-iteration.md) | dev | 3 h (+ soak time) | 04, 04b, 05 | 1 | todo |
| 07 | [dm1-band-ruling](07-dm1-band-ruling.md) | architect | 1 h | 06, E2 | 1 | todo |
| 08 | [character-cosmetics-and-anim-instance](08-character-cosmetics-and-anim-instance.md) | dev-hard | 3 h | ADR §4 | 2 (parallel with 1) | todo |
| 09 | [view-model-and-weapon-cosmetics](09-view-model-and-weapon-cosmetics.md) | dev | 3 h | 08 | 2 | todo |
| 10 | [audio-foundation](10-audio-foundation.md) | dev | 3 h (may split: content script / hooks+announcer) | 09 | 2 | todo |
| 11 | [character-asset-pipeline](11-character-asset-pipeline.md) | dev | 3 h | 08, Q1, Q3 | 3 | todo |
| 12 | [animation-set](12-animation-set.md) | dev | 2–3 h | 11 | 3 | todo |
| 13 | [weapon-and-item-models](13-weapon-and-item-models.md) | dev | 3 h | 09, 10, 11 | 3 | todo |
| 14 | [modular-kit](14-modular-kit.md) | dev | 3 h | 11, Q4 | 3 | todo |
| 15 | [dm1-art-pass](15-dm1-art-pass.md) | dev | 3 h | 07, 14 | 4 | todo |
| 16 | [lighting-bake-and-perf-audit](16-lighting-bake-and-perf-audit.md) | dev (A) / human + dev (B) | A 3 h; B bake 1–3 h on the Apple Silicon Mac | A: 05 (tool), 15; B: 15, the Mac (E0.1) | 4 | todo |
| 17 | [m5-playtest-checklist](17-m5-playtest-checklist.md) | dev | 1 h | 15, 16, 08–13 | 4 | todo |
| 18 | [architect-review-cosmetics](18-architect-review-cosmetics.md) | architect | 1–2 h | 08, 09, 10 merged | 4 | done (approve with follow-ups, E1) |
| 18b | [cosmetics-review-fixes](18b-cosmetics-review-fixes.md) | dev | 2–3 h | 18 (D1–D3, D5–D7); before 17 runs on the Mac | 4 | todo |
| 18c | [weapon-cosmetics-consolidation](18c-weapon-cosmetics-consolidation.md) | dev | 3 h | 18b, E1; before 13 fills the weapon meshes | 4 | todo |

## Wave plan (from ADR 0006 implementation notes)

- **Wave 0 — infrastructure, all four in parallel:** 01 answers the lighting question before anything depends on it; 02 gives the DM1 generator its library; 03 pays the nav-lint debt; 04 makes the soaks map-aware. No art, no new gameplay. **Done 2026-10-31; rulings in ADR 0006 E0.** Wave 0b (03b, 04b) closes the findings and runs in parallel with 05.
- **Wave 1 — the map as gameplay:** 05 blockout (gated by lint + FT), 06 bots iterate the layout (report-only sets, evidence), 07 architect fixes the DM1 bands as ADR 0006 E3 (E1 = cosmetics review, E2 = dwell rule from the 03b finding, task 04c).
- **Wave 2 — character/cosmetics code, parallel with wave 1:** 08 (dev-hard: replicated pawn/PlayerState surface), then 09 and 10. Everything works with empty soft references, so no art is needed.
- **Wave 3 — art pipeline, parallel tasks:** 11–14 each produce a Blender script, a spec document and a verified headless import path. If Blender is on no host they end as "script + spec + fixture-proven import"; the human then runs the scripts on the Mac (or approves `apt install blender` here) and the import is one command.
- **Wave 4 — assembly and acceptance:** 15 art pass (same soaks, same bands; fallback-C lighting, not in `MapsToCook`), 16 part A audit + tightened `LightingBuilt` + cook dry run on the VM, 16 part B the human's Production bake on the Apple Silicon Mac + `MapsToCook` + default map, 17 checklist, 18 review. M5 is done when the ROADMAP criteria hold, the bake is committed *and* the human signs the checklist on the Apple Silicon Mac.

Rules repeated for every dispatch: never touch `Movement/` or the lag-compensation path; cosmetics only via `TSoftObjectPtr`; soaks only under the soak lock on an idle host; keep ≥ 15 GB free; `.blend1` never committed; one task per agent invocation.

## Questions for the human (copied into `STATUS.md` as 31–40; **all decided 2026-10-31**, rulings in ADR 0006 E0.5 — kept here for the record)

1. **Install Blender on this VM?** `apt install blender` (Debian 13 ships 4.2 LTS; ≈ 1 GB with dependencies; runs headless with `blender -b`). Options: (a) yes — agents produce `.blend`/FBX themselves and the import is end-to-end here; (b) no — agents write scripts + specs, you run the scripts on the Mac (Blender 4.x, one command per script) and commit the FBX. **Recommendation: (a)**; disk stays above the 15 GB floor (21 GB free now).
2. **Art direction.** (a) Low-poly, flat-shaded, hard-edged "stylised brutalist" that scripts can generate (baked lighting does the beauty work); (b) textured, hand-made art by you/an artist later, agents only block out. **Recommendation: (a)** for M5 — readability first, and it is replaceable piece by piece (the kit is ≤ 25 meshes).
3. **Character skeleton and animations.** (a) Agent-built body rigged to the UE5 Mannequin bone names + procedural clips (ADR 0006 §4.2–4.4); (b) same body, but you export Manny/Quinn's animations from the Mac's ThirdPerson template (Epic content, allowed in UE projects) for retargeting; (c) a Mixamo/marketplace set. **Recommendation: (a) now, (b) as a drop-in upgrade** once the Mac is available.
4. **DM1 theme and name.** The layout is fixed (q3dm6-class, ADR 0006 §3); the dressing is free: (a) concrete and steel bunker, (b) gothic stone (q3 classic), (c) clean sci-fi. **Recommendation: (a)** — the kit is simplest and the neutral palette gives skins the most contrast. Please also give a display name.
5. **Audio sourcing.** (a) Synthesised placeholders now (task 10), replaced later by a CC0 pack you pick (e.g. Kenney, freesound CC0) — agents write the mapping; (b) you record/commission from the start. Announcer: `espeak-ng` placeholder (may I `apt install espeak-ng`, ~ 10 MB?) vs. recorded by you. **Recommendation: (a) + espeak-ng placeholder.**
6. **Enemy skin forcing default.** (a) On, enemies always `Green` (Q3 bright-skin culture; maximum readability); (b) off, players see each other's chosen skins. **Recommendation: (a)**; teams in M7 override it anyway.
7. **If the VM cannot bake (task 01 fails):** (a) you bake on the Mac with `build-mac.sh --bake` (Production, 1–3 h, then commit the `.umap`); (b) accept M5 with unbuilt lighting — the ADR forbids this and I advise against it. **Recommendation: (a).** Nothing to decide if task 01 succeeds.
8. **Powerups on DM1.** (a) Quad in FFA only, none in Duel (Q3 standard, ADR §3); (b) no powerups on map 1 at all; (c) Quad in both modes. **Recommendation: (a).**
9. **LFS and disk budget.** M5 adds ≤ 600 MB of binary assets (textures, WAV, `.blend`, FBX, baked map). OK? Alternative: 300 MB by dropping `.blend` sources from the repo (keep only FBX) — **not recommended**, the ROADMAP asks for Blender sources in LFS.
10. **Default map after acceptance.** Switch `ServerDefaultMap` (and the Mac's quick-launch) to `L_DM1`? **Recommendation: yes**; the test maps stay for the soaks.
