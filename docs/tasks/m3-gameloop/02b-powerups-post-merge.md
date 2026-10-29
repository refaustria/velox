# 02b — Powerups post-merge cleanups

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** todo (start after 01b merges)
**Depends on:** 02, 05, 01b

## Scope / items (from `notes/02-report.md` "Open")
- `VeloxEffects::GetMatchFrozenTag()` returns `VeloxMatchTags::State_MatchFrozen` (native tag from `GameModes/VeloxMatchEffects.h`) instead of the by-name lookup.
- Fire abilities: add MatchFrozen to `ActivationBlockedTags` in the constructor; remove the `CanActivateAbility` frozen check.
- `Velox.Items.PowerupStacking`: enable the frozen part (currently skipped with `AddInfo`).
- `Tools/editor/create_bootstrap_content.py`: add a `PowerupTint` vector parameter (default black/no tint) to `M_PlayerBody` so the tint becomes visible; regenerate only `M_PlayerBody`.
- Merged build check of m3-gameloop after 01b/02/03 merges as available.

## Acceptance criteria
- [ ] `infra/ci/build.sh` green, test count reported.
- [ ] `velox.items.PowerupTest quit` PASS; log shows the tint parameter applied.
- [ ] A frozen pawn cannot fire (test row).
