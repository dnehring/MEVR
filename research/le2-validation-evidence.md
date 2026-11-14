<!-- SPDX-License-Identifier: MIT -->
# LE2 validation tooling evidence

Checkpoint: 2026-11-14 UTC. **Scripted launch, normal fixture loading, eight edited resource checks, and opening the normal pause/Save menus now have live proof. Normandy scene, readable menu captures and restoration verified. New Save submission/reload persistence, gameplay scenarios, general console calls and VR remain pending.**

## Live diagnostic connection, 2026-11-14 UTC

### Save initialization retained; rejected UI row identified

Protected run `80d7434b-698d-4c98-af82-0f688e617f9a` used candidate
`d4d34008be41f0fef49c0ada26264611f6276bfe`, ASI SHA256
`695401a96ba45038d2d0c813c6c63eafc8508c838fbe5593c169b83fb6ce0648`, with the
verified loader. Three ordinary builds, **50/50** full tests and **7/7**
diagnostic tests passed, including actual-C++ client/collector and capture
tests. SDK observer Release/Debug suites passed; independent review resolved
a pending-panel continuity gap before integration. Build receipt SHA256:
`31c9f0648b70ea43b9c89f52519ef7183a3f868733eb9135cc619f597395f50b`.

The entire launch/load/menu/capture sequence used scripts, with no computer-use
fallback or injected desktop input. Reviewed desktop-client captures show Save
99 selected, Shepard on the Normandy crew deck, the initialized Mission Computer
with Save enabled, and the resulting New Save screen. All eight prepared
resources matched the live exporter again. The loaded-scene image SHA256 is
`42f7d1bd8d8d00956ae7d525acbd7fec5596a8f3ba2241fd3bbbe4064d5d1bc5`.

Save-menu request `0c58258b-592d-4805-894e-291430c0618b` returned normally.
Observations `1ee29042-3cd2-42bc-abe4-23c881974475` and
`72e1ceb9-7bc3-4cc6-89f2-0d5635741c71` retained
`sameOpenedSaveFlow=true` and `pendingPanelIdentityBound=true` after normal
initialization. Their diagnostic row arrays were identical. The first row had
empty career/save-name strings with count/capacity zero, index/type zero, and a
null SaveGame pointer. Its FriendlyName was the location label, not the displayed
Create New Save text. Rows 1 and 2 were the real Save 99 and Save 36 descriptors.
The reviewed UI showed Create New Save followed by those two saves; image SHA256
`148584cc70bb957c851676ceed4bfadbc8e184823db0cfad0cc82e7752ce6f4f`.

These observations diagnose the rejected first row and show that the asynchronous
panel correction works in this run. They are explicitly not coherent complete
descriptor receipts: `recordsComplete` remained false and no New Save request
was submitted. The next implementation must recognize only the reviewed UI-row
signature, account for every row, resample its complete state, preserve strict
real-save validation, and keep the collector's exact fixture binding.

The game closed normally and all four protected roots were restored and
verified. The owned invitation was removed. Private requests, four readable
captures, exported state/checks and cleanup receipts remain in
`local-tools/le2-diagnostic-handoff/run-20261114T1446Z/`. New Save completion and
reload persistence remain pending.

### Closed-HUD pause and normal Save-menu transitions proven

Protected run `ee6a8779-e188-4226-8963-77280c69f646` used clean candidate
`f614aeea2c7b3ac9ef65bdb0daa9646986863b4d`, ASI SHA256
`cbe1ed770c5c9e7dc2cd89c74944d0c30e995b876f6dfb209a38f054a0a8c1d2`, and
the verified `59b5e775` loader. Three ordinary configurations, **50/50** full
tests, **7/7** diagnostic tests, **37/37** actual-C++ client tests and **20/20**
collector tests passed. SDK observer Release and Debug suites also passed;
independent review found no blocker. Build receipt SHA256:
`0cd457aeae119bba8b45dfb8e46a92161cee904fc103b2639078d7fd2d571f80`.

