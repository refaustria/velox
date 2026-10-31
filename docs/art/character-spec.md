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
`add_leaf_bones=False`, mesh + armature only, `mesh_smooth_type=FACE`, no animation. Animation FBX files (task 12) use the same settings with armature only and `bake_anim=True`.

## Animation clips (task 12, `Tools/blender/character.py --animations`)

Thirteen procedural clips, 60 fps, armature only, `root` never moves, one FBX each in `ArtSource/Export/A_Velox_<Name>.fbx`, imported
by `Tools/editor/import_art.py --animations` as `/Game/Characters/Velox/Anims/A_Velox_<Name>` on `SKEL_Velox`:

| Clip | Length | Loop | Notes |
|---|---|---|---|
| `Idle` | 2.0 s | yes | breathing, weapon arms ready |
| `Run_Fwd` / `Run_Back` / `Run_Left` / `Run_Right` | 0.6 s | yes | reference cadence at 800 cm/s, legs by two-bone IK (planted feet), arms counter-swing, torso lean 5 deg towards the travel direction |
| `Jump_Start` | 0.15 s | no | idle to the first Air_Loop pose |
| `Air_Loop` | 0.5 s | yes | |
| `Land` | 0.2 s | no | air pose, deep crouch at 40 %, idle pose at the end |
| `Crouch_Idle` | 2.0 s | yes | pelvis 20 cm down (capsule half height 70 to 50) |
| `Crouch_Walk` | 0.8 s | yes | forward only; the 4-way pick is not used crouched |
| `Fire_Upper` | 0.15 s | no | additive, `AAT_LocalSpaceBase`, base = own frame 0 (the reference pose); upper-body bones only |
| `Death_A` / `Death_B` | 0.8 s | no | fall backwards / forwards onto the floor, end pose within the capsule footprint + 40 cm, hold the last frame |

Facing: the Blender -Y forward axis imports as +Y, so the mesh needs `MeshYawDeg -90`, which `DA_Cosmetics_Velox` already has.

## ABP_Velox state machine (Mac only; built in the editor, about 30 minutes)

Until it exists `UVeloxCharacterCosmetics::bUseSequencePlayback` plays the clips above directly with hard cuts
(`AVeloxCharacter::PlaySequenceForState`), and the game is complete without the Blueprint; `ABP_Velox` only adds blends and the fire slot.

**Create:** Content Browser, `/Game/Characters/Velox`, Add, Animation, Animation Blueprint, parent class `VeloxAnimInstance`, skeleton `SKEL_Velox`,
name `ABP_Velox`. Open the AnimGraph. Then set `DA_Cosmetics_Velox` `AnimClass = ABP_Velox` (with an AnimClass the sequence playback is off).

**Variables** (all set by C++ in `NativeUpdateAnimation`, read-only in the graph): `LegState` (`EVeloxLegState`), `RunDirection`
(`EVeloxRunDirection`, 4-way pick already made), `PlayRate` (Speed2D / 800 clamped 0.5 to 1.6 for Run and CrouchWalk, else 1), `bFiring`
(true for one update after a Fire cue), `bDead`, `AimPitch`; `Speed2D`, `DirectionDeg`, `bAirborne`, `bCrouching` are available but the
transitions below do not need them (C++ already decided).

**Graph:** `State Machine "Legs"` -> `Slot "UpperBody"` -> `Output Pose`. The slot node is inserted after the state machine.

**States of "Legs"** (entry state `Idle`; one transition per row, each rule is a comparison of `LegState` with the state's own value):

| State | Plays | Loop | Play rate | Transitions in (rule, blend) |
|---|---|---|---|---|
| `Idle` | `A_Velox_Idle` | yes | 1 | `LegState == Idle`, 0.10 s |
| `Run` | `Blend Poses by Enum` on `RunDirection`: `Fwd` -> `A_Velox_Run_Fwd`, `Back` -> `Run_Back`, `Left` -> `Run_Left`, `Right` -> `Run_Right` (blend time 0.05 s, sync group `Run`) | yes | `PlayRate` | `LegState == Run`, 0.10 s |
| `JumpStart` | `A_Velox_Jump_Start` | no | 1 | `LegState == JumpStart`, 0.05 s |
| `Air` | `A_Velox_Air_Loop` | yes | 1 | `LegState == Air`, 0.05 s |
| `Land` | `A_Velox_Land` | no | 1 | `LegState == Land`, 0.05 s |
| `CrouchIdle` | `A_Velox_Crouch_Idle` | yes | 1 | `LegState == CrouchIdle`, 0.10 s |
| `CrouchWalk` | `A_Velox_Crouch_Walk` | yes | `PlayRate` | `LegState == CrouchWalk`, 0.10 s |
| `Dead` | `A_Velox_Death_A` (or `Death_B`, pick per life with a `Random Sequence Player` on both), "hold last frame" (loop off) | no | 1 | `LegState == Dead`, 0 s; no transition out (a corpse stays) |

Every state can go to every other state (use a transition from each state to each of the others with the rule above, or one
"conduit" state that fans out on `LegState`); leaving `Dead` is never allowed.

**Fire slot (additive):** `A_Velox_Fire_Upper` is already an additive sequence (local-space, base = its frame 0, which is the reference
pose). Make a Montage `AM_Velox_Fire` from it with slot `DefaultGroup.UpperBody` (blend in 0.02 s, blend out 0.08 s). In the AnimGraph the
chain is `Legs` state machine -> `Slot 'UpperBody'` -> `Output Pose`; the slot node plays the montage additively on top of the leg state.
In the Event Graph (`Event Blueprint Update Animation`) branch on `bFiring` and call `Montage Play` on `AM_Velox_Fire` (restart it on every
shot: it is 0.15 s long and overlaps itself at the MG and plasma fire rates, which is intended). If the slot output looks doubled, put an
`Apply Additive` node (alpha 1) between the montage pose and the state machine pose instead of relying on the slot's own blending.

**Blend times summary:** locomotion transitions 0.10 s, jump/air/land transitions 0.05 s, death 0 s, run direction crossfade 0.05 s.

**Checks in PIE** (Mac): run a 4-way square at full speed, strafe-jump (Air for the whole jump, Land only on touch-down), crouch while
running (CrouchWalk), get killed (Dead holds). Compare against sequence playback with `AnimClass` empty: the same clips, no blends.

## What the human may replace later

The agent-built body and its procedural clips can be swapped for a Manny/Quinn-based set (decision 33): import Manny clips and retarget
through an IK Rig onto `SKEL_Velox`, or replace `SK_Velox` with a mesh that keeps the bone names, sockets and the 4-channel mask.
