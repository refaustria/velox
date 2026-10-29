# 09 — Combat playtest checklist

**Milestone:** M2 Combat
**Agent:** dev
**Status:** done (checklist `docs/playtests/m2-combat-checklist.md`, 29 items)
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

## Addendum from the architect review of 05/06 (2026-10-29)
- Rocket-jump feel at 100 ms (no snap at launch or landing); plasma climb; rocket hit marker; death camera (facing, distance, wall back-off; should it turn toward the killer?); hit-marker visibility; respawn on fire/jump after 1.7 s; readability of all 8 palette colours against the greybox; does 0 s spawn protection feel right (open question 2).

## Outcome (2026-10-29)
- `docs/playtests/m2-combat-checklist.md`: Mac setup, 29 items (weapons 1–9, damage/armor/rocket jump 10–15, feedback/switch 16–21 incl. 250 ms, death/respawn/colours 22–27, frame rate 28–29), feedback template, sign-off block.
- Unverified from source: physical keys for slots and left-mouse fire (binary input assets); `GiveAllWeapons` reaching the server world in dedicated-server PIE (fallback: Play As Listen Server); `Net PktLag` settings copied from the M1 checklist.
- Gap: no health/armor HUD yet; the checklist reads values from the Output Log. The HUD belongs to M3.