Steam launched ME2 normally. The request channel verified the exact loaded
candidate and process, then loaded the unique prepared resource fixture with
request `3e403cbb-8af2-40af-b23a-db10ede1d25a`. Resource observation
`c4fe3bff-03bd-4b66-9077-7061fcea4814` passed all eight exported starting checks.
The revised observer reported the persistent PowerWheel panel interactive but
the wheel itself invisible, mode NONE/0 and not transitioning, with all required
reciprocal links matching. `pauseStructurallySupported` was true.

Normal pause request `790efc71-adba-4c3d-962e-f6ec59289db0` returned normally.
A fresh observation found nine complete browser pages, one Save-type page at
row 0, no initialization wait, and enabled raw/effective Save state. The
coordinator reviewed a readable computer-use screenshot of the initialized
Mission Computer with Save enabled and the edited resources visible. No
keyboard or mouse input was sent through computer use. Normal Save-menu request
`faeaa878-b8a5-4ead-b053-06fce624fa82` then returned with its 12-byte frame intact.

The actual Save screen displayed Create New Save followed by Save 99 and Save
36, both Normandy. Observer `778765c7-dce8-4823-8c5f-d81d3974332a` reported a
ready Save panel and three rows, but rejected the descriptor list as invalid.
It exposed no rejected-row values, so the exact reason cannot be established
from this observation. The visible extra Create New Save entry suggests a
synthetic row, but neither its descriptor representation nor a safe exception
is proven. Separately, the action's immediate post-observation shows the newly
created Save panel before initialization/focus; the adapter invalidated its
continuation at that intermediate state. Both adapter assumptions need bounded
diagnostics or correction. **No New Save request was submitted.**

PrintWindow returned black samples throughout this run; those failures remain
preserved. Computer-use screenshots supplied the independent menu review.
An explicit, separately tested `desktop-client` fallback then captured the
owned foreground client region without focus/input or settings changes. Its
receipt records the distinct BitBlt method, unchanged identity/geometry and
image SHA256 `aa54a06f1ea54ffe05564eb23717bd89c60558794b915ff5181b6b895bc00126`.
The coordinator viewed that image and confirmed the same New Save screen.
This method requires the exact game window already in front; overlays still
require image review. Default capture behavior remains PrintWindow.
The integrated fallback passed **21/21** capture tests and **20/20** collector
tests with the actual candidate C++ decoder. One initial collector invocation
used the wrong executable filename; the corrected path passed without a code
change or skipped test.

The game closed normally and all four protected roots were restored and
verified; the owned invitation was removed. Private evidence lives in
`local-tools/le2-diagnostic-handoff/run-20261114T1425Z/`. This establishes normal
pause/Save-menu transitions and a usable scripted capture fallback, not New
Save completion, resource persistence after reload, or VR acceptance.

### New Save submission and file collector: offline checks complete

Clean candidate `11e63b352869c3beaf842b99ed1f07b7c60d06bc` adds the normal
New Save event and complete Save-list observation. Frozen ASI SHA256:
`06966f1f450a7f79c749cec3cdb42643a3c5641d29dc4357c3868bd380c0ea21`.
Three ordinary builds, **49/49** full CTests, **7/7** diagnostic tests and
**37/37** actual-C++ client tests passed. SDK observer Release/Debug builds
also passed, including the production virtual route under `/W4 /WX`.

The action uses the game's normal command 11 with a properly constructed empty
SDK argument array. Native preparation, free-space checks and slot selection
remain in the game's path. Complete descriptor, current-flow, parameter and
one-entry checks are covered by synthetic tests and independent source reviews;
none of those tests establishes live UI readiness or save completion.

The separate [file collector](../tools/qa-le2/save-evidence.md) passed **20/20**
offline tests, including actual C++ decoding, the real client preflight import,
the LE2 save parser and mixed manual/auto/chapter descriptors. It binds a
reviewed capture and complete pre-save career inventory to the action, then
can archive the actual new stable file without guessing a filename from an
index. Prior-file changes and unexpected additions remain explicit failures.
Independent TSE decoding and a fresh normal reload are separate next checks.

This candidate subsequently ran in the authorized next free game window;
the bounded result is recorded below. New Save itself has not entered natively.

### Scripted launch/load repeated; idle HUD blocks the pause guard

