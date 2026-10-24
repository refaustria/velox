#!/usr/bin/env python3
"""Dependency-aware trimming of Engine/Plugins in an installed UE tree (stdlib only).

Default mode is READ-ONLY: scans, writes a markdown report, never deletes.
  trim-plugins.py [--ue-root /opt/UnrealEngine-5.8] [--uproject Velox.uproject]
                  [--report infra/engine/TRIM-REPORT.md]
  trim-plugins.py --apply --yes --restore-list deleted.txt   # actually deletes the SAFE list
  trim-plugins.py --prune-platforms Android,IOS,VisionOS,TVOS  # also mark Engine/Platforms/<X> (deleted only with --apply --yes)
  trim-plugins.py --verify-deps [--manifest DIR]  # plugin deps + Build.cs module refs + readelf NEEDED of kept .so (exit 1 on violations)
  trim-plugins.py --check-orphans   # platform-extension plugins without parent; exit 1 if any (also run after --apply)
Platform-extension plugins (Engine/Platforms/<P>/Plugins/**/<Name>_<P>.uplugin) are deleted together with
their parent plugin; UBT aborts otherwise ("Unable to find parent plugin ...").

Categories:
  KEEP        enabled by default / enabled in .uproject / transitive hard dependency of those
  UNREFERENCED  not enabled by default, not enabled by project, not required by any kept plugin
                -> safe to delete (the engine never loads it unless a project enables it)
  AGGRESSIVE-SAFE   kept only because EnabledByDefault, matches DENYLIST, and no kept plugin outside
                the deletion set hard-depends on it
  AGGRESSIVE-BLOCKED  matches DENYLIST but kept plugins depend on it (list shown); to remove it you
                must delete/disable the dependants too (or accept it)
"""
import argparse, json, os, re, shutil, subprocess, sys

# ---- EDIT ME -------------------------------------------------------------------------------
# Regexes matched (re.search) against plugin name for EnabledByDefault-but-irrelevant plugins.
DENYLIST = [
    r"^MetaHuman", r"Datasmith", r"^USD", r"GeoReferencing", r"PixelStreaming", r"^NNE", r"^NNERuntime",
    r"TextToSpeech", r"OpenXR|OculusXR|SteamVR|^XR|HoloLens|^OpenXR|LiveLink.*XR|^MagicLeap|^Oculus|^AR",
    r"^AndroidMedia", r"^AvfMedia", r"^WmfMedia", r"^ImgMedia", r"^WebMMedia", r"^MediaPlayerEditor",
    r"^Paper2D", r"^ModelingTools", r"^GeometryProcessing", r"^MeshModeling", r"^Bridge$", r"^Fab$",
    r"^HairStrands", r"^GroomPlugin", r"^ChaosCloth", r"^ChaosVehicles", r"^ChaosNiagara", r"^ChaosSolver",
    r"^ChaosUserDataPT", r"^ChaosCaching", r"^FullBodyIK", r"^ControlRig", r"^IKRig", r"^MovieRender",
    r"^LiveLink", r"^Cascade|^AlembicHairImporter|^AlembicImporter|^GeometryCache", r"^Substrate",
    r"^PCG", r"^Interchange.*Assets|^InterchangeTests", r"^ProceduralMeshComponent$", r"^Water$",
    r"^Landmass", r"^Takes$(?!)", r"^MobileLauncher|^AndroidDeviceProfile|^AndroidPermission|^IOSDeviceProfile",
    r"^OnlineSubsystemIOS|^OnlineSubsystemGoogle|^OnlineSubsystemFacebook|^Apple|^GoogleCloud",
    r"^WebBrowser|^ExampleDeviceProfileSelector|^Mutable|^CustomizableObject", r"^AnimationLocomotion|^MotionWarping$(?!)",
    r"^SpeedTree|^Houdini|^Substance|^Volumetric|^Sequencer.*Scripting$(?!)", r"^Chooser$(?!)",
    r"^CameraCalibration|^Compositing|^nDisplay|^ICVFX|^Remote.*Control.*(Web|Protocol)|^RemoteControl",
    r"^StateTree$(?!)", r"^NiagaraSimCaching$(?!)", r"^ProxyLODPlugin|^MeshPainting|^TextureFormat|^ColorGrading",
    r"^PlatformCrypto$(?!)", r"^OodleNetwork$(?!)", r"^AudioCapture$(?!)", r"^Wwise|^FMOD",
    r"^CLionSourceCodeAccess|^VisualStudio|^XCodeSourceCodeAccess|^RiderSourceCodeAccess$(?!)|^DumpGPUServices|^N10XSourceCodeAccess|^KDevelop|^NullSourceCodeAccess$(?!)",
]
# Names that must never be proposed (project needs them); also root plugins.
NEVER_DELETE = {
    "GameplayAbilities", "GameplayTags", "GameplayTasks", "EnhancedInput", "Niagara", "MetaSound",
    "AudioSynesthesia", "OnlineSubsystem", "OnlineSubsystemNull", "OnlineSubsystemUtils", "OnlineSubsystemSteam",
    "OnlineSubsystemEOS", "OnlineServicesEOS", "EOSShared", "SocketSubsystemEOS", "PythonScriptPlugin",
    "EditorScriptingUtilities", "FunctionalTesting", "AutomationController", "ChaosMover", "Takes",
    "ModelingToolsEditorMode$",
}
# Extra roots (plugins the project plans to use in later milestones), merged with uproject roots.
PROTECTED_OPTIONAL = {"CommonUI", "GameFeatures", "ModularGameplay", "ModularGameplayActors", "AudioModulation",
                      "OnlineServices", "OnlineServicesNull", "OnlineServicesOSSAdapter", "OnlineFramework",
                      "NetworkPrediction", "Chooser", "StateTree", "GameplayStateTree", "SmartObjects",
                      "AnimationWarping", "MotionWarping", "PoseSearch", "IKRig", "LiveLink"}  # Lyra-adjacent; keep until decided
EXTRA_ROOTS = PROTECTED_OPTIONAL | {"Niagara", "Metasound", "MetaSound", "OnlineSubsystemSteam", "OnlineSubsystemEOS", "OnlineServicesEOS",
               "FunctionalTesting", "GameplayTags", "GameplayTasks", "Takes"}
# --------------------------------------------------------------------------------------------

