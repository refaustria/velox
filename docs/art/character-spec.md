# Velox character spec

Built by `Tools/blender/character.py` (Blender 4.3.2, headless), imported by `Tools/editor/import_art.py`. ADR 0006 section 4.

## Pipeline

```
blender -b --factory-startup --python Tools/blender/character.py -- --out ArtSource/Export
  -> ArtSource/Export/SK_Velox.fbx, ArtSource/Characters/velox_character.blend, ArtSource/Textures/T_Velox_Mask.png
UnrealEditor-Cmd Velox.uproject -run=pythonscript -script="$PWD/Tools/editor/import_art.py --character $PWD/ArtSource/Export/SK_Velox.fbx --verify" \
    -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
  -> /Game/Characters/Velox: SK_Velox, SKEL_Velox, PA_Velox, T_Velox_Mask, M_Character_Master, MI_Skin_*, DA_Cosmetics_Velox
```

`import_art.py --character --fixture` is a self-test of the import path with an engine skeletal mesh (no Blender needed).

## Proportions

140 cm standing (feet at Z 0, head top at Z 140), shoulder pads 40 cm wide, widest point (A-pose hands) about 63 cm, -Y forward,
+X is the character's left. Capsule 37.5 radius x 70 half height; the mesh component sits at Z -70 and yaw -90 (`DA_Cosmetics_Velox`).
About 900 triangles (budget 6000), one 1024 x 1024 mask texture, one material slot (`M_Character_Master`).

## Skeleton (26 bones, UE5 Mannequin names and hierarchy)

root > pelvis > spine_01 > spine_02 > spine_03 > spine_04 > spine_05 > neck_01 > neck_02 > head;
spine_05 > clavicle_l/r > upperarm > lowerarm > hand; pelvis > thigh_l/r > calf > foot > ball.
No fingers, no ik_* bones. Deform rig only, no control rig. Skin weights: rigid parts, linear two-bone blend across joints
(at most 2 influences, limit 4); computed in `character.py` because the body is disjoint solids.
Bone axes are Blender's, not Epic's; Manny clips therefore retarget through an IK Rig (identity retarget is not promised).

## Sockets (on SK_Velox)

| Socket | Bone | Note |
|---|---|---|
| `weapon_r` | `hand_r` | offset zero; the human tunes it when the weapon meshes (task 13) are seen in hand |
| `foot_l`, `foot_r` | `ball_l`, `ball_r` | footstep/decal anchors |
| `eye` | `head` | offset zero, for later |

## Mask texture (UV0, `T_Velox_Mask`, sRGB off, TC_Masks, no mips, never stream)

UV space is four vertical bands (flip-proof): R u 0-0.25 body, G 0.25-0.5 trim (boots, belt, backpack, shoulder pads), B 0.5-0.75 skin/visor
(visor, neck, hands), A 0.75-1 emissive strips (chest, back, thigh, calf).

## Skins (material instances of M_Character_Master)

Order = `PlayerColorPalette` order. ADR 0006 names Orange as fifth skin; the code palette entry is Magenta, so Magenta ships (HUD colours stay stable) pending an architect ruling.
TrimColor = same hue, S 1.0, V 0.65. SkinColor = same hue, S 0.7, V 1.0.

| Skin | Body sRGB | Body H / S / V | Trim | Skin |
|---|---|---|---|---|
| Red | #FF2828 | 0 / 0.84 / 1.00 | #A60000 | #FF4D4D |
| Blue | #288CFF | 212 / 0.84 / 1.00 | #004DA6 | #4DA0FF |
| Green | #28FF46 | 128 / 0.84 / 1.00 | #00A617 | #4DFF65 |
| Yellow | #FFE61E | 53 / 0.88 / 1.00 | #A69300 | #FFEB4D |
| Magenta | #FF3CDC | 311 / 0.76 / 1.00 | #A60088 | #FF4DDF |
| Cyan | #1EF0F0 | 180 / 0.88 / 0.94 | #00A6A6 | #4DFFFF |

`M_Character_Master` parameters: `BodyColor`, `TrimColor`, `SkinColor` (vector), `EmissiveStrength` (scalar, 0.35), `PowerupTint` (rgb tint, a strength; set by the feedback component).
Base colour = R*Body + G*Trim + B*Skin + A*Body, lerped to PowerupTint. Emissive = base * lerp(EmissiveStrength, 1, A).

## FBX export settings

Scene unit scale 0.01 (1 BU = 1 cm); `apply_unit_scale`, `bake_space_transform`, axes -Y forward / Z up, `armature_nodetype=NULL`,
`add_leaf_bones=False`, mesh + armature only, `mesh_smooth_type=FACE`, no animation (animations are separate FBX files, task 12).

## ABP_Velox state machine (task 12 extends this; built in the editor)

Parent class `UVeloxAnimInstance`. Variables: `LegState`, `Speed2D`, `DirectionDeg`, `bAirborne`, `bCrouching`, `AimPitch`, `bFiring`, `bDead`.
States on `LegState`: Idle, Run (4-way pick Fwd/Back/Left/Right by DirectionDeg, +-45 deg quadrants, play rate Speed2D/800 clamped 0.5-1.6),
JumpStart (0.15 s, then Air), Air (loop), Land (0.2 s, then Idle/Run), CrouchIdle, CrouchWalk, Dead (Death_A/B, hold last frame).
Hard cuts (no blends) like Q3. Upper-body slot `Fire_Upper` layered on spine_02 when `bFiring`. Until the Blueprint exists,
`bUseSequencePlayback` plays the sequences directly.

## What the human may replace later

The agent-built body and its procedural clips can be swapped for a Manny/Quinn-based set (decision 33): import Manny clips and retarget
through an IK Rig onto `SKEL_Velox`, or replace `SK_Velox` with a mesh that keeps the bone names, sockets and the 4-channel mask.
