# ADR 0001 — Project and module structure

**Status:** Accepted (2026-10-24, project lead) (architect, 2026-10-24) → accepted by the lead on merge of M0 skeleton.
**Scope:** everything a `dev` agent needs to scaffold the UE 5.8 project (M0) and a `dev-hard` agent needs to start M1 without guessing. Later ADRs refine M2 (weapons/GAS details), M4 (bots), M6 (online subsystem, replication graph).

## Context

Velox is a Quake-3-style arena FPS on Unreal Engine 5.8: movement feel first, high stable frame rate, multiplayer from day one (dedicated server, client-predicted movement, lag-compensated hitscan), bots on the player movement component. The team is a lead plus three agents, so the structure must minimise coordination overhead and compile time, not maximise modularity. Build hosts: a headless Linux VM (no GPU) for compile/cook/server, the human's Mac for editor/client use and playtests. Lyra is the reference; we copy ideas, not modules.

One fact dominates the build-host decision: **Client and Server targets cannot be built with an installed (prebuilt) engine** — the Epic Games Launcher build on macOS and the prebuilt Linux editor zip are both "installed builds" whose precompiled engine modules exist only for the Game/Editor build environment; `VeloxServer`/`VeloxClient` need a unique build environment and therefore a **source build** of the engine (Epic docs, "Setting Up Dedicated Servers"). See Decision 5 and the open questions.

## Decision

### 1. Project and Source layout

**One runtime module, domain folders.** `Source/Velox/` is a single primary game module split into domain folders. Reasons: (a) one module keeps compile/link time and PCH count minimal and avoids `_API` export headaches across module boundaries while the code base is small; (b) boundaries are enforced by a layering rule and review, which is cheap for agents to follow; (c) splitting later is mechanical if every reflected class already carries `VELOX_API`. An optional `VeloxEditor` module (details customisations, asset validators) is added only when first needed, not in M0.

Layering rule (lower layers never include from higher layers; checked in review):
`Core/ , Input/  →  Movement/  →  Character/  →  Combat/  →  Net/  →  Weapons/  →  Items/  →  GameModes/  →  Bots/  →  UI/`. `Movement/` has **no GAS dependency**: bots and automation tests must drive it standalone. `Tests/` may include anything.

```
Velox.uproject                      EngineAssociation "5.8", plugins per §5
Source/
  Velox.Target.cs                   TargetType.Game   (standalone, listen server, PIE; also `-server` on installed engines)
  VeloxEditor.Target.cs             TargetType.Editor
  VeloxClient.Target.cs             TargetType.Client (strips server code; needs source engine)
  VeloxServer.Target.cs             TargetType.Server (dedicated; needs source engine)
  Velox/
    Velox.Build.cs, VeloxModule.cpp (IMPLEMENT_PRIMARY_GAME_MODULE)
    Core/       VeloxLog.h/.cpp (LogVelox, LogVeloxMovement, LogVeloxNet, LogVeloxAbility)
                VeloxGameplayTags.h/.cpp (native tags), VeloxCollisionChannels.h
                VeloxGameInstance, VeloxAssetManager, VeloxPawnData
    Input/      VeloxInputConfig, VeloxInputComponent
    Movement/   VeloxMovementComponent, VeloxMovementSettings, VeloxMoveSim (pure math),
                VeloxNetworkMoveData (saved move + move-data container), VeloxJumpPad
    Character/  VeloxCharacter
    Combat/     VeloxAbilitySystemComponent, VeloxAttributeSet (base), VeloxHealthSet, VeloxAmmoSet,
                VeloxGameplayAbility, VeloxAbilitySet, VeloxHealthComponent, VeloxDamageExecution
    Net/        VeloxLagCompensationSubsystem, VeloxHitboxComponent
    Weapons/    VeloxWeaponDefinition, VeloxInventoryComponent, VeloxGameplayAbility_Fire*, VeloxProjectile, VeloxFakeProjectile
    Items/      VeloxPickup, VeloxPowerup*
    GameModes/  VeloxGameMode, VeloxGameState, VeloxPlayerState, VeloxPlayerController, VeloxGameMode_FFA, VeloxGameMode_Duel
    Bots/       VeloxBotController (AAIController, bWantsPlayerState=true), VeloxAimModel, VeloxNavLink*
    UI/         VeloxHUD, VeloxUserWidget, VeloxGameUserSettings
    Tests/      *Test.cpp under WITH_DEV_AUTOMATION_TESTS
Content/   Maps/ (L_Empty, L_MovementTest, Tests/FT_*), Input/ (IA_*, IMC_*), Data/ (DA_*), GameplayCues/, Characters/, Weapons/, UI/
Config/    DefaultEngine.ini, DefaultGame.ini, DefaultInput.ini, DefaultEditor.ini, DefaultGameplayTags.ini, Tags/VeloxTags.ini
Tools/     editor Python (headless map creation), Blender export scripts
infra/     ci/build.sh, ci/build-mac.sh, nginx/, systemd/ (M6)
```

