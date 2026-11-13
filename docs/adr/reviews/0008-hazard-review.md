# Review — ADR 0008 §3, hazard and bot-edge track (tasks M7 H1, H2, H3, H4, H5, H7)

Reviewer: architect (Fable 5.2), 2026-11-13, task `docs/tasks/m7-maps/11-h6-hazard-review.md` (scope extended by the lead to H7).

**Code:** `origin/m7-maps` at 9e6079a (`git diff origin/main..origin/m7-maps`): `Combat/VeloxHealthComponent.*`,
`VeloxHazardRules.h`, `VeloxHazardVolume.*`, `VeloxHazardSubsystem.*`, `VeloxCombatSettings.h`, `Core/VeloxGameplayTags.*`,
`UI/VeloxHUDFormat.h`, `Bots/VeloxBotCombat.*`, `VeloxBotController.*`, `VeloxBotPathFollowingComponent.*`, `VeloxBotManeuver.h`,
`VeloxBotSettings.h`, `VeloxBotSubsystem.cpp`, `VeloxNavAreas.*`, `VeloxNavCoverage.*`, `Tools/editor/create_hazard_test_map.py`,
`Tools/editor/maps/fornax_layout.py`, the runner changes in `infra/ci/`.
**Read against:** ADR 0008 §3 and §5, ADR 0003 (damage path), ADR 0004 §6, ADR 0005 §5/§10 (D17, D18, D44, D47), ADR 0007 F2,
task files 06–10 and 26 with their notes.
**Method:** code reading only. Unreal was not run. Every measured number below is the implementer's, from the task notes.
Findings marked *predicted* follow from the code and the layout numbers and have not been observed in a run.
`create_map.py --map dm1 --golden check` was run at the end: OK, byte-identical.

## Verdict: APPROVE WITH FOLLOW-UPS, two of them before B4 (Fornax)

The combat half (H1, H2) is sound and matches §3.2/§3.3: server-only, nothing replicated or predicted, the movement component
untouched, scoring through the existing death path. Keep it as merged.

The bot half works on `L_HazardTest`, which is what its tests measure. Two defects (F1, F2) come from the H7 lava nav area and
will show on Fornax, not on the fixture; they must be fixed before B4. One all-map behaviour change (the H7 pad-flight steering)
is correct but has not been confirmed on DM1 and `L_ItemTest` under quiet-host conditions (F3). The cost of the new bot code
has not been judged on any run (F4).

## What was verified (no finding)

- **Last attacker (H1).** Written only from the health set's `OnDamaged` on the authority, before the re-broadcast; rejected
  events never clear it; reset in `InitializeWithAbilitySystem` / `UninitializeFromAbilitySystem`. `ShouldRecordAttacker`
  excludes `Damage.Self`, `Damage.Hazard*` and `Damage.Environment`, so a credited lava tick (instigator = attacker) cannot
  refresh its own credit. Window edge is strict (`age < window`).
- **Damage path (H2).** `MakeDamageSpec` on the credited attacker's player-state ASC, else on the victim's own ASC with
  `bSelf = false`, as `FellOutOfWorld`. `HandleDeath` resolves the killer from the instigator (player state or pawn), so no
  scoring code changed. `FellOutOfWorld` is untouched and skips corpses, so a corpse falling through the void slab to KillZ
  logs no `world_kill`.
- **Lava timing.** Contact hit on entry, skipped if the last hit is younger than `LavaTickSeconds`; a left pawn's record is
  kept 1 s; the actor ticks only with occupants; only the capsule counts.
- **Hazard subsystem.** Registered on server and clients, queried by bots and lint only, applies no damage.
- **Gate.** Every bot entry point added by H4/H5/H7 returns on `VeloxBotCombatGlue::IsEdgeAware` except the two listed under
  "DM1 / L_ItemTest" below.
- **Runners.** `world_kill` still matches `Damage.Environment` only; hazard tags are outside that subtree.

## Net version

- Three native tags were added (`Velox.Damage.Hazard`, `.Lava`, `.Void`). ADR 0007 F2 folds the tag hash into the network
  version, so every `m7-maps` build is already incompatible with the deployed 0.6.1 server and clients. That is the intended
  effect, no code is needed.
- Nothing else in the track touches the wire: no replicated property, no RPC, no new replicated actor.
  `AVeloxHazardVolume` is a non-replicated level actor, `UVeloxNavArea_Hazard` exists on the server only.
