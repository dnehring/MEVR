<!-- SPDX-License-Identifier: MIT -->
# Workstream integration and worktree cleanup, 2026-11-15

## Result

The ME3 checkpoint `0f02fd38` (119 commits beyond the previous main) was
reconciled with main's four newer commits and landed on local main as
`ccc9325d`. Documentation conflicts retained the newer native evidence and
the overall handoff. The source difference between the previous ME3 checkpoint
and the reconciled commit is documentation only.

Main's concurrent cockpit and launcher work remains uncommitted and preserved.
The one overlapping code hunk was resolved by keeping both the cockpit's
`FixedSeatPoseActive()` bone-hook admission and the new
`arm_ik::Tick(deltaSeconds, BodyOverlayMenu())` call. The rest of the landing used
the repository's three-way merge and concurrent-edit checks.

All 71 tests passed across the full checkpoint test run and a focused rerun:
68 passed initially; three installer/local-network tests passed outside the
sandbox. This validates the committed checkpoint, not every concurrent local
edit. No game was launched, deployed, or accepted for release in this cleanup.
Nothing was pushed.

**80 worktrees were removed: 93 checkouts became 13, including main.** The two
cleanup receipts measured approximately **28.2 GiB reclaimed** in total, with
about **66.1 GiB free** immediately after cleanup. Disk figures can change as
other sessions work.

## Preservation and cleanup method

- Inventoried every checkout, its HEAD, dirty/locked state, ancestry, patch
  equivalence, age and local directories before removal.
- Integrated the shared ME3 checkpoint before comparing child lanes with main.
  Many lanes were already cherry-picked and differed only in commit identity.
- Extended `tools/clean-worktrees.py` with opt-in `--patch-equivalent`. It accepts
  only histories where every patch is present on main, rejecting unique merge
  commits, command failures, empty/unproven histories and unique patches. The
  existing dirty, locked, idle and second-check guards remain in place.
- Tested the default ancestry gate and equivalent, dirty, unique and locked
  cases in a disposable Git fixture. The default cleanup behavior is unchanged.
- Preserved ignored scripts, logs, scratch evidence and release packages before
  deleting their checkouts. Same-volume moves were checked against SHA-256 file
  manifests before and after. Disposable build trees/caches were removed; links
  to shared dependencies were unlinked without traversing their targets.
- Kept Git branches. No branch was force-deleted. The removed detached checkout
  was an ancestor of main, so its commits remain reachable there.

Private inventory and preservation receipts are under the main checkout's
ignored `local-tools/` directory:

- `worktree-inventory-nov15.json` and `worktree-patches-nov15.json`
- `worktree-preservation-20261115/`: first-pass plan, SHA-256 receipt and cleanup log
- `worktree-preservation-20261115-phase2/`: additional merged/release trees,
  their evidence/packages, manifest and cleanup log
- `integration-ctest.txt`, `integration-ctest-rerun.txt`, `me3-land-result.txt`

Keep these local. They may contain private paths or game-derived evidence and
must not be committed. Some archived local-tools directories include old build
outputs; review those separately if further disk reclamation is necessary.

## Retained checkouts and next decisions

| Worktree | Reason retained / next action |
| --- | --- |
| Main checkout | Shared integration destination; concurrent cockpit/launcher edits remain. |
| `agent-a08810c1900a1136a` | Integrated ME3 checkpoint, built artifacts and private native evidence needed for the next bounded validation run. |
| `agent-a2e1237ec285988ed` | Locked and 13 modified files, including ME2 traversal, IK and bone-pose work. Preserve ownership and finish/review that lane before landing. |
| `ecstatic-dijkstra-a23f93` | Merged, but the idle guard observed recent activity during this audit. Recheck ownership/age before removal. |
| `le2-diagnostic-pad` | Two untracked diagnostic-pad source files differ from main. Review against the current implementation. |
| `le2-menu-evidence` | Patch identity differs. Main contains a later same-subject commit (`9974169a`) and substantially newer evidence; reconcile remaining historical details rather than replacing current docs. |
| `le2-save-menu-observation` | Two untracked UTF-16 source files match main after line-ending normalization. Confirm against committed main, preserve a receipt, then clean the duplicate files and prune the lane. |
| `le2-save-tool` | Three untracked oracle files; two scripts differ from main, while the Cargo lock matches after line-ending normalization. Review script changes before cleanup. |
| `le3-full-save-codec` | Nine commits fail exact patch-equivalence. Several have same-subject integrated counterparts; native-save, squad, metadata and galaxy changes need a hunk-level reconciliation. The name alone does not prove a missing save-codec feature. |
| `le3-presentation-owner` | HEAD is integrated; the idle guard observed recent activity during inspection. Recheck ownership/age before removal. |
| `le3-shared-presentation` | Two commits fail exact patch-equivalence, with integrated same-subject counterparts `d1f967c2` and `9420f755`. Verify adapted hunks before retirement; do not revert newer presentation fixes. |
| `le3-weapon-hud` | HEAD is integrated; the idle guard observed recent activity during inspection. Recheck ownership/age before removal. |
| `left-hand` | Three unique left-hand-mode commits. This is a feature integration requiring current shared-input/menu/weapon compatibility review and validation, not a duplicate cleanup. |

The idle timestamp includes Git's index, so inspection itself can refresh it.
Recent timestamps alone do not prove another agent is working, but this pass
retained those checkouts instead of overriding an ambiguous activity signal.

## Next cleanup pass

Start with `tools/clean-worktrees.py --patch-equivalent` in dry-run mode. Review
ignored content and ownership, then apply to explicitly selected completed lanes.
Keep using a branch-preserving cleanup; do not use `git clean -fdx` across these
worktrees or delete their parent directory recursively. The remaining unique
work is discoverable from the table and retained branches.

See the [overall ME3 handoff](me3-overall-goal-handoff.md) and
[galaxy-projector handoff](galaxy-projector-handoff.md) for implementation and
runtime acceptance work still outstanding. Integration does not close those gates.