Includes are module-root relative (`#include "Movement/VeloxMovementComponent.h"`); `Velox.Build.cs` adds `PublicIncludePaths.Add(ModuleDirectory)`. No `Public/Private` split inside the game module.

**Skeleton classes (M0 creates all of these, minimal bodies):**

| Class | Base | Notes |
|---|---|---|
| `UVeloxGameInstance` | `UGameInstance` | set via `GameInstanceClass` |
| `UVeloxAssetManager` | `UAssetManager` | `StartInitialLoading()` calls `UAbilitySystemGlobals::Get().InitGlobalData()` (Lyra idea); set via `AssetManagerClassName` |
| `AVeloxGameMode` | `AGameMode` | `AGameMode` (not `GameModeBase`) for its match-state machine: warmup/ready-up/in-progress/post-match map onto it in M3 |
| `AVeloxGameState` | `AGameState` | match timer, item timers later |
| `AVeloxPlayerState` | `APlayerState` | owns `UVeloxAbilitySystemComponent` + attribute sets (§2); `SetNetUpdateFrequency(100)` |
| `AVeloxPlayerController` | `APlayerController` | Enhanced Input mapping context push; input routing to pawn/ASC |
| `AVeloxCharacter` | `ACharacter` | `ObjectInitializer.SetDefaultSubobjectClass<UVeloxMovementComponent>(ACharacter::CharacterMovementComponentName)`; implements `IAbilitySystemInterface` forwarding to PlayerState's ASC; `UVeloxHitboxComponent` (M2) |
| `UVeloxMovementComponent` | `UCharacterMovementComponent` | M1; reads `UVeloxMovementSettings` |
| `UVeloxMovementSettings` | `UPrimaryDataAsset` | all movement tunables |
| `UVeloxPawnData` | `UPrimaryDataAsset` | `PawnClass`, `MovementSettings`, `InputConfig`, `AbilitySets` (Lyra pawn-data idea) |
| `UVeloxInputConfig` | `UDataAsset` | `TArray<FVeloxInputAction{UInputAction*, FGameplayTag}>` native + ability actions |
| `AVeloxHUD` | `AHUD` | canvas debug HUD (speed, velocity, FPS, ping) in M1; UMG from M3 via `UVeloxUserWidget` |
| `UVeloxAbilitySystemComponent` | `UAbilitySystemComponent` | `AbilityInputTagPressed/Released`, Mixed replication |

**`Velox.Build.cs`:**
```csharp
PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs; IWYUSupport = IWYUSupport.Full; CppStandard = CppStandardVersion.Cpp20;
PublicDependencyModuleNames: Core, CoreUObject, Engine, InputCore, EnhancedInput, GameplayAbilities, GameplayTags, GameplayTasks,
  NetCore, AIModule, NavigationSystem, UMG, Slate, SlateCore, PhysicsCore, DeveloperSettings
PrivateDependencyModuleNames: (empty in M0; add FunctionalTesting when the first FT map lands)
```
Targets: `DefaultBuildSettings = BuildSettingsVersion.Latest; IncludeOrderVersion = EngineIncludeOrderVersion.Latest; ExtraModuleNames.Add("Velox")`. Server target: `bUseLoggingInShipping = true`. All targets: `bUseIris = false` (stay on the classic replication system until M6 decides).

**Enhanced Input.** `DefaultInput.ini`: `DefaultPlayerInputClass=EnhancedPlayerInput`, `DefaultInputComponentClass=EnhancedInputComponent`, `bEnableMouseSmoothing=False`, `bViewAccelerationEnabled=False`, `DefaultViewportMouseCaptureMode=CapturePermanently_IncludingInitialMouseDown`. Assets in `Content/Input/`: `IMC_Default`, `IA_Move` (Axis2D), `IA_Look` (Axis2D, raw mouse delta, no modifiers), `IA_Jump`, `IA_Crouch`, `IA_Fire`, `IA_Zoom`, `IA_WeaponNext`, `IA_WeaponPrev`, `IA_Weapon1..IA_Weapon9`, `IA_Scoreboard`. `UVeloxInputConfig` maps each to a tag: native actions (`InputTag.Move`, `InputTag.Look.Mouse`, `InputTag.Jump`, `InputTag.Crouch`) are bound in `AVeloxCharacter` directly; everything else is routed to the ASC by tag (`InputTag.Fire`, `InputTag.Weapon.Slot1..9`, `InputTag.Weapon.Next/Prev`, `InputTag.Zoom`) — Lyra's tag-driven ability input. Jump is handled by movement, never by an ability: the movement component needs the held state every tick for auto-hop.