- `Config/DefaultGame.ini` still says `ProjectVersion=0.6.1`. The next release must bump it (0.7.0) and ship server and client
  downloads together (ADR 0008 §5.2 already says so). Until then no `m7-maps` build goes to production.

## DM1 / L_ItemTest: what changed off hazard maps

| Change | Off hazard maps | Verdict |
|---|---|---|
| H4 edge overlay, hop landing guard | gated, inert | unchanged |
| H5 escape, airborne recovery, pad zeroing | gated, inert | unchanged |
| H7 probe hazard rejection, own-hop landing, nav area | gated / lava volumes only | unchanged |
| H1 last-attacker record | written on every map, read by hazard volumes only | no behaviour |
| H7 `VeloxNav link_span` warning | log line at nav build, all maps | no behaviour |
| **H7 pad-flight arrive wish** (`UVeloxBotPathFollowingComponent::TickManeuver`) | **active on every map** | **behaviour change, see F3** |

ADR 0008 §3.6 and its Consequences promise that the DM1 and test-map bands stay valid "by construction". Since H7 that is
no longer true for JUMP_PAD flights.

## Defects, ranked

### Before B4 (Fornax)

**F1 — The lava nav area covers Fornax's slag bridge, most of the Red Armor island and every lava-hop link end (*predicted*).**
`AVeloxHazardVolume::AddLavaNavArea`, `UVeloxBotSettings::HazardNavPaddingCm` (100) / `HazardNavPaddingZCm` (80),
`UVeloxNavArea_Hazard` (cost 50, entering 5000).
The modifier box is the volume grown by 100 cm sideways and 80 cm up. On `L_HazardTest` that is a strip beside one channel.
On Fornax (`fornax_layout.py`): `Slag_Bridge` is 200 cm wide between `Pool_SW` and `Pool_SE`, so both paddings meet in the
middle and the whole bridge is hazard area; `RA_Island` is 300 × 300 with lava on three sides, so only a core of about
100 cm around the armor is free; the lava hops start 60 cm before the take-off edge and end 100 cm onto the landing, so every
hop link starts inside the area; the 300 cm strip between `Trench_T1` and `Trench_T2` keeps a 100 cm core.
`UVeloxBotItemRouter` computes travel time from `max(Path.Cost, Path.Length)` (`VeloxBotItemRouting.cpp:286`). A route over
the bridge costs at least 5000 + 250 × 50 = 17 500 units against 250 cm of length, about 29 s of extra ETA at 600 cm/s.
Expected result: the router ranks the Red Armor as far away, the hop links are never chosen, and the B4 gate "RA and Mega
taken every round" fails. On Fornax the bed has no nav (`kind hazard`), so the area buys nothing there except this penalty.
*Fix:* the modifier marks the lava footprint only: horizontal padding 0, vertical from the bed to 10 cm above the surface
(below the 20 cm minimum drop the validator demands). Keeping bots off the rim is the job of the edge probe and the hop
guard, and H7's own A/B shows it: "routes alone never entered the lava in either mode". Add a nav-build lint in
`UVeloxBotSubsystem::MarkNavReady`: `VeloxNav hazard_area` (Warning) for every pickup, spawn and link end whose nav poly has
area `UVeloxNavArea_Hazard`; count must be 0 on every map. Re-run `FtBotLavaAvoid` seeds 1–5.
*Also check in that task:* H5 and H7 notes speak of "the bed's mesh", ADR 0008 §1.3 makes the bed invisible to navigation.
Dump whether nav polys exist inside the lava box on `L_HazardTest`. If they do, the generator rule is not doing its job and
that is its own defect; if they do not, say so in the notes.

**F2 — The nav-lint hazard exemption grew by the nav-area padding.**
`VeloxNavCoverage::Run` builds `HazardBoxes` from `Volume->GetComponentsBoundingBox(true)`, which includes the non-colliding
`LavaNavArea` child added by H7. Evidence: `navlint.sh L_HazardTest` reports `hazard_m2 70`; the channel is
300 × 1200 cm = 36 m², the padded box 500 × 1400 cm = 70 m². So 100 cm of rim floor around every lava volume is exempt from
the coverage gate, on Fornax the whole slag bridge and most of the island. ADR 0008 §3.5 exempts "samples inside a hazard
volume", nothing more.
*Fix:* use `Volume->GetBox()->Bounds.GetBox()`. Expected `hazard_m2` on `L_HazardTest`: 36 plus the void slab's share
(state the split in the notes). F1's fix shrinks the box as well, but the lint must not depend on that.

