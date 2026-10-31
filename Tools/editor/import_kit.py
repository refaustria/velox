"""Velox kit mesh import (ADR 0006 sections 2, 5, 8; task 14): the `--kit` path of the art importer.

Lives in its own module so it cannot collide with task 11's `import_art.py`; that script dispatches `--kit <dir>` here
(`import_kit.run(dir, verify=..., fixture=...)`). Standalone use, headless:
  UnrealEditor-Cmd Velox.uproject -run=pythonscript \
      -script="$PWD/Tools/editor/import_kit.py --kit ArtSource/Export [--verify] [--fixture]" \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
(or set VELOX_KIT_ARGS="--kit ... --verify"). Run create_kit_materials.py first (the MI_Kit_* must exist).

  --kit <dir>   import every SM_Kit_*.fbx listed in <dir>/kit_manifest.json to /Game/Environment/Kit/Meshes with
                bGenerateLightmapUVs, DstLightmapIndex 1, MinLightmapResolution 64, the per-piece LightMapResolution
                from the manifest, one material slot (MI_Kit_*) and NoCollision (the clip boxes carry all collision).
  --verify      assert per piece: UV channels == 2, triangles <= 600 (and == manifest), one material slot, NoCollision,
                lightmap resolution/index, <= 25 pieces. Also runs after --kit.
  --fixture     self-test of the import path: exports /Engine/BasicShapes/Cube, imports it through the same code under
                /Game/Environment/Kit/_Fixture, verifies, deletes it again.
Fails (exception) on any problem.
"""
import json
import os
import shlex
import sys

import unreal

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ERRORS = []

MESH_DIR = "/Game/Environment/Kit/Meshes"
MAT_DIR = "/Game/Environment/Kit/Materials"
FIXTURE_DIR = "/Game/Environment/Kit/_Fixture"
MAX_PIECES = 25
MAX_TRIS = 600
MIN_LIGHTMAP_RES = 64


def log(msg):
    unreal.log("[VeloxKitImport] " + msg)


def fail(msg):
    unreal.log_error("[VeloxKitImport] " + msg)
    ERRORS.append(msg)


def mesh_subsystem():
    return unreal.EditorStaticMeshLibrary


def triangle_count(mesh):
    """Triangles of LOD 0 as built (render data), via the editor subsystem."""
    return mesh.get_num_triangles(0)


def import_fbx(fbx_path, dest_dir, name):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    data = ui.static_mesh_import_data
    data.set_editor_property("combine_meshes", True)
    data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("generate_lightmap_u_vs", True)
    data.set_editor_property("remove_degenerates", True)
    data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    data.set_editor_property("normal_generation_method", unreal.FBXNormalGenerationMethod.MIKK_T_SPACE)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx_path)
    task.set_editor_property("destination_path", dest_dir)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    ATOOLS.import_asset_tasks([task])
    mesh = EAL.load_asset(f"{dest_dir}/{name}")
    if mesh is None:
        fail("import failed: " + fbx_path)
    return mesh


def configure(mesh, lightmap_res, material):
    sub = mesh_subsystem()
    bs = sub.get_lod_build_settings(mesh, 0)
    bs.set_editor_property("generate_lightmap_u_vs", True)
    bs.set_editor_property("min_lightmap_resolution", MIN_LIGHTMAP_RES)
    bs.set_editor_property("src_lightmap_index", 0)
    bs.set_editor_property("dst_lightmap_index", 1)
    sub.set_lod_build_settings(mesh, 0, bs)
    mesh.set_editor_property("light_map_resolution", lightmap_res)
    mesh.set_editor_property("light_map_coordinate_index", 1)
    sub.remove_collisions(mesh)
    body = mesh.get_editor_property("body_setup")
    inst = body.get_editor_property("default_instance")
    inst.set_editor_property("collision_profile_name", "NoCollision")
    body.set_editor_property("default_instance", inst)
    if material is not None:
        mesh.set_material(0, material)
    if not EAL.save_loaded_asset(mesh, False):
        fail("could not save " + mesh.get_path_name())


