#!/usr/bin/env python3
"""Dependency-aware trimming of Engine/Plugins in an installed UE tree (stdlib only).

Default mode is READ-ONLY: scans, writes a markdown report, never deletes.
  trim-plugins.py [--ue-root /opt/UnrealEngine-5.8] [--uproject Velox.uproject]
                  [--report infra/engine/TRIM-REPORT.md]
  trim-plugins.py --apply --yes --restore-list deleted.txt   # actually deletes the SAFE list

Categories:
  KEEP        enabled by default / enabled in .uproject / transitive hard dependency of those
  UNREFERENCED  not enabled by default, not enabled by project, not required by any kept plugin
                -> safe to delete (the engine never loads it unless a project enables it)
  AGGRESSIVE-SAFE   kept only because EnabledByDefault, matches DENYLIST, and no kept plugin outside
                the deletion set hard-depends on it
  AGGRESSIVE-BLOCKED  matches DENYLIST but kept plugins depend on it (list shown); to remove it you
                must delete/disable the dependants too (or accept it)
"""
import argparse, json, os, re, shutil, sys

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
    with open(p, encoding="utf-8-sig", errors="replace") as f:
        t = f.read()
    try: return json.loads(t)
    except ValueError: return json.loads(strip_json(t))

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
    proot = os.path.join(ue, "Engine", "Plugins")
    for r, ds, fs in os.walk(proot):
        ups = [f for f in fs if f.endswith(".uplugin")]
        if not ups: continue
        up = ups[0]; name = up[:-8]
        try: d = load_json(os.path.join(r, up))
        except Exception as e:
            print(f"warn: cannot parse {r}/{up}: {e}", file=sys.stderr); continue
        # nested plugins live in subdirs; size = own dir minus nested plugin dirs is overkill; compute full dir
        plugs[name] = dict(name=name, path=r, enabled_default=bool(d.get("EnabledByDefault", False)),
            deps=[(x["Name"], x.get("Enabled", True), bool(x.get("Optional", False))) for x in d.get("Plugins", [])],
            modules=[(m.get("Name"), m.get("Type"), m.get("LoadingPhase")) for m in d.get("Modules", [])],
            platforms=d.get("SupportedTargetPlatforms"), size=0)
        ds[:] = [x for x in ds]  # keep walking: nested plugins exist
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

def closure(roots, plugs, disabled=frozenset()):
    seen, st = set(), list(roots)
    while st:
        n = st.pop()
        if n in seen or n not in plugs: continue
        seen.add(n)
        for dn, en, opt in plugs[n]["deps"]:
            if en and not opt and dn not in disabled: st.append(dn)
    return seen

def sz_(plugs, n): return plugs[n]["size"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ue-root", default="/opt/UnrealEngine-5.8")
    ap.add_argument("--uproject", default=os.path.join(os.path.dirname(__file__), "..", "..", "Velox.uproject"))
    ap.add_argument("--report")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--yes", action="store_true")
    ap.add_argument("--restore-list")
    ap.add_argument("--no-sizes-of-dirs", action="store_true", help="skip other-dir sizing")
    a = ap.parse_args()
    ue = a.ue_root
    plugs = scan(ue)
    en, dis = uproject_roots(a.uproject)
    modroots = project_module_roots(plugs, os.path.join(os.path.dirname(os.path.abspath(a.uproject)), "Source"))
    roots = {n for n, p in plugs.items() if p["enabled_default"]} | en | EXTRA_ROOTS | modroots
    roots -= dis
    roots = {r for r in roots if r in plugs}
    keep = closure(roots, plugs, dis)
    rdeps = {n: [] for n in plugs}
    for n, p in plugs.items():
        for dn, e, opt in p["deps"]:
            if dn in rdeps: rdeps[dn].append((n, e, opt))
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
    L.append("## rm -rf block for the safe list (NOT executed)\n\n```bash\n" + "\n".join(f"rm -rf '{plugs[n]['path']}'" for n in sorted(top, key=sz, reverse=True)) + "\n```\n")

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
    print(f"safe total {hs(sum(sz(n) for n in top))}; {len(top)} dirs; blocked {len(blocked)}")
    if a.apply:
        if not a.yes: sys.exit("--apply requires --yes")
        if a.restore_list:
            open(a.restore_list, "w").write("\n".join(plugs[n]["path"] for n in top) + "\n")
        for n in top:
            print("rm", plugs[n]["path"]); shutil.rmtree(plugs[n]["path"], ignore_errors=True)

if __name__ == "__main__":
    main()