Protected run `f402b0a6-a183-4b2b-a16f-b6a36995a824` launched candidate
`11e63b352869c3beaf842b99ed1f07b7c60d06bc` through Steam with the verified
loader. The channel bound the actual process, loaded ASI and game thread.
The normal splash and Load-menu routes returned normally, enumeration found
the unique prepared fixture among four current descriptors, and load request
`90c293c5-73d9-41e0-b6d3-0b90087ca510` returned `dispatched` with its parameter
frame intact. No desktop input was needed to launch or load.

The current Load-menu capture was completely black and is retained as a failed
visual sample, not selected-save image proof. The loaded-scene capture was
readable and separately reviewed: Shepard stood on the Normandy crew deck
with the Crew Quarters / Open prompt. Image SHA256:
`22e4e6a4595e32c450868f0e92e6978b24508c4ec3f39e76816ea8392bb4f9e8`.
Resource request `62121078-8922-4b34-b87a-65f71932fcf6` independently sampled
all eight expected edited values; the exporter returned zero with all starting
checks matched. This repeats loading/resource proof, not persistence proof.

Two read-only Save-flow observations reported active Default mode 0,
`allowPauseMenu=true`, `browserAllowed=true`, and `playersOnly=false`, but a
focused `PowerWheel` panel. No wheel overlay was visible in the reviewed
capture. The initial adapter requires no focused panel, so it reported
`pauseStructurallySupported=false`. No pause, Save-menu or New Save action was
submitted. Source inspection identifies PowerWheel as part of the persistent
HUD; accepting that case requires verified closed-wheel state and ownership,
not a tag-only exception or an assumption from the image.

The game closed normally. The runner verified all four protected roots
restored and removed its owned invitation. The private run directory
`local-tools/le2-diagnostic-handoff/run-20261114T1409Z/` retains requests,
captures, export checks, module logs and restoration evidence. Launch and
normal fixture loading work; Save/reload remains pending a corrected and
tested pause-context guard.

### Pause and Save-menu candidate: built, native attempt held

Candidate `e472c0ff5f72ceb34266cbc24d37b8c1ba5bb9e8` adds the
[bounded normal pause/Save flow](le2-save-flow-observation.md) and its explicit
control verbs. Frozen ASI SHA256:
`112af1c4c02a3233740cd12778bc005d1bc62ffe153151f3cc7adcf433501e7b`.
Three ordinary builds passed, SDK observer Release/Debug checks passed, focused
diagnostic tests passed **7/7**, and actual-C++ client checks passed **33/33**.
The full suite passed **48/49** initially: setup-updates hit a Windows file lock
while deleting a disposable font file. The unchanged focused retry passed;
all 49 test identities passed across those two runs. Both logs are retained.

Protected attempt `1ca7a5fd-7387-44f9-b933-6f9fc71caf54` staged the previously
verified resource fixture and candidate while the desktop and shared locks were
clear. A `MassEffectLauncher` process appeared after deployment. The launch
adapter refused before forwarding to Steam or binding ME2. A subsequent
read-only inventory found no game/launcher, but ownership clarification remains
pending. No pause/Save-menu native operation, New Save or persistence test ran.

The private attempt `local-tools/le2-diagnostic-handoff/run-20261114T0828Z/`
was initially retained while ownership was checked. Subsequent cleanup required
a fresh quiet desktop, the same owned lock, no bound/dispatched game, and exact
comparison of every protected file and directory. Only the session's staged
save, ASI, loader and preserved stock Bink differed, with their expected hashes;
no intervening player-file, config or launcher changes were found. The protected
reset then archived the attempt and verified all four roots restored. Its
`owned-staging-cleanup-precheck.json` records that limited cleanup justification.
No test candidate or save was left deployed by this cleanup. The later run
above used the user's explicit next-free-window authorization and fresh
ownership checks; cleanup itself did not grant a new native window.

### Eight edited resources observed after normal loading

Protected run `4664cdc3-560b-41d0-be33-605607a1da2c` tested clean candidate
`cf764fc7f9d7c449dab10f2dec59db79a09a3016`, ASI SHA256
`683929845accb5505f0fe1d0616e2b369756a1a8e3cf17523246c3618e8926ae`, with the
verified `59b5e775` loader. Three ordinary builds, **48/48** candidate CTests,
**7/7** diagnostic tests and **32/32** actual-C++ client tests passed. The
Python exporter/test-registration revision then passed **49/49** CTests;
its numeric-overflow fix passed **21/21** focused exporter tests, including the
actual C++ decoder. ME2 PID 17328 started at `2026-11-14T08:15:12.5157653Z`;
the producer sampled on game thread 34840.

