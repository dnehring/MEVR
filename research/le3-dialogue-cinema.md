<!-- SPDX-License-Identifier: MIT -->
# ME3 native dialogue and cinema ownership

**Current dialogue status:** the shared spoken-reply adapter is implemented for LE3 after the 361-row native metadata check and the native string/index audit below. Its public source guard is removed; `[Thespian] on` still defaults off. Host `setThespian` publication/cleanup must be wired by integration. Full conversation/headset acceptance remains pending. First-person world conversations and native voice suppression remain disabled. Earlier sections record the preceding staged gates.

## Verified native dialogue contract

### Native pad selections and missing console route (2026-11-15)

Protected session `20261115-200511-376-LE3-navigation` follows the original
AutoSave through Cronos Station's Enter Orbit and Dock prompts into the native
briefing. The HUD capture shows the eligible **We go now!** reply. Holding the
left stick toward that reply while pressing A advances from mode 7 to the
authored transition in mode 8, followed by the pre-assault cabin scene. A second
visible reply (**Absolutely**) similarly advances that scene. Separate direction
and confirm pulses, with the stick neutral at confirmation, did not advance the
first wheel. Use a held direction plus confirm in synthetic native-pad fixtures.

The relevant captures are `20-10-33-279` (briefing wheel), `20-12-58-277`
(accepted briefing transition), `20-13-37-439` (cabin wheel), and
`20-14-32-695` (accepted cabin transition), all on 2026-11-15. Inspect the
separate `-hud.png` as well as cinema images: the dialogue wheel is not in the
cinema source image. Fifteen captures report no errors; the owned run exits
successfully and restores profile/game/VR files with zero differences.

This is bounded native pad-selection evidence, not voice, interrupt, full-text,
movement-lock or physical-headset acceptance. The same run finds that
`mevr thespian cards` incorrectly returns `unknown on ME3`: `RunOursLe3` omitted
the route already present in LE1/LE2. It now delegates to the existing shared
`thespian::Command`, retaining its live-wheel and input-owner checks. No second
dialogue implementation or first-person conversation mode is added. Native
inspection/selection through this repaired console route still needs a rebuilt
runtime check.

Offline validation of the routing change: all three development host/mod pairs
and player ASIs build, as do the required LE1 development targets. The main
build's 71-test suite passes. The trilogy suite passes across its full run and
three focused installer retries outside the sandbox (sandboxed backup paths
and localhost connectivity failed initially). SPDX, knowledge-link and whitespace
checks pass. These checks do not promote the native command gate above.

Installed LE3 `SFXGame.pcc` scripts and class-default properties were inspected with the existing local conversation scanner. A private copy changes its default-object exclusion to an inclusion; no public scanner or game package was modified. Raw outputs remain ignored locally.

`SFXGameModeConversation` inherits `bAllowMovement = false` and `bAllowRotationUpdate = false` from the base default. Its `Activated` calls the base activation, which invokes `IgnoreMoveInput(true)` and `IgnoreLookInput(true)` for these flags. The gameplay mode explicitly enables them. Authored conversations therefore retain the game's movement lock, including seated scenes. The mod's first-person world conversation path remains disabled.

The conversation handler uses left-stick X/Y as its reply-wheel direction. The PC variant accepts those axes in gamepad mode. Zeroing the stick in the LE3 bridge consequently disabled replies; the bridge now preserves conversation axes while leaving cinematic axes suppressed. The shared controller mapping strips stale gameplay wheel/hand-walk ownership in this context so it neither rotates the reply direction nor steals A/X. Native menu/calibration filtering still has final ownership of the pad.

The native mode binds LT to Paragon interrupt, RT to Renegade interrupt, A to reply selection and X to skip. The mode calls `SFXGUIInteraction.GetConversationHandler(player)`, whose movie tag selects the live conversation handler. Interrupt methods invoke the movie's native interrupt action; the subsequent handler callback obtains the full `BioConversationController` and requests its interruption. We retain this complete native route rather than invoking a guessed conversation index or selecting an interrupt directly.