VERDICTS = """## Verdicts for non-plugin directories (manual, evidence from grep of `*.Build.cs` on 2026-10-24)

Nothing below is touched by the script. Confidence: H = checked directly, M = inferred, L = guess.
The engine is an *installed build*: UBT compiles Velox against prebuilt `Binaries/` and generated headers in
`Intermediate/Build/<Platform>/<arch>/UnrealGame/Inc`, so those are NOT disposable like in a source build.

| Path | Size | Needed? | Evidence / caveat | Conf. |
|---|---|---|---|---|
| `Engine/Binaries/ThirdParty/CEF3` | 2.0 GB | Not for the game or a dedicated server. Editor: only the `WebBrowser` module uses it (`WebBrowser.Build.cs` L96-97, `CEF3Utils`); `MainFrame` and 3 plugins link WebBrowser. Removing the dir risks editor load failures (Fab/launcher panels, tutorials) unless the module fails gracefully. | Test on a copy first; do not delete blindly. | M |
| `Engine/Binaries/ThirdParty/DotNet/10.0/linux-arm64` | 0.66 GB | Not on an x64 host: UBT/RunUAT use the host-platform runtime (`linux-x64`, 0.63 GB, keep). | Inferred from layout; verify `Engine/Build/BatchFiles/Linux/GetDotnetPath.sh`. | M |
| `Engine/Binaries/ThirdParty/Vulkan/{Android,Mac,Win64}` | 0.14 GB | Not on Linux builds (keep `Linux`). | Platform names. | H |
| `Engine/Binaries/ThirdParty/TechSoft` | 0.37 GB | Only referenced by Datasmith CAD (`CADKernel*`, `DatasmithCADImporter` Build.cs). Not needed once Datasmith is removed; but `CADKernelEngine` lives in `Engine/Source/Runtime/Datasmith`, check nothing else links it. | grep shows only Datasmith users. | M |
| `Engine/Binaries/ThirdParty/ShaderConductor`, `Python3`, `ICU` | 0.35/0.18/0.06 GB | KEEP. Shader compile (Vulkan/SPIR-V), PythonScriptPlugin, core. | | H |
| `Engine/Binaries/ThirdParty/OpenXR` | 23 MB | Not needed (no XR); keep unless OpenXR plugin deleted -- it is in the safe list already. | | M |
| `Engine/Binaries/Android`, `Engine/Intermediate/Build/Android` | 2.1 GB + 2.6 GB | Not needed (no Android target). These are prebuilt installed-build artifacts for the Android target only. Largest hidden win, but only the installer/owner can say if the build is ever re-run for Android. | Directory names; no Linux target references them. | M |
| `Engine/Binaries/LinuxArm64`, `Engine/Intermediate/Build/LinuxArm64` | 1.1 GB + 1.3 GB | Not needed on an x64 host unless an arm64 dedicated server is planned. | Same. | M |
| `Engine/Intermediate/Build/Linux/x64/UnrealGame` | 1.3 GB | KEEP. Holds generated headers/`.modules` for compiling game modules against the installed engine. | Installed-build layout. | M |
| `Engine/Binaries/Linux/UnrealGame-Linux-DebugGame` | 0.44 GB | Only if you build/run DebugGame configurations. Optional; keep `UnrealGame` and `-Shipping`. | | M |
| `Engine/Binaries/DotNET/AutomationTool/AutomationScripts` | 2.3 GB | KEEP: RunUAT BuildCookRun / automation. | | H |
| `Engine/Source/ThirdParty/Intel/Embree/Deploy` | 1.0 GB | Referenced by `NaniteBuilder`, `MeshUtilities` (Build.cs). Editor/cook needs Embree even with Nanite disabled in-game. KEEP. | | H |
| `Engine/Source/ThirdParty/Catch2` | 0.47 GB | Only `Catch2Extras`, `LowLevelTestsRunner`, `ChaosTestHarness` Build.cs. Not needed for game/editor/server builds. Probably safe, but Source/ThirdParty is part of the module graph (UBT enumerates Build.cs) -- deleting only the `v3.11.0` payload while leaving `.Build.cs` files is the conservative variant. | | M |
| `Engine/Source/ThirdParty/WebRTC` | 0.19 GB | Only WebRTC.Build.cs, PixelStreaming(+Player) -- PixelStreaming already removed. Payload removable, keep `.Build.cs`. | | M |
| `Engine/Source/ThirdParty/Boost`, `ICU`, `EOSSDK`, `Protobuf`, `HarfBuzz` | 0.09-0.2 GB each | KEEP (core / EOS in M6). | | M |
| `Engine/Content/Localization` | 0.88 GB | Editor UI translation data (tooltips, property names). Needed only for non-English editor/game text. English runs without; **L** confidence that nothing crashes. Deleting `Localization/{ToolTips,PropertyNames,Editor,Category,Keywords}` loses localized editor strings only. | | L |
| `Engine/Content/Internationalization` | 0.29 GB | ICU culture data. KEEP (cooked builds stage it). | | M |
| `Engine/Content/Editor`, `EngineMaterials`, `EngineSky`, `Engine_MI_Shaders` | 30-90 MB | KEEP (editor/engine default assets). `VREditor` (58 MB) is removable if the VREditor plugin is removed (not verified). | | M |
| `Engine/Intermediate` | 5.1 GB | See above; Linux/x64 part must stay. | | M |
| `Engine/DerivedDataCache/Compressed.ddp` | 1.1 GB | OPTIONAL. Pre-baked DDC for engine content; without it the editor/cook rebuilds shaders/textures on demand (slow first launches, more CPU, and local DDC growth on a disk that is already tight). Not needed for dedicated-server *runtime*. Keep unless desperate. | | M |
| `Engine/Extras/ThirdPartyNotUE/SDKs` | 1.5 GB | KEEP: contains the Linux clang toolchain (`HostLinux/.../v*_clang-*`). | See commit b662a7f. | H |
"""

def strip_json(text):
    out, i, n, instr = [], 0, len(text), False
    while i < n:
        c = text[i]
        if instr:
            out.append(c)
            if c == "\\": out.append(text[i+1]); i += 1
            elif c == '"': instr = False
        elif c == '"': instr = True; out.append(c)
        elif text.startswith("//", i):
            while i < n and text[i] != "\n": i += 1
            continue
        elif text.startswith("/*", i):
            i = text.find("*/", i) + 2; continue
        else: out.append(c)
        i += 1
    return re.sub(r",(\s*[}\]])", r"\1", "".join(out))

def load_json(p):
    """Parse a .uplugin/.uproject. UE accepts raw control characters (tabs) inside strings, so strict=False is
    mandatory (>200 engine .uplugin files have tabs in Description). BOM, // and /* */ comments and trailing
    commas are tolerated. Raises ValueError on failure -- callers must NOT treat that as 'no dependencies'."""
    with open(p, encoding="utf-8-sig", errors="replace") as f:
        t = f.read().lstrip("\ufeff")
    try: return json.loads(t, strict=False)
    except ValueError: return json.loads(strip_json(t), strict=False)

