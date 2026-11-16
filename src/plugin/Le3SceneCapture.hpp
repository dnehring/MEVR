// SPDX-License-Identifier: MIT
#pragma once
namespace mevr::hook { class Context; }
namespace mevr::host::le3_scene_capture {
// Host lifetime, before rendering starts; hooks stay installed until process exit.
void Attach(hook::Context& hooks);
}