Replies in ME3 live on `BioConversationController`, reached through `BioWorldInfo.GetConversationManager().GetFullConversation()`. Its native wheel uses `m_nSlotsUsed[6]`, refuses GUI style 4, and handles investigate/return submenu entries before queueing a reply. At the initial ownership checkpoint the LE1/LE2 Thespian adapter used `BioConversation` fields and could not be enabled by widening a guard. The dedicated controller/data adapter and audited dispatch below supersede that intermediate refusal; native speech suppression and world-camera repositioning remain disabled.

The default browser pages also confirm squad/map/journal entries but no standalone codex entry. `GameMenuRequest` discovers pages dynamically; a missing codex entry remains an explicit refusal until the combined `SFXGUI_JournalCodex` tab contract is audited.

## Offline acceptance

The shared PadMerge regression includes conversation direction, A and X with stale squad, weapon-wheel, power-wheel and hand-walk flags. It passes along with the existing 20,000-step LE1 mapping comparison (zero mismatches) and interrupt/orbital tests. Native conversation, an actual interrupt window and a seated-scene movement attempt remain required. Verify that moving the left stick selects replies while Shepard's transform remains unchanged; test menu/calibration takeover and close latches in the same scene.

## Remaining stereo cinema seams

Shared `CineScreen`, `CinemaBackdrop`, `TransitionFade`, `HudPanel` and `WorldPanels` are the implementation sources. The LE2 bridge and renderer already provide the lens-dependent stereo baseline, no-head-look/no-head-position director camera, no gameplay FOV widening, separate eye quads with calculated convergence crops, and a separate UI surface. These must be ported as renderer seams and shared host presentation, not replaced by a new menu or a monoscopic floor effect.

LE3's existing SFR draw classifier and pass-zero capture provide reusable insertion points. Its current overlay RT path is skipped by the SFR branch, so merely enabling `uiOverlay` does not separate dialogue UI. The shared `HudPanel::Redirect` must receive the source-verified late UI draws, with the existing classification/fallback intact. Stereo submission must use actual separate captured eyes, preserve the director lens, and put the wheel/subtitles/interrupt prompts on one readable UI plane. No inferred per-page crop should be used.

First-person conversations are not part of this work. True stereo cinema, its view transitions and native input must be accepted using separate-eye snapshots and a real headset before the port is called complete.

The existing LE2 baseline, stereo quad submission and UI-plane placement now live in shared plugin/SequelCinema; LE2's bridge delegates to that module. LE3 renderer seams will use the same entry points. This extraction alone does not activate stereo cinema on LE3.


## LE3 stereo cinema candidate

LE3 now calls the shared cinema planner and compositor for corroborated conversation/cinematic sources in SFR. The director retains position, orientation and native lens; the parallel camera baseline comes from that view's projection, and gameplay convergence/swap-eyes settings do not alter the screen pair. The actual pass-zero capture and live pass-one backbuffer feed separate eye-visible quads. GUI/movie modes retain the native mono panel. The full-rate setting participates in the existing pacing policy; no second OpenXR session or renderer was added.

Late Scaleform UI draws can enter the existing HudPanel redirect before Halcyon's mirror fallback. This preserves the native wheel, subtitle and interrupt layout on one plane ahead of the cinema surface, with the shared floor and transition fade. An old native UI overlay is suppressed while that cinema surface is submitted. This does not enable native wrist crops or gameplay HUD relocation.

If pass zero is stale/missing, both screen eyes use the same centered crop at unchanged magnification; the stereo convergence crop must not manufacture disparity on identical images. `cineScreen.stereoSource` and `monoFallbackFrames` distinguish this source fallback from genuine separate-eye submissions. The shared planner's baseline remains the intended camera plan, not a claim that both native views rendered.

Offline checks: the modified LE3 XR, camera and D3D capture translation units compile as the existing C++17 Halcyon target; the shared compositor, LE3 bridge and settings module compile with the host's strict C++20 warnings. A focused math regression covers centered fallback at multiple resolutions/depths; its permanent assertion is in TestCineScreen. Full linked builds remain the integration coordinator's check. Required native acceptance: separate-eye captures of a stationary conversation and moving cutscene, actual source counters, clean subtitle/wheel/interrupt separation and menu takeover, no movement in seated scenes, transition/session-recreation checks, and physical-headset comfort. First-person world conversations remain disabled.

