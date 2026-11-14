<!-- SPDX-License-Identifier: MIT -->
# LE2 validation tooling evidence

Checkpoint: 2026-11-13. **Offline foundation implemented; native load/save observed and restoration verified. Reload, scripted control and gameplay/VR proof remain pending.**
Use the [workflow](../tools/qa-le2/README.md) and [coverage contract](../docs/le2-validation-coverage.md).
This report does not promote the three public draft scenarios to validated.

## Implemented and checked

| Requirement | Evidence and limit |
|---|---|
| Established tool reuse | TSE `6451f23d1fba62358328dce4e87733970f36ebf1` schema/codec inspected; Python adapter is original, span-preserving code. Direct-library and CLI-fork alternatives documented in [save adapter research](le2-save-adapter.md). Headless verification reuses actual upstream source. |
| Safe resource/save preparation | Full v30 parse, checksum, bounded input, fixed-size allowlist, hash and before-value preconditions, exclusive output, constrained semantic diff and receipt. Supports existing resource/ammo/power/squad/transform/plot fields; native legal values are not inferred from format validity. |
| Independent encoding verification | TSE source decoder and serializer accept original and prepared upstream/private saves and independently reserialize them identically. Cross-check verifies all declared scalar edits and rejects unrequested semantic changes. The official editor GUI also opened a prepared private copy and showed the expected resource values; native load/save succeeded in the later baseline; reload remains pending. |
| Protected lifecycle | Full profile/config/Win64 snapshot and verification; frozen inputs; new save slot; operator load handoff; exact process binding; archive/reset and recovery. Temporary-install tests exercise corrupt/incomplete backups, interrupted staging/reset, absent paths, new files, changed ownership and stale PID identity. Both a no-launch staging/reset and a later native load/save attempt verified restoration of the current installation, including archival of game-created saves. |
| Scenario contracts | Reload, interrupt and scanning drafts include variants, acquisition tasks, actions, artifact requirements and assertions. Bound recipes and observed starting state are checked; evidence carries run/build/fixture identity and hashes. No arbitrary story recipe is represented as coherent or validated. |
| Cross-module workflow | All three templates bind to a synthetic v30 fixture through the actual save adapter, recipe/receipt, scenario loader and protected runner; preparation changes the intended scalar, incorrect starting state is rejected and reset restores the fake installation. Only process inventory is injected; no scenario-loader stub is used. |
| Evidence boundaries | Native, VR, stereo visual, physical-headset, performance and campaign gates remain separate. Missing observations remain pending; native inputs cannot prove VR, and synthetic headset evidence cannot prove comfort. Artifact integrity does not authenticate producer truth. |

## Additional offline work during the ME1 conversation session

The ME1 owner is testing first-person conversations. This batch used private
copies and read-only source inspection; it did not launch/deploy a game, stage
the live profile, write shared request files, release ownership or change the
OpenXR runtime.

- [Resource boundary generation](../tools/qa-le2/variant-usage.md) now produces
  pinned saves/recipes/receipts and a hashed index for zero, insufficient, exact
  and surplus balances. A one-probe launch has only three distinct balances:
  zero and insufficient are equivalent. Root prepared a cached private base at
  0/1/2 probes and independently decoded/reserialized every output through the
  pinned TSE oracle; only `player.probes` changed. The retained private batch is
  `local-tools/le2-validation-evidence/probe-boundaries/`, with
  `independent-check.json` and index SHA256
  `e2cd3878d835f54385ef294afb74dd59735caf0bba2ba85ea9909d1d9be53698`.
  Game limits, legal launch and persistence remain untested.
- [Story review](../tools/qa-le2/story-review-usage.md) compares hash-bound save
  endpoints with exact package/map-qualified source effects. Unsupported effects,
  ambiguous writes and incomplete source reports remain unresolved; all other
  save changes remain visible. Root checked actual event-4551 source data against
  an artificial private endpoint with only booleans 4419/5679 changed, then
  checked unchanged endpoints to distinguish already-present values. Both checks
  passed without claiming event execution. Retained private diagnostics are in
  `local-tools/le2-validation-evidence/story-review-4551-diagnostic/`.
  **Those edited endpoints are test data, not gameplay fixtures or natural
  progression evidence.** Ordinary before/after saves are still required.