IGNORE_PARSE_ERRORS = False
PARSE_ERRORS = []

def dir_size(p):
    tot = 0
    for r, _, fs in os.walk(p):
        for f in fs:
            try: tot += os.lstat(os.path.join(r, f)).st_blocks * 512
            except OSError: pass
    return tot

def hs(b):
    for u in ("B", "KB", "MB", "GB"):
        if b < 1024 or u == "GB": return f"{b:.1f} {u}" if u != "B" else f"{b} B"
        b /= 1024

def scan(ue):
    plugs = {}
    PARSE_ERRORS.clear()
    proot = os.path.join(ue, "Engine", "Plugins")
    for r, ds, fs in os.walk(proot):
        ups = [f for f in fs if f.endswith(".uplugin")]
        if not ups: continue
        up = ups[0]; name = up[:-8]
        try: d = load_json(os.path.join(r, up))
        except Exception as e:
            PARSE_ERRORS.append((os.path.join(r, up), str(e)))
            print(f"ERROR: cannot parse {os.path.join(r, up)}: {e}", file=sys.stderr)
            if not IGNORE_PARSE_ERRORS: continue  # collected; scan() aborts below
            continue
        # nested plugins live in subdirs; size = own dir minus nested plugin dirs is overkill; compute full dir
        plugs[name] = dict(name=name, path=r, enabled_default=bool(d.get("EnabledByDefault", False)),
            deps=[(x["Name"], x.get("Enabled", True), bool(x.get("Optional", False))) for x in d.get("Plugins", [])],
            modules=[(m.get("Name"), m.get("Type"), m.get("LoadingPhase")) for m in d.get("Modules", [])],
            platforms=d.get("SupportedTargetPlatforms"), size=0)
        ds[:] = [x for x in ds]  # keep walking: nested plugins exist
    if PARSE_ERRORS and not IGNORE_PARSE_ERRORS:
        sys.exit(f"ABORT: {len(PARSE_ERRORS)} .uplugin file(s) could not be parsed (listed above). A plugin that cannot be "
                 "parsed would be treated as dependency-free and wrongly deleted. Fix the parser or pass --ignore-parse-errors.")
    # canonical (case-insensitive, like UE's plugin manager) dependency names
    lc = {n.lower(): n for n in plugs}
    for p in plugs.values():
        p["deps"] = [(lc.get(dn.lower(), dn), e, o) for dn, e, o in p["deps"]]
    # sizes: exclusive of nested plugin directories
    paths = {p["path"] for p in plugs.values()}
    for p in plugs.values():
        tot = 0
        for r, ds, fs in os.walk(p["path"]):
            ds[:] = [x for x in ds if os.path.join(r, x) not in paths]
            for f in fs:
                try: tot += os.lstat(os.path.join(r, f)).st_blocks * 512
                except OSError: pass
        p["size"] = tot
    return plugs

def scan_platform_ext(ue, plugin_names):
    """Platform-extension plugins: Engine/Platforms/<Platform>/Plugins/**/*.uplugin whose parent
    (file stem minus _<Platform>, else the directory name) is a plugin in Engine/Plugins.
    Returns list of dicts(path=dir, file, platform, name, parent, parent_exists)."""
    out = []
    base = os.path.join(ue, "Engine", "Platforms")
    if not os.path.isdir(base): return out
    for plat in sorted(os.listdir(base)):
        proot = os.path.join(base, plat, "Plugins")
        for r, _, fs in os.walk(proot):
            for f in fs:
                if not f.endswith(".uplugin"): continue
                stem = f[:-8]
                suffix = "_" + plat
                parent = stem[:-len(suffix)] if stem.endswith(suffix) else None
                if parent is None:
                    d = os.path.basename(r)
                    if d in plugin_names: parent = d
                    else: parent = stem  # unknown naming: treat stem as parent name
                out.append(dict(path=r, file=os.path.join(r, f), platform=plat, name=stem,
                                parent=parent, parent_exists=parent in plugin_names))
    return out

def find_orphans(ue):
    """Platform-extension plugins whose parent plugin is gone. Only names ending in _<Platform> count
    (those are the ones UBT requires a parent for)."""
    plugs = scan(ue)
    return [e for e in scan_platform_ext(ue, set(plugs))
            if e["name"].endswith("_" + e["platform"]) and not e["parent_exists"]]

def report_orphans(ue):
    orph = find_orphans(ue)
    if orph:
        print(f"ORPHANS: {len(orph)} platform-extension plugin(s) without parent plugin:")
        for e in orph: print(f"  {e['file']} (missing parent {e['parent']})")
    else:
        print("orphan check: 0 orphaned platform-extension plugins")
    return orph

def uproject_roots(path):
    en, dis = set(), set()
    if os.path.exists(path):
        for x in load_json(path).get("Plugins", []):
            (en if x.get("Enabled", True) else dis).add(x["Name"])
    return en, dis

def project_module_roots(plugs, srcdir):
    """Plugins whose modules are referenced from the project's *.Build.cs."""
    mod2plug = {m[0]: p["name"] for p in plugs.values() for m in p["modules"]}
    found = set()
    for r, _, fs in os.walk(srcdir):
        for f in fs:
            if f.endswith(".Build.cs") or f.endswith(".Target.cs"):
                t = open(os.path.join(r, f), errors="replace").read()
                for w in set(re.findall(r'"([A-Za-z0-9_]+)"', t)):
                    if w in mod2plug: found.add(mod2plug[w])
    return found

def closure(roots, plugs):
    # NOTE: a uproject "Enabled": false does NOT cut the dependency edge. If a kept plugin hard-requires X, UE refuses
    # to load that plugin when the project disables X, so X must stay (and the uproject entry is a conflict, see
    # disabled_conflicts). Cutting the edge here was the real cause of the HairStrands/ModelingToolsEditorMode deletion.
    seen, st = set(), list(roots)
    while st:
        n = st.pop()
        if n in seen or n not in plugs: continue
        seen.add(n)
        for dn, en, opt in plugs[n]["deps"]:
            if en and not opt: st.append(dn)
    return seen

def verify_deps(plugs, remaining):
    """Violations: (plugin, dep) for each remaining plugin with an Enabled:true, non-Optional dependency not in remaining."""
    return sorted((n, dn) for n in remaining for dn, e, o in plugs[n]["deps"] if e and not o and dn not in remaining)

def print_violations(title, v):
    print(f"verify-deps [{title}]: {len(v)} violation(s)")
    for n, dn in v: print(f"  {n} -> {dn} (hard dependency missing)")

