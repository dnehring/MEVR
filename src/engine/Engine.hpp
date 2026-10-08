#pragma once

namespace LESDK {
class Initializer;
}

namespace mevr::engine {

// Resolves the SDK globals and hooks UGameEngine::Tick. Returns false if anything fails to
// resolve; in that case no hook is installed and the game runs untouched.
bool Initialize(::LESDK::Initializer& init);

} // namespace mevr::engine