The immutable Normandy base was changed only at the eight resource fields.
The pinned Trilogy Save Editor source oracle independently reopened both files,
verified checksums and byte-identical reserialization, and confirmed exactly
these semantic changes, with no story/location edits:

| Resource | Base | Prepared and observed live |
|---|---:|---:|
| Credits | 32193 | 32194 |
| Medi-gel | 7 | 6 |
| Element zero | 4193 | 4192 |
| Iridium | 71154 | 71153 |
| Palladium | 93177 | 93176 |
| Platinum | 37333 | 37332 |
| Probes | 30 | 29 |
| Fuel | 1500 | 1499 |

Prepared fixture SHA256:
`00054f9a9b5d56840fecfc443da27318734efe89622817e6973e5f3f4696282c`.
Recipe SHA256:
`fd19f141b7bdd35f01deb3201f59283a982a9d3b2f0951b6e696ec6ea255e777`.
The reviewed career/stem mapping was reused with the new content hash; its
prior image remained naming evidence, not proof of edited content. This run
also produced a readable current Load image showing Save 99 / Normandy
highlighted, SHA256
`a6eb525e37ff5f184dbfc27434484157c54272e1f4a3ed48bfd0586e0615307f`.

Ten correlated requests covered handshake, normal menu transitions, complete
four-row descriptor scan, one normal fixture load, and live resource observation.
Load request `7f21caf6-1a3e-40a0-b856-50d2412cee9d` returned `dispatched` through
the exact owned base route with its 12-byte parameter frame intact. Resource
request `e41da53e-5d5e-429d-8ae6-574805570c16` returned 4,818 bytes: all eight
fields available, the squad/player route agreeing, stable map `Biop_Nor`, and
the soldier player pawn. The observer read live inventory fields twice; it
neither called getters nor used the save cache. See its
[source and identity limits](le2-resource-observation.md).

The [state exporter](../tools/qa-le2/export-state.md) accepted the archived
request/result, exact process/module identity and frozen fixture/recipe/scenario,
and returned **0 / matched-observed-start-only**, with no failed or unavailable
checks. Exported state SHA256:
`41858e917e8adca2774d7b08db5de8d58ca55a0f2c2eb34f5c44b3803f24a523`.
The coordinating agent separately viewed the loaded PNG and saw Shepard on the
Normandy crew deck with Crew Quarters / Open; image SHA256
`a38498e28d7d97d7d59bc6d54b920ec5f76299deb92f3f38e0765c463628bd9e`.
Map/actor-world containment remains the observer's documented limitation;
the combined load, state and image evidence supports this bounded fixture test.

The game closed normally. All four protected roots were restored and verified,
the owned invitation was removed, and no request remained outstanding. Private
`local-tools/le2-diagnostic-handoff/run-20261114T0815Z/` retains frozen inputs,
raw and decoded traffic, module/log/capture records, exported state/check,
normal-close/restoration receipts and sealed `native-outcome.json`.
The preceding staged attempt `f8d5de26-4426-46a6-9f0f-a2f6426309b9` was restored
before launch when review found a 258-byte reviewer note exceeded the host's
256-byte bound. Its frozen evidence was preserved; the successful attempt used
a fresh plan with a 186-byte note.

This proves resource preparation through **normal loading**, not in-game
save/reload persistence or native resource consumption. The next step uses the
[normal save menu source route](le2-normal-save-source.md), then independently
checks and reloads the resulting save. Reload/interrupt/scanning gameplay and
all VR gates remain open.

### Frozen manual fixture loaded through the normal menu