**`DefaultEngine.ini` essentials** (dev copies these verbatim, then verifies key names against the 5.8 `BaseEngine.ini`):
```ini
[/Script/EngineSettings.GameMapsSettings]
EditorStartupMap=/Game/Maps/L_Empty.L_Empty   ; M1 switches all three to L_MovementTest
GameDefaultMap=/Game/Maps/L_Empty.L_Empty
ServerDefaultMap=/Game/Maps/L_Empty.L_Empty
GlobalDefaultGameMode=/Script/Velox.VeloxGameMode
GameInstanceClass=/Script/Velox.VeloxGameInstance
[/Script/Engine.Engine]
AssetManagerClassName=/Script/Velox.VeloxAssetManager
bSmoothFrameRate=False
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/OnlineSubsystemUtils.IpNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
[/Script/Engine.RendererSettings]          ; forward renderer + MSAA, no Nanite/Lumen/VSM, static lighting allowed
r.ForwardShading=True
r.MSAACount=4
r.AntiAliasingMethod=3                      ; MSAA (FXAA=1 as the cheap fallback option in settings)
r.Nanite.ProjectEnabled=False
r.DynamicGlobalIlluminationMethod=0
r.ReflectionMethod=0
r.Shadow.Virtual.Enable=0
r.GenerateMeshDistanceFields=False
r.AllowStaticLighting=True
r.DefaultFeature.AutoExposure=False
r.DefaultFeature.MotionBlur=False
r.DefaultFeature.Bloom=False
r.DefaultFeature.LensFlare=False
r.DefaultFeature.AmbientOcclusion=False
r.SupportSkyAtmosphere=False
r.VSync=False
[/Script/OnlineSubsystemUtils.IpNetDriver]
NetServerMaxTickRate=60                     ; dedicated server tick is capped by this (UGameEngine::GetMaxTickRate)
MaxNetTickRate=60
MaxClientRate=100000
MaxInternetClientRate=100000
[/Script/Engine.Player]
ConfiguredInternetSpeed=100000
ConfiguredLanSpeed=100000
[/Script/Engine.GameNetworkManager]
ClientNetSendMoveDeltaTime=0.0166           ; client sends moves at <= 60 Hz
MaxMoveDeltaTime=0.125
[/Script/Engine.CollisionProfile]           ; names in Core/VeloxCollisionChannels.h
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel1,DefaultResponse=ECR_Block,bTraceType=True,Name="Weapon")
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel2,DefaultResponse=ECR_Block,bTraceType=False,Name="Projectile")
[SystemSettings]
net.IsPushModelEnabled=1
```
Why forward + MSAA: Q3 readability needs crisp edges and no temporal ghosting; maps have baked lighting and few dynamic lights (muzzle flashes, rockets), which is exactly the case where forward beats deferred on frame time. The costs (no Lumen/VSM, limited per-pixel dynamic lights, no deferred decals) are costs we want anyway. Mobile is not a target. **Network Prediction plugin and Mover are not used**: the custom `UCharacterMovementComponent` path (CLAUDE.md) is mature, Lyra-proven and what `AAIController` path-following drives.

`DefaultGame.ini`: `ProjectName=Velox`, packaging `Build=IfProjectHasCode`, `UsePakFile=True`, `bShareMaterialShaderCode=True`, `+MapsToCook` for each shipped map. `DefaultGameplayTags.ini`: `ImportTagsFromConfig=True`, `FastReplication=True` (same build on both ends; shrinks tag replication), `+GameplayTagTableList` none; designer tags in `Config/Tags/VeloxTags.ini`, code tags native (§2). `EngineAssociation` is `"5.8"`; on Linux the build scripts always invoke the engine via `$UE_ROOT` so the association is informational (optionally registered in `~/.config/Epic/UnrealEngine/Install.ini`); on the Mac the Launcher registers 5.8.

### 2. GAS usage

