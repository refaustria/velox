# Velox art style guide

Source of truth: ADR 0006 sections 4.3 and 5. Low-poly, flat-shaded, concrete-and-steel environment (decision 32/34), bright players.

## Colour rule: the player is the most saturated thing in any frame

| | HSV value | HSV saturation |
|---|---|---|
| Environment (kit, map, props) | <= 0.50 | <= 0.25 |
| Player body, trim and skin/visor colours | >= 0.60 | >= 0.70 |

HSV is computed in sRGB space on the 0-255 colour. `Tools/editor/import_art.py` asserts the player rule for every
`MI_Skin_*` (BodyColor, TrimColor, SkinColor) and fails the import otherwise. Pickups and powerups may be brighter than the
environment but never use a skin hue at skin brightness on large surfaces.

## Shading

- Flat shading everywhere (face smoothing on export, no baked normal maps on characters).
- One material slot per character mesh; skins are material instances of `M_Character_Master` only.
- Emissive on characters is a floor, not a light: whole figure at `EmissiveStrength` (0.35) of its lit colour, strips at full.

## Light rules (summary of ADR 0006 section 5)

All lights `Static`, no `Stationary` light anywhere. The only movable lights are muzzle flashes and rocket lights
(`CastShadows false`, at most 2 concurrent). Static sky light with a small cubemap.

## Silhouette

Characters: 140 cm, big head (about 21 cm), visor on the front, trim-coloured boots, gloves-like hands and shoulder pads so the
outline reads from 40 m. Limbs may leave the 75 cm capsule by up to 15 cm in animation extremes.
