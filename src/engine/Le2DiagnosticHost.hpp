// SPDX-License-Identifier: MIT
#pragma once

namespace mevr::engine::le2_diagnostic {

using TickCallback = void (*)(float);

// Register once after Legacy::Init and before the first host Tick. No callback is installed by
// default. Replacement (including clearing) after registration or first Tick is rejected.
// The callback runs synchronously after the engine's original tick; it must not retain engine
// pointers for workers/rendering. This seam does not validate objects, parameters or requests.
bool SetTickCallback(TickCallback callback);
void Tick(float deltaSeconds);
// Host must stop ticking before shutdown/unload. Clears the callback and thread binding.
void Shutdown();

} // namespace mevr::engine::le2_diagnostic
