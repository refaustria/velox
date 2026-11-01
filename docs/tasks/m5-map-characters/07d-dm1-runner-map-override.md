# 07d — `--map` for DM1Run and FT_DM1_Reachability

**Milestone:** M5 First real map + characters
**Agent:** dev
**Status:** todo
**Depends on:** 15 (`L_DM1` exists; report deviation: both hard-code `L_DM1_Blockout`)

## Goal
`infra/ci/fts.sh` and the DM1Run script in `infra/ci/nettest.sh` hard-code `/Game/Maps/DM1/L_DM1_Blockout`. After this task both run on `L_DM1` (the art map) with a flag, so the human and later CI can check the real map without hand-edited copies. Default stays `L_DM1_Blockout` until decision 40 at acceptance.

## Scope — files/modules the agent may touch
- `infra/ci/fts.sh` — an option (e.g. `--dm1-map <name>` or env `VELOX_DM1_MAP`) for the FtDm1Reachability map
- `infra/ci/nettest.sh` — DM1Run honours `--map` (or the same env), band file looked up as for the other scripts
- `infra/ci/README.md` — document it
- No `Source/` change unless the FT map is compiled in; if it is, report back first.

## Acceptance criteria
- [ ] `infra/ci/fts.sh` default unchanged: 8/8 PASS.
- [ ] FtDm1Reachability on `L_DM1` via the new option: PASS, checks=5 failed=0.
- [ ] `nettest.sh --scripts DM1Run --map L_DM1 --repeat 1`: PASS, 3 pads, strays 0; default run on `L_DM1_Blockout` unchanged.
- [ ] Every UE step under `flock -o <scratchpad>/soak.lock`, all UE processes you start are killed at the end.