Protected run `7fbcb0df-d6bf-405c-a5cc-1245011a51c9` tested clean candidate
`71f17c7db1b5fceae0bb91526be010750457af1b`, ASI SHA256
`d3dc8e72a549166ac603cf14e032589c3c8410a806979c85eedaf7aecea364ed`, with the
verified `59b5e775` loader. All three ordinary builds, **48/48** CTests,
**7/7** focused diagnostic tests and **31/31** client tests with actual-C++
conformance passed. ME2 PID 33732 started at `2026-11-14T07:44:35.4674983Z`;
the producer answered on game thread 60496.

Ten requests reached terminal responses: ping, splash observation, splash
advance, main-menu observation, load-menu opening, Load observation,
`observe-fixture`, `load-fixture`, post-load `observe`, and final ping.
The frozen plan bound the complete manual descriptor and staged/frozen file
hash `8c18e1d97c04a19a4a63af58e97ecb2e4c27226240c9cc34807324d5c185784b`.
Its mapping attestation referenced the preceding run's complete descriptors and
readable menu capture. The host reported current file binding available while
explicitly retaining `imageContentsIndependentlyVerified=false`.

Request `35920ebd-d670-40b1-95de-d99d1740ac11` completed a full four-row scan,
found exactly one match at current UI row 0 (`Save_0099`, descriptor Index 99,
Type 0), and observed the owned base route. Request
`825e04ed-786a-4d9e-8a21-bd56bb064c0a` then invoked the normal menu LoadGame path
once and returned **`dispatched`**, with an 8,156-byte payload, exact-owned-base
routing, 12 parameter bytes, intact adjacent guards and normal return. This
response itself still reported world validation pending; it was not relabelled
as gameplay completion.

The separate post-load request `5df769e1-dbc1-41cb-bd76-67d27efb7433` completed
with a 14,100-byte structural observation: world outer package `Biop_Nor`,
validated pawn class `SFXGame.SFXPawn_PlayerSoldier`, and finite Actor.Location
approximately `(-3.1190218925, 4908.1416015625, -459.3500061035)`. These are the
observer's engine coordinates, not calibrated VR coordinates. The independently
decoded original fixture has the same X/Y and Z `-458.8500061035`; observed Z is
0.5 engine units lower, with cause untested. This is not exact unchanged-position
proof. The coordinating agent viewed `loaded-fixture-001/frame.png` (5120 x 1440), SHA256
`62a43ea56a519d2b1f56e0c31238876eb649c2a041f6782badd0717ce4cc104d`, and saw
Shepard on the Normandy crew deck with the Crew Quarters / Open prompt.
Together, the frozen reviewed binding, unique current descriptor, guarded call,
post-load world/pawn and visible scene support **this bounded scripted fixture
load**. No single map string or call return supplies that conclusion alone.

Two earlier images in this same run, `selected-fixture-001` and
`selected-fixture-002`, had machine status `captured` but appeared nearly black
to the coordinating agent. They are **rejected as visual selection evidence**,
not discarded or promoted by their capture status. Their SHA256 values are
`8cfcc44f0d890f2253ab0ac5608774d94ea8b2d5faaea67f20ae1b29cce41ae6` and
`9960740363b98d167b203dab8ddea7409d5ace87e45b131efd1e1091e9cf216a`.
The earlier readable run's image remains the mapping-attestation reference;
the current unique descriptor/file checks provide current admission evidence.
Capture receipts and structural observers retain their own limited acceptance
labels; the attributed scene review does not rewrite them.

ME2 accepted normal window close and exited. All four protected roots were
restored and verified, only the owned invitation was removed, and no outstanding
request remained. Private `local-tools/le2-diagnostic-handoff/run-20261114T0745Z/`
preserves all ten request/result archives, decoded observations, frozen plan and
fixture, mapped-module/deployment evidence, all three captures and receipts,
normal-close and verified-restoration records, plus the sealed
`native-outcome.json` review.