# ---- module-level (Build.cs) and binary-level (readelf) verification --------------------------
# Module/library names that are legitimately undefined in an installed Linux build (platform-only, stripped,
# or absent from the installed build from the start). Matched case-insensitively. Extend as evidence arises.
ALLOWLIST = {
    "GRDK", "DiaSDK", "Catch2", "uLangCore", "uLangJSON", "LinuxTargetPlatformSettings",
}
DEP_FIELDS = r"(?:Public|Private)DependencyModuleNames|DynamicallyLoadedModuleNames|(?:Public|Private)IncludePathModuleNames"
_DEP_RE = re.compile(r"\b(" + DEP_FIELDS + r")\s*\.\s*(AddRange|Add)\s*\(")
_STR_RE = re.compile(r'"([A-Za-z0-9_\-\.]+)"')
_FS_CACHE = {}

def strip_cs_comments(t):
    out, i, n, instr = [], 0, len(t), False
    while i < n:
        c = t[i]
        if instr:
            out.append(c)
            if c == "\\" and i + 1 < n: out.append(t[i+1]); i += 1
            elif c == '"': instr = False
        elif c == '"': instr = True; out.append(c)
        elif t.startswith("//", i):
            while i < n and t[i] != "\n": i += 1
            continue
        elif t.startswith("/*", i):
            j = t.find("*/", i + 2); i = n if j < 0 else j + 2; continue
        else: out.append(c)
        i += 1
    return "".join(out)

_LINUX_RE = re.compile(r"\b(?:Linux|LinuxArm64|LinuxX64|Unix)\b")
_OTHER_PLAT_RE = re.compile(r"\b(?:Win64|Win32|Windows|Mac|Android|IOS|TVOS|VisionOS|Apple|Microsoft|Switch|PS4|PS5|Sony|"
                            r"XboxOne|Xbox|GDK|HoloLens|Lumin|Quail)\b")
_NEG_RE = re.compile(r"(?:!=\s*(?:Unreal(?:TargetPlatform|PlatformGroup)\s*\.\s*)?|!\s*[\w\.]*IsInPlatformGroup\s*\(\s*"
                     r"(?:UnrealPlatformGroup\s*\.\s*)?)$")

def _match_close(t, i, open_c, close_c):
    """t[i-1] is open_c; return index just after the matching close_c (strings respected)."""
    depth, instr, j = 1, False, i
    while j < len(t) and depth:
        c = t[j]
        if instr:
            if c == "\\": j += 1
            elif c == '"': instr = False
        elif c == '"': instr = True
        elif c == open_c: depth += 1
        elif c == close_c: depth -= 1
        j += 1
    return j

def platform_condition_kind(cond):
    """Heuristic for the condition of an `if (...)` in a Build.cs: 'other' if the guarded block is for non-Linux
    platforms only (positive mention of Win64/Mac/Android/GDK/... without a positive Linux/Unix mention, or `!= Linux`),
    else 'active'. Mixed or unrecognised conditions are 'active' (conservative: the reference counts)."""
    pos_lin = neg_lin = pos_oth = neg_oth = False
    for rx, is_lin in ((_LINUX_RE, True), (_OTHER_PLAT_RE, False)):
        for m in rx.finditer(cond):
            neg = bool(_NEG_RE.search(cond[:m.start()]))
            if is_lin: neg_lin |= neg; pos_lin |= not neg
            else: neg_oth |= neg; pos_oth |= not neg
    if pos_lin or neg_oth: return "active"
    if neg_lin or pos_oth: return "other"
    return "active"

def _if_blocks(t):
    """[(cond, body_start, body_end)] for every `if (cond) {...}` / `if (cond) stmt;` in comment-stripped text."""
    out = []
    for m in re.finditer(r"\bif\s*\(", t):
        ce = _match_close(t, m.end(), "(", ")")
        cond = t[m.end():ce - 1]
        k = ce
        while k < len(t) and t[k].isspace(): k += 1
        if k < len(t) and t[k] == "{": end = _match_close(t, k + 1, "{", "}")
        else:
            end = t.find(";", k); end = len(t) if end < 0 else end + 1
        out.append((cond, k, end))
    return out

_BCS_CACHE = {}

def parse_build_cs_ex(path):
    """{referenced module: None if referenced unconditionally (or inside a Linux-relevant condition), else the
    platform-conditional `if` condition text}. Comments stripped, calls paren-balanced."""
    if path in _BCS_CACHE: return _BCS_CACHE[path]
    t = strip_cs_comments(open(path, encoding="utf-8-sig", errors="replace").read())
    ifs = _if_blocks(t)
    # flags declared `bool X = false;` and never assigned again in this file (only a platform subclass could set them)
    dead = {n for n in re.findall(r"\bbool\s+(\w+)\s*=\s*false\s*;", t)
            if len(re.findall(r"\b" + n + r"\s*=(?!=)", t)) == 1}
    refs = {}
    for m in _DEP_RE.finditer(t):
        i = m.end()
        j = _match_close(t, i, "(", ")")
        cond = None
        for c, bs, be in ifs:
            if not (bs <= m.start() < be): continue
            cs = c.strip()
            if cs in dead: cond = f"{cs} [constant-false flag]"; break
            if platform_condition_kind(c) == "other": cond = " ".join(c.split()); break
        for r in _STR_RE.findall(t[i:j]):
            if r not in refs: refs[r] = cond
            elif cond is None: refs[r] = None   # any unconditional occurrence makes the reference count
    _BCS_CACHE[path] = refs
    return refs

def parse_build_cs(path):
    """Referenced module names that count for Linux (platform-conditional references excluded)."""
    return {r for r, c in parse_build_cs_ex(path).items() if c is None}

def fs_index(ue):
    """One walk: all *.build.cs (case-insensitive) under Engine/Source, Plugins, Platforms and all *.so under
    Engine/Binaries/Linux and Engine/Plugins/**/Binaries/Linux (also Platforms/**). Cached per ue root."""
    if ue in _FS_CACHE: return _FS_CACHE[ue]
    bcs, sos = [], []
    for sub in ("Source", "Plugins", "Platforms"):
        for r, ds, fs in os.walk(os.path.join(ue, "Engine", sub)):
            for f in fs:
                lf = f.lower()
                if lf.endswith(".build.cs"): bcs.append(os.path.join(r, f))
                elif lf.endswith(".so") and (os.sep + "Binaries" + os.sep + "Linux") in r + os.sep and sub != "Source":
                    sos.append(os.path.join(r, f))
    for r, ds, fs in os.walk(os.path.join(ue, "Engine", "Binaries", "Linux")):
        sos += [os.path.join(r, f) for f in fs if f.endswith(".so")]
    _FS_CACHE[ue] = (bcs, sorted(set(sos)))
    return _FS_CACHE[ue]

