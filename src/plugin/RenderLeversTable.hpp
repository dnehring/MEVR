// SPDX-License-Identifier: MIT
#pragma once

// plugin/RenderLevers' per-game table (research/sequel-render-levers.md "Shared module"): the one place the module's
// native facts come from, so the same RenderLevers.cpp is every game's.
//
//   Addresses  rows of engine/Addresses.hpp's kTable, found BY NAME in the current game's table at compile time
//              (Row). The names are the contract: LE1's rows have carried them since research/34 stage 4, and a sequel
//              gets a lever by adding a row with the same name (with its pattern, so the exe gate checks it at attach).
//              A game without the row reads 0 and every lever that needs it stands down with one log line
//              (render_levers::Install).
//   Layouts    offsets inside engine objects (FSceneRenderer, FViewInfo, the Umbra commander, the D3D11 RHI...), in
//              Layout below: LE1's values as the defaults, a game's own where they were found to differ, and one
//              `...Verified` flag per lever group that says the game's layout was read in ITS code (never assumed from
//              LE1's). A lever whose rows exist but whose layout isn't verified stands down too.
//
// LE1's values are the ones plugin/RenderLevers.cpp carried as constants until 2026-11-16; the static_asserts at the end
// pin them, so a renamed or edited LE1 row can't silently move or disable an LE1 lever.

#include "engine/Addresses.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mevr::host::render_levers::table {

// A table's row by name: its RVA, or 0.
template <std::size_t N>
constexpr std::uint32_t RowIn(exe::Entry const (&rows)[N], std::string_view name) {
    for (exe::Entry const& e : rows) {
        if (std::string_view(e.name) == name) {
            return e.rva;
        }
    }
    return 0;
}

// ---- The row names (engine/Addresses.hpp) ------------------------------------------------------------------------------
namespace row {
// The scene render and the Umbra commander: the accounting, umbraQueryWait.
inline constexpr char kSceneRender[] = "FSceneRenderer::Render";            // (renderer): one call per scene render, render thread
inline constexpr char kCommanderCommand[] = "UmbraCommander::command";      // (commander, code): BioWare's side of the Umbra resolve
inline constexpr char kUmbraResolveStart[] = "Umbra resolve start stamp";   // float: appSeconds at the resolve's start
inline constexpr char kGetQueryData[] = "FD3D11DynamicRHI::GetQueryData";   // (rhi, query, out, size, bWait)
inline constexpr char kSetCommander[] = "SetUmbraCommander / wireframe state";  // (byte)
// keepBoundShaderStates.
inline constexpr char kBoundShaderStateCtor[] = "FD3D11BoundShaderState constructor";
inline constexpr char kDynamicRhi[] = "GDynamicRHI";
// minScreenRadiusPx, umbraSharedVisibility.
inline constexpr char kUmbraInstanceVisible[] = "UmbraCommander visible instance";
// umbraEyeMatrix, umbraShareResults.
inline constexpr char kUmbraIssueQueries[] = "UmbraCommander::IssueOcclusionQueries";
inline constexpr char kUmbraReadResult[] = "Umbra read of one query result";
// sharePass: the renderer kept (1, 2), the replay's tail mark and the passes (3, 5).
inline constexpr char kDeleteRenderer[] = "FSceneRenderer deleting destructor";
inline constexpr char kAllocateTargets[] = "FSceneRenderTargets::Allocate";
inline constexpr char kDpgLightTargets[] = "FSceneRenderer::RenderDpgLightTargets";
inline constexpr char kPrepassViews[] = "RenderUmbraBasePass";
inline constexpr char kTranslucencyCheck[] = "translucency check";
inline constexpr char kBeforeLights[] = "before-lights pass";
inline constexpr char kDpgPass[] = "DPG pass";
inline constexpr char kDpgLights[] = "DPG lights";
inline constexpr char kForeground[] = "foreground pass";
inline constexpr char kFinish[] = "scene render finish";
inline constexpr char kSceneRenderTargets[] = "GSceneRenderTargets";
inline constexpr char kSceneFrame[] = "scene frame counter";
inline constexpr char kDepthDirty[] = "depth dirty flags";
inline constexpr char kSkipBeforeLights[] = "GSkipBeforeLights";
inline constexpr char kCommanders[] = "Umbra commanders";
inline constexpr char kCommanderIndex[] = "Umbra commander index";
// Scene captures nested in an eye's render (research/47).
inline constexpr char kCaptureScene[] = "scene capture's CaptureScene thunk";
inline constexpr char kBeginBackBuffer[] = "FSceneRenderTargets::BeginRenderingBackBuffer";
// The show flags' bits per word (32), a data global: eligibility for the kept-renderer and replay paths.
inline constexpr char kShowFlagBitsPerWord[] = "show flags' bits per word";
// ME3 only (Layout::umbraCommandGate): a dword the commander's command() tests first. Not in kRowNames: LE1 and ME2 have
// no such gate, so their tables have no row of this name.
inline constexpr char kUmbraCommandGate[] = "Umbra command gate";
} // namespace row