- [Observer audit and archive inspector](le2-runtime-observer.md) identify the
  current LE1-only capture integration, missing LE2 runtime-state/input producers
  and shared request hazards. Archive inspection reuses the recording parser but
  emits only offline diagnostics with unknown game identity. Its 11 synthetic
  tests passed; no real capture or runtime observer was validated.
- [Reload source inspection](le2-ammo-fixture-source.md) establishes the native
  save-field mapping and documents early-cancel ammo transfer and tutorial event
  ordering. The draft scenario now distinguishes resource boundaries and three
  cancellation phases. These findings guide fixture acquisition and observer
  implementation; native timings and physical reload behavior remain pending.

Root integration passed all **10 selected CTest entries**, using the command
below after reconfiguring the same isolated build directory. The three added
suites contain 14 resource-variant tests, 14 story-review tests and 11 archive
inspection tests. Optional checks used the upstream sample, one cached private
base, the real TSE oracle and the actual scoped event-4551 report. The existing
session suite retains its Windows symlink-creation privilege skip. SPDX and
whitespace checks passed for the committed work. No C++ runtime change or ASI
build/deployment is part of this batch.

## Isolated reload coordinator checkpoint

The subsequent [P0-P5 offline implementation checkpoint](../docs/le2-integration-contract.md)
adds an original request coordinator, not another native acceptance result.
Root independently configured `tests/le2-reload-core` under private
`local-tools/le2-reload-core-root/`, built Release and Debug with MSVC 19.44 and
`/W4 /WX`, and passed all 18 always-active checks in each configuration.
The standalone project does not build or link any ASI/production target.
The [prototype report](le2-reload-core.md), [engine audit](le2-engine-access-discovery.md),
[renderer audit](le2-renderer-integration.md) and [movement script findings](le2-movement-source.md)
record scope and unresolved gates. Production trilogy/monolithic builds and
native deployment remain work for the actual runtime integration candidate.

## Earlier foundation checks

Configured a separate `build-le2-validation-tools` CMake directory with game build
targets and main menu disabled, offline tests enabled. Ran:

```text
ctest --test-dir build-le2-validation-tools -C Release -R "mevr-(le[12]-|okf)" --output-on-failure
```

All seven selected CTest entries passed: LE1 save regression, LE2 save adapter,
session lifecycle, scenario audit, independent oracle, cross-module workflow and
knowledge-format validation. `LE2_TEST_SAVE` pointed to the upstream sample and
three private saves; `LE2_TSE_ORACLE` pointed to the source-built decoder. Counts:
13 save tests, 17 session tests (16 passed, one native symlink-creation test skipped
for Windows privileges; mocked reparse refusal also tested), 17 scenario tests,
12 oracle tests including real corpus integration, and one workflow test with
three template subcases. No C++ runtime implementation changed, and this is not
an ASI build/deployment or headset result.

The oracle used Rust `1.100.0 (0ef1bc386 2026-11-09)`, Windows MSVC target, a locked
Cargo dependency graph, and pinned Git blobs for models, codecs and the `rcize`
macro. Only the UI derive implementations are omitted. Upstream code generated
one non-local-impl warning; unused `ron`/`ryu` dependency warnings were also
reported. The build succeeded. Full byte equality catches trailing payloads or
serialization differences that the upstream decoder alone may accept.

Private proof artifacts are under `local-tools/le2-validation-evidence/` and the
oracle build under `local-tools/le2-oracle/pinned-project/`. Keep these private.
Two retained cross-check reports are `pinned-upstream-probe/result.json` and
`pinned-private-probe/result.json`; the optional corpus test additionally checked
the other two private saves in temporary directories. Each retained report binds
the input/output, adapter, executable, source manifest, dependency lock and recipe.

| Retained identity | SHA256 |
|---|---|
| Oracle executable | `cf7cdc3f2155070dcdb157513f3e02df70dce3531dc23a1df103153d75406562` |
| Source manifest | `2c2ab79d52718110b5e226df2f3b19d1c8e23145eb461d860e2122583382b8c8` |
| Cargo.lock | `5b2782d5a9f50e5a5f67bfbb2581f5e24bfca9515d5edba736c3c5bbe8e05ee1` |
| Adapter at verification | `6645b2ef8b472d75a198efdef9dee6637c820e04ec72c0e4bf78885e244801c2` |

These are historical evidence identities, not assertions about future local
builds. Re-run checks after changes. The corpus is narrow; four files do not prove
all classes, DLC, campaign states or save variants.

