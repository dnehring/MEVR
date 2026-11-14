<!-- SPDX-License-Identifier: MIT -->
# LE2 validation tooling evidence

Checkpoint: 2026-11-13. **Offline foundation implemented; native proof pending.**
Use the [workflow](../tools/qa-le2/README.md) and [coverage contract](../docs/le2-validation-coverage.md).
This report does not promote the three public draft scenarios to validated.

## Implemented and checked

| Requirement | Evidence and limit |
|---|---|
| Established tool reuse | TSE `6451f23d1fba62358328dce4e87733970f36ebf1` schema/codec inspected; Python adapter is original, span-preserving code. Direct-library and CLI-fork alternatives documented in [save adapter research](le2-save-adapter.md). Headless verification reuses actual upstream source. |
| Safe resource/save preparation | Full v30 parse, checksum, bounded input, fixed-size allowlist, hash and before-value preconditions, exclusive output, constrained semantic diff and receipt. Supports existing resource/ammo/power/squad/transform/plot fields; native legal values are not inferred from format validity. |
| Independent encoding verification | TSE source decoder and serializer accept original and prepared upstream/private saves and independently reserialize them identically. Cross-check verifies all declared scalar edits and rejects unrequested semantic changes. The official editor GUI also opened a prepared private copy and showed the expected resource values; native load/save/reload remains pending. |
| Protected lifecycle | Full profile/config/Win64 snapshot and verification; frozen inputs; new save slot; operator load handoff; exact process binding; archive/reset and recovery. Temporary-install tests exercise corrupt/incomplete backups, interrupted staging/reset, absent paths, new files, changed ownership and stale PID identity. Actual installation restoration has not been exercised. |
| Scenario contracts | Reload, interrupt and scanning drafts include variants, acquisition tasks, actions, artifact requirements and assertions. Bound recipes and observed starting state are checked; evidence carries run/build/fixture identity and hashes. No arbitrary story recipe is represented as coherent or validated. |
| Cross-module workflow | All three templates bind to a synthetic v30 fixture through the actual save adapter, recipe/receipt, scenario loader and protected runner; preparation changes the intended scalar, incorrect starting state is rejected and reset restores the fake installation. Only process inventory is injected; no scenario-loader stub is used. |
| Evidence boundaries | Native, VR, stereo visual, physical-headset, performance and campaign gates remain separate. Missing observations remain pending; native inputs cannot prove VR, and synthetic headset evidence cannot prove comfort. Artifact integrity does not authenticate producer truth. |

## Checks at this checkpoint

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
TSE. `HANDOFF.md` gives the next steps; its owner marker remains PENDING. Nothing
was staged into the live profile, no session lock was acquired, and no native
acceptance or restoration is claimed. The three gameplay scenarios remain drafts.

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

1. Exclusive native ownership handoff. The inspected shared game-slot record names
   an earlier conversation-sweep owner. A missing game process alone does not
   establish permission to discard its lock or restore its old files. No old
   rollback was run and no system OpenXR setting changed.
2. Normal ME2 selection/loading and in-game save/reload persistence. Two private saves parse as Normandy saves and the
   chapter save as `BioP_EndGm2`; neither establishes access to a particular
   scenario without loading and inspection.
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

Continue useful offline discovery while native ownership is unavailable. After
handoff, prove a minimal no-op fixture load and restore before increasing scope.
Then validate a resource-only preparation, save/reload persistence and each
natural scenario. Keep every result attributable to its actual candidate and
loaded binary; retain unsuccessful runs and restoration outcomes.