No native save or reload was requested. Resource values, persistence, gameplay
controls/scenarios, general ConsoleCommand ownership and VR are unproven.
The next gate is a normal save/reload cycle plus a bounded resource observer
using the existing engine/SDK and protected runner, with actual values compared
to the fixture and resulting save. Continue from the working menu/control path.
See the
[frozen-fixture workflow](../tools/qa-le2/control.md#reviewed-frozen-fixture-loading).

### Normal menu transitions and rendered desktop evidence

Protected run `decfc779-b5a0-4dfa-a321-0a32205c7b34` tested clean candidate
`a0596b4fc3c4d47544c5ecf375cd91d18a3390a1`, ASI SHA256
`a3e69e4b554ed2bdfe59ce0b349a1f4126202a5f9a0b5feb46db21bd344cffa1`, with the
verified `59b5e775` loader. All three ordinary builds, **48/48** CTests,
**7/7** focused diagnostic tests and **27/27** client tests with actual-C++
conformance passed for this candidate.

The protected launcher bound ME2 PID 44936, started at
`2026-11-14T07:28:17.1524564Z`. Seven correlated requests completed on game thread
54180: initial ping, splash observation, `advance-splash`, main-menu observation,
`open-load-menu`, Load observation and final ping. Both explicit native actions
returned `dispatched` through `exact-owned-base`, RVA `0xD8D50`; the load opener's
parameter frame remained intact. Their compact payloads were 5,909 and 8,666
bytes. The subsequent main-menu and Load observations were 4,877 and 7,939 bytes.
Each action entered once; no retry or general console command was sent.

The later observation found the active `SFXSFHandler_PCLoad`, GuiMode 1,
LoadMode 0, `fromMainMenu=true`, `CurrentCareerIndex=-1`, enumeration finished
and checking-data complete. Its complete four-row snapshot contained:

| UI row | Descriptor SaveName | Descriptor Index | Type |
|---|---|---|---|
| 0 | `Save_0099` | 99 | Manual (0) |
| 1 | `Save_0036` | 36 | Manual (0) |
| 2 | `AutoSave` | -1 | Auto (2) |
| 3 | `ChapterSave` | -1 | Chapter (3) |

All four descriptors had the same career. These are observed descriptor values,
not a general filename-construction rule. The staged immutable fixture's SHA256
was independently checked as
`8c18e1d97c04a19a4a63af58e97ecb2e4c27226240c9cc34807324d5c185784b`.
The observer still reported `fixtureIdentityProven=false`: this run did not
freeze a descriptor-to-file binding or call `LoadGame`.

The run-bound capture helper produced three 5120 x 1440 desktop PNGs with
`captured` receipts. The coordinating agent viewed all three: the splash showed
Press Any Key, the next image showed Main Menu, and the final image showed the
Load screen with Save 99 / Normandy highlighted. That attributed image review
supports the visible transitions; the machine receipts retain their separate
`pending-human-review` label. A highlighted row is not a loaded fixture, and
these desktop images provide no stereo or headset evidence.

ME2 accepted normal window close and exited. The session reached `restored`,
with all four protected roots restored and verified, only its owned invitation
removed, and no outstanding request. Private
`local-tools/le2-diagnostic-handoff/run-20261114T0725Z/` preserves the seven
request/result archives and decoded reviews, deployment and mapped-module
identity, three PNG/JSON capture pairs, fixture, normal-close and restoration
receipts. No raw game data or screenshots are published here.

This proves reuse of the game's existing splash advance and Default-mode load
menu opener, followed by independently observed UI transitions. It does not
prove selected-fixture loading, native save/reload, general ConsoleCommand
allocation/ownership, gameplay scenarios or VR. Next work must bind a complete
observed descriptor to the frozen fixture and revalidate the current handler,
mode and unique row before any separately reviewed normal `LoadGame` action.
See the [source handoff](le2-menu-load-source.md) and
[current control workflow](../tools/qa-le2/control.md).

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
   ammo, plot and selected-save assertions. The live resource exporter and one
   resource-edited normal load are now proven above; remaining scenario-specific
   observers and actual VR input are pending.
6. Native baseline behavior for reload, timed interrupt (including missed window)
   and scanning/discovery, followed by actual VR interaction proof when those
   features exist. Restore and verify the real protected installation afterward.
7. Both-eye review, physical seated/standing comfort, repeatable performance and
   campaign/DLC coverage. The matrix identifies the work but is not evidence of
   completion.

Use the proven request channel to complete the minimal no-op fixture
load/save/reload cycle before increasing scope.
Resource-only preparation and live values after normal loading are now proven;
continue with save/reload persistence and each natural scenario. Keep every result attributable to its actual candidate and
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