// Every row name the module asks for (the tests check each one is a row of LE1's table).
inline constexpr char const* kRowNames[] = {
    row::kSceneRender, row::kCommanderCommand, row::kUmbraResolveStart, row::kGetQueryData, row::kSetCommander,
    row::kBoundShaderStateCtor, row::kDynamicRhi, row::kUmbraInstanceVisible, row::kUmbraIssueQueries, row::kUmbraReadResult,
    row::kDeleteRenderer, row::kAllocateTargets, row::kDpgLightTargets, row::kPrepassViews, row::kTranslucencyCheck,
    row::kBeforeLights, row::kDpgPass, row::kDpgLights, row::kForeground, row::kFinish, row::kSceneRenderTargets,
    row::kSceneFrame, row::kDepthDirty, row::kSkipBeforeLights, row::kCommanders, row::kCommanderIndex,
    row::kCaptureScene, row::kBeginBackBuffer, row::kShowFlagBitsPerWord,
};

// ---- Layouts ------------------------------------------------------------------------------------------------------------
struct ByteRange {
    std::size_t begin, end;
};

struct Layout {
    // FSceneRenderer (LE1: 0x110 bytes): +0x00 scene, +0x08 the family copy (+0x18 render target, +0x28 show flags),
    // +0x5C Umbra path, +0x60/+0x68 views (FViewInfo, viewStride apart), +0xD8 the scene-begun flag.
    std::size_t viewStride = 0x12F0;             // FViewInfo's size: the stride of FSceneRenderer::Views
    std::size_t rendererViews = 0x60;            // FSceneRenderer::Views data; the count is 8 further
    std::size_t viewState = 0x8;                 // FSceneView::State
    // The show flags that send a renderer down the path without Umbra, whatever its +0x5C says: such a renderer is not
    // eligible for the kept-renderer and replay paths. LE1: 0x33. ME3's Umbra branch also leaves on 0x47, 0x15 and 0x1B
    // (research/sequel-render-levers.md "ME3", difference 3).
    unsigned ineligibleShowFlags[4] = {0x33, 0, 0, 0};
    std::size_t ineligibleShowFlagCount = 1;
    // FSceneViewState's per-primitive occlusion history (a TSet) and its size: perEyeOcclusion.
    std::uintptr_t stateOcclusionHistory = 0xD10;
    // GDynamicRHI's vtable slot of RHIAddRef for a bound shader state, and the handle inside the object the constructor
    // returns: keepBoundShaderStates.
    std::size_t rhiAddRefBoundShaderStateSlot = 0x8A0;
    std::size_t boundShaderStateHandle = 0xC;
    // The view's Umbra camera and its parameter word: umbraSharedVisibility.
    std::size_t viewUmbraCamera = 0x4D4;
    std::size_t umbraCameraImpl = 0x10;
    std::size_t umbraCameraImplParams = 0x420;
    // The visible-instance record's sphere radius, the commander's view and the view's projection matrix: minScreenRadiusPx.
    std::size_t instanceRecordRadius = 0x38;
    std::size_t commanderView = 0x68;            // UmbraCommander's view (FViewInfo*)
    std::size_t viewProjectionMatrix = 0xD0;
    // umbraEyeMatrix: the commander's "view parameters set" flag and the view's matrices (FSceneView's first 0x330 bytes
    // are laid out alike in all three games: src/halcyon/le1/render_hook.cpp and le2/le3 calcview_hook.cpp).
    std::size_t commanderViewParamsSet = 0x5C;
    std::size_t viewViewMatrix = 0x90;
    std::size_t viewTranslatedViewMatrix = 0x190;
    std::size_t viewTranslatedViewProj = 0x1D0;
    std::size_t viewPreViewTranslation = 0x250;
    std::size_t viewViewProj = 0x260;