### High

**F3 — The H7 pad-flight steering changed DM1 and `L_ItemTest` behaviour and was not confirmed there.**
`UVeloxBotPathFollowingComponent::TickManeuver`, `UVeloxBotSettings::PadArriveDeadbandCmS`.
The change itself is right (ruling 3). The evidence is thin: BotRun × 3 on `L_DM1_Blockout` with 12 pad attempts in round 1,
cost FAIL in all rounds with no base A/B, `max_dwell 8 s` in round 1 recorded as behaviour PASS without naming the band that
allows it, and no `L_ItemTest` run at all although BotDuel there has the known marginal pad (ADR 0007 F2).
*Fix (runs, no code):* on a quiet host, the default nettest list once, BotRun × 3 and BotDuel × 3 on `L_ItemTest`,
FFA × 10 on `L_DM1`; JumpPad success per link compared with the stored sets; the dwell line explained or failed. If JumpPad
on `L_ItemTest` improves, the F2 note in ADR 0007 about `VeloxNavLink_0`/`_4` can be closed by the lead.

**F4 — The cost of the hazard behaviours has never been judged, and three queries are uncached.**
Every cost verdict in H4, H5 and H7 is FAIL "host load"; H7 has no base A/B. No run on a hazard map has a cost verdict.
Per bot on a hazard map: `HasHopLanding` runs one nav projection and one line trace on every grounded tick that wants a hop
(a hopping bot: every landing tick, and every tick while refused); `UpdateHazardLanding` runs the same pair on every airborne
tick of every own hop until a target is latched; the airborne recovery calls `FindHazardEscapeTarget` (65 projections) on
every tick while the predicted landing is over lava.
*Fix:* cache `HasHopLanding` for `EdgeProbeCheckSeconds` keyed on the grounded stretch; `UpdateHazardLanding` probes once per
`EdgeProbeCheckSeconds`; the recovery re-searches its safe target at `HazardEscapeSearchSeconds`, not per tick. Then one
quiet-host BotDuel × 3 on `L_HazardTest` (or the first Fornax set) with the D47 cost windows judged.

**F5 — The hop landing guard refuses every hop on a slope steeper than about 10° (*predicted*).**
`UVeloxBotPathFollowingComponent::HasHopLanding`, `AVeloxBotController::UpdateHazardLanding`.
The landing point is taken at the take-off height and projected with a vertical half extent of `OnMeshHalfHeightCm` (100).
A hop at 800 cm/s covers about 580 cm; on Fornax's 30° chute the ground is 330 cm lower (or higher) there, on a 20° ramp
210 cm. The projection fails and the hop is refused, so on hazard maps Hard bots walk every ramp and stair. Not a death, but
it changes how fast bots play exactly on the two maps whose bands are still to be derived.
*Fix:* find the ground first: one blocking trace down from `Landing + (0, 0, hop apex)` over
`apex + Velocity2D × AirSeconds × tan(walkable slope)`, then project the hit with the 20 cm radius and a 50 cm half height.
A landing with no ground in that window stays refused. Unit test in `Velox.Bots.CombatRules` for the window; one FT case on a
ramp added to `L_HazardTest` or checked on Fornax's chute in B4.

### Medium

