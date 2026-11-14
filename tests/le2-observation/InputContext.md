<!-- SPDX-License-Identifier: MIT -->
# Input-context observation checks

`engine/Le2InputContext.hpp` exposes `CaptureInputContext(thread)`,
`SameContext(before, fresh)` and `ContextJson(context)` in
`mevr::engine::le2_diagnostic::input_context`. The helper reuses the diagnostic
observer's bounded class/object checks, `ReadSaveFlow`, `PanelContext` and proven
closed PowerWheel predicate. It executes no engine function and changes no input,
binding, panel, plot or save state.

`Context` contains copied process-local identities and flags. Keep it on the
verified game-thread owner. JSON omits addresses and exposes only diagnostic
indices; those indices are not wire handles or durable object identities.
Capture again each game tick and cancel an admitted pulse permanently when the
fresh sample is unavailable or `SameContext` is false. Restoring old values must
not rearm it. The helper does not observe transitions entirely between samples
or revoke an already returned polling result; hook serialization and shutdown
remain separate responsibilities. Position is intentionally excluded from
equality so ordinary movement does not invalidate its own pulse.

Initial contexts are exact default mode 0 and exact GUI mode 7 with a focused
PartySelect panel and concrete PCPartySelection handler. Default admission also
requires active mode, movement/camera flags, no controller cinematic/ignore
input state, no world pause/players-only transition, and null focus or the
validated closed wheel. Party selection requires both interactive initialized
panel state and `m_bSFScreenInitialized`; selected members and NoSelectMode are
copied as change detectors, not interpreted as enabled controls. Unknown focused
panels and galaxy/orbital modes are not enabled.

SDK provenance is the pinned LExSDKv2 submodule
`6bd05809cccb1a099a520371a95779d4a56bb246`: Engine PlayerController's PlayerInput
at 0x6F8 and ignore-input bytes at 0x7A5/0x7A6; SFXGame game-mode, party handler
and GUI declarations. Read-only inspection of installed SFXGame package SHA256
`681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`
shows `BioSFHandler_PartySelection.DoInitialization` sets the screen flag just
before requesting `FinishInitialize`. Thus the flag does not prove Flash finished
drawing or a particular button is enabled. A fresh reviewed scene and actual
binding/input evidence remain necessary. Raw source extraction stays private.

`InputContextTests.inc` is part of the existing standalone actual-SDK observation
test. It uses synthetic objects for default/closed-wheel/PartySelect cases,
controllability loss, missing/foreign/replaced input, malformed focus, array
bounds, changed handler lists and selected members, second-read mutations and
wrong-thread refusal. It never installs a hook or produces native input.

```powershell
cmake -S tests/le2-observation -B local-tools/input-context-sdk-tests `
  -DMEVR_OBSERVATION_SDK_DIR="$((Resolve-Path external/LExSDKv2).Path)"
cmake --build local-tools/input-context-sdk-tests --config Release
ctest --test-dir local-tools/input-context-sdk-tests -C Release --output-on-failure
cmake --build local-tools/input-context-sdk-tests --config Debug
ctest --test-dir local-tools/input-context-sdk-tests -C Debug --output-on-failure
```

The project also compiles the production observer object with `/W4 /WX`.
Passing these tests is offline structural evidence, not native input acceptance.
