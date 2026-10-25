# 09 — Combat playtest checklist

**Milestone:** M2 Combat
**Agent:** dev
**Status:** todo
**Depends on:** 01–08

## Goal
Implement ADR 0003 implementation-notes task **`09-human-playtest`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `docs/playtests/m2-combat-checklist.md`

## Acceptance criteria
- [ ] Checklist: all six weapons, armor/health pickups absent (M3) so TestDamage/console gives armor, rocket jump, rail duel at 100 ms / 2 %; sign-off block.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.

## Addendum from the architect review of 04 (2026-10-25)
- Requires 04c (server-side pattern seed, cue policy) merged first.
- Checklist additions: "switch feel at 100 ms: does the first shot after a switch come when you expect?" (B18) and "redirecting a switch mid-drop" (review defect 9).