**ASC lives on `AVeloxPlayerState`**, `ReplicationMode = Mixed`, `AVeloxCharacter` implements `IAbilitySystemInterface` by forwarding to its PlayerState. Reasons for arena play: the pawn dies every ~30 s; granting weapon/passive abilities and the inventory on the PlayerState means respawn is "spawn pawn, call `InitAbilityActorInfo(PlayerState, NewPawn)`, apply `GE_SpawnInit`" with no re-grant, and spectating/dead players keep an ASC for scoreboard attributes and powerup timers. Costs: PlayerState `NetUpdateFrequency` must be raised from its 1 Hz default (we set 100; the server tick caps it at 60), and `InitAbilityActorInfo` must be called in both `PossessedBy` (server) and `OnRep_PlayerState` (client). Bots: `AVeloxBotController` sets `bWantsPlayerState = true` so bots take the identical path.

**Attribute sets.** `UVeloxAttributeSet` is the abstract base (ATTRIBUTE_ACCESSORS macro, push-model `MARK_PROPERTY_DIRTY`, `GAMEPLAYATTRIBUTE_REPNOTIFY`). Concrete: `UVeloxHealthSet` {Health, MaxHealth, Armor, MaxArmor; meta: IncomingDamage, IncomingHealing} and `UVeloxAmmoSet` {Bullets, Shells, Cells, Rockets, Lightning, Slugs}. Q3 armor rule (armor absorbs 2/3 of damage) and self-damage factor live in `UVeloxDamageExecution : UGameplayEffectExecutionCalculation`, reading tunables from `UVeloxCombatSettings : UPrimaryDataAsset` (M2). `UVeloxHealthComponent` on the pawn subscribes to attribute changes and broadcasts `OnDeathStarted/Finished` (Lyra health-component idea) so UI/game mode never touch the ASC directly.

**Granting.** `UVeloxAbilitySet : UPrimaryDataAsset` (Lyra ability-set idea) with `TArray<FVeloxAbilitySet_GameplayAbility{TSubclassOf<UVeloxGameplayAbility>, int32 Level, FGameplayTag InputTag}>`, `_GameplayEffect`, `_AttributeSet`, and `GiveToAbilitySystem(UVeloxAbilitySystemComponent*, FVeloxAbilitySet_GrantedHandles* Out)` / `TakeFromAbilitySystem`. `UVeloxPawnData` lists the default ability sets; weapon pickups grant their `UVeloxWeaponDefinition::AbilitySet`. `UVeloxGameplayAbility` adopts Lyra's `EVeloxAbilityActivationPolicy { OnInputTriggered, WhileInputActive, OnSpawn }` — `WhileInputActive` is how automatic weapons fire. Weapon details (inventory replication, switch times, fire abilities) are specified in the M2 ADR.

**Gameplay tag layout** (native tags via `UE_DECLARE_GAMEPLAY_TAG_EXTERN` in `Core/VeloxGameplayTags.h`; designers add leaves under the same roots in `Config/Tags/VeloxTags.ini`):
```
InputTag.{Move, Look.Mouse, Jump, Crouch, Fire, Zoom, Scoreboard, Weapon.Next, Weapon.Prev, Weapon.Slot1..9}
Velox.Weapon.{Gauntlet, MachineGun, Shotgun, PlasmaGun, RocketLauncher, LightningGun, Railgun}
Velox.Ammo.{Bullets, Shells, Cells, Rockets, Lightning, Slugs}
Velox.State.{Dead, SpawnProtected, WeaponSwitching, Firing}          ; server-set, replicated; movement state is NOT tagged
Velox.Damage.{MachineGun, Shotgun, Plasma, Rocket, Lightning, Rail, Gauntlet, Fall, Environment, Telefrag}  Velox.Damage.Self (added alongside the weapon tag)
Velox.Powerup.{Quad, Haste, Regen, BattleSuit}   Velox.Item.Health.{Small, Medium, Large, Mega}   Velox.Item.Armor.{Shard, Yellow, Red}
Velox.Ability.{Weapon.Fire, Weapon.Switch, Death, Respawn}
GameplayEvent.{Death, Damage, Pickup}     SetByCaller.{Damage, Knockback}
GameplayCue.Weapon.<Name>.{Fire, Impact}  GameplayCue.Character.{Damage, Death, Spawn, Jump, Land}  GameplayCue.Pickup.<Item>  GameplayCue.Powerup.<Name>
Velox.Team.{Red, Blue}  (M7)
```

**Gameplay cue policy.** Cues are cosmetic only and never read by simulation. One-shots (fire, impact, pickup) use `UGameplayCueNotify_Burst` (static, no actor); only persistent effects (Quad glow, Lightning beam) use `AGameplayCueNotify_Looping`. The predicting client plays its own cues immediately inside the ability's prediction window (GAS dedups the server echo by prediction key); server→others cues are unreliable multicasts. Cue assets live under `/Game/GameplayCues` (`GameplayCueNotifyPaths`), are async-loaded on clients and never loaded on the dedicated server (`UVeloxGameplayCueManager::ShouldAsyncLoadRuntimeObjectLibraries` returns false when `IsRunningDedicatedServer()`). Hit-impact cues carry location/normal in `FGameplayCueParameters`, not via extra RPCs.