def load_manifest(mdir):
    """module (lowercase) -> sorted plugin names, from the original .uplugin files."""
    m = {}
    if not mdir or not os.path.isdir(mdir): return None
    for r, _, fs in os.walk(mdir):
        for f in fs:
            if not f.endswith(".uplugin"): continue
            try: d = load_json(os.path.join(r, f))
            except Exception as e:
                print(f"warn: manifest {f}: {e}", file=sys.stderr); continue
            for mod in d.get("Modules", []):
                m.setdefault(mod.get("Name", "").lower(), set()).add(f[:-8])
    return {k: sorted(v) for k, v in m.items()}

def _under(p, dirs): return any(p == d or p.startswith(d + os.sep) for d in dirs)

def classify(mod, manifest, existing):
    """('violation', providers) if a now-deleted plugin provided the module; ('allow', ..) ; else ('unknown', ..)."""
    prov = (manifest or {}).get(mod.lower(), [])
    if any(p not in existing for p in prov): return "violation", prov
    if mod.lower() in {x.lower() for x in ALLOWLIST}: return "allow", prov
    return "unknown", prov

def verify_modules(ue, plug_dirs, deleted_dirs, manifest, existing):
    """plug_dirs: {plugin_name: dir} for all plugins incl. platform extensions. Returns (violations, unknown) lists of
    (owner/module, missing, providers)."""
    bcs, _ = fs_index(ue)
    bcs = [b for b in bcs if not _under(b, deleted_dirs)]
    defined = {os.path.basename(b)[:-9].lower() for b in bcs}
    pd = sorted(plug_dirs.items(), key=lambda kv: -len(kv[1]))
    viol, unk, cond = [], [], []
    for b in bcs:
        owner = next((n for n, d in pd if b.startswith(d + os.sep)), None)
        if owner is None: continue  # engine Source modules: not checked (not removable by this script)
        mod = os.path.basename(b)[:-9]
        for ref, c in sorted(parse_build_cs_ex(b).items()):
            if ref.lower() in defined: continue
            if c is not None:
                if classify(ref, manifest, existing)[0] != "allow": cond.append((f"{owner}/{mod}", ref, c))
                continue
            kind, prov = classify(ref, manifest, existing)
            row = (f"{owner}/{mod}", ref, prov)
            if kind == "violation": viol.append(row)
            elif kind == "unknown": unk.append(row)
    return sorted(viol), sorted(unk), sorted(cond)

_NEEDED_CACHE = {}

def needed_map(ue):
    """{so path: set of NEEDED libUnrealEditor-<Module>.so names} for every indexed .so (readelf, cached)."""
    if ue in _NEEDED_CACHE: return _NEEDED_CACHE[ue]
    _, sos = fs_index(ue)
    res = {s_: set() for s_ in sos}
    for i in range(0, len(sos), 150):
        cur = None
        p = subprocess.run(["readelf", "-d", "-W", *sos[i:i+150]], capture_output=True, text=True, errors="replace")
        for line in p.stdout.splitlines():
            if line.startswith("File: "): cur = line[6:].strip(); continue
            m = re.search(r"\(NEEDED\)\s+Shared library: \[(libUnrealEditor-[^\]]+\.so)\]", line)
            if m and cur in res: res[cur].add(m.group(1))
    _NEEDED_CACHE[ue] = res
    return res

def verify_binaries(ue, deleted_dirs, manifest, existing):
    """NEEDED libUnrealEditor-*.so not present in the kept set. Returns (violations, unknown) of (so, needed, providers)."""
    nm = needed_map(ue)
    sos = [s_ for s_ in nm if not _under(s_, deleted_dirs)]
    have = {os.path.basename(s_) for s_ in sos}
    viol, unk = [], []
    for so in sos:
        for lib in sorted(nm[so]):
            if lib in have: continue
            kind, prov = classify(lib[len("libUnrealEditor-"):-3], manifest, existing)
            row = (os.path.relpath(so, ue), lib, prov)
            if kind == "violation": viol.append(row)
            elif kind == "unknown": unk.append(row)
    return sorted(viol), sorted(unk)

# ---- unified dependency graph (keep-closure) -----------------------------------------------------
def build_edges(ue, plugs, pext, manifest):
    """edges[src] = list of (dst, type, detail), type in {'uplugin','build.cs','readelf'}; src/dst are plugin names in `plugs`.
      uplugin : Enabled, non-Optional entry in src's .uplugin
      build.cs: a module of src lists module M in a Dependency/DynamicallyLoaded/IncludePath list, M owned by plugin dst
                (owner = plugin whose dir contains M.build.cs, else the manifest provider). References inside an `if` whose
                condition is non-Linux-only are skipped and returned in `cond_ignored`.
      readelf : a src .so NEEDs libUnrealEditor-M.so, M owned by dst
    Returns (edges, cond_ignored)."""
    dirs = {n: p["path"] for n, p in plugs.items()}
    ext_parent = {e["name"]: e["parent"] for e in pext if e["parent"] in plugs}
    dirs.update({e["name"]: e["path"] for e in pext})
    pd = sorted(dirs.items(), key=lambda kv: -len(kv[1]))
    def owner_of(path):
        n = next((n for n, d in pd if path.startswith(d + os.sep)), None)
        return ext_parent.get(n, n) if n else None
    lc = {n.lower(): n for n in plugs}
    modowner = {}                                   # module (lower) -> plugin
    for n, p in plugs.items():
        for m in p["modules"]:
            if m[0]: modowner.setdefault(m[0].lower(), n)
    bcs, _ = fs_index(ue)
    engine_defs = set()
    for b in bcs:
        o = owner_of(b); mod = os.path.basename(b)[:-9].lower()
        if o: modowner.setdefault(mod, o)
        else: engine_defs.add(mod)
    def owners(mod):
        o = modowner.get(mod.lower())
        if o: return [o]
        if mod.lower() in engine_defs: return []
        return [lc[x.lower()] for x in (manifest or {}).get(mod.lower(), []) if x.lower() in lc]
    edges = {n: [] for n in plugs}
    for n, p in plugs.items():
        for dn, e, o in p["deps"]:
            if e and not o and dn in plugs: edges[n].append((dn, "uplugin", f"{n}.uplugin lists {dn}"))
    cond_ignored = []
    for b in bcs:
        src = owner_of(b)
        if not src: continue
        mod = os.path.basename(b)[:-9]
        for ref, c in sorted(parse_build_cs_ex(b).items()):
            for dst in owners(ref):
                if dst == src: continue
                if c is not None: cond_ignored.append((src, f"{src}/{mod}.build.cs", ref, dst, c))
                else: edges[src].append((dst, "build.cs", f"{src}/{mod}.build.cs -> {ref}"))
    for so, libs in needed_map(ue).items():
        src = owner_of(so)
        if not src: continue
        for lib in sorted(libs):
            mod = lib[len("libUnrealEditor-"):-3]
            for dst in owners(mod):
                if dst != src: edges[src].append((dst, "readelf", f"{os.path.basename(so)} NEEDS {lib}"))
    return edges, cond_ignored