## Shared gameplay overlay candidate

The LE3 host now composes the existing Vignette, SightLayers/ScopeCaptureSource, HandReticle and PowerWheel modules into Halcyon's single XR submission. Sight-first ordering and the 300 ms reticle suppression policy are extracted from the LE2 bridge into SequelWeaponLayers and used by both sequel adapters. The power wheel keeps the same icons, coach and lock marker. Menus/calibration suppress these gameplay layers; semantic native mode checks restrict powers and sights to on-foot play. The LE3 reticle additionally requires a recent valid mod-published aim ray, so simply connecting a controller does not claim working weapon aim. Native crosshair hiding remains unpublished.

CMake includes the actual draw/capture implementations, not only their data publishers. The modified shared compositor, both sequel bridges, LE3 renderer settings and all newly linked draw modules compile under the strict host warnings. This is candidate source/compile evidence; the coordinator must complete linked host builds. Suggested host callbacks after those links: setComfort -> Vignette target; setSight/setScopeCapture -> shared scope/sight state; setPowerWheel/setPowerLock -> shared power state; setAimRay/setHandAim -> shared aiming state and the renderer ownership flag. Publishing these services still requires their genuine native producer. Do not publish hideScreenReticle while its LE3 adapter is absent.

The remaining full HUD work is concrete. HudPieces only has Me1/Me2 layouts; neither is an ME3 crop. The installed LE3 SDK names SFXSFHandler_HUD (legacy movie adapter), SFXSFHandler_PCHUD, SFXGUI_WeaponReticleBase and SFXGUI_CrosshairReticle. Audit GetHUDMovie, InitDisplay, OnAspectRatioChanged, weapon/ammo updates and the reticle Show/Hide ownership through the installed package scanner. Then capture the original HUD at at least two backbuffer sizes, during weapon swap/reload, ammo-power changes, damage/shields, squad changes and Atlas use. Prove the actual stage anchors/bands and classify late UI draws before introducing Layout::Me3 or enabling any native mask. Keep native wheel/interrupt/notification content outside a weapon crop. A failed/unsupported crop must leave the original HUD intact. This candidate therefore leaves the gameplay HUD panel/pieces disabled while keeping the separate cinema UI plane available.


### Native HUD geometry acquisition

The installed-package audit now verifies the HUD contracts above beyond SDK declarations. `GetHUDMovie` is a native getter. `InitDisplay` binds separate weapon icon, spare ammo, magazine ammo, grenade, overheat and two player-power clips. `UpdateWeaponDisplay` updates magazine/spare ammunition separately, uses the inventory grenade resource, changes low-ammo indication, and controls icon and grenade visibility. `OnAspectRatioChanged` is an empty event in this package; it supplies no layout transform. Native reticle visibility is also a native entry; its show/hide events manage impact subscriptions, so toggling it casually would change more than alpha.

Use the existing request channel with `mevr guimouse hud` on LE3. The bounded read-only probe calls the verified live HUD getter, reads its Stage width/height, and reports twelve specifically named clips through reflected GFxValue getters (position, width, height, alpha and visibility). Missing objects/functions report unavailable. It neither opens a menu nor changes a clip, save, camera, input or HUD setting. Pair the report with existing snapshots at the required states and sizes. Clip position is local to its parent; it is **not** accepted as an absolute stage/backbuffer crop until parent transforms and native anchoring are verified against the images. Raw scanner output remains ignored; no game source is committed.


LE3 also composes shared TraversalCue label/blink layers between the weapon layers and menus, using the existing setTraversal producer contract. Only on-foot world frames are eligible; menus/calibration suppress them, and session stop/recenter forget stale cue positions. Cinematic/conversation modes cannot acquire traversal presentation. Native ladder/vault activation, blink timing and physical-headset comfort remain the traversal adapter's acceptance, not something proven by drawing the layer.

## Shared squad presentation (L3-6 consumer)

