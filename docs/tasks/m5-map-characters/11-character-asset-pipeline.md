# 11 — Character asset pipeline: Blender script, FBX export, headless import, skins

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** todo
**Depends on:** 08 (cosmetics asset to fill), human question 1 (Blender on the VM?) and 3 (skeleton source); ADR 0006 §4.1–4.3

## Goal
`Tools/blender/character.py` builds the stylised Velox character (≤ 6 k triangles, 140 cm, inside the capsule) rigged to the UE5 Mannequin bone names, with the 4-channel skin mask, and exports `ArtSource/Export/SK_Velox.fbx`. `Tools/editor/import_art.py --character` imports it headless, creates `M_Character_Master` and the six `MI_Skin_*`, fills `DA_Cosmetics_Velox`, and verifies bone count, sockets and budget. If Blender is on no host, the task ends with the script, `docs/art/character-spec.md`, and a verified import path using a fixture.

## Scope — files/modules the agent may touch
- `Tools/blender/export_common.py`, `Tools/blender/character.py` (new), `Tools/editor/import_art.py` (new, `--character`, `--verify`), `Tools/editor/create_cosmetics_content.py` (skins, master material parameters), `.gitignore` (`*.blend1`)
- `ArtSource/Characters/velox_character.blend`, `ArtSource/Export/SK_Velox.fbx`, `ArtSource/Textures/T_Velox_Mask.png` (LFS), `Content/Characters/Velox/**` (LFS)
- `docs/art/style-guide.md` (palette rules from ADR 0006 §4.3), `docs/art/character-spec.md`

## Relevant ADRs / references
- ADR 0006 §4.1 (proportions, capsule, PA with one capsule), §4.2 (bone list, sockets, FBX settings, units), §4.3 (mask channels, master material, skin colours, HSV rules)
- `Tools/editor/README.md` (5.8 Python API notes); UE Python: `unreal.AssetImportTask`, `unreal.FbxImportUI`/Interchange, `unreal.SkeletalMesh`, `unreal.EditorSkeletalMeshLibrary`/`SkeletalMeshEditorSubsystem` (sockets), `unreal.MaterialEditingLibrary`

## Acceptance criteria
- [ ] `character.py` runs with `blender -b --python Tools/blender/character.py -- --out ArtSource/Export` (Blender 4.x; deterministic: no random seeds, saves the `.blend` with compression): body mesh ≤ 6 000 tris, height 140 cm, shoulders ≤ 75 cm, feet at Z 0, −Y forward; armature with the Mannequin names of ADR 0006 §4.2 (verified list printed), skin weights by automatic weights then clamped to 4 influences; UV0 for the mask, mask PNG written (R body, G trim, B skin/visor, A emissive strips).
- [ ] `import_art.py --character ArtSource/Export/SK_Velox.fbx` creates `SK_Velox`, `SKEL_Velox`, `PA_Velox` (one capsule body on `pelvis`, radius 37.5, half height 70), sockets `weapon_r` (hand_r), `foot_l`, `foot_r`, `eye`; imports `T_Velox_Mask` (sRGB off, no mips streaming); creates `M_Character_Master` (parameters `BodyColor`, `TrimColor`, `SkinColor`, `EmissiveStrength 0.35`) and `MI_Skin_Red/Blue/Green/Yellow/Orange/Cyan` with colours obeying HSV V ≥ 0.6, S ≥ 0.7 (asserted in the script); fills `DA_Cosmetics_Velox` (`SkeletalMesh`, `Skins`, `ViewModelSettings`); `--verify` reopens everything and asserts bone count ≤ 70, socket names, one material slot, triangle budget, and the LFS total.
- [ ] Without Blender on this host: the import path is still proven with a fixture — export `/Engine/EngineMeshes/SkeletalCube` or any engine skeletal mesh to FBX via `unreal.AssetExportTask`, re-import it through `import_art.py --character --fixture`, verify. State clearly in the report that the real character awaits a Blender run (and where: VM after question 1, or the human's Mac with the exact command).
- [ ] `docs/art/character-spec.md`: proportions, bone list, sockets, mask channel meaning, skin colour table (sRGB hex + HSV), export settings, the `ABP_Velox` state machine (task 12 extends it), and what the human must produce if the script is not run.
- [ ] Headless load: `L_ItemTest -game -nullrhi ?Bots=2` with the cosmetics set logs the mesh assigned and the skin applied for each pawn, no warnings. Build green.

## Verification steps
1. `which blender` → record. If present: run the script, `ls -la ArtSource/Export`.
2. `UnrealEditor-Cmd "$VELOX_PROJECT" -run=pythonscript -script="$PWD/Tools/editor/import_art.py" -nullrhi … -- --character <fbx> --verify` → exit 0 and the verify summary.
3. `git lfs ls-files | /usr/bin/grep -c Characters` and `du -sh Content/Characters ArtSource`.
4. `infra/ci/build.sh`; headless load as above.

## Report back
Standard format; whether Blender ran; the verify summary; the LFS size added.

## Notes / outcome