def closure_graph(roots, plugs, edges):
    """Fixed point over all edge types. Returns (keep, why): why[n] = list of (src, type, detail) from kept sources."""
    seen, st = set(), [r for r in roots if r in plugs]
    while st:
        n = st.pop()
        if n in seen: continue
        seen.add(n)
        st.extend(d for d, _, _ in edges.get(n, []) if d not in seen)
    why = {n: [] for n in seen}
    for n in seen:
        for d, t, det in edges.get(n, []):
            if d in why: why[d].append((n, t, det))
    return seen, why

def run_layer_checks(title, ue, plugs, pext, deleted_dirs, manifest, mdir):
    """Prints module-level and binary-level results; returns number of violations."""
    if manifest is None:
        print(f"verify-deps [{title}]: manifest {mdir!r} missing; module/binary checks need --manifest"); return 1
    plug_dirs = {n: p["path"] for n, p in plugs.items()}
    plug_dirs.update({e["name"]: e["path"] for e in pext})
    existing = {n for n, d in plug_dirs.items() if not _under(d, deleted_dirs)}
    mv, mu, mc = verify_modules(ue, plug_dirs, deleted_dirs, manifest, existing)
    print(f"verify-deps [{title}] module-level (Build.cs): {len(mv)} violation(s), {len(mu)} pre-existing/unknown (informational)")
    for o, r, pr in mv: print(f"  VIOLATION {o} -> {r} (provided by deleted plugin: {', '.join(pr)})")
    for o, r, c in mc: print(f"  platform-conditional (ignored) {o} -> {r} [in if ({c[:80]})]")
    for o, r, pr in mu[:40]: print(f"  info {o} -> {r} (manifest: {', '.join(pr) or 'not in manifest'})")
    if len(mu) > 40: print(f"  info ... {len(mu) - 40} more")
    bv, bu = verify_binaries(ue, deleted_dirs, manifest, existing)
    print(f"verify-deps [{title}] binary-level (readelf NEEDED): {len(bv)} violation(s), {len(bu)} pre-existing/unknown (informational)")
    for o, r, pr in bv: print(f"  VIOLATION {o} NEEDS {r} (provided by deleted plugin: {', '.join(pr)})")
    for o, r, pr in bu[:40]: print(f"  info {o} needs {r} (manifest: {', '.join(pr) or 'not in manifest'})")
    if len(bu) > 40: print(f"  info ... {len(bu) - 40} more")
    return len(mv) + len(bv)