    // ---- Where a game's code calls the same natives differently (each read in that game's code) ----
    // The visible-instance function is also called to keep books, not only to mark an instance visible: the size cull and
    // the shared-visibility filter may answer "not visible" only for the marking call's shape, a7 != 0 && a6 == 0 (ME1's
    // only call). ME3: true (its wrapper 0x3E3650 calls it five ways; "ME3", difference 1).
    bool instanceCallAlsoKeepsBooks = false;
    // A scene capture can run the Umbra branch (the light targets of DPG 1, the base pass, the finish) inside the eye's
    // render: the replay's tail is then marked only outside a capture. ME3: true (difference 2). ME1's captures never do.
    bool captureTakesUmbraBranch = false;
    // A dword the commander's command() checks first and does nothing while it is 0 (an RVA; 0: the game has no such
    // gate). ME3: 0x1767208 (difference 4), the table's "Umbra command gate" row (its reference has an immediate after the
    // displacement: exe::Entry::tail). Read once under SEH, for the log.
    std::uintptr_t umbraCommandGate = 0;
    // The capture hooks (CaptureScene, BeginRenderingBackBuffer) are this module's to install. ME3: false while
    // plugin/Le3SceneCapture hooks the same two functions (two hooks would each bind the target; the ledger's Forks 9).
    bool ownsSceneCaptureHooks = true;

    // ---- Each game's own default for a lever whose best value differs by game (research/sequel-render-levers.md
    // "Performance research"): what an empty or absent [Performance] key means on this game. ME1's are the values its
    // levers have always had; a sequel's is its renderer's own behaviour until the owner chooses another.
    // perEyeViewState: 1 each stereo pass renders through a scene view state of its own (Halcyon ME2's and ME3's SFR
    // always did); 0 both passes through the game's one state (ME1's gameplay).
    int perEyeViewStateDefault = 0;
    int umbraQueryWaitDefault = 1;   // 1 the resolve waits for its query results (the game's own); 0 never
    int ue3QueryWaitDefault = 1;     // 1 the engine's occlusion reads wait (the game's own); 0 a second render's never do
    // umbraShareResults: 1 the second eye's unready Umbra query results are the first eye's of the same frame (only
    // where umbraSharedVerified admits the lever); 0 each eye waits for its own (the game's own).
    int umbraShareResultsDefault = 0;
    // sfrReplayRetry (ME2's renderer only): 1 Halcyon ME2's same-frame retry of a pass 1 that returned in under 100 us
    // (upstream's [NOOPRETRY]); 0 never.
    int sfrReplayRetryDefault = 1;

    // Which lever groups are on for this game: the layout was read in this game's own code AND the lever ran in the game
    // (its A/B at the baseline spot, or for the two passive groups a first run with the hooks in). LE1: all
    // (research/22, 32, 35, 37).
    bool sceneRendererVerified = false;      // the scene render's hook (the accounting), the renderer's target and views
    bool umbraCommanderVerified = false;     // the commander's command() and the RHI's query read: the resolve's timing, umbraQueryWait, ue3QueryWait
    bool boundShaderStateVerified = false;   // keepBoundShaderStates
    bool umbraInstanceVerified = false;      // minScreenRadiusPx
    bool umbraQueriesVerified = false;       // umbraEyeMatrix
    bool umbraSharedVerified = false;        // umbraSharedVisibility, umbraShareResults (the camera word, the query ring)
    bool occlusionHistoryVerified = false;   // perEyeOcclusion
    bool sharePassVerified = false;          // sharePass 1-5: the kept renderer, the pass functions' signatures, the replay's view ranges
    bool sceneCaptureVerified = false;       // a capture's final post-process pass bound to its own target (BeginRenderingBackBuffer's contract)
};

