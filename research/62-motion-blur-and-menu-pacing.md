<!-- SPDX-License-Identifier: MIT -->
# LE1 motion blur and menu pacing (2026-11-08)

Status: the initial render-guard candidate passed host tests, native-layout/signature checks, and
physical Quest/VDXR instrumentation, including a forced re-enable and crew-conversation return.
The combined menu/VDXR/blur candidate was installed at 15:38 on 2026-11-08; combined headset QA is pending.
The 2026-11-09 [startup/culling follow-up](68-culling-and-startup-followup.md) additionally validates
guard-aware removal of the timed settings resets across repeated full title-to-game transitions.
The owner could not judge sharpness reliably in Normandy; planetary visual acceptance remains open.
The combined candidate adds a conditional menu-return fallback, described below. AA and One Draw remain enabled.

The owner's acceptance boundary is blur-free gameplay. Cinematic blur may remain if suppressing
it adds complexity, but returning from a cinematic must not leave gameplay blurred.

## Why false in an INI is insufficient

The local LE1 2.0.0.48602 executable has multiple representations of motion blur:

| State / operation | Local native evidence |
|---|---|
| Game-thread system setting | `GSystemSettings` at RVA `0x1795D60`; `MotionBlur` at `+0x34` (`0x1795D94`). Its console/settings table pairs this field with the UTF-16 `MotionBlur` name. |
| Profile graphics application | `BioGamerProfile.ApplyGraphicsOptions`, native body `0x9FF380`, copies `GameOptions.m_nMotionBlur` at `+0x58` directly to that live setting. The script exec thunk `0xB9CC00` calls vtable slot `+0x480`, which resolves to this body. |
| Render-thread settings | The settings propagation routine `0x8B5B60` reads the game-thread field and either transfers it directly or queues a command. The command body `0x8BA2B0` compares and updates a separate cached blur value at `0x17961C0`, alongside other render settings. A change invokes additional renderer work. |
| Additional blur permission | `BioPlayerController.DisableMotionBlur` (`0xC73E80`) writes zero to `0x1795D9C`; `RestoreMotionBlur` (`0xC77380`) writes one. These are a separate flag from the stored MotionBlur preference. Exec thunks `0xBAE810` / `0xBAE850` dispatch to controller vtable slots `+0xBF8` / `+0xC00`. |

The profile can therefore overwrite the live setting without reading the mod's INI. This is a
concrete re-enable path, not proof that it caused every previously observed cinematic incident.
Likewise, the existence of `RestoreMotionBlur` does not establish when a particular cinematic calls it.
Its flag is not sufficient by itself: some paths check the main preference without this extra permission.

Sources: local decrypted executable and private Ghidra project; the native-function dump;
`external/LExSDKv2/Src/LESDK/LE1/SFXGame_structs.hpp` (`BioGamerProfile.GameOptions`, size `0xB0`);
`Engine_structs.hpp` (`FPostProcessSettings`); and the native exec-to-vtable traces above.
Raw executable/decompiler output remains outside the repository under the local Ghidra data directory.
Names for internal unnamed routines describe observed behavior, not recovered symbols.

## A durable guard must cover the renderer's choice

Two native effect-proxy factories (`0x284FB0`, `0x285540`) select distinct blur-enabled and
blur-disabled variants. Both read the main blur preference, the effect's enable flag, and an optional
per-view post-process enable bit. The first also checks the additional blur permission. The post-process
chain assembly routine `0x4D2D50` uses the main preference when accumulating blur settings. The standalone
motion-blur effect's visibility routine `0x43ED90` checks the preference and permission, selecting
game-thread or render-thread copies according to execution context.

These are viable investigation points for a blur-only renderer guard. They are not safe to replace with
an unconditional return from a combined post-process function: the same factories retain other effects.
Disabling only `RestoreMotionBlur`, repeatedly zeroing one global, or setting blur amount in one camera
would leave uncovered paths or cached state.

A narrower first intervention is to clamp only the blur member of the options passed to
`ApplyGraphicsOptions`, leaving the remaining settings intact. That prevents the verified profile
overwrite, but must not be presented as complete protection against all post-process overrides.
The final renderer guard needs validated signatures, correct thread ownership, and coverage for already
constructed proxies. Avoid temporarily flipping shared globals around a function call: another thread
can observe the temporary value.

## Implemented render guard

`plugin/MotionBlurGuard` hooks the shared blur proxy render body at `0x279360`. Before any
blur shader parameters are bound, it replaces a recognized proxy's vtable with the matching
no-blur vtable and enters the native no-blur render body at `0x27A100`. The changed identity
stays with that proxy, so subsequent eyes/frames dispatch directly to no-blur. A newly created
blur proxy is caught again on its first render. This also covers proxies cached before the hook.