`SequelSquadLayers` now contains the existing LE2 placement-arc and squad-panel frame composition; both sequel bridges call it. LE3 links the existing `SquadPanel`, `SquadPanelDraw`, `SquadPlaceDraw`, `GalaxyHologramDraw`, and shared published-state modules. This supplies actual consumers for `setSquadMenu` and `setSquadPlace`; the latter also supplies the existing squad-selection wheel rendered by `PowerWheel`. The game adapter remains responsible for verified native squad commands.

Only semantic on-foot world frames can show these layers. Main menu, renderer menu, guided calibration, missing head/projection state, and layer capacity retain ownership. The placement arc uses the renderer's actual submitted eye poses and FOVs, not raw tracked eyes. Recenter discards old tracking-space placement data and places the squad panel afresh. Session stop/start forgets both modules' swapchains explicitly, including runtime session-handle reuse, and clears old published views.

Strict compilation passed for both sequel bridges and all linked squad draw translation units. Full host linking is coordinator-owned. Native orders/weapon choice, floor placement, menu suppression, repeated open/close, recenter/session restart, and physical-headset visual placement remain acceptance work; compilation does not establish them.

## Voice host lifecycle versus native dialogue adapter

LE3 now runs the same `Voice::OnPresent` request/key/listen maintenance as LE2; the coordinator owns the matching attach `Init`, unload `Shutdown`, cancellation and HostServices wiring. VoiceModel's optional local llama dependency is available to all three hosts and still reports unavailable when absent. This supplies microphone and recognizer machinery without changing native dialogue playback.

The current game-side Thespian adapter has LE1 and LE2 branches only. LE3 must not inherit LE1's ISACT muting/skip path or the first-person ConversationWorld reader. Native LE3 reply text/queue/handler/ownership evidence and an independent live conversation reader are still required before enabling spoken dialogue selection. First-person world conversations remain deferred; authored seated scenes retain native traversal locks.

## Interaction-label consumer

LE3 links and calls the existing `HudPieceLayers::Interaction` implementation. A visible, fresh game publication with a valid explicit `[Interact] crop` can arm the existing late SFR HUD redirect, copy that band to the selected object's quad, and mask it from the full body HUD panel only after a successful copy. The original captured HUD remains complete if the label copy fails; a label is never submitted without the panel. Capacity is reserved for both. The source capture remains disabled in ordinary gameplay without this explicit crop, and there is no guessed LE1 default for LE3. Menu/calibration, native dialogue/cinema, power/squad menus, non-world frames and missing head state suppress gameplay relocation. Recenter/session clear the old tracking-space label.

Host/capture compilation passed; this is an explicit-crop candidate, not native crop or target-selection acceptance. The installed native HUD's parent/stage transform probe is the next step toward automatic layout and weapon-integrated HUD. The game still needs a live selected-object fixture plus paired original/relocated image captures before the default can be enabled.

## Read-only parent-transform probe

`mevr guimouse hud parents` extends the existing HUD report on demand. It records the movie root transform and, for each of the twelve verified HUD fields, up to eight GFx parent wrappers with local position, scale, rotation, alpha and the raw 4x4 display matrix. It terminates at a null/repeated parent or the depth limit. It does not invoke ActionScript, set properties, free engine-owned wrappers, or derive an automatic crop from unproven matrix conventions.

Installed `GFxUI.pcc` declares native `GFxValue.GetObject(string member, optional Class<GFxValue> Type = Class'GFxValue')` and `GetDisplayMatrix() -> Object.Matrix`; SDK metadata gives a 32-byte GetObject parameter block (member 0/16, Type 16/8, return 24/8) and a 64-byte matrix return. The adapter supplies the actual reflected GFxValue class, follows `_parent` through the native getter, and reads return values through Params. Independent reflection rows are supplied by checkpoint `59e46446`; native execution of these new rows and this parent report is still pending.

The earlier live report (stage 2560x720; weapon ammo local X about -621) proves why local bounds are insufficient. Parent transforms and a combat/weapon HUD frame must be captured together before stage-to-image normalization, crop union and weapon/wrist layout can be accepted. Strict LE3 GUI translation-unit compilation passes for this diagnostic extension.