def verify_mesh(mesh, expect_tris, lightmap_res, max_slots=1, expect_bounds=None, expect_uv=2):
    n = mesh.get_name()
    sub = mesh_subsystem()
    uv = mesh.get_num_tex_coords(0)  # render data: UV0 from the FBX + the generated lightmap UV
    if uv != expect_uv:
        fail(f"{n}: {uv} UV channels, expected {expect_uv} (UV0 material, UV1 generated lightmap)")
    tris = triangle_count(mesh)
    if tris > MAX_TRIS:
        fail(f"{n}: {tris} triangles > {MAX_TRIS}")
    if expect_tris is not None and tris != expect_tris:
        fail(f"{n}: {tris} triangles, manifest says {expect_tris}")
    slots = len(mesh.get_editor_property("static_materials"))
    if slots != max_slots:
        fail(f"{n}: {slots} material slots, expected {max_slots}")
    bs = sub.get_lod_build_settings(mesh, 0)
    if not (bs.get_editor_property("generate_lightmap_u_vs") and bs.get_editor_property("dst_lightmap_index") == 1
            and bs.get_editor_property("min_lightmap_resolution") == MIN_LIGHTMAP_RES):
        fail(f"{n}: lightmap build settings wrong")
    if mesh.get_editor_property("light_map_resolution") != lightmap_res or mesh.get_editor_property("light_map_coordinate_index") != 1:
        fail(f"{n}: LightMapResolution/CoordinateIndex wrong")
    prof = mesh.get_editor_property("body_setup").get_editor_property("default_instance").get_editor_property("collision_profile_name")
    if str(prof) != "NoCollision":
        fail(f"{n}: collision profile {prof}")
    if expect_bounds is not None:  # the pivot / axis convention of kit-spec.md survived the FBX round trip
        bb = mesh.get_bounds()
        lo = [bb.origin.x - bb.box_extent.x, bb.origin.y - bb.box_extent.y, bb.origin.z - bb.box_extent.z]
        hi = [bb.origin.x + bb.box_extent.x, bb.origin.y + bb.box_extent.y, bb.origin.z + bb.box_extent.z]
        if any(abs(a - b) > 0.5 for a, b in zip(lo + hi, list(expect_bounds[0]) + list(expect_bounds[1]))):
            fail(f"{n}: bounds {[round(v, 1) for v in lo]}..{[round(v, 1) for v in hi]} != manifest {expect_bounds}")
    agg = mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
    shapes = sum(len(agg.get_editor_property(k)) for k in ("sphere_elems", "box_elems", "sphyl_elems", "convex_elems"))
    if shapes:
        fail(f"{n}: {shapes} simple collision shapes")
    log(f"verify {n}: tris={tris} uv={uv} slots={slots} lm={lightmap_res}")
    return tris


def run(kit_dir, verify=True, fixture=False):
    ERRORS.clear()
    if fixture:
        run_fixture()
    if kit_dir:
        kit_dir = os.path.abspath(kit_dir)
        with open(os.path.join(kit_dir, "kit_manifest.json")) as fh:
            manifest = json.load(fh)
        pieces = manifest["pieces"]
        if len(pieces) > MAX_PIECES:
            fail(f"{len(pieces)} pieces > {MAX_PIECES}")
        if not EAL.does_directory_exist(MESH_DIR):
            EAL.make_directory(MESH_DIR)
        total = 0
        for p in pieces:
            mi = EAL.load_asset(f"{MAT_DIR}/{p['material']}")
            if mi is None:
                fail(f"missing {p['material']} (run create_kit_materials.py first)")
            mesh = import_fbx(os.path.join(kit_dir, p["fbx"]), MESH_DIR, p["name"])
            if mesh is None:
                continue
            configure(mesh, p["lightmap_res"], mi)
            if verify:
                total += verify_mesh(mesh, p["tris"], p["lightmap_res"], expect_bounds=(p["bounds_min"], p["bounds_max"]))
        log(f"kit: {len(pieces)} pieces, {total} triangles verified={verify}")
    if ERRORS:
        raise RuntimeError("import_kit failed: " + "; ".join(ERRORS))


def run_fixture():
    """Cube -> FBX -> the same import/configure/verify path -> delete."""
    scratch = os.path.join(unreal.Paths.project_saved_dir(), "KitFixture")
    os.makedirs(scratch, exist_ok=True)
    fbx = os.path.join(scratch, "SM_Kit_Fixture.fbx")
    if EAL.does_directory_exist(FIXTURE_DIR):
        EAL.delete_directory(FIXTURE_DIR)
    EAL.make_directory(FIXTURE_DIR)
    # The engine cube already carries a lightmap UV set of its own, so the import must add exactly one more channel
    # (the generated one); kit meshes from Blender have UV0 only and must end with exactly 2.
    src = EAL.duplicate_asset("/Engine/BasicShapes/Cube", FIXTURE_DIR + "/Cube_Src")
    src_uvs = src.get_num_tex_coords(0)
    log(f"fixture source has {src_uvs} UV channel(s)")
    exp = unreal.AssetExportTask()
    exp.set_editor_property("object", src)
    exp.set_editor_property("filename", fbx)
    exp.set_editor_property("automated", True)
    exp.set_editor_property("replace_identical", True)
    exp.set_editor_property("exporter", unreal.StaticMeshExporterFBX())
    if not unreal.Exporter.run_asset_export_task(exp) or not os.path.exists(fbx):
        fail("fixture export failed")
        return
    mesh = import_fbx(fbx, FIXTURE_DIR, "SM_Kit_Fixture")
    if mesh is not None:
        configure(mesh, 64, None)
        verify_mesh(mesh, None, 64, max_slots=1, expect_uv=src_uvs + 1)
        log("fixture ok (cube imported with generated lightmap UVs)")
    EAL.delete_directory(FIXTURE_DIR)


def parse_args():
    argv = list(sys.argv[1:])
    env = os.environ.get("VELOX_KIT_ARGS")
    if env:
        argv += shlex.split(env)
    kit = None
    if "--kit" in argv:
        kit = argv[argv.index("--kit") + 1]
    return kit, "--verify" in argv or kit is not None, "--fixture" in argv


if __name__ == "__main__":
    _kit, _verify, _fixture = parse_args()
    run(_kit, _verify, _fixture)