Five pairs are supported (blur -> no-blur vtable RVAs): `0x105AEF8 -> 0x105AE08`,
`0x105AF48 -> 0x105AE58`, `0x105AF98 -> 0x105AEA8`, `0x105C9A8 -> 0x105C908`,
`0x105C9F8 -> 0x105C958`. Native constructor comparisons show the three base pairs differ
only in their vtable assignment; the other two specializations share those constructors and
identical extension-field initialization. Each pair has the same destructor and allocation
layout. Both native render bodies take the same six integer/pointer argument slots. Their
observed code difference is the blur shader-parameter binding, with virtual shader selectors
also supplied by the paired vtable. No game code is copied into the mod.

Installation requires exact native signature matches and complete fingerprints of all ten
vtable slots in every pair, normalized to the executable base. A mismatch prevents installation.
An unknown proxy entering the hook follows the original code and increments `unknown`; it is
never reinterpreted as a known class. The existing settings workaround is retained for either
case. The vtable write occurs on the render-owned proxy and is journaled. The hook performs no
console commands, file I/O, shader compilation, allocation, or logging; counts are reported on
the game thread and in `renderLevers.motionBlurGuard` in performance probes.

This guard disables these blur variants throughout a mod session, including cinematics; allowing
cinematic blur is not required to achieve the owner's acceptance boundary. It does not change
profile settings, AA, One Draw, bloom, or the remaining post-process parameter fields.
`[Performance] motionBlurGuard = 0` is a restart-only diagnostic opt-out. It does not restore
already-converted objects within a running session.

Automated checks passed: all five pairs, argument/return forwarding, persistent native field
updates, cached second-eye dispatch, recreated effects, unknown-class fallback, and refusal to
hook unverified code. Local executable checks passed all ten vtable fingerprints, paired
destructors/dispatch targets, the three base constructor comparisons, and twelve address
signatures. The isolated monolithic LE1 candidate builds successfully. These checks are not
headset or runtime visual acceptance.

## Physical VDXR validation

The isolated candidate was installed on 2026-11-08 at 15:04, retaining the prior replay fixes.
Its SHA256 is `571FBD72D6CCFEBDEB35E4E2E8E43FA024F8423093937A28AB5CABFC1B9DF48C`.
The next cold launch verified the native addresses and installed the guard at 15:05:29.423.
The owner used Quest 3 through Virtual Desktop, 90 Hz, source 2284 x 2448, eyes 2688 x 2880,
AA enabled, and One Draw (`sharePass = 5`) enabled.

For each challenge, the old settings reapply was paused with `apply = 0`, then the game's own
`BioPlayerController.RestoreMotionBlur` was called on the game thread and `scale set MotionBlur True`
was issued. This combination produced actual blur-enabled proxies for the guard to intercept.
The harness restored MotionBlur False and the previous reapply policy in its cleanup block.

| Capture start / tag | Frames | Seconds | Cumulative proxies converted |
|---|---:|---:|---:|
| 15:07:08 / `blur-guard-challenge` | 1,321 | 15.0101 | 1,547 |
| 15:08:05 / `blur-guard-menu-conversation` | 5,305 | 60.0025 | 5,257 |
| 15:09:24 / `blur-guard-conversation-return` | 5,383 | 60.0067 | 10,858 |

All three probes report `installed: true`, zero unknown proxies, zero recorded access violations,
zero replay failures, and `sharePass: 5`. Counts are cumulative across the session, not per capture
or distinct rendered frames. This live scene exercised the fourth of the five supported pairs;
the other four have native-layout and automated test coverage, not observed live coverage here.

The navigator conversation began at 15:08:42.977 and ended at 15:09:03.579, while the old settings
workaround was still paused. The second capture includes the conversation and about 1.6 seconds
after returning to gameplay. The third capture is post-conversation gameplay with another forced
restore, not a second conversation. The guard continued converting effects after the return.
Final readback confirms MotionBlur False, AA True, unchanged source size and installed ASI hash,
with no pending requests. The normal settings fallback remains enabled.

This verifies the guard is active and handles a real native re-enable path through a conversation
return. The owner answered "Couldn't tell" to the sharpness comparison and explained that the
asset-disappearance symptom occurs mainly on planets. That symptom is not marked fixed by this
Normandy test. Frame-time stalls also remain: the first two captures contain approximately 107 ms
intervals dominated by `xrEndFrame`; the post-conversation capture's maximum interval is 40.90 ms.
No general pacing or intro-performance improvement is claimed.

Private receipts are in `scratch/motion-blur-guard-20261108/`: `runtime-summary.json`, the three
timestamped challenge directories (probe JSON, frame/tick CSVs, console results and logs),
`stage.json`, `final-state.json`, and pre-install backups. These artifacts remain local.

## Connection to the menu-return hitch