The LE3 Thespian game entry points now explicitly refuse: Tick cannot start/continue a turn, commands cannot read or pick native replies, live conversation/wheel readers return null, status reports unavailable, and shutdown does not touch LE1 audio. This is independent of whether the host provides radio voice services or an old ini requests Thespian mode. All three game translation units compile with the repository's warning policy. The coordinator separately keeps first-person ConversationWorld and native ISACT hooks out of the LE3 tick.

## LE3 dialogue adapter evidence in progress

Installed SFXGame source separates static BioConversation data from the live BioConversationController. The live reply positions, current entry, speaker/player, subtitle and VO timing therefore need their own field bindings; ME2's BioConversation binding cannot be inherited. BioConversationManager.GetFullConversation supplies the live controller. The six-slot conversation movie still resolves visible reply positions through m_nSlotsUsed. SelectConversationEntry is the native movie's selection route, including investigate/return and conversation removal; SelectEntry also skips a node and is unsuitable when preserving Shepard playback.

CheckLe3Dialogue adds independent SDK comparisons for this split, reply/handler functions and parameter blocks. The isolated LE3 SDK translation unit compiles. The next protected metadata run must validate these rows and provide native RVAs for GetReplyParaphraseText and GetStringFromStringRef before their FString ownership is accepted. LE3 has no ME2-style GetReplyText/GetEntryText on its conversation controller. Live NPC subtitle is available as a borrowed field; a localized full reply needs an audited string-reference conversion, not an assumption that the asset's sText is populated. The current explicit unavailable gate remains until the adapter is complete. Native Wwise mute/skip and first-person world conversation remain disabled.

LE3 also links the existing host Thespian renderer and composes its published panel only in native conversation mode, after gameplay overlays and before our menus. Main/Omni menu and guided calibration, missing head tracking and exhausted layer budget suppress it. Session creation/stop forget the old swapchain and publication; recenter invalidates placement. The existing shared renderer and LE3 bridge compile with /W4 /WX. This is the presentation consumer only: the game-side unavailable guard still prevents native text/selection dispatch until its ownership audit completes, and no host capability is advertised by this commit.

The game adapter now has compiled LE3-specific live-controller/data-object bindings, an independent manager/movie reader, native visible-slot eligibility (including investigate submenu signatures), borrowed NPC subtitle, movie HideWheel/UpdateConversationOptions, and a selection path through SelectConversationEntry. It retains the shared pending-playback ownership so a delayed wheel transition cannot dispatch twice. All three game translation units compile with the repository's warning policy. The public LE3 Tick/command/status refusal remains intentionally in place: native string return ownership and the reply-index interpretation must first pass the protected metadata/native audit. Full localized reply text is still absent; no asset sText fallback is advertised as the spoken line. The native wheel's interrupt triggers remain routed by the existing LE3 input ownership; this increment does not invent spoken interrupt semantics or alter seated traversal.

A better full-line source is now identified: installed SFXGUIMovie.GetUIString is static final native, taking a string reference and optional parsing flag. Metadata checkpoint 3920417f covers its exact ABI. The candidate adapter resolves each validated reply node's srText through that function on the live conversation movie, with parsing enabled. This avoids a power-action receiver or treating asset editor sText as localized dialogue. It compiles for LE3; the public adapter gate still protects the unverified FString return/mapping. Enable only after the protected function metadata and native ownership audit, then exercise normal replies, investigate/back, unavailable persuasion, interrupts, menu cancellation and authored seated cinema without traversal.

## Native parent-probe rejection correction

The protected 355-row presentation probe validated reflected fields/signatures, not transform semantics. Its raw GetDisplayMatrix results are unsuitable for crops: depth-zero translations duplicated X into Y, disagreeing with GetPosition, and depth-two parent wrappers returned huge finite values with homogeneous W=0. Being a live GFxValue UObject does not establish that its wrapped ActionScript object is a display object. No crop or stage normalization is accepted from this run.