def sz_(plugs, n): return plugs[n]["size"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ue-root", default="/opt/UnrealEngine-5.8")
    ap.add_argument("--uproject", default=os.path.join(os.path.dirname(__file__), "..", "..", "Velox.uproject"))
    ap.add_argument("--report")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--yes", action="store_true")
    ap.add_argument("--restore-list")
    ap.add_argument("--prune-platforms", default="",
                    help="comma list of Engine/Platforms/<X> dirs to mark for deletion, e.g. Android,IOS,VisionOS,TVOS "
                         "(never Linux). Only marks; deleting still needs --apply --yes")
    ap.add_argument("--check-orphans", action="store_true",
                    help="only check for platform-extension plugins whose parent is missing; exit 1 if any")
    ap.add_argument("--verify-deps", action="store_true",
                    help="check every remaining plugin's hard deps exist (current tree and planned post-state); exit 1 on violations")
    ap.add_argument("--manifest", default="/home/bojan/ue-5.8.4-uplugin-manifest",
                    help="dir with the original .uplugin files (same relative layout); used by the Build.cs/readelf layers to name the "
                         "plugin that provided a missing module")
    ap.add_argument("--ignore-parse-errors", action="store_true",
                    help="continue although some .uplugin files fail to parse (UNSAFE: those plugins are invisible)")
    ap.add_argument("--no-sizes-of-dirs", action="store_true", help="skip other-dir sizing")
    a = ap.parse_args()
    ue = a.ue_root
    global IGNORE_PARSE_ERRORS
    IGNORE_PARSE_ERRORS = a.ignore_parse_errors
    if a.check_orphans:
        sys.exit(1 if report_orphans(ue) else 0)
    prune = [x.strip() for x in a.prune_platforms.split(",") if x.strip()]
    if any(x.lower() == "linux" or "/" in x or x in (".", "..") for x in prune):
        sys.exit("--prune-platforms: refusing Linux / path-like names")
    plugs = scan(ue)
    pext = scan_platform_ext(ue, set(plugs))
    en, dis = uproject_roots(a.uproject)
    lcn = {n.lower(): n for n in plugs}
    canon = lambda n: lcn.get(n.lower(), n)
    en, dis = {canon(x) for x in en}, {canon(x) for x in dis}
    global EXTRA_ROOTS
    EXTRA_ROOTS = {canon(x) for x in EXTRA_ROOTS}
    modroots = project_module_roots(plugs, os.path.join(os.path.dirname(os.path.abspath(a.uproject)), "Source"))
    roots = {n for n, p in plugs.items() if p["enabled_default"]} | en | EXTRA_ROOTS | modroots
    roots -= dis
    roots = {r for r in roots if r in plugs}
    manifest = load_manifest(a.manifest)
    if manifest is None: print(f"warn: manifest {a.manifest!r} missing; module ownership limited to the current tree", file=sys.stderr)
    edges, cond_ignored = build_edges(ue, plugs, pext, manifest)
    keep, why = closure_graph(roots, plugs, edges)
    conflicts = sorted({(n, dn) for n in keep for dn, t, _ in edges[n] if dn in dis})
    if conflicts:
        print(f"uproject conflicts: {len(conflicts)} plugin(s) disabled in the .uproject but hard-required by kept plugins (they are KEPT):")
        for n, dn in conflicts: print(f"  {dn} (Enabled:false in uproject) <- required by {n}")
    rdeps = {n: [] for n in plugs}
    for n in plugs:
        for dn in {d for d, _, _ in edges[n]}: rdeps[dn].append((n, True, False))
    unref = sorted(set(plugs) - keep)
    # nested plugin dirs: deleting a parent dir would delete children; only delete a plugin dir if
    # every plugin below it is also deleted.
    deny = [re.compile(x) for x in DENYLIST]
    never = [re.compile(x) for x in NEVER_DELETE]
    def denied(n): return any(d.search(n) for d in deny) and not any(x.fullmatch(n) or x.match(n) for x in never)
    cand = {n for n in keep if denied(n) and n not in (roots & (en | EXTRA_ROOTS | modroots))}
    # fixpoint: drop candidates that have hard dependants outside (cand | unref)
    gone = set(cand) | set(unref)
    changed = True
    while changed:
        changed = False
        for n in list(cand):
            blockers = [d for d, e, opt in rdeps[n] if e and not opt and d not in gone and d in keep]
            if blockers:
                cand.discard(n); gone.discard(n); changed = True
    agg_safe = sorted(cand)
    blocked = {}
    for n in keep:
        if denied(n) and n not in cand and n not in (en | EXTRA_ROOTS | modroots):
            blocked[n] = sorted(d for d, e, opt in rdeps[n] if e and not opt and d in keep and d not in cand)
    def rclosure(n):
        seen, st = set(), [n]
        while st:
            x = st.pop()
            for d, e, opt in rdeps.get(x, []):
                if e and not opt and d in keep and d not in seen: seen.add(d); st.append(d)
        return seen
    pinned = en | EXTRA_ROOTS | modroots
    chain = {}
    for n in blocked:
        dd = rclosure(n)
        chain[n] = (sorted(dd), sorted(dd & pinned), sum(sz_(plugs, x) for x in dd | {n}))
    safe_unref = unref
    safe_all = safe_unref + agg_safe
    # nested-dir protection
    gone = set(safe_all)
    def nested_ok(n):
        p = plugs[n]["path"] + os.sep
        return all(m in gone for m, q in plugs.items() if q["path"].startswith(p) and m != n)
    safe_all = [n for n in safe_all if nested_ok(n)]
    # drop plugins whose dir is inside another deleted dir (avoid double counting)
    dirs = {n: plugs[n]["path"] for n in safe_all}
    top = [n for n in safe_all if not any(plugs[n]["path"].startswith(dirs[m] + os.sep) for m in safe_all if m != n)]
    # platform extensions of deleted parents go with them (UBT aborts on orphaned extensions)
    prune_dirs = []
    for x in prune:
        pd = os.path.join(ue, "Engine", "Platforms", x)
        if os.path.isdir(pd): prune_dirs.append(pd)
        else: print(f"warn: --prune-platforms: {pd} does not exist", file=sys.stderr)
    def in_pruned(p): return any(p == d or p.startswith(d + os.sep) for d in prune_dirs)
    gone_all = set(safe_all)
    ext_removed = [e for e in pext if e["parent"] in gone_all and not in_pruned(e["path"])]
    ext_by_parent = {}
    for e in ext_removed: ext_by_parent.setdefault(e["parent"], []).append(e)
    sz = lambda n: plugs[n]["size"]
    total = sum(p["size"] for p in plugs.values())
    rel = lambda p: os.path.relpath(p, ue)
    is_agg = set(agg_safe)

    L = []
    L.append("# Engine plugin trim report\n")
    L.append(f"Generated by `infra/engine/trim-plugins.py` (read-only run). UE root: `{ue}`; project: `{os.path.basename(a.uproject)}`.\n")
    L.append(f"- Plugins found: {len(plugs)}; total size: {hs(total)}")
    L.append(f"- Roots: {len(roots)} (enabled by default + uproject-enabled + extra roots + plugins referenced from project Build.cs: {sorted(modroots) or 'none'})")
    L.append(f"- Kept (roots + transitive hard deps): {len(keep)}; unreferenced (never loaded by this project): {len(unref)}")
    L.append(f"- **Safe deletion total: {hs(sum(sz(n) for n in top))}** ({len(top)} dirs: {hs(sum(sz(n) for n in top if n not in is_agg))} unreferenced + {hs(sum(sz(n) for n in top if n in is_agg))} aggressive)\n")
    L.append("Dependency note: only hard deps (`Enabled:true`, not `Optional`) count as blockers. Optional deps just degrade features, but the engine/editor modules of a kept plugin may still `#include` nothing from them at link time because optional modules are loaded dynamically.\n")
    L.append("## Why each non-root plugin is kept (edge types: uplugin = .uplugin hard dep, build.cs = module dep, readelf = binary NEEDED)\n")
    L.append("Keep-closure is computed over the union of all three edge types; reasons list kept sources only.\n")
    nonuplugin_only = [n for n in keep if n not in roots and why[n] and not any(t == "uplugin" for _, t, _ in why[n])]
    L.append(f"- Kept only through build.cs/readelf edges (invisible to the .uplugin graph): {len(nonuplugin_only)}: {', '.join(sorted(nonuplugin_only)) or 'none'}\n")
    L.append("| Plugin | Edge types | Examples |\n|---|---|---|")
    for n in sorted(keep - roots):
        w = why[n]; ty = sorted({t for _, t, _ in w})
        pri = {"build.cs": 0, "readelf": 1, "uplugin": 2}
        ex = []
        for t in ("build.cs", "readelf", "uplugin"):
            ex += [d for _, tt, d in sorted(w) if tt == t][:2]
        L.append(f"| {n} | {', '.join(ty)} | {'; '.join(ex[:4])}{' ...' if len(w) > 4 else ''} |")
    L.append("\n## Platform-conditional references (ignored)\n")
    L.append("Build.cs references inside an `if (...)` whose condition names only non-Linux platforms (heuristic, see README):\n")
    if cond_ignored:
        L.append("| Source plugin | Build.cs | Module | Provided by | Condition |\n|---|---|---|---|---|")
        for sp, b_, r, dst, c in sorted(set(cond_ignored)): L.append(f"| {sp} | {b_} | {r} | {dst} | `{c[:90]}` |")
    else: L.append("None.")
    L.append("")
    L.append("## Top 40 plugins by size\n\n| Plugin | Size | Status | Path |\n|---|---|---|---|")
    def status(n):
        if n in top: return "SAFE-DELETE (aggressive)" if n in is_agg else "SAFE-DELETE (unreferenced)"
        if n in blocked: return "BLOCKED by " + ", ".join(blocked[n][:4])
        if n in unref: return "unreferenced (nested, parent kept)"
        return "KEEP" + (" (root)" if n in roots else " (dependency)")
    for n in sorted(plugs, key=sz, reverse=True)[:40]:
        L.append(f"| {n} | {hs(sz(n))} | {status(n)} | `{rel(plugs[n]['path'])}` |")
    L.append("\n## Safe deletion list (largest first)\n\n| Plugin | Size | Cumulative | Kind | EnabledByDefault |\n|---|---|---|---|---|")
    cum = 0
    for n in sorted(top, key=sz, reverse=True):
        cum += sz(n)
        L.append(f"| {n} | {hs(sz(n))} | {hs(cum)} | {'aggressive' if n in is_agg else 'unreferenced'} | {plugs[n]['enabled_default']} |")
    L.append("\n## Platform extensions removed with their parent\n")
    if ext_removed:
        L.append("| Extension | Platform | Parent (deleted) | Path |\n|---|---|---|---|")
        for e in sorted(ext_removed, key=lambda e: (e["parent"], e["platform"])):
            L.append(f"| {e['name']} | {e['platform']} | {e['parent']} | `{rel(e['path'])}` |")
    else:
        L.append("None.")
    L.append("\n## Platform dirs marked by --prune-platforms\n")
    if prune_dirs:
        for d in prune_dirs: L.append(f"- `{rel(d)}`: {hs(dir_size(d))}")
    else:
        L.append("None (suggestion on a Linux-only host: `--prune-platforms Android,IOS,VisionOS,TVOS`).")
    L.append("\n## Blocked candidates (match denylist but kept plugins depend on them)\n")
    L.append("| Plugin | Size | Kept dependants (hard) | EnabledByDefault |\n|---|---|---|---|")
    for n in sorted(blocked, key=sz, reverse=True):
        L.append(f"| {n} | {hs(sz(n))} | {', '.join(blocked[n]) or '-'} | {plugs[n]['enabled_default']} |")
    L.append("\n### Chain analysis: disabling a blocked plugin requires disabling its whole dependant closure\n")
    L.append("| Plugin | Transitive kept dependants | Pinned by project? | Total freed if whole chain removed |\n|---|---|---|---|")
    for n in sorted(blocked, key=lambda x: -chain[x][2]):
        dd, pin, tot_ = chain[n]
        L.append(f"| {n} | {len(dd)}: {', '.join(dd[:12])}{'...' if len(dd) > 12 else ''} | {', '.join(pin) or 'no'} | {hs(tot_)} |")
    L.append("\nTo remove a blocked plugin, add it to the uproject as `\"Enabled\": false` ONLY if no kept plugin hard-depends on it (otherwise UBT/editor errors out); "
             "its dependants must be disabled first. The list above shows the dependants.\n")
    agg_names = sorted(n for n in top if plugs[n]["enabled_default"])
    L.append("## Uproject entries for deleted EnabledByDefault plugins\n")
    L.append("Deleted plugins that were `EnabledByDefault` should be marked disabled in `Velox.uproject` to silence 'missing plugin' warnings:\n")
    L.append("```json\n" + ",\n".join(f'{{ "Name": "{n}", "Enabled": false }}' for n in agg_names if n not in dis) + "\n```\n")
    L.append("## rm -rf block for the safe list (NOT executed)\n\n```bash\n" + "\n".join(f"rm -rf '{plugs[n]['path']}'" for n in sorted(top, key=sz, reverse=True))
             + "".join(f"\nrm -rf '{e['path']}'" for e in ext_removed)
             + "".join(f"\nrm -rf '{d}'" for d in prune_dirs) + "\n```\n")

    # other dirs
    L.append("## Other large engine directories (not deleted by this script)\n")
    def sizes(rel_, k):
        base = os.path.join(ue, rel_)
        if not os.path.isdir(base): return []
        r = [(dir_size(os.path.join(base, e)), e) for e in os.listdir(base) if os.path.isdir(os.path.join(base, e))]
        return sorted(r, reverse=True)[:k]
    if not a.no_sizes_of_dirs:
        for rel_, k in (("Engine/Binaries/ThirdParty", 8), ("Engine/Source/ThirdParty", 5), ("Engine/Content", 5)):
            L.append(f"### {rel_} (top {k})\n\n| Dir | Size |\n|---|---|")
            for s, e in sizes(rel_, k): L.append(f"| {e} | {hs(s)} |")
            L.append("")
        for d in ("Engine/Intermediate", "Engine/DerivedDataCache"):
            p = os.path.join(ue, d)
            L.append(f"- `{d}`: {hs(dir_size(p)) if os.path.isdir(p) else 'absent'}")
        L.append("")
    L.append(VERDICTS)
    text = "\n".join(L)
    if a.report:
        os.makedirs(os.path.dirname(os.path.abspath(a.report)), exist_ok=True)
        open(a.report, "w").write(text)
        print("report written:", a.report)
    print(f"plugins parsed: {len(plugs)}; parse errors: {len(PARSE_ERRORS)}")
    bad = []
    if a.verify_deps or a.apply:
        cur = verify_deps(plugs, set(plugs))
        post = verify_deps(plugs, set(plugs) - set(safe_all))
        print_violations("current tree", cur)
        print_violations("planned post-state (all - deletion set)", post)
        bad = post if a.apply else (cur or post)
        if True:
            del_dirs = [plugs[n]["path"] for n in top] + [e["path"] for e in ext_removed] + prune_dirs
            nl_cur = run_layer_checks("current tree", ue, plugs, pext, [], manifest, a.manifest)
            nl_post = run_layer_checks("planned post-state", ue, plugs, pext, del_dirs, manifest, a.manifest)
            layer_bad = nl_post if a.apply else (nl_cur or nl_post)
            bad = bad or ([("module/binary layer", "see above")] if layer_bad else [])
    print(f"safe total {hs(sum(sz(n) for n in top))}; {len(top)} dirs; blocked {len(blocked)}")
    if a.verify_deps and bad and not a.apply: sys.exit(1)
    if a.apply:
        if bad: sys.exit("ABORT --apply: planned post-state has hard-dependency violations (see above); nothing deleted.")
        if not a.yes: sys.exit("--apply requires --yes")
        if a.restore_list:
            open(a.restore_list, "w").write("\n".join([plugs[n]["path"] for n in top] + [e["path"] for e in ext_removed] + prune_dirs) + "\n")
        for n in top:
            print("rm", plugs[n]["path"]); shutil.rmtree(plugs[n]["path"], ignore_errors=True)
        for e in ext_removed:
            print("rm", e["path"]); shutil.rmtree(e["path"], ignore_errors=True)
        for d in prune_dirs:
            print("rm", d); shutil.rmtree(d, ignore_errors=True)
        after = scan(ue)
        v = verify_deps(after, set(after))
        print_violations("after apply", v)
        _FS_CACHE.clear()
        after_pext = scan_platform_ext(ue, set(after))
        nl = run_layer_checks("after apply", ue, after, after_pext, [], manifest, a.manifest)
        if report_orphans(ue) or v or nl: sys.exit(1)

if __name__ == "__main__":
    main()