// A development build for a sequel's first run and its A/Bs (cmake -DMEVR_RENDER_LEVERS_TRY=ON, never a landed default):
// the groups whose layouts lanes R2 and R3 read in the game's own code are hooked, so a lever can be measured before its
// flag is set for good below. In it the two levers that are on by default on ME1 (keepBoundShaderStates 1,
// minScreenRadiusPx 8) start at the game's own (0) unless the ini says otherwise, so an A/B begins from a clean 0 (a kept
// bound shader state stays kept). Not sharePass, the shared-visibility levers, the occlusion history or the captures:
// those need more than a layout (research/sequel-render-levers.md).
#if defined(MEVR_RENDER_LEVERS_TRY) && !defined(MEVR_GAME_LE1)
#define MEVR_RENDER_LEVERS_TRY_HERE 1
inline constexpr bool kTryBuild = true;
#else
inline constexpr bool kTryBuild = false;   // LE1 has nothing to try: its levers are all on
#endif
constexpr void TryReadGroups(Layout& l) {
#if defined(MEVR_RENDER_LEVERS_TRY_HERE)
    l.sceneRendererVerified = l.umbraCommanderVerified = l.boundShaderStateVerified = l.umbraInstanceVerified =
        l.umbraQueriesVerified = true;
    // umbraShareResults' A/B (research/sequel-render-levers.md "Performance research"): its two rows and the context's
    // +0x1C8/+0x1CC were read in both games (lanes R2, R3). The group also admits umbraSharedVisibility, which stays at
    // its default 0 (never set it: research/35 attempt 46).
    l.umbraSharedVerified = true;
#else
    (void)l;
#endif
}

#if defined(MEVR_GAME_LE1)
inline constexpr Layout kLayout = [] {
    Layout l;
    l.sceneRendererVerified = l.umbraCommanderVerified = l.boundShaderStateVerified = l.umbraInstanceVerified =
        l.umbraQueriesVerified = l.umbraSharedVerified = l.occlusionHistoryVerified = l.sharePassVerified =
        l.sceneCaptureVerified = true;
    return l;
}();
inline constexpr char kGameName[] = "LE1";
#elif defined(MEVR_GAME_LE2)
// ME2 (lane R2, research/sequel-render-levers.md "ME2" > Layouts: every offset the levers use was seen in ME2's own code
// and is ME1's, except the FViewInfo stride; its visible-instance function has ME1's one call shape and nothing in ME2
// makes a scene capture). A group's `...Verified` flag is set here only after it ran in the game: the two passive groups
// (the accounting, the commander's timing) after a first protected run with their hooks in, a lever after its A/B at
// the baseline spot ("Measurements"). Until then the group stands down, whatever the rows say.
// sceneCaptureVerified also waits for someone to read where ME2's final pass gets its viewport (its
// BeginRenderingBackBuffer sets none: lane R2's one "U").
inline constexpr Layout kLayout = [] {
    Layout l;
    l.viewStride = 0x11A0;         // 11 functions add or multiply by it
    l.perEyeViewStateDefault = 1;  // Halcyon ME2's SFR: a state per pass (src/halcyon/le2/calcview_hook.cpp)
    // Ran in ME2 on 2026-11-16 (research/sequel-render-levers.md "ME2: the baseline and the A/Bs", session
    // 20261116-141434-005-LE2-pl-levers): the accounting and the commander's timing; keepBoundShaderStates (161 kept, the
    // reference count 0 -> 1 through the live RHI's slot 0x8A0, RVA 0x2B2640); the query-matrix hook, idle there.
    // minScreenRadiusPx (umbraInstanceVerified) stays off: no measurable gain, and no still-head pair for pop-in yet.
    l.sceneRendererVerified = l.umbraCommanderVerified = l.boundShaderStateVerified = l.umbraQueriesVerified = true;
    // The second eye (research/sequel-render-levers.md "The batch before the headset test", stretch
    // LE2-20261116-201015): the first eye's Umbra results stand in for the second eye's unready ones, and the renderer's
    // same-frame retry of pass 1 is off (a third scene render on 1-3% of moving frames, on 94-98% with the world paused).
    l.umbraSharedVerified = true; l.umbraShareResultsDefault = 1; l.sfrReplayRetryDefault = 0;
    TryReadGroups(l);
    return l;
}();
inline constexpr char kGameName[] = "LE2";
#elif defined(MEVR_GAME_LE3)
// ME3 (lane R3, research/sequel-render-levers.md "ME3" > Layouts: every offset the levers use was read in ME3's code and
// is ME1's, except the FViewInfo stride). A group's `...Verified` flag is set here only after it ran in the game: the
// two passive groups (the accounting, the commander's timing) after a first protected run with their hooks in, a lever
// after its A/B at the baseline spot ("Measurements"). Until then the group stands down, whatever the rows say.
inline constexpr Layout kLayout = [] {
    Layout l;
    l.viewStride = 0x11B0;         // read in 0x3E5890, 0x3E55C0, 0x3600A0
    l.perEyeViewStateDefault = 1;  // Halcyon ME3's SFR: a state per pass (src/halcyon/le3/calcview_hook.cpp)
    l.ineligibleShowFlags[1] = 0x47;
    l.ineligibleShowFlags[2] = 0x15;
    l.ineligibleShowFlags[3] = 0x1B;
    l.ineligibleShowFlagCount = 4;
    l.instanceCallAlsoKeepsBooks = true;
    l.captureTakesUmbraBranch = true;
    l.umbraCommandGate = RowIn(engine::addresses::kTable, row::kUmbraCommandGate);
    l.ownsSceneCaptureHooks = false;
    // Ran in ME3 on 2026-11-16 (research/sequel-render-levers.md "ME3: the baseline and the A/Bs", session
    // 20261116-143006-726-LE3-pl-levers): the accounting and the commander's timing (the command gate read 1);
    // keepBoundShaderStates (436 kept, the reference count 0 -> 1 through RVA 0x2CB940, as lane R3's recipe says); the
    // query-matrix hook, idle there. minScreenRadiusPx (umbraInstanceVerified) stays off: no measurable gain, and no
    // still-head pair for pop-in yet.
    l.sceneRendererVerified = l.umbraCommanderVerified = l.boundShaderStateVerified = l.umbraQueriesVerified = true;
    // The second eye (research/sequel-render-levers.md "The batch before the headset test", stretch
    // LE3-20261116-202316): the first eye's Umbra results stand in for the second eye's unready ones.
    l.umbraSharedVerified = true; l.umbraShareResultsDefault = 1;
    TryReadGroups(l);
    return l;
}();
inline constexpr char kGameName[] = "LE3";
#endif

