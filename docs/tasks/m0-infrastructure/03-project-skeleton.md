# 03 — Unreal project skeleton

**Milestone:** M0 Infrastructure
**Agent:** dev-hard
**Status:** done (static review passed; first real compile pending)
**Depends on:** ADR 0001

## Goal
`Velox.uproject` and the `Velox` C++ module exist with every skeleton class, target and config named in ADR 0001 §1/§2 and the implementation checklist, so that the project compiles for `VeloxEditor` and `Velox` on first attempt on a machine with UE 5.8 installed (the human's Mac via Launcher; later this Linux VM). No gameplay logic yet beyond what the ADR requires for the skeleton (movement component is a subclass with settings hook and empty saved-move plumbing, ASC exists on PlayerState, HUD/GameMode stubs).

## Scope — files/modules the agent may touch
- `Velox.uproject`
- `Source/Velox.Target.cs`, `Source/VeloxEditor.Target.cs`, `Source/VeloxClient.Target.cs`, `Source/VeloxServer.Target.cs`
- `Source/Velox/Velox.Build.cs`, `Source/Velox/Velox.h/.cpp` (module + log category)
- `Source/Velox/<Domain>/` per ADR folder list, `Source/Velox/Tests/`
- `Config/DefaultEngine.ini`, `Config/DefaultGame.ini`, `Config/DefaultInput.ini`, `Config/DefaultGameplayTags.ini` (or tag ini per ADR)
- `Content/` — only `.gitkeep` style folder markers; no binary assets (they cannot be created without the editor)
- `docs/adr/0001-...md` — read only

## Relevant ADRs / references
- `docs/adr/0001-project-and-module-structure.md` (all sections, especially the implementation checklist)
- `CLAUDE.md` section 6

## Acceptance criteria
- [ ] Every file in the ADR implementation checklist exists with the exact class names given there; `UPROPERTY`/`UFUNCTION` on everything reflected; tunables referenced via `UVeloxMovementSettings` / data assets, no magic numbers.
- [ ] `Velox.Build.cs` lists exactly the dependencies the ADR names; `.uproject` enables/disables the plugins the ADR lists; `EngineAssociation` is `"5.8"`.
- [ ] `Config/DefaultEngine.ini` matches the ADR snippet (forward renderer, MSAA, Nanite/Lumen/VSM off, static lighting, net settings, default game mode classes, default maps pointing at `/Game/Maps/L_Empty` which task 04 will create).
- [ ] `Source/Velox/Tests/VeloxSmokeTest.cpp`: `IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVeloxCoreSmokeTest, "Velox.Core.Smoke", ...)` guarded by `WITH_DEV_AUTOMATION_TESTS`, trivially passing; plus one test on `FVeloxMoveSim` if the ADR defines its interface (otherwise a stub test file with a TODO is acceptable).
- [ ] Movement component: `UVeloxMovementComponent : UCharacterMovementComponent` with `FSavedMove_Velox`, `FVeloxNetworkMoveData`, `FVeloxNetworkMoveDataContainer` wired in (`SetNetworkMoveDataContainer` in constructor, `GetPredictionData_Client` override, `FVeloxNetworkPredictionData_Client` returning the saved-move subclass). Flags carry at least jump-held. Behaviour may still be default CMC; the plumbing must be complete and correct.
- [ ] Code is written for UE 5.8 API (e.g. `FCharacterNetworkMoveData::Serialize` signature, `FSavedMove_Character::GetCompressedFlags`, `UCharacterMovementComponent::GetPredictionData_Client`), includes are minimal and correct (`.generated.h` last), forward declarations preferred, `VELOX_API` export macros on all public classes.
- [ ] Since UE is not installed on this VM: perform a careful static review pass of every header/cpp for compile errors (missing includes, wrong override signatures, missing `Super::` calls, mismatched UCLASS specifiers). Report explicitly: "not compiled; static review done".

## Verification steps
1. `find Source Config -type f | sort` matches the ADR checklist.
2. `grep -rn "new " Source/ | grep -v "// "` shows no raw `new` of UObjects.
3. If `$UE_ROOT` exists: `"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" VeloxEditor Linux Development -Project="$PWD/Velox.uproject"` succeeds. Otherwise report that it was not run.

## Report back
Standard dev-hard format incl. design summary and the explicit compile status.
