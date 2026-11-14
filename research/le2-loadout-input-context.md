<!-- SPDX-License-Identifier: MIT -->
# Bounded loadout input context

This implements the follow-up to the [party/loadout source audit](le2-party-confirm-input.md)
using the same pinned SDK, installed package and read-only scanner. New API value
`Category::Loadout` serializes as `loadout`; the existing input source, host and
normal native UI callbacks retain authority. There are no new engine calls.
The implementation remains a structural observation until separately accepted
in a native run.

Admission requires active GUI mode 7 and the focused, interactive exact
`ChoiceGUI` panel with exact `BioSFHandler_PCChoiceGUI`. Existing controller,
player, GUI-manager and WorldInfo reciprocals are reused. `MissionCompletion`
and unrelated callbacks are excluded despite sharing the PCChoiceGUI class.
Nonfocused same-tag panels may coexist: the inspected source creates separate
top/submenu handlers, while the native factory's removal/tag policy is opaque.

The handler's input delegate must name exactly `CharacterMenuHandler`,
`TopMenuHandler`, or `SubMenuHandler` on a live `SFXSeqAct_ChooseLoadoutBase` or
SDK-declared `SFXGameContent.SFXSeqAct_ChooseLoadout`. The corresponding action
`TopMenu` or `SubMenu` must point back to the focused handler. The action must
be active and unfinished, with a valid enabled ParentSequence, matching Outer,
and exactly one occurrence in each bounded SequenceObjects/ActiveSequenceOps
array. No unrelated sequence members are dereferenced.

The source creates exact `SFXGameChoiceGUIData` under the selected handler;
that class and Outer relationship are checked. Bounded arrays and body fields
are sampled as follows:

| Data | Limit and stage checks |
|---|---|
| AvailableHenchmen | At most 32; nonempty; first entry is `None`; `CurrentPawn` is an integer index into this array, with 0 meaning Shepard. |
| LoadoutWeapons | At most 6; weapon stages require 6; each nonnull class has validated SFXWeapon ancestry. |
| WeaponMenuEnabledWeaponIDs | At most 6 unique indices within LoadoutWeapons, each with a nonnull class. |
| SubMenuEnabledWeapons | At most 64; names, unlock-name identities, rating and special-weapon flag sampled. Weapon submenu index must refer to an enabled group. |
| Choice rows | At most 64; count matches characters, enabled groups, or submenu weapons for the exact callback stage. IDs, disabled and nested flags sampled. Disabled rows remain legitimate. |
| Parent sequence arrays | At most 4096 each; exact active action membership and array headers sampled. |

Raw validated SFXName identities keep the bounded snapshots compact; callback
text is also retained. Choice captions and cosmetic strings are not copied.
The native ChoiceGUI callback-context integer is reported without presenting it
as the currently highlighted row: native initialization/index mapping remains
opaque. Source-backed stage/count constraints do not prove that mapping.

Two guarded samples compare all captured roots, callback/stage/action/data
identities, array headers and routing values, activation count, and relevant
panel state. A later changed context cancels a previously admitted pulse through
the existing host. No automatic stage continuation or rearming is added.

World provenance is deliberately limited: the current-world GUI handler is
bound to this reciprocal active action/sequence. The SDK leaves UWorld/ULevel
sequence links as unknown padding; this code invents no offsets and makes no
GWorld-Level containment claim. It does not invoke GetWorldInfo or inspect the
native action implementation. The concrete sequence edge from a particular
party confirmation to this loadout action still needs native observation.

Two conservative prerequisites remain native-unverified. The script constructs
choice data with the relevant handler as Outer, but native `Initialize` retaining
that exact object in `m_ChoiceData` is not visible in the inspected script.
Likewise, SDK declarations expose `bActive` and `ActiveSequenceOps`, but the
inspected loadout script does not prove the scheduler's membership timing.
If either guard refuses live input, inspect the actual relationship before
changing it; a matching caption cannot establish ownership or scheduling state.

The SDK suite covers all three stages, MissionCompletion/foreign callback
exclusion, modal parent coexistence, focus switches, missing/duplicate active
membership, invalid indices/counts/classes, and choice/body/callback/activation
changes between samples. Test capture storage uses a reusable heap Context to
avoid accumulating large by-value SDK snapshot temporaries in the enclosing
fixture stack; the actual two-read sampler is still invoked every time.

Native handoff: archive `observe-input` for the precise stage and callback,
then issue one explicit bounded pulse in that category. Reconcile output and
neutral release, capture the screen and observe the new context before another
request. Leaving a screen, applying a loadout, saving it, and VR acceptance are
separate outcomes, not implied by context availability or XInput output.