#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)

// This game's row.
constexpr std::uint32_t Row(std::string_view name) {
    return RowIn(engine::addresses::kTable, name);
}

// ---- The module's constants, under the names RenderLevers.cpp has always used ------------------------------------------
inline constexpr std::uintptr_t kSceneRenderRva = Row(row::kSceneRender);
inline constexpr std::uintptr_t kCommanderCommandRva = Row(row::kCommanderCommand);
inline constexpr std::uintptr_t kUmbraResolveStartRva = Row(row::kUmbraResolveStart);
inline constexpr std::uintptr_t kGetQueryDataRva = Row(row::kGetQueryData);
inline constexpr std::uintptr_t kSetCommanderRva = Row(row::kSetCommander);
inline constexpr std::uintptr_t kDeleteRendererRva = Row(row::kDeleteRenderer);     // FSceneRenderer deleting destructor (this, flags)
inline constexpr std::uintptr_t kAllocateTargetsRva = Row(row::kAllocateTargets);   // GSceneRenderTargets.Allocate(this, x, y, 0)
inline constexpr std::uintptr_t kWireframeStateRva = Row(row::kSetCommander);       // (byte): 0 on the normal path
inline constexpr std::uintptr_t kDpgLightTargetsRva = Row(row::kDpgLightTargets);   // (renderer, dpg): per light and view, into scene targets
inline constexpr std::uintptr_t kPrepassViewsRva = Row(row::kPrepassViews);         // RenderUmbraBasePass(renderer): the opaque base pass per view
inline constexpr std::uintptr_t kTranslucencyCheckRva = Row(row::kTranslucencyCheck);
inline constexpr std::uintptr_t kBeforeLightsRva = Row(row::kBeforeLights);         // only when GSkipBeforeLights == 0
inline constexpr std::uintptr_t kDpgPassRva = Row(row::kDpgPass);                   // (renderer, dpg, uint* dirty): the lights
inline constexpr std::uintptr_t kDpgLightsRva = Row(row::kDpgLights);               // (renderer, dpg, flag, uint* dirty, arg5): translucency etc.
inline constexpr std::uintptr_t kForegroundRva = Row(row::kForeground);             // (renderer, 2, int* out, uint* dirty, arg5)
inline constexpr std::uintptr_t kFinishRva = Row(row::kFinish);                     // (renderer, flag, dirty)
inline constexpr std::uintptr_t kCaptureSceneRva = Row(row::kCaptureScene);         // (capture info, main renderer): per capture, in the loop at the top of the scene render (research/47)
inline constexpr std::uintptr_t kBeginBackBufferRva = Row(row::kBeginBackBuffer);   // GSceneRenderTargets.BeginRenderingBackBuffer: the uber post's final pass (research/47)
inline constexpr std::uintptr_t kSceneRenderTargetsRva = Row(row::kSceneRenderTargets);
inline constexpr std::uintptr_t kDynamicRhiRva = Row(row::kDynamicRhi);             // GDynamicRHI; LE1: vtable +0x628 begin, +0x620 end
inline constexpr std::uintptr_t kSceneFrameRva = Row(row::kSceneFrame);             // incremented per scene render
inline constexpr std::uintptr_t kDepthDirtyRva = Row(row::kDepthDirty);
inline constexpr std::uintptr_t kSkipBeforeLightsRva = Row(row::kSkipBeforeLights);
inline constexpr std::uintptr_t kCommandersRva = Row(row::kCommanders);             // BioWare's Umbra commanders [index]
inline constexpr std::uintptr_t kCommanderIndexRva = Row(row::kCommanderIndex);
inline constexpr std::uintptr_t kBoundShaderStateCtorRva = Row(row::kBoundShaderStateCtor);
inline constexpr std::uintptr_t kUmbraInstanceVisibleRva = Row(row::kUmbraInstanceVisible);
inline constexpr std::uintptr_t kUmbraReadResultRva = Row(row::kUmbraReadResult);
inline constexpr std::uintptr_t kUmbraIssueQueriesRva = Row(row::kUmbraIssueQueries);