HudReport now requires successful, finite GetPosition results for each clip and parent before invoking display/member getters; an unproven parent stops the chain. Matrix outputs are checked for finite/bounded values, homogeneous affine structure, planar/nondegenerate basis and agreement with the independent position getter. Failures print a reason and bounded diagnostic translation/W, never a repaired transform. Even a consistent matrix is labelled unverified; the actual native getter implementation and coordinate convention remain to audit. Nine focused cases include the observed duplicate-X and finite-garbage outputs, non-display wrappers, malformed W, NaN, 3D/singular transforms and valid rotated/scaled matrices. The regression executable and LE3 GUI compilation pass. A fresh native probe is still needed to confirm the new stand-down path.


## Native dialogue ownership audit and enabled candidate

The protected presentation probe reports **361/361** comparisons agreeing, no mismatches or unreflected entries, with exact native targets. The retained local LE3 image uses timestamp `60AEF0C4` and captured text SHA-256 `50c79c1c3e64ceca9af90ce87af919f01361cc9f5adb7efa2de8166e58bcd95d`. Existing Ghidra tooling inspected only bounded functions; raw outputs stay private.

| Native evidence | Verified contract | Adapter consequence |
| --- | --- | --- |
| `BioConversationController.GetReplyParaphraseText`, RVA `0xB3CFB0` | Exec thunk deep-copies its temporary FString into caller return storage, then destroys the temporary. Registered constructor `0xA25690` installs vtable `0x131BBF8`; slot `+0x2C8` resolves `0xA23F00`. | Copy returned text and free the caller-owned allocation once. |
| Paraphrase implementation `0xA23F00` | Position indexes `m_aCurrentReplyIndices`; its value indexes the current entry's `ReplyListNew`. The native resolves that offer's paraphrase string reference. | Existing guarded position -> offer -> reply-node mapping is correct. Check all arrays/current entry before calling any native getter. |
| `SFXGUIMovie.GetUIString`, RVA `0xB68790`, leaf `0xBC3250` | Static native wrapper and leaf create independent string copies using the shared localized talk-table lookup. Returned storage is owned by the caller. The inspected leaf does not consume the optional parse flag. | Resolve validated reply-node `srText` on the live conversation movie; free the returned copy once. Preserve native lookup/token behavior; do not claim extra token expansion. |
| `GFxValue.GetDisplayMatrix`, RVA `0x9108D0`, leaf `0x93AB00` | Display-object branch assigns the same local translation value to X and Y. Non-display values do not initialize the returned matrix. | Keep the rejection checks from `c010e1f1`; never derive or repair a crop from this getter's inconsistent output. |

LE3 Tick/commands/status/shutdown now use the dedicated native adapter and shared spoken-turn state machine. Replies dispatch through the movie's `SelectConversationEntry`, retaining its unavailable-choice/submenu/end handling and a pending interval against repeat dispatch. Native Shepard playback remains enabled; `kSkipSpokenReply=false` also refuses inherited silent-hand settings. NPC subtitles are borrowed fields and never freed. The panel remains in cinema, without world-speaker positioning or traversal changes. The shared voice menu is visible for LE3 and accurately describes sequel playback restrictions.

Validation: LE1/LE2/LE3 game translation units compile under the repository warning policy; existing Thespian turn, microphone ownership, menu cancellation and panel tests pass; the menu document parses. No live dialogue dispatch, microphone choice, interrupt timing or VR readability has yet been accepted. Next native fixture: report cards/indices, pick one ordinary reply, inspect investigate/back and unavailable persuasion, cancel a partial transcript through menu/calibration, test both native interrupts and a seated scene with movement locked. Check localized full-line text for tokens against the game's playback before claiming text fidelity.

Integration wiring: under the existing sequel/main-menu branch, publish `sizeof(vr::thespian::Panel)` and `&host::thespian::SetPanel` for LE3 as for LE2; clear its publication on mod unload. The LE3 renderer consumer/lifecycle is already in `13e3b109`. Keep the separate ConversationWorld/ISACT guards. The existing Mod ThespianLink already uses the ABI-checked panel callback and VoiceLink, and Tick already calls Thespian; its old unavailable comment should be updated.


## Native dialogue reader and focus refusal

