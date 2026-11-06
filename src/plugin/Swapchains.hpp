// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ID3D11Texture2D;

// Every swapchain our quads make (LE1, Halcyon's session), through one place, because the runtime limits them: SteamVR
// answers XR_ERROR_LIMIT_REACHED past 16 live in a session, whatever their size (tools/swapchain-probe, SteamVR 2.18.4,
// 2026-11-06; a freed one is given back at once). Halcyon holds 4 (two eyes, its menu, its black scene): 12 are ours.
// Each owner made its own lazily, never freed it, and gave up for the session on
// a refusal: after the first refusal (the calibration's board) a dozen features failed one after another (the
// omni-tool's rail, so the wrist tap "did nothing"; the sights; the galaxy map's eyes; the squad arc...). Here:
//   - a refused create asks for room: at the end of that frame (the test loop's xrEndFrame, after the runtime's, when no
//     frame holds any image) the least recently shown of ours that no frame has submitted for a second are freed
//     through their owners' `evict`, and the refused owner tries again on the next frame;
//   - a refused owner tries again every 2 s on its own (RetryDue; Create itself won't ask the runtime sooner unless
//     something was freed), never gives up for the session;
//   - an Essential create (the calibration, where a refusal loses what the player is measuring) may also free ours shown
//     0.1 s ago or more (they make theirs again when there's room);
//   - the log names what's live at a refusal, and snapshots carry it ("swapchains").
// Present thread, except StatusJson (any thread).
namespace mevr::host::swapchains {

enum class Need { Normal, Essential };

// xrCreateSwapchain and its D3D11 images. On success `handle` and `images` are filled. `evict` lets go of it through
// the owner's own path (its Destroy, or forgetting the handle, images and views: then it's destroyed here), leaving the
// owner able to make it again when it's next wanted; it runs at a frame's end on the Present thread. A refusal is logged
// here once per name until it's made; the owner need only say what goes missing.
XrResult Create(XrSession session, XrSwapchainCreateInfo const& info, char const* name, std::function<void()> evict, XrSwapchain& handle,
    std::vector<ID3D11Texture2D*>& images, Need need = Need::Normal);

// Destroys it (the test loop forgets it too). A handle made elsewhere is destroyed all the same.
void Destroy(XrSwapchain handle);
// Its session is gone (and its swapchains with it): forget it, destroy nothing.
void Forget(XrSwapchain handle);

// A create refused at `failedMs` (GetTickCount64) may be tried again: 2 s later, or once a frame's end freed room.
bool RetryDue(std::uint64_t failedMs);

// A new session: everything of the old one is gone.
void OnSessionCreated(XrSession session);
// The test loop's xrEndFrame, after the runtime's: notes each layer's swapchains as shown and frees what refused creates
// asked for.
void AfterEndFrame(XrFrameEndInfo const& info);

// {"ours": n, "halcyons": n, "refusals": n, "freed": n, "live": [{"name", "w", "h", "idleMs"}...]}.
std::string StatusJson();

} // namespace mevr::host::swapchains