inline constexpr std::size_t kViewStride = kLayout.viewStride;
inline constexpr std::uintptr_t kShowFlagBitsPerWord = Row(row::kShowFlagBitsPerWord);   // 0: no renderer is eligible
inline constexpr std::uintptr_t kStateOcclusionHistory = kLayout.stateOcclusionHistory;
inline constexpr std::size_t kOcclusionHistoryBytes = 0x48;   // a TSet: the sparse array, its allocation bits, the free list, the hash (UE3's own type)
inline constexpr std::uintptr_t kRendererViews = kLayout.rendererViews;    // FSceneRenderer::Views data (FViewInfo[]), count 8 further
inline constexpr std::uintptr_t kViewState = kLayout.viewState;            // FSceneView::State
inline constexpr std::size_t kRhiAddRefBoundShaderStateSlot = kLayout.rhiAddRefBoundShaderStateSlot;
inline constexpr std::size_t kBoundShaderStateHandle = kLayout.boundShaderStateHandle;
inline constexpr std::size_t kViewUmbraCamera = kLayout.viewUmbraCamera;
inline constexpr std::size_t kUmbraCameraImpl = kLayout.umbraCameraImpl;
inline constexpr std::size_t kUmbraCameraImplParams = kLayout.umbraCameraImplParams;
inline constexpr std::size_t kInstanceRecordRadius = kLayout.instanceRecordRadius;
inline constexpr std::size_t kCommanderViewField = kLayout.commanderView;
inline constexpr std::size_t kViewProjectionMatrix = kLayout.viewProjectionMatrix;
inline constexpr std::uintptr_t kCommanderView = kLayout.commanderView;            // UmbraCommander's view (FViewInfo*)
inline constexpr std::uintptr_t kCommanderViewParamsSet = kLayout.commanderViewParamsSet;  // 0 until SetViewParameters was called this render
inline constexpr std::uintptr_t kViewViewMatrix = kLayout.viewViewMatrix;
inline constexpr std::uintptr_t kViewTranslatedViewMatrix = kLayout.viewTranslatedViewMatrix;
inline constexpr std::uintptr_t kViewTranslatedViewProj = kLayout.viewTranslatedViewProj;
inline constexpr std::uintptr_t kViewPreViewTranslation = kLayout.viewPreViewTranslation;
inline constexpr std::uintptr_t kViewViewProj = kLayout.viewViewProj;