## Read-only native-source tooling

The [conversation scanner](../tools/conversation-scan/README.md) now has explicit
LE2 selection, package identity checks, string-reference/reply metadata, interrupt
visibility keys and actual conversation/sequence/cinematic-node crosslinks. Root
integration built it privately with zero warnings/errors and passed 19 CLI/corpus
checks, including 15 synthetic property invariants, the installed mechanic/reload
candidates and unchanged package hashes. LE1 default/explicit output matched the
previous executable byte-for-byte on the checked captain scene; the existing
conversation-sweep suite also passed.

The first root build with an external intermediate-output directory exposed stale
local `obj/` generated sources being compiled twice. The project now explicitly
excludes `obj/**` and `bin/**`; the documented isolated build then succeeded
without deleting another build's artifacts. These checks establish offline source
inspection, not a runtime observation producer or legal interrupt timing.

The subsequent LE2 `--state-events` integration passed **28** CLI/corpus checks
on root, including exact event 4551's two immediate boolean changes, explicit
missing-ID/parse-error failure and LE1 state-event text equivalence. It requires
an explicit package selector and reports directory scope and package hashes;
it does not claim mounted or transitive story completeness.

A private first-load attempt is prepared under
`local-tools/le2-native-handoff/`. Its no-op recipe, fixture, source preconditions,
executable hash, scenario and session plan pass offline validation. The prepared
save is byte-identical to its base and independently reserialized identically by
TSE. `HANDOFF.md` retains the original offline handoff. The later protected attempt
below records actual staging and restoration, but no game launch. The three
gameplay scenarios remain drafts.

## Independent editor GUI check