Before the combined change below, `src/game/SystemSettings.cpp` reapplied all requested graphics values after a transition from
mode >4 back to gameplay, as well as on world changes and settle timers. Every boolean goes through its
opposite first. This was added because simply sending False while False was already stored did not
clear the visible blur (see the recurring-blur gotcha).

The latest physical VDXR Balanced capture contains a 119.951 ms frame interval overlapping a 126.224 ms
host tick and this reapply after the game menu closed. That establishes correlation with this particular
transition, not a measurement of the cost of MotionBlur alone. The same run still had occasional
submission-side stalls after gameplay settled; fixing the reapply cannot be claimed to remove those.

The code already skips unrelated INI rewrites when graphics values are unchanged. Its log reason gives
file timestamp changes precedence over menu return, so "the file's values changed" alone does not prove
any graphics value changed.

### Combined candidate: conditional menu-return reset

At the owner's request after the physical guard test, the combined candidate contains the committed
menu-rail changes (`6d38cbc`), Virtual Desktop/compute-FXAA replay fix (`9252d10`), native blur guard,
and a conditional replacement for the blanket menu/conversation-return reapply.

HostServices v37 appends a size/version-checked `motionBlurProtected` callback. It returns true only
when the hook is installed and its cumulative unknown-proxy count is zero. Older hosts, a missing
callback, disabled/failed installation, or any unknown proxy retain the old workaround. Unknown
coverage stays latched for the session. If coverage is lost while already playing, the mod schedules
a fallback apply at its next one-second poll and retries if the player is temporarily unavailable.

While protection is healthy, returning from a menu or conversation consumes the transition without
sending the blanket `scale set` sequence. Rewriting the INI with identical settings also sends no
commands. Startup, level changes, the existing 3/15-second settle applies, actual graphics edits,
and explicit `apply=0` behavior are retained. This change does not disable all settings application.

The log reports `menu-return reapply disabled: native motion-blur guard healthy` and records skipped
returns. A loss reports `enabled: native motion-blur protection unavailable` and the fallback apply.
Probe telemetry adds `renderLevers.motionBlurGuard.protectsGameplay`.

Focused tests exercise the actual settings command path: guarded menu/conversation returns,
identical-file saves, actual edits, initial/load/settle applies, missing/old/short host services,
guard loss without a transition, unavailable-player retry, and explicit opt-out. Guard tests also
verify that a foreign effect revokes the callback's protection result. Combined headset validation
and a measured menu-return timing comparison remain pending; residual slow-pan submission stalls
are not claimed fixed. The combined source/build/install receipts live privately under
`scratch/combined-vdxr-20261108/`.

The monolithic LE1 build passed, followed by all seven selected CTest checks (core/menu, WARP draw
replay, blur guard, settings path, performance probe, RVA table and OKF). Installed ASI SHA256:
`CB2DD0DCE2E9A4CB5D501BCE72F14DA4A2F05EC1B0D6FEEF8F418C78269624EA`. The install preserves the
Mako-seat changes that another task installed during compilation, traced to that task's frozen
source and receipt. Final readback verified all six runtime menu assets, AA True, sharePass 5,
source 2284 x 2448, unchanged renderer/game configuration files, and VDXR selected. The only host
INI change points Menu.assets to the matching installed assets. The game was left closed for the
owner's test; this is build/install acceptance, not combined runtime or visual acceptance.

## Separate startup lead

The startup log shows 12 DrawReplay variant hooks installed across approximately 0.93 s, with adjacent
installs roughly 70-86 ms apart. Both our private MinHook backend and loader SPI backend enable each hook
individually. MinHook's enable path suspends/resumes threads per call; its documentation recommends
queueing changes to do this once. This is a plausible contributor to the intro stalls, not a measured
attribution of the entire interval. Loader SPI currently lacks a batch operation; changing its virtual
ABI without negotiation would break compatibility. Any batching work must also publish original
function pointers before detours become callable.

## Remaining visual and pacing validation

1. Exercise additional planetary/cinematic effects; this session covered cold launch, Normandy, menu,
   and navigator conversation return, but only one proxy variant was observed live.
2. The native restore plus MotionBlur True positive control passed above. `scale set MotionBlur True`
   alone previously failed to reproduce blur; do not use that alone as proof of guard coverage.
3. Verify planetary gameplay remains sharp with AA and One Draw enabled. L/R sharpness equality is not a useful
   blur detector under One Draw, because the replay can blur both eyes alike.
4. Compare menu-return timing in the combined candidate against the earlier unconditional reset.

Related: [motion-blur gotcha](../docs/knowledge/gotchas/motion-blur-in-stereo.md),
[settings precedence](../docs/knowledge/gotchas/system-settings-precedence.md),
[performance experiments](35-performance-attempt-log.md), and [VDXR replay failures](61-vdxr-replay-rejections.md).