// FSceneView (the first 0x4E4 bytes of LE1's FViewInfo; copy constructor 0x763450): plain matrices and vectors in these
// ranges, owned containers at 0x44, 0x140 (the frustum's planes) and 0x330 (never copied). The word at +0x8C differs
// between a frame's two eye passes (the fake-headset run, 2026-10-23: the only difference the pairing test found), so it
// goes with the per-pass values.
inline constexpr ByteRange kViewPlainRanges[] = {{0x8C, 0x140}, {0x190, 0x330}, {0x480, 0x4B0}, {0x4C0, 0x4D0}};
// The same without the +0x8C word, every range starting on a 16-byte boundary of the view: the draw replay's
// substitution keys are the differing 16-byte chunks of these bytes, and a 4-byte offset at the start put every key
// astride two matrix rows (research/37, experiment 3: 0 substitutions).
inline constexpr ByteRange kViewCaptureRanges[] = {{0x90, 0x140}, {0x190, 0x330}, {0x480, 0x4B0}, {0x4C0, 0x4D0}};
inline constexpr std::size_t kViewCompareEnd = 0x8C;  // the view block compared for compatibility: +0x60 to here

#if defined(MEVR_GAME_LE1)
// LE1's values, as RenderLevers.cpp carried them: a row renamed, removed or edited in LE1's table fails the build here.
static_assert(kSceneRenderRva == 0x76EE20 && kCommanderCommandRva == 0x8C34C0 && kUmbraResolveStartRva == 0x1795D40 &&
    kGetQueryDataRva == 0x9A04B0 && kSetCommanderRva == 0x8C1AE0 && kDeleteRendererRva == 0x4ED700 &&
    kAllocateTargetsRva == 0x772EB0 && kWireframeStateRva == 0x8C1AE0 && kDpgLightTargetsRva == 0x77BE00 &&
    kPrepassViewsRva == 0x8BF8F0 && kTranslucencyCheckRva == 0x7666A0 && kBeforeLightsRva == 0x759240 &&
    kDpgPassRva == 0x76D650 && kDpgLightsRva == 0x76D0A0 && kForegroundRva == 0x76CA70 && kFinishRva == 0x76DD10 &&
    kCaptureSceneRva == 0x742C80 && kBeginBackBufferRva == 0x773D30 && kSceneRenderTargetsRva == 0x167D0E0 &&
    kDynamicRhiRva == 0x17938C8 && kSceneFrameRva == 0x178779C && kDepthDirtyRva == 0x1795D38 &&
    kSkipBeforeLightsRva == 0x167D49C && kCommandersRva == 0x1798E88 && kCommanderIndexRva == 0x1795D28 &&
    kBoundShaderStateCtorRva == 0x9A89F0 && kUmbraInstanceVisibleRva == 0x8BE0B0 && kUmbraReadResultRva == 0xD48380 &&
    kUmbraIssueQueriesRva == 0x8B6550,
    "LE1's render-lever rows in engine/Addresses.hpp must keep their names and RVAs");
static_assert(kViewStride == 0x12F0 && kStateOcclusionHistory == 0xD10 && kRendererViews == 0x60 && kViewState == 0x8 &&
    kRhiAddRefBoundShaderStateSlot == 0x8A0 && kBoundShaderStateHandle == 0xC && kViewUmbraCamera == 0x4D4 &&
    kUmbraCameraImpl == 0x10 && kUmbraCameraImplParams == 0x420 && kInstanceRecordRadius == 0x38 &&
    kCommanderViewField == 0x68 && kViewProjectionMatrix == 0xD0 && kCommanderView == 0x68 &&
    kCommanderViewParamsSet == 0x5C && kViewViewMatrix == 0x90 && kViewTranslatedViewMatrix == 0x190 &&
    kViewTranslatedViewProj == 0x1D0 && kViewPreViewTranslation == 0x250 && kViewViewProj == 0x260 &&
    kShowFlagBitsPerWord == 0x164C720 && kLayout.ineligibleShowFlagCount == 1 && kLayout.ineligibleShowFlags[0] == 0x33 &&
    !kLayout.instanceCallAlsoKeepsBooks && !kLayout.captureTakesUmbraBranch && kLayout.umbraCommandGate == 0 &&
    kLayout.ownsSceneCaptureHooks && kLayout.sceneCaptureVerified,
    "LE1's layouts are the measured ones");
static_assert(kLayout.perEyeViewStateDefault == 0 && kLayout.umbraQueryWaitDefault == 1 && kLayout.ue3QueryWaitDefault == 1 &&
    kLayout.umbraShareResultsDefault == 0,
    "LE1's lever defaults are the ones its levers have always had");
#endif

#endif // a game

} // namespace mevr::host::render_levers::table
