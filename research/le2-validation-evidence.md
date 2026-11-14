<!-- SPDX-License-Identifier: MIT -->
# LE2 validation tooling evidence

Checkpoint: 2026-11-14 UTC. **Native load/save observed; scripted Steam selection, ASI loading, structural observation and one bounded scalar script call proven. Restoration verified. Reload, gameplay commands and VR proof remain pending.**

## Live diagnostic connection, 2026-11-14 UTC

### Passive controller route and scalar return proven

Protected run `71bd4047-ea4b-4143-b928-f3c6cf5050c4` tested clean candidate
`eede67a1e6b6616e1e60045dc29452198d6ac334`, ASI SHA256
`47a428c7c80a963c2b403a41386c8294b4d4c544b4734768698a9029ffbac8f1`, with the
verified `59b5e775` loader. All three ordinary builds, **46/46** CTests and
**6/6** focused diagnostic tests passed, including the actual-C++ compact-wire
regression. All 23 client tests ran with C++ conformance enabled. SDK observer
and production-route compilation had also passed independently in Release/Debug.

Five correlated requests succeeded on game thread 65496: ping, an observation
that armed the passive window, a second observation, exactly one `probe-fov`,
and a final ping. The compact observations were 13,047 and 13,084 bytes. The
entry-menu world, engine and local-player slot 72 all pointed at base RVA
`0xD8D50`. The controller pointed at RVA `0x3F7AD0`, with exact unwind-table
bounds `[0x3F7AD0, 0x3F7B3E)`; no pawn was available. This was not a simple
recognized unconditional jump to the base target.

During one fixed ten-second epoch, **11,940** calls for the same controller and
game thread reached the owned base hook from inside those controller-function
bounds, and all 11,940 returned normally from its original next hop. The recorded
return address was RVA `0x3F7B29`. The completed window was not renewed. This
establishes the observed forwarding relationship for that current identity,
not lifetime across loading or arbitrary-call safety.

The explicitly requested getter revalidated metadata and identity, used the
controller's existing virtual route, and returned status **`dispatched`**.
Its NaN sentinel `0x7FC12345` changed to finite float bits `0x4280F115`
(approximately 64.470863); both adjacent canaries remained intact. The
controller, world/local-player scope, getter and return-property identity,
metadata and slot target were unchanged afterward. The host recorded one entry
spent, and the following ping completed. This proves a bounded scalar script
return and its production response routing, not visible FOV calibration,
ConsoleCommand allocation/string ownership, save selection or VR behavior.

ME2 closed normally; all four protected roots were restored and verified, and
only the owned invitation was removed. No pending request remained. Private
run `local-tools/le2-diagnostic-handoff/run-20261114T0647Z/` preserves protocol
bytes, decoded reviews, deployed hashes, mapped modules, logs and restoration.
The exact ASI/loader are frozen in `candidate-eede67a1/`. An earlier retry
(`584b9ec1-2e09-4595-9e1a-be04d414765f`, directory `run-20261114T0645Z/`) stopped
when another launcher appeared during backup, before fixture staging. Its
backups remain private; it released its lock and wrote no live test save.

### Expanded observation serialization failure

Protected run `97fb98d5-5232-4f69-b196-7d33d8b502cd` used candidate `ebf02a0f`,
ASI SHA256 `fa9b8cbd77239a6b49c87937ebe09ee4620f1729507f563eee9f874fccc2a77b`
and the verified `59b5e775` loader. Scripted Steam selection, exact module
attachment and ping succeeded on game thread 61024. The next `observe` request
was consumed but produced no response. The host logged `result encoding failed
after claim` and disabled the channel; late collection found no result.
No `probe-fov`, general console, save selection or VR operation was sent.

Review found that the observer budget tests serialize compact JSON, while the
host serialized with the default two-space indentation. Expanded nested fields
can therefore exceed the 16 KiB wire cap despite the compact fixture fitting.
The failed live payload was not preserved, so its exact byte count is unknown.
The correction uses compact host serialization, retains the cap and records
payload bytes if encoding fails again. A new actual-C++ endpoint smoke assertion
rejects indented responses; it failed against the unchanged `ebf02a0f` binary.

ME2 remained responsive and closed normally. All four protected roots were
restored and verified, and only the owned invitation was removed. The consumed
observation and unresolved intent remain preserved under the private
`local-tools/le2-diagnostic-handoff/run-20261114T0635Z/`, together with host logs,
mapped-module identity and normal-close/restoration receipts. This is a failed
expanded-observer attempt, not passive-route or scalar-call evidence.

### Later target and rejection check

