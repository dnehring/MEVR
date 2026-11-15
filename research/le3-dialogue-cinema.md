<!-- SPDX-License-Identifier: MIT -->
# ME3 native dialogue and cinema ownership

## Verified native dialogue contract

Installed LE3 `SFXGame.pcc` scripts and class-default properties were inspected with the existing local conversation scanner. A private copy changes its default-object exclusion to an inclusion; no public scanner or game package was modified. Raw outputs remain ignored locally.

`SFXGameModeConversation` inherits `bAllowMovement = false` and `bAllowRotationUpdate = false` from the base default. Its `Activated` calls the base activation, which invokes `IgnoreMoveInput(true)` and `IgnoreLookInput(true)` for these flags. The gameplay mode explicitly enables them. Authored conversations therefore retain the game's movement lock, including seated scenes. The mod's first-person world conversation path remains disabled.

The conversation handler uses left-stick X/Y as its reply-wheel direction. The PC variant accepts those axes in gamepad mode. Zeroing the stick in the LE3 bridge consequently disabled replies; the bridge now preserves conversation axes while leaving cinematic axes suppressed. The shared controller mapping strips stale gameplay wheel/hand-walk ownership in this context so it neither rotates the reply direction nor steals A/X. Native menu/calibration filtering still has final ownership of the pad.

The native mode binds LT to Paragon interrupt, RT to Renegade interrupt, A to reply selection and X to skip. The mode calls `SFXGUIInteraction.GetConversationHandler(player)`, whose movie tag selects the live conversation handler. Interrupt methods invoke the movie's native interrupt action; the subsequent handler callback obtains the full `BioConversationController` and requests its interruption. We retain this complete native route rather than invoking a guessed conversation index or selecting an interrupt directly.

Replies in ME3 live on `BioConversationController`, reached through `BioWorldInfo.GetConversationManager().GetFullConversation()`. Its native wheel uses `m_nSlotsUsed[6]`, refuses GUI style 4, and handles investigate/return submenu entries before queueing a reply. The existing LE1/LE2 Thespian adapter uses `BioConversation` fields and must not be enabled by widening a guard. Native speech suppression, automatic reply dispatch and world-camera repositioning are not enabled here.

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
