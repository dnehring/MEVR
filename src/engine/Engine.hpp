#pragma once

namespace LESDK {
class Initializer;
}

namespace mevr::engine {

// Resolves the SDK globals this module uses (GMalloc, GObjObjects, SFXName pools, GEngine,
// GWorld). Each DLL that links LExSDKv2 has its own copies, so the host and the hot-reloadable
// mod both call this. Returns false if anything fails to resolve.
bool ResolveGlobals(::LESDK::Initializer& init);

} // namespace mevr::engine