The official [v2.2.1 release](https://github.com/KarlitosVII/trilogy-save-editor/releases/tag/v2.2.1)
was installed in ignored `local-tools/trilogy-save-editor/app/` for this check.
Installer SHA256: `d52b59684353ae58a3aed7865c258a60fb423816bf5fdc2288920a9fe971d4ad`;
editor executable SHA256: `661e37ca5438b19b89f056e0a6d9e302bd2e0d125bbcfb191884b833ddcb5497`.
Opened `pinned-private-probe/prepared.pcsav` through the editor's documented save
argument. The General page showed credits, medi-gel, all four minerals and probes
at 2, and fuel at 42.5, matching the recipe. No Save action was taken. The prepared
file retained SHA256 `14b370084223cde445bebbebd932fc6fd2561be97a218a7846f91e79781889bf`.

Private `pinned-private-probe/editor-ui-observation.json` records the observed
accessibility tree and method; SHA256 is
`14c1b60274fb6e0b694353bba5b136a423d6930fb4dea087309c9103811c28f6`.
This proves editor reopening for this edited save and visible resources, not
story coherence or all fields through the GUI. Both editor processes created for
the check were closed afterward. The existing game session was left running.

## Completion audit and next work

The following requirements are **not achieved** at this checkpoint:

1. A proven ME2 scripted control path. Desktop access succeeded on retry, but
   the LE2 ASI directory was empty. Follow the [scripted testing handoff](../docs/le2-scripted-testing.md)
   instead of extending UI automation. Recheck ownership before another run.
2. In-game reload persistence. The retry below selected the staged Normandy
   save, loaded the crew deck and created a new native save; reload was not
   performed. The chapter save's `BioP_EndGm2` string still establishes no
   access to a particular gameplay scenario without loading and inspection.
3. Naturally acquired, verified base fixtures for all three scenarios. Determine
   career/slot discovery through Load Game and bind actual starting conditions.
4. Complete dependency coverage and named coherent story recipes. [Zanethu source
   inspection](le2-galaxy-fixture-source.md) now identifies the planet, feature,
   immediate discovery transition and landing conditions, and the scanner links
   one interrupt to its node. Natural before/after saves and ordinary progression
   still need to establish the complete effects. Raw plot primitives alone do not
   satisfy this requirement.
5. A verified LE2 runtime observation/input capture producer, or documented manual
   observations tied to raw captures. The normalized state schema is a contract,
   not an implemented in-game exporter. No game-thread debug-control integration
   is claimed.
6. Native baseline behavior for reload, timed interrupt (including missed window)
   and scanning/discovery, followed by actual VR interaction proof when those
   features exist. Restore and verify the real protected installation afterward.
7. Both-eye review, physical seated/standing comfort, repeatable performance and
   campaign/DLC coverage. The matrix identifies the work but is not evidence of
   completion.

Prioritize the ME2 request-channel integration and complete the minimal no-op
fixture load/save/reload cycle before increasing scope.
Then validate a resource-only preparation, save/reload persistence and each
natural scenario. Keep every result attributable to its actual candidate and
loaded binary; retain unsuccessful runs and restoration outcomes.


## Protected native attempt: desktop access failure

Attempted 2026-11-13 local time (2026-11-14 UTC), run
`49cdcf65-88f3-43f3-b33a-3709937f17be`. The user authorized proceeding, and the
conversation-testing owner had explicitly reported its runs finished and the
game free. Fresh process checks found no game/ME launcher/SteamVR session. The
leftover November 10 reservation was archived with its hash and ownership
receipt; its old backup was preserved and never restored.

The real runner acquired its own lock, froze the no-op fixture and bindings,
verified backups of all 44 existing profile files and nine LE2 Win64 files,
recorded the absent LE2 Config directory, and staged the new save slot. The LE2
ASI directory was empty; no renderer or plugin was installed. Executable and
fixture hashes still matched the prepared handoff.

The normal launcher boundary was recorded. Windows desktop automation then
returned a black Steam capture and `GetCursorPos failed: Access is denied.
(0x80070005)` on window activation. The cause is unresolved; a locked desktop is
only a possibility. **No game launched, no process was bound, and no load, scene,
resource, in-game save/reload or native gameplay acceptance was observed.**

The runner reset immediately with no native process active. It archived the
staged test file, verified restoration of all three protected roots, removed the
test slot and released only its own lock. `phase` is `restored` and
`restorationVerified` is `true`. This establishes a real staging/reset cycle,
not restoration after gameplay writes. Private context, frozen inputs, backups,
post-attempt archive and access-failure receipt remain under
`local-tools/le2-native-handoff/run-20261114T032522Z/`.

Retry with a fresh plan/run after desktop access and ownership checks. Select
the staged save through normal Load Game, establish actual selected-save
identity, then save to a new slot, reload, close normally and verify restoration.
Native, VR and headset gates remain pending.


## Native retry: load and save proven, reload pending

The permission retry on November 13 reached Steam and the trilogy selector.
Run `a69e9f89-1c16-47a4-9d67-6f3cf0948217` used a fresh protected backup, the same
no-op fixture and verified LE2 executable. Its exact process/start identity was
bound by the runner. No ASI or renderer was installed.

Original captures show **Save 99 - Normandy SR-2** selected in Load Game,
followed by Shepard on the crew deck with the Crew Quarters prompt. The native
pause menu showed credits 32193, medi-gel 7, heavy weapon ammo 6, palladium 93177,
iridium 71154, platinum 37333 and element zero 4193. Create New Save produced
`Save_0100.pcsav`, SHA256
`2d52882c152b582eaae6e5ca4a05099279e18aa402e4998367df893e27cbc236`.
The pinned TSE decoder accepted it, verified its checksum and independently
reserialized it byte-identically. The semantic diff has no player-field changes.
It does include time/loading/streaming changes, a small position adjustment,
level-array changes and plot integer 257 changing from 11 to 0.
Do not infer story coherence or a completely unchanged state from this result.

Reload was not performed. A keypress was withheld by approval review while the
last capture still showed saving, then desktop automation became stuck between
minimized-window recovery and manual-input detection. The user redirected work
to the repository's scripted testing skills. The existing save was preserved and
its hash rechecked before cleanup. A normal close request did not finish exiting;
only the identity-verified owned game was subsequently terminated, and its
launcher closed. This is not proof of graceful game shutdown.

The protected runner archived the post-test profile, including both test saves,
restored its three protected roots and released its own lock. Fresh independent
snapshots matched the recorded original content, timestamps and presence. The
private run is `local-tools/le2-native-handoff/run-20261114T035022Z/`; original
captures, independent save inspection, diff, context and `manual-review.json`
remain there. These are attributed manual observations, not an implemented LE2
runtime exporter or a passing gameplay/VR audit.

Next: implement and prove the bounded [ME2 scripted control slice](../docs/le2-scripted-testing.md),
then complete normal save/reload and the three gameplay scenarios. The LE1
fake-headset runner must not be deployed unchanged into this ME2 test.