**Lyra: adopted vs rejected.** Adopted: ASC on PlayerState; ability sets; pawn data; input config + `AbilityInputTagPressed/Released`; activation policy; health component over attribute set; asset-manager-driven `InitGlobalData`; native gameplay tags file; client-side target data transport for weapon fire (`FGameplayAbilityTargetDataHandle`). Rejected: Game Features / Modular Gameplay / Experience definitions (one game, no runtime feature plugins; the init-state machine of `PawnExtensionComponent`/`HeroComponent` exists only to serve that stack — we initialise explicitly in `PossessedBy`/`OnRep_PlayerState`); Common UI / CommonGame / CommonUser / UIExtension (console-centric and heavy; plain UMG with one `UVeloxUserWidget` base and a `UVeloxGameUserSettings : UGameUserSettings` subclass instead); GameSettings plugin; Lyra teams subsystem (M7 writes a small one); cosmetics system; Lyra's trust-the-client hit validation (replaced by server rewind, §3).

### 3. Networking model

**Topology.** Dedicated server is authoritative for everything; listen server exists only for local testing (Game target). Target scale ≤ 16 players, one map per server. Classic replication (no Iris, no Replication Graph) until M6, where `UReplicationGraph` (ShooterGame's graph as reference) is evaluated with real player counts.

**Movement prediction** goes exclusively through the CMC saved-move path, never ad-hoc RPCs:
- `FSavedMove_Velox : FSavedMove_Character` — adds Velox flags/payload, overrides `Clear`, `GetCompressedFlags`, `CanCombineWith`, `SetMoveFor`, `PrepMoveFor`. Reserve `FLAG_Custom_0..3` for future movement inputs; M1 needs none beyond Jump/Crouch, but the subclass exists so adding one never changes architecture.
- `FVeloxNetworkMoveData : FCharacterNetworkMoveData` + `FVeloxNetworkMoveDataContainer : FCharacterNetworkMoveDataContainer`, installed in the movement component constructor (`SetNetworkMoveDataContainer`). Extra serialised fields are added only when a move needs data that `Acceleration` (wish direction) and flags cannot express.
- `FNetworkPredictionData_Client_Velox : FNetworkPredictionData_Client_Character` allocating `FSavedMove_Velox`.
- `UVeloxMovementComponent` factors all friction / acceleration / air-acceleration / ramp math into world-free functions in `Movement/VeloxMoveSim.h` (`FVeloxMoveSim::ApplyFriction`, `Accelerate`, `AirAccelerate`, `ClipVelocity`, each taking `const UVeloxMovementSettings&` and `float Dt`), called from `PhysWalking`/`PhysFalling`. This is what makes the determinism tests (§4) possible and keeps bots on the same code.
- External velocity changes enter through one API: `UVeloxMovementComponent::AddKnockback(FVector Impulse, bool bOverrideVelocity)` accumulates into a pending launch consumed at the start of the next `PerformMovement` on whichever side calls it. Jump pads (`AVeloxJumpPad`, static trigger volume) call it from overlap, which both client and server simulate, so pads are predicted with no replication. Weapon knockback from the server is a legitimate mispredict corrected by the standard path; rocket-jump self-knockback is additionally predicted on the owning client when its fake projectile detonates (M2 ADR specifies the key/dedup).
- Server correction tuning (M1): `NetworkMinTimeBetweenClientAdjustments`, `ServerCheckClientError` tolerances, `NetworkSimulatedSmoothLocationTime` (0.1 s), `bNetworkSkipProxyPredictionOnNetUpdate`, pawn `NetUpdateFrequency = 60`, `MinNetUpdateFrequency = 30`. Gravity for pawns is a `UVeloxMovementSettings` tunable (`GetGravityZ()` override), not the project gravity. `UnitScale` (Q3 units → cm) is a tunable too; the M1 task proposes the value.

**Lag-compensated hitscan** (MG, SG, LG, RG), server side, `UVeloxLagCompensationSubsystem : UWorldSubsystem` (created only on the server):
- Each `AVeloxCharacter` carries a `UVeloxHitboxComponent` defining 8–12 bone-attached shapes (head, chest, pelvis, upper/lower limbs). Every server tick the subsystem records `FVeloxHitboxSnapshot { float ServerTime; TArray<FVeloxHitboxPose> }` per pawn in a ring buffer covering `HistoryWindow` (default 1.0 s at 60 Hz).
- Fire: the client's fire ability traces locally for immediate feedback (hit marker predicted), then sends `FGameplayAbilityTargetData_VeloxHitscan { FVector_NetQuantize10 Origin; FVector_NetQuantizeNormal Dir; float ClientFireServerTime }` through GAS target data (Lyra transport). `ClientFireServerTime` = `AGameStateBase::GetServerWorldTimeSeconds()` at fire minus the client's interpolation delay (`NetworkSimulatedSmoothLocationTime`), because the client aimed at proxies rendered that far in the past. `ServerWorldTimeSecondsUpdateFrequency` is lowered to 0.1 s.
- Server: `T = clamp(ClientFireServerTime, Now - MaxRewind, Now)` with `MaxRewind` default 0.25 s (tunable; Q3 unlagged allowed more, modern games less); `Origin` must lie within `MaxOriginError` of the shooter's eye at `T`, else snapped. Trace world geometry at current time (static), then intersect the ray with every other pawn's hitboxes interpolated between the two snapshots bracketing `T`; closest hit wins. Apply damage via GE with `SetByCaller.Damage` and `Velox.Damage.<Weapon>`. Rail/LG penetration rules are M2 details.
- Pawn capsules **ignore** the `Weapon` trace channel; world geometry blocks it. Hitscan vs pawns is resolved only by the subsystem, so there is one source of truth for "what the shot hit" and no mismatch between physics shapes and rewound shapes.

**Projectiles** (RL, PG): server spawns `AVeloxProjectile` (replicated, `UProjectileMovementComponent`, collides with pawn capsules on the `Projectile` object channel at current server time — no rewind, as in Q3). The owning client spawns a non-replicated `AVeloxFakeProjectile` at fire time; when the real one replicates in, the fake is destroyed and the real one is shown (UT4's `MyFakeProjectile` pattern). The server optionally forward-simulates the real projectile by `min(RTT/2, ProjectileForwardPredictionMax = 0.1 s)` so both line up. Explosions and knockback are server-only; self-knockback prediction as above.

**Rates.** Server 60 Hz; client move send ≤ 60 Hz; client render uncapped (`t.MaxFPS=0`, VSync off); `MaxClientRate = MaxInternetClientRate = 100000` B/s; GameState 10 Hz, PlayerState 100 (capped by tick), pawns 60/30. Push-model replication enabled globally; attribute sets and all `AVeloxPlayerState`/`AVeloxGameState` replicated properties use `DOREPLIFETIME_WITH_PARAMS_FAST` + `MARK_PROPERTY_DIRTY_FROM_NAME`.

### 4. Test strategy

- **Unit automation tests** in `Source/Velox/Tests/*Test.cpp`, wrapped in `#if WITH_DEV_AUTOMATION_TESTS`, using `IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVelox<Area><Name>Test, "Velox.<Area>.<Name>", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)` (dev verifies the exact flag spelling in the 5.8 `AutomationTest.h`). M0 ships `Velox.Core.Smoke` (asserts the module loaded and `UVeloxGameInstance::StaticClass()` is valid). Movement tests (M1) call `FVeloxMoveSim` directly, no world: friction decay over N ticks, air-accel speed gain for a scripted strafe sequence, bunny-hop speed retention across a landing, jump-pad launch vector.
- **Determinism test** (`Velox.Movement.Determinism`): record a scripted `TArray<FVeloxMoveInput{WishDir, bJump, Dt}>`; run it once through `FVeloxMoveSim` as the "client" and once as the "server replay" of the identical saved moves; assert bitwise-equal end state. Determinism is required for identical inputs and identical `Dt` per move (that is what the server replays); it is not required across different frame rates.
- **Functional test maps** `Content/Maps/Tests/FT_*.umap` with `AFunctionalTest` actors for anything needing a world (jump pad reaches platform, pickup respawn, bot reaches pad-only platform). Run headlessly with `-game -nullrhi`; if that proves unreliable on the GPU-less VM, they run on the Mac and the Linux CI runs unit tests only.
- **Networked verification** is human PIE on the Mac (Net Mode: Play As Client, Run Dedicated Server, 2 players) with `p.NetEmulation`/`Net PktLag=100 PktLoss=2`. Automated multi-process net tests are deferred; the determinism test plus the lag-compensation "rewind picks the right historical pose" unit test (M2) are the automated proxies.
- **Headless run (Linux, Mac):**
  `"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$VELOX_PROJECT" -ExecCmds="Automation RunTests Velox" -TestExit="Automation Test Queue Empty" -ReportExportPath="$OUT/automation" -nullrhi -unattended -nopause -nosplash -log`
  `build.sh` must parse `$OUT/automation/index.json` for failed tests (and grep the log for `Test Completed. Result={Fail`) rather than trusting the process exit code.

### 5. Build hosts, scripts, plugins

- **Linux VM (no GPU):** compile all targets, cook, run tests and dedicated servers; never PIE. Everything editor-side runs `UnrealEditor-Cmd … -nullrhi`. Engine at `$UE_ROOT` (from `infra/env.example`, proposed `/opt/UnrealEngine-5.8`). Installed vs source engine is detected by `test -f "$UE_ROOT/Engine/Build/InstalledBuild.txt"`.
- **Mac:** UE 5.8 from the Epic Games Launcher (`UE_ROOT="/Users/Shared/Epic Games/UE_5.8"`) + Xcode; builds Editor and Game targets, runs PIE playtests, packages Mac clients. No cross-compiling from Linux to Mac. Xcode workspace generated locally (gitignored) with `"$UE_ROOT/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" -project="$VELOX_PROJECT" -game`.
- **Target availability:** with an installed engine only `VeloxEditor` and `Velox` build. `VeloxServer`/`VeloxClient` require a source build. Until a source engine is available, the dedicated server is the Game target run with `-server` (`Velox -server -log -port=7777`; identical game logic, larger binary, NullRHI), and PIE's dedicated-server mode on the Mac. `build.sh` builds Server/Client when the engine is a source build and otherwise prints a loud `SKIPPED (installed engine)` line, exit code 0. ROADMAP M0's "compiles for VeloxServer" is therefore conditional — see open questions.
- **UBT invocations** (Development; `$P=$VELOX_PROJECT`):
  Linux: `"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" VeloxEditor Linux Development -Project="$P" -WaitMutex`, same for `Velox`, `VeloxServer`, `VeloxClient`.
  Mac: `"$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" VeloxEditor Mac Development -Project="$P" -WaitMutex`, same for `Velox`.
- **UAT invocations** (`RunUAT.sh BuildCookRun -project="$P" -unattended -utf8output -noP4 -nocompileeditor`):
  Linux client package: `-platform=Linux -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="$OUT/LinuxClient"`;
  Linux dedicated server (source engine): `-platform=Linux -server -serverplatform=Linux -noclient -serverconfig=Development -build -cook -stage -pak -archive -archivedirectory="$OUT/LinuxServer"`;
  Mac client: `-platform=Mac -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="$OUT/MacClient"`.
  Cooking needs no GPU (shader compilation is CPU via ShaderCompileWorker).
- **`infra/ci/build.sh`** (Linux) and **`build-mac.sh`**: `source infra/env` (gitignored, from `env.example`); steps `build` (targets per availability), `test` (headless automation, §4), optional `package`; `set -euo pipefail`; non-zero exit on any build or test failure; `--targets` and `--skip-tests` flags; logs under `Saved/CI/`.
- **`.uproject` plugins.** Enabled: `GameplayAbilities`, `EnhancedInput`, `OnlineSubsystem`, `OnlineSubsystemNull`, `OnlineSubsystemUtils`, `PythonScriptPlugin` (editor; headless map creation), `EditorScriptingUtilities` (editor). Disabled (default-on plugins we do not want loading or cooking): `Bridge`, `Fab`, `ModelingToolsEditorMode`, `ChaosVehiclesPlugin`, `Paper2D`, `MovieRenderPipeline`, `DatasmithContent`, `HairStrands` — dev verifies each name exists under `$UE_ROOT/Engine/Plugins` in 5.8 and drops any that another enabled plugin depends on. Not enabled until their ADRs: `OnlineSubsystemSteam`/`OnlineSubsystemEOS`, `ReplicationGraph` (M6); `NetworkPrediction`, `Mover` never.

## Consequences

- One module: fast compiles, trivial linking; discipline on include direction is a review item, not a compiler error. Splitting later costs a day because `VELOX_API` is already everywhere.
- ASC on PlayerState: respawn is cheap and inventory/powerups survive death trivially; the price is the two-sided `InitAbilityActorInfo` and the PlayerState update-frequency change, both one-time.
- Forward + MSAA + static lighting locks the art pipeline (M5) to baked light, simple materials, few dynamic lights — intended.
- Hitscan is resolved by one server subsystem, not physics: hit registration is testable in isolation and consistent; it means hitbox shapes must be authored once per character (M2/M5).
- Dedicated-server and client targets need a source engine. Until then the Game target with `-server` stands in; M6 cannot ship without a source build on the Linux host (disk ≥ 150 GB, RAM ≥ 32 GB, many hours of compile).
- No automated multi-process net tests before M6; human PIE sign-off on the Mac is the gate for M1/M2 netcode.

## Alternatives considered

- **Multiple modules** (`VeloxMovement`, `VeloxCombat`, …): cleaner boundaries, but export macros, module dependency plumbing and longer links for a three-agent team gain nothing yet. Revisit when `VeloxEditor` or a shared `VeloxCore` is genuinely needed.
- **ASC on the pawn**: simpler actor-info setup, but every respawn re-grants abilities/attributes and inventory must be saved out to the PlayerState anyway. Wrong trade for a game with a 30-second life expectancy.
- **Deferred renderer + TSR/TAA**: more lighting features, but temporal AA blurs and ghosts fast motion and Lumen/VSM are forbidden anyway. FXAA remains available as a settings option.
- **Network Prediction plugin / Mover**: better long-term rollback architecture, but still maturing, incompatible with the `UCharacterMovementComponent` requirement in CLAUDE.md and with AI path-following as it stands.
- **Trust-the-client hit validation (Lyra's default)**: simple, but exploitable and does not meet "lag-compensated hitscan". Server rewind is the standard arena-FPS answer.
- **Iris replication**: promising, but changes replication semantics; evaluated at M6 together with the replication graph.
- **GitHub Actions / hosted CI**: unavailable until 2026-11-01; local scripts now, wrap them in Actions later without changing the scripts.

## Implementation notes for dev / dev-hard

M0 skeleton checklist (`dev`; task files reference this list):
1. `Velox.uproject` (`EngineAssociation "5.8"`, module `Velox` Runtime/Default, plugins per §5) and the four `Source/*.Target.cs`.
2. `Source/Velox/Velox.Build.cs`, `VeloxModule.cpp`, `Core/VeloxLog.{h,cpp}`, `Core/VeloxCollisionChannels.h`, `Core/VeloxGameplayTags.{h,cpp}` (InputTag.* and Velox.State.* natives only), `Core/VeloxGameInstance`, `Core/VeloxAssetManager`, `Core/VeloxPawnData`, `Input/VeloxInputConfig`, `Input/VeloxInputComponent`, `Movement/VeloxMovementComponent` (empty subclass, constructor installs `FVeloxNetworkMoveDataContainer`), `Movement/VeloxMovementSettings` (fields: `MaxGroundSpeed`, `GroundAcceleration`, `GroundFriction`, `AirAcceleration`, `AirMaxWishSpeed`, `JumpVelocity`, `GravityZ`, `UnitScale`, `RampSlideMaxAngle`, `StepHeight` — values filled in M1), `Movement/VeloxNetworkMoveData.{h,cpp}` (empty-but-complete saved-move/container/prediction-data subclasses), `Character/VeloxCharacter`, `Combat/VeloxAbilitySystemComponent`, `GameModes/{VeloxGameMode, VeloxGameState, VeloxPlayerState, VeloxPlayerController}`, `UI/VeloxHUD`, `Tests/VeloxSmokeTest.cpp`.
3. `Config/DefaultEngine.ini`, `DefaultGame.ini`, `DefaultInput.ini`, `DefaultGameplayTags.ini`, `Tags/VeloxTags.ini`, `DefaultEditor.ini` — contents per §1/§2.
4. `Content/Maps/L_Empty.umap` via `Tools/editor/create_empty_map.py` run with `UnrealEditor-Cmd "$P" -run=pythonscript -script=... -nullrhi -unattended` (fallback: the human saves an empty map on the Mac). `Content/Input/IMC_Default` + `IA_*` and `Content/Data/DA_PawnData_Default`, `DA_MovementSettings_Default` the same way (fallback identical).
5. `infra/ci/build.sh`, `infra/ci/build-mac.sh`, `infra/env.example` (`UE_ROOT`, `VELOX_PROJECT`, `VELOX_OUT`).
6. `.gitattributes`/`.gitignore` per task 01; add `*.xcworkspace`, `*.xcodeproj`, `Saved/`, `Intermediate/`, `Binaries/`, `DerivedDataCache/`, `.vscode/`, `infra/env`.

M1 entry points (`dev-hard`): implement `FVeloxMoveSim` first (unit-tested, world-free), then `UVeloxMovementComponent::PhysWalking/PhysFalling/ProcessLanded/DoJump` on top of it, then saved-move flags and corrections, then `AVeloxJumpPad` and the debug `AVeloxHUD`, then `L_MovementTest`. Never bypass the saved-move path; report any ADR disagreement rather than deviating.