Run `ea509781-66e5-4736-a284-2c1668aa1860` tested clean candidate
`152c9597b141362d13d6cd56e0c619b6e7366cdd`, ASI SHA256
`24c381b88debc6dc76cc086eaddae17988a00a63f3a1249a059fe08dbc5d42f8`, with the
previously verified `59b5e775` loader. The routine launcher path accepted the
reviewed capability receipt and selected ME2 without the selection-probe mode.
An earlier preparation check refused a different active launcher before writes;
the successful attempt acquired ownership only after it closed.

Five archived requests established: normal ping, structural observation,
rejection of an empty ping with a wrong lease, rejection of an expired empty
ping, and successful normal ping afterward. The negative replies carried the
producer's actual identity and exact reasons `identity mismatch` and
`expired or excessive deadline`. Both were consumed and correlated; no pending
intent remained. The same game thread, 53984, answered normal requests.

The entry-menu sample matched the exact GetFOVAngle declaration: flags `0x20802`,
one FloatProperty return at offset zero, size four, flags `0x580`, native index
zero. GMalloc and its LE2 Malloc/Free slots were readable and pointed to committed
executable pages; allocation ownership and call behavior remain unvalidated.

**The controller target prerequisite did not pass.** The host recorded its base
ProcessEvent installation at RVA `0xD8D50`, with `AtRvaUnchecked` classification
and a two-hop current chain to its exact owned detour. The controller's slot 72
instead pointed to RVA `0x3F7AD0`. Both slot-to-installation match fields were
false. The SDK declaration and inspection of our compiled Legacy call site agree
on byte offset `0x240`; this rules out an obvious compiler indexing discrepancy,
not an SDK/game mismatch. A native controller override is a hypothesis, not a
validated explanation. Do not change the slot or bypass it by calling the base
address merely to satisfy the test. Follow the [target investigation](le2-native-console-reuse.md#observed-controller-target-disagreement).

No native getter or console function was invoked. No save was selected and no
VR operation occurred. Normal window closure succeeded; all protected profile,
Win64, configuration and launcher content was restored and verified, and only
the owned invitation was removed. The private run is
`local-tools/le2-diagnostic-handoff/run-20261114T0602Z/`; its actual deployed
ASI/loader were copied from the post-run archive into `candidate-152c9597/` and
rehashed. All three ordinary builds, **45/45** tests and **5/5** focused diagnostic
tests passed at this candidate. This checkpoint advances native diagnostic and
rejection evidence, not gameplay-command or scenario acceptance.

### Initial connection proof

After the user authorized the next free game window, the protected runner
acquired ownership and backed up the full profile, LE2 Win64/configuration and
launcher directory. Two one-attempt Steam selection probes both launched and
bound the exact new ME2 process without computer use. The installed launcher
proxy hashes and preserved attempt records establish observed game-2 selection
for those installed files, not source-matched binary provenance.
The private reviewed receipt is
`local-tools/le2-diagnostic-handoff/launcher-capability-20261114/receipt.json`,
SHA256 `7f2fd3612ce7e497448d96b9e579241e7128141d5cc58444d39b4d3bdb092158`.
The capability parser verified its installed-file and two evidence hashes.

First run `1f10095e-87b4-4a44-ac65-35895aaa1d22` deployed candidate `95ea25ed`,
but module inspection showed it was not loaded: ME2 still used stock RAD Bink,
not an ASI loader. An independent attachment bug also rejected an unrelated EA
overlay reparse path. The correction validates only exact selected host/mod
paths, retaining their strict mapping/hash/reparse requirements. Its client
suite passed 18 tests with C++ conformance enabled; attachment passed 15 tests.
The first game closed normally and restoration verified before the next run.

Second run `43d8f792-c575-4e71-b441-9f478928676d` used the existing MEVR loader
and diagnostic ASI from clean candidate `59b5e775991ef282dee943181d61baf659d85c04`:

- ASI SHA256 `f9ac2a4951efb9bcb2f5e4a8e100afffc20a8308776f50fab047f6a37da218f5`.
- Loader SHA256 `dbcc07cb1c82116462a19319b4c97a073e247bfaddf3819fa314eee14e9f1663`.
- Stock Bink, preserved for forwarding and restored afterward:
  `653247e35db8d6453e83a008c805a877fd2d56a1d844282f9065ce2f34388fec`.

The loader log records ASI attachment and installation of ProcessEvent/Tick
hooks. The attachment helper verified the actual mapped ASI, then the producer
published its own process-bound identity. `ping` completed at tick 3374 on
thread 40612; `observe` completed at tick 6235 on the same thread. It reported
`entrymenu`, SFXEngine, LocalPlayer, SFXPlayerController and no pawn. The live
ConsoleCommand metadata matched the SDK's three parameters, size `0x24`, return
offset `0x14`, flags `0x24400` and native index zero. These are structural samples,
not proof of safe native dispatch, loaded-save identity or gameplay readiness.
`nativeConsole` and authoritative `observeWorld` remain false; `ready` remains false.

No save was selected and no gameplay command, input replay or XR operation was
performed. Stale/wrong-lease rejection remains covered offline, not yet by a live
negative-control run. Normal window closure succeeded. The runner archived new
logs/binaries, verified restoration of every protected root, and removed only
its own invitation after reset. No deployed diagnostic or loader was retained.
Private launch, deployment, mapping, identity, request/result, loader-log and
restoration evidence lives in `local-tools/le2-diagnostic-handoff/` under
`run-20261114T0537Z` and `run-20261114T0542Z`.
The exact tested ASI and loader were copied from the verified post-run archive
to `local-tools/le2-diagnostic-handoff/candidate-59b5e775/` and rehashed, so later
incremental builds cannot replace this candidate's evidence.

Candidate `59b5e775` passed all three ordinary build configurations and **42/42**
CTest checks. Its console string-lifetime fix is independently tested against
the packed SDK in Release/Debug; it does not enable console dispatch.

## Diagnostic control candidate, 2026-11-14 UTC

Candidate `a78b0390650b6f6eac38a848d5e2361d9f4bfebd` builds the existing host as
an LE2-only monolithic diagnostic, with renderer/gameplay/shared-console polling
disabled. Its ASI SHA256 is
`021e1394b63cabb95e4be53556d67a0367ff73473203fb43f51193af3a63e9ab`, and its
embedded version is `0.3.0+a78b039065`. This is a compiled control connection for
the existing mod host, not an imported ME2 renderer or a VR interaction proof.

The run-bound producer independently checks its actual process/module files and
the protected session before answering. Ping contains host tick/thread evidence;
observe adds bounded SDK-based world/player/pawn/package/position samples. Those
samples retain `nativeValidation: pending` and `ready: false`. Native console
dispatch stays disabled. See [control instructions](../tools/qa-le2/control.md)
and [observation limits](le2-diagnostic-observation.md).

Evidence at this candidate: diagnostic Release compile/link; focused CTest **3/3**
(dispatch gate, ten filesystem-test groups, real Python client talking to the C++
endpoint over temporary files); standalone observer checks **1/1** in Release
and Debug. The filesystem symlink-creation subcase was skipped for missing Windows
privilege. Ordinary builds at this same candidate passed all three required
configurations (LE1 development, SDK-free LE1 monolithic, and LE1/LE2/LE3 trilogy)
and the full **37/37** registered tests. These are offline checks.

Test-registration commit `6aa5ee5e` added the launcher, attachment, client and
codec checks to the standard loop: **41/41** passed, including all 18 client
tests with real C++ byte conformance enabled (no skipped client test).

Subsequent diagnostic candidate `95ea25ed510334421367da503d98d68148a1af2a` adds
read-only `ConsoleCommand` reflection metadata and compares the observed call
block with the pinned SDK declaration. It does not enable dispatch. Its ASI
SHA256 is `a98b4ace593b66915d96fb2de41da83a00a770d209ffbd1b9f47a45d7b280282`,
embedded version `0.3.0+95ea25ed51`. The actual diagnostic ASI compiled/linked;
focused tests passed **3/3**, and the updated standalone observer passed **1/1**
in Release and Debug. The original candidate remains preserved. Both candidates
and the test-registration change are reachable from main's integration history.

The first protected native preparation was refused because ME1 and SteamVR were
active. A fresh desktop read confirmed the ME1 process started at
`2026-11-14T04:53:49.6315656Z`. Preparation stopped before acquiring a lock,
staging a save or deploying a binary. No cleanup of that other session was
attempted. The private evidence identifier is `le2-diagnostic-preflight-occupied`,
SHA256 `1034a4fb42f83812076df4f324c06129d317b96b7b5775c0f0c1d4722c13a935`.
At that earlier preflight neither candidate had been loaded. The later live
checkpoint above supersedes the handshake status; normal reload, the three
scenarios and all VR/hardware gates remain unproven.
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

1. Scripted selected-save loading and gameplay controls. The bound request
   channel, structural observer and one scalar script call now have live proof,
   as recorded above. General console dispatch remains disabled. Use the
   [source handoff](le2-save-load-entrypoints.md) to complete normal save/load
   through validated native routes; recheck ownership before another run.
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
5. Scenario-specific authoritative runtime observation and input capture. The
   structural diagnostic and bounded call are implemented; they do not populate
   the normalized gameplay-state schema or prove ammo, plot and selected-save
   assertions. Those exporters and actual VR input remain pending.
6. Native baseline behavior for reload, timed interrupt (including missed window)
   and scanning/discovery, followed by actual VR interaction proof when those
   features exist. Restore and verify the real protected installation afterward.
7. Both-eye review, physical seated/standing comfort, repeatable performance and
   campaign/DLC coverage. The matrix identifies the work but is not evidence of
   completion.

Use the proven request channel to complete the minimal no-op fixture
load/save/reload cycle before increasing scope.
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