**F6 — The edge overlay probe still uses `GetOnMeshExtent()` (100 cm).**
`VeloxBotCombatGlue::Overlay`. H4 found that a 100 cm extent accepts points about 55 cm past a ledge and fixed it for the
hop guard only. The overlay probe has the same flaw; its 150 cm reach hides it, with little margin (FtBotEdge: minimum edge
distance −18 / 5 / 5 cm over three seeds).
*Fix:* a setting `EdgeProbeProjectRadiusCm` (default 45, the agent radius, so "on nav" means "the capsule centre is over
floor"); re-run FtBotEdge seeds 1–3 and report mirror/drop counts and the minimum edge distance. If drops rise so far that the
bot stops fighting (target visible < 50 %), shorten `EdgeProbeCm` instead of widening the radius.

**F7 — The backed-off wish is not probed.** `Overlay`, `bEdgeHazardBack`: after a Drop whose remaining wish leads over lava
the wish is reversed for the 0.1 s cache period without a probe. On a 300 cm island the reverse can point at lava or at a
ledge too. *Fix:* probe the reversed wish; if it fails, write no overlay wish (the path wish stays).

**F8 — Bots "hear" the credited attacker on every lava tick.** `VeloxBotCombatGlue::HandleDamaged` resolves
`Event.Instigator`; for a credited hazard hit that is the attacker, so a burning bot gets the attacker's exact position
(`LastSeenLocation`) four times a second without line of sight, and arms a reaction. *Fix:* return early when the event's
tags match `Velox.Damage.Hazard`. One line; unit-test with a credited lava event.

**F9 — The runners have no `hazard_death_self` gate.** ADR 0008 §5.3 makes it a provisional hard gate for Fornax and Orbita;
`nettest.sh`, `ffa_summary.py` and `ffa_set.py` print the metric as "report-only, never judged". *Fix:* band variables
`BAND_HAZARD_SELF_SET_MAX` and `BAND_HAZARD_SELF_ROUND_MAX`, judged when set, in `bands/L_Fornax_Blockout.sh` (6 over
FFA × 10, 1 per BotDuel round) and `bands/L_Orbita_Blockout.sh` (10, 2). A band file that sets only these must not switch
the other bands from report-only to judged.

**F10 — A bot hit during a pad flight on a hazard map has no recovery.** `ApplyHazardBehaviours` returns for `IsOnLink()`
before the foreign-flight branch and zeroes the input for the pad flight, so a rocket hit mid-arc leaves the bot ballistic
into the void. Resolved by ruling 3 (the arrive wish stays active on hazard maps).

### Low

**F11 — Lava tick drift.** `AVeloxHazardVolume::Tick` sets `LastDamageTime = Now`. The interval is `LavaTickSeconds` rounded
up to the next server frame, exact only at a 60 Hz step; on the production host's 8–9 ms frames it is about 3 % slow.
*Fix:* advance by `LavaTickSeconds` (`LastDamageTime += Tick`, clamped so at most one hit is applied per frame). The FT's
"0.250 s apart" then holds at any frame rate.

**F12 — `Combat/` reaches into `Bots/`.** `VeloxHazardVolume.cpp` includes `Bots/VeloxBotSettings.h` and
`Bots/VeloxNavAreas.h` and reads the cvar `velox.bots.EdgeAware` by name at load. §3.3 says the volume knows nothing about
bots. *Fix:* `UVeloxBotSubsystem` creates the nav modifier per lava volume when nav setup starts (it already owns nav
readiness and can read the subsystem's volume list); the volume keeps no `NavArea` member. This removes the "A/B only at
load" trap together with F1.

**F13 — `ProjectsOntoNav` traces by object type `WorldStatic`.** It hits any query-enabled WorldStatic component, also
overlap-only ones (pad and pickup triggers, the void slab). A trigger above a rim hides the hazard under it, and a void slab
within `HazardTraceDepthCm` would be treated as lava (`bEdgeHazardBack`). No current map shows either. *Fix:* trace the
`Pawn`-blocking channel for "is there a floor above the lava" and ask `UVeloxHazardSubsystem::IsInHazard(Point, Lava)` for
the point under it; add `Type` to the query so void never triggers the back-off.

**F14 — A void entry that deals no damage becomes a `world_kill`.** Void applies one hit on begin overlap; if the spec is
refused (invulnerable test dummy, a future spawn protection or Battle Suit) the pawn falls on to KillZ and `FellOutOfWorld`,
which the runners count as a defect. Accepted for M7: nothing in the five maps makes a live player immune. Whoever adds an
immunity must exempt `Damage.Hazard.Void` from it.

**F15 — H3's task file was never filled.** `08-h3-hazard-metrics.md`: status open, no notes, all boxes unchecked, among them
"DM1 `--reeval` unchanged". The code is merged. *Fix (lead):* record the verification or have `dev` run `--reeval` on a
stored DM1 set and note the result.

**Accepted as is.** A credited attacker who left the server credits nobody and the victim gets the suicide (−1): simplest
rule, rare. `HazardEscapeMaxSeconds` restarts the escape on the next tick while the bot is still in lava: harmless, the lava
kills first. A corpse lying in lava keeps the volume ticking until the corpse timer removes it: negligible.

## Rulings on the six flagged deviations

1. **H4, hop landing projection with 20 cm instead of `GetOnMeshExtent()`: accepted.** The ADR wording was wrong.
   `GetOnMeshExtent()` answers "is the bot near the mesh"; a guard that must refuse a point past a ledge needs a radius below
   the agent radius, because the mesh is eroded by 45 cm. The same reasoning applies to the overlay probe (F6). The vertical
   window needs the F5 fix. Amendment G2.
2. **H5, airborne recovery on "foreign flight" instead of `IsFlight` alone: accepted.** `IsFlight` becomes true 0.83 s after
   take-off, by then the bot is more than 300 cm below the ledge and air control cannot bring it back. The latch (no jump key
   at take-off, or knockback grace seen, or `IsFlight`) excludes exactly the bot's own hop, which H7 covers separately when
   its landing is over lava. Walking down stairs or over a crest latches a foreign flight too; that is harmless, a landing on
   the mesh is "on course" and writes no wish. Amendment G3.
3. **H7, pad flights steer onto the landing trajectory on all maps; should that replace zero input on hazard maps: yes.**
   On all maps: accepted as a bug fix (the old wish "toward End at scale 1" accelerates any pad slower than `AirMaxWishSpeed`
   past its target; Sacrarium 86 % to 100 %), subject to the F3 confirmation runs. On hazard maps: the arrive wish replaces
   the zero input. It is zero whenever the flight is on its ballistic course (deadband 30 cm/s), so an undisturbed pad flight
   is unchanged, and it is the only thing that corrects a launch from the trigger edge or a hit in mid-arc (F10). One
   behaviour on every map, one code path. Condition: `FtBotKnockOff`'s pad part stays 10/10 with the zeroing removed, and it
   gains a seeded case with a 300 cm/s sideways impulse during the pad flight (lands on the platform in ≥ 8 of 10). Aim stays
   live as before. Amendment G4.
4. **H7, lava carries a costly nav area that extends 100 cm past the volume: "costly, not excluded" accepted, the 100 cm
   rejected.** Excluding the area would leave a bot in the lava without a path and make a goal reachable only through lava
   unreachable; a cost is right. The padding is what breaks Fornax (F1) and the lint (F2): footprint only. Amendment G5.
5. **H2, overlap events enabled when the world is not a client instead of `HasAuthority()`: accepted.** A level actor with
   `bReplicates = false` has `ROLE_Authority` on clients, so `HasAuthority()` would run the damage path on every client. The
   test `IsGameWorld() && GetNetMode() != NM_Client` is evaluated in `PostInitializeComponents` and again in the overlap
   handler, which covers a world whose net mode is settled late. Rule for the code base: for non-replicated level actors,
   "server only" is a net-mode test, never a role test. Amendment G1.
6. **H1/H2, hazard damage not Quad-scaled because Quad is applied at fire time: accepted.** `VeloxDamageExecution.cpp` reads
   nothing from the source ASC that scales damage, and `FtHazard` `lava_quad` proves 25 per tick with Quad on the attacker.
   The invariant this depends on is now written down (G7): a hazard spec is the only damage whose source ASC did not fire a
   weapon, so anything that later reads the source ASC in the execution or in statistics must skip `Velox.Damage.Hazard`.

## Proposed amendments

Not yet written into the ADRs: G4, G5 and G6 change code that the follow-up task below still has to deliver. The lead copies
them into `docs/adr/0008-m7-five-maps.md` "Amendments" (and the D/C lines into ADR 0005 and 0004) when that task is merged.

**ADR 0008**

- **G1 (§3.3).** "Overlap events are enabled on the authority only" reads: enabled only in a game world whose net mode is
  not `NM_Client`. Non-replicated level actors have authority on clients; role is not the test.
- **G2 (§3.6.1, §3.6.2).** Edge and landing probes do not use `GetOnMeshExtent()`. The hop landing guard projects with
  `HopLandingProjectRadiusCm` (20); the overlay probe with `EdgeProbeProjectRadiusCm` (45). The landing point is placed on
  the ground found under the predicted landing (slope window), not at the take-off height.
- **G3 (§3.6.3).** Airborne recovery acts on a *foreign flight*: an airborne stretch that began without the jump key, or in
  which a knockback grace was seen, or that outlasted the bot's own hop. It steers from the predicted landing toward the
  nearest nav point at or below the feet (600 cm extent, 800 cm down) that lies outside every hazard volume. The bot's own
  hop is corrected only when its predicted landing is over lava.
- **G4 (§3.6.3, Consequences).** Pad discipline: in the airborne phase of a `JUMP_PAD` maneuver, after the pad's launch, the
  only move input is the ballistic arrive correction toward the link end (`ArriveWish`, deadband `PadArriveDeadbandCmS`),
  on **every** map. No zero-input rule remains. This is the one bot behaviour of the hazard track that is active off hazard
  maps; the sentence "bot behaviour on DM1 and the test maps is unchanged by construction" is withdrawn for pad flights and
  replaced by the F3 confirmation sets.
- **G5 (§3.6, new 3.6.5 "lava avoidance").** (a) Edge probe and hop landing guard reject a point whose ground is lava.
  (b) Each lava volume marks the nav polys inside its own footprint (no horizontal padding, bed to 10 cm above the surface)
  with `UVeloxNavArea_Hazard`, costly and not excluded; the modifier is created by `UVeloxBotSubsystem`, not by the volume.
  (c) No pickup, spawn or link end may lie on a hazard-area poly (`VeloxNav hazard_area`, count 0). Void needs no area.
- **G6 (§3.5).** The nav-coverage exemption is the hazard volume's own box. `hazard_death_self` is judged by the runners
  through `BAND_HAZARD_SELF_SET_MAX` / `BAND_HAZARD_SELF_ROUND_MAX` when a band file sets them; the §5.3 numbers go into the
  Fornax and Orbita blockout band files.
- **G7 (§3.3).** Hazard damage is applied from an ASC that did not fire a weapon. Damage scaling by the source (Quad) stays
  at fire time; any code that reads the source ASC or the instigator of a damage event for scaling, statistics, hit feedback
  or bot perception skips events tagged `Velox.Damage.Hazard`. Lava ticks advance by `LavaTickSeconds`, independent of the
  server frame time.

**ADR 0005** (the D-numbers §3.6 promised "when implemented")

- **D52 — Edge-aware overlay (§5, §10; ADR 0008 §3.6.1, G2).** As implemented in `VeloxBotCombatGlue::Overlay`: Keep,
  Mirror, Drop, cached `EdgeProbeCheckSeconds`; after a Drop the remaining wish is probed and, over lava, reversed only if the
  reversed wish probes clean. Hazard maps only.
- **D53 — Hop landing guard (extends D18; ADR 0008 §3.6.2, G2).** `HasHopLanding`, hazard maps only.
- **D54 — Airborne recovery and pad arrive (extends D17; ADR 0008 §3.6.3, G3, G4).** Recovery on hazard maps only; the pad
  arrive wish on all maps.
- **D55 — Hazard escape and lava avoidance (ADR 0008 §3.6.4, G5).** Escape runs ahead of `UpdateOffMeshRecovery`; bots ignore
  hazard-tagged damage events for hearing (F8).

**ADR 0004**

- **C12 — Hazard deaths (§6; ADR 0008 §3.2, §3.3).** A death tagged `Velox.Damage.Hazard.*` is a frag for the last other
  player who damaged or knocked the victim within `HazardKillCreditSeconds` (5), else a suicide (−1). KillZ deaths
  (`Damage.Environment`) never credit anyone. Feed: "A [LAVA] B", "B [VOID] (suicide)".

## Follow-up tasks

| Id | Tier | Task | Acceptance |
|---|---|---|---|
| H8 | dev-hard | F1, F2, F4 (caches), F5, F6, F7, F8, F11, F12, F13 and ruling 3 (remove the pad zeroing). Files: `Bots/*`, `Combat/VeloxHazardVolume.*`, `Bots/VeloxNavCoverage.cpp`, `Tests/VeloxBot*Ft.cpp`, `Tests/VeloxHazardFt.cpp` | build and automation green; `fts.sh` green incl. the new knock-in-pad-flight case; `navlint.sh L_HazardTest` PASS with `hazard_m2` equal to the volume boxes; `VeloxNav hazard_area` count 0 on `L_HazardTest` and in `create_map.py --map fornax` terms (no item, spawn or link end inside a lava footprint, already validated); FtBotEdge, FtBotLavaAvoid seeds 1–5, FtBotLavaEscape unchanged green; DM1 golden OK. **Before B4.** |
| H9 | dev | F9 (runner gate, band files), F15 (H3 notes, DM1 `--reeval`), then the F3 and F4 runs on a quiet host after H8 is merged | hazard gate judged in a dry run on a stored set; default nettest list, BotRun × 3 and BotDuel × 3 on `L_ItemTest`, FFA × 10 on `L_DM1` inside their bands with per-link JumpPad numbers; one cost-judged set on a hazard map |

B4 (Fornax) then starts on a tree where the router sees the Red Armor at its real distance. If its first soak still shows
bots avoiding the island or the hops, that comes back as a layout or routing question, not as a nav-area one.

## Open questions for the human

None.
