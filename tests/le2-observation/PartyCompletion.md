<!-- SPDX-License-Identifier: MIT -->
# Read-only party completion sample

`ObservePartyCompletion(thread, sampledContext)` belongs only on the existing
`observe-input` diagnostic response. It does not alter `Context`, `SameContext`,
pulse admission, or polling. It accepts a current party-confirm context whose
external delegate names `OnPartySelectionComplete`, then independently checks
the exact owner class `SFXGameContent.BioSeqAct_ShowPartySelectionGUI`.

The source candidate and callback/scheduler distinction are documented in
[the party-confirm source audit](../../research/le2-party-confirm-input.md).
The content class has no SDK declaration. Its completion flag is read through
its listed, exact `Core.BoolProperty` declaration: bounded complete owner-child
walk, exact name/outer, scalar four-byte storage, one-bit mask, no parameter or
return flags, and a span within the class's bounded `PropertySize`. A layout
overlapping superclass storage is reported as unsupported, not universally
invalid. Duplicate declarations, foreign fields, malformed chains, and missing
metadata make this diagnostic unavailable.

The implementation reuses the guarded observer's `Table`, `ClassMetadata`,
`ReadObject`, `NameText`, `BoundedArray` and identity copies. General
`ue3::Reflection` was inspected: its cached/raw layout lookup and class fallback
are not the exact guarded declaration check required here. No second reflection
framework, global object search, generated wrapper, engine allocator or native
call is added.

Inherited `bActive`, `bLatentExecution` and `ActivateCount` use the pinned SDK's
`Engine.SequenceOp` layout. Parent membership reuses the existing loadout
observer's bounded arrays and outer relationship. It counts only references to
this action and never dereferences unrelated members. Missing parent metadata
is separately unavailable; inactive, zero or duplicate membership is reported
as data, not rejected as an input-readiness condition. This proves neither
current-world sequence containment nor scheduler progress.

Two guarded complete samples and fresh input contexts must match. Metadata,
flag word, action identity, scheduler scalars, parent identity, array headers
and membership counts participate in comparison. Unreadable memory or change
returns unavailable. Nothing is retained for later pointer dereference. As with
other diagnostic samples, transitions entirely between reads and pointer/index
reuse cannot be ruled out.

The JSON preserves explicit false claims for callback execution, scheduler
progress, mounted script-body identity and durable object identity. A true flag
is a sampled state, not proof that this particular input invoked the callback.
Repeated stable states may inform a scheduler investigation but do not alone
prove a stall.

`PartyCompletionTests.inc` runs inside the existing actual-SDK synthetic suite,
including its production compile target. Tests cover true/false and inactive
states; exact metadata and size/mask validation; cyclic/duplicate declarations;
unsupported superclass overlap; bounded and unreadable parent arrays; stale
owner/context; and changes to flag, mask, activation count, parent or callback
context during sampling. Failing diagnostics leave valid input admission
unchanged. Tests never launch the game or call engine functions.