Protected run `20261115-203330-839-LE3-navigation` exercises the routed
`mevr thespian cards` command against three successive native reply wheels:
the Hackett mission briefing and two cabin wheels. It returns live paraphrases,
categories and localized full spoken lines. The ordinary pad route advances the
authored sequences; holding the appropriate stick direction through A succeeds.
This is native reader evidence, not microphone or spoken-selection acceptance.

`mevr thespian pick 0` refuses because another owner holds input. Inspection
shows the actual XR session is unfocused even with valid head tracking and
replayed controllers. The LE3 bridge deliberately requires current focused
context; synthetic pad input does not grant that ownership. Preserve this gate
and validate command/voice selection with a genuinely focused session. Do not
weaken it or infer a failed text reader from the selection refusal. Seated
traversal, interrupts, investigate/back, persuasion eligibility and headset
readability remain separate unaccepted gates.

## Selection-ownership review corrections

The review found three concrete shared-path defects before dialogue acceptance: a different native entry could reuse the same reply positions/slots and inherit the old transcript; a failed selection was still counted/flashed as taken; and stop/start requests could coalesce across wheels, admitting late ASR finals to the next entry. These are corrected in the shared adapter, not duplicated for LE3.

Each wheel now captures world/controller/conversation/data/movie identity, current entry, shared scene generation and offered-slot signature. A changed identity ends the old turn and requires the existing stability interval again. Immediately before selecting, the adapter re-reads live cards and eligibility and requires the matched reply, text, style and category to remain identical. Failed eligibility reads refuse a card; illegal persuasion is only drawn if present on the current native wheel. Menu/calibration is checked through `Link.otherUiOwnsInput` again at dispatch, in addition to the tick's existing ownership snapshot. Integration supplies that callback using the same current host/calibration predicates as VoiceLink; this is an internal Link field, not a HostServices ABI change.

Native refusals neither increment successful spoken dispatches nor flash a committed row. They discard the transcript, release the microphone and restore only the still-current native wheel. Pending playback cannot survive a changed world/generation. A new spoken turn waits for the old microphone stream to report closed before requesting its own stream, and only admits listens newer than the prior ID and its own request time. Late finals remain excluded from new dialogue turns. Owned native FString returns are still copied and freed exactly once; borrowed NPC subtitle storage is never freed.

Regression coverage adds same-slot/new-entry, scene generation and object identity changes, investigate signature changes, old/coalesced ASR streams and rejected-selection restart. Existing turn/menu/listen/panel tests pass together with these cases, and all three game adapter translation units compile. Native conversation, microphone timing and menu-transition acceptance remain pending.

## Fresh host input eligibility (2026-11-15)

HostServices v42 adds the optional `thespianInputBlocked` predicate. LE3 requires the capability; an older/missing host blocks native spoken selection, including console commands before the first Thespian tick. LE1/LE2 retain their existing ownership contract. The coordinator connects the existing internal `Link.otherUiOwnsInput` to this host predicate and the shared menu/rail/calibration flags; all immediate reply dispatches already recheck that callback.

The LE3 bridge publishes a 100 ms eligibility lease from the existing overlay pass only for a focused, tracked native-conversation context with a layer slot and no input-owning menu/calibration. This is eligible context, not actual panel visibility: requiring an existing panel would prevent the first turn from starting. Session stop/recreation, recenter, missing head, lost focus, non-conversation mode and exhausted layer budget revoke the lease. It expires when rendering stops. Game-side native conversation/wheel identity is separately reread at selection.

The any-thread predicate reads only atomics and existing guarded menu/latch state; it makes no OpenXR or native calls. It freshly checks the renderer's own menu, guided calibration, shared menu and post-close input latch. LE3's imported menu-open state is now atomic for this query. Focus is sampled on the XR owner thread and published atomically. Panel composition uses the same eligibility decisions. Missing/old/future leases refuse input; tests include a normal 33 ms null-headset frame and the 100/101 ms boundary.

Native acceptance remains pending for spoken selection while opening/closing each menu, entering calibration, losing headset focus/tracking and returning from recenter. Native Shepard playback remains enabled; first-person world conversations and traversal during authored seated scenes remain excluded.
