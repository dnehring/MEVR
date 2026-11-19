// SPDX-License-Identifier: MIT
#include "engine/ClipPose.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/Rigid.hpp"
#include "engine/Skeleton.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <unordered_map>
#include <vector>

namespace mevr::engine::clip_pose {
namespace {

constexpr int kMaxBones = 512;

std::atomic<ue3::Object const*> gWanted{nullptr};
ue3::Object* gBody = nullptr;
std::shared_ptr<ik::clip::Clip const> gClip;
double gRate = 1.0;
double gSeconds = 0.0;
double gTimeScale = 1.0;
LARGE_INTEGER gLastCall{};
std::atomic<bool> gBroken{false};
Stats gStats;

struct Binding {
    int boneCount = 0;
    std::vector<int> parents;
    std::vector<int> mapped;   // per clip bone: the mesh's index
    int root = -1;
};
std::unordered_map<ue3::Object const*, Binding> gBindings;  // per body component

bool ReadGuarded(ue3::Object* mesh, skeleton::BoneAtom* out, int capacity, int* count) {
    __try {
        std::span<skeleton::BoneAtom> const bases = skeleton::SpaceBases(mesh);
        int const n = static_cast<int>(bases.size());
        if (n <= 0 || n > capacity) {
            return false;
        }
        for (int i = 0; i < n; ++i) {
            out[i] = bases[static_cast<std::size_t>(i)];
        }
        *count = n;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gBroken.store(true);
        return false;
    }
}

bool WriteGuarded(ue3::Object* mesh, skeleton::BoneAtom const* atoms, int n) {
    __try {
        std::span<skeleton::BoneAtom> const bases = skeleton::SpaceBases(mesh);
        if (static_cast<int>(bases.size()) != n) {
            return false;
        }
        for (int i = 0; i < n; ++i) {
            bases[static_cast<std::size_t>(i)] = atoms[i];
        }
        write_journal::Wrote("ClipPose.bones", bases.data(), static_cast<std::size_t>(n) * sizeof(skeleton::BoneAtom));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gBroken.store(true);
        return false;
    }
}

Binding const* Bind(ue3::Object* body) {
    int const count = skeleton::RefBoneCount(body);
    auto it = gBindings.find(body);
    if (it != gBindings.end() && it->second.boneCount == count) {
        return &it->second;  // one body played at a time, the cache cleared by every Play
    }
    auto const skel = skeleton::ReadRefSkeleton(body);
    if (!skel || count <= 0 || static_cast<int>(skel->names.size()) != count || count > kMaxBones) {
        return nullptr;
    }
    Binding b;
    b.boneCount = count;
    b.parents = skel->parents;
    std::unordered_map<std::string, int> index;
    for (int i = 0; i < count; ++i) {
        index.emplace(skel->names[static_cast<std::size_t>(i)], i);
    }
    for (std::string const& name : gClip->bones) {
        auto const found = index.find(name);
        b.mapped.push_back(found != index.end() ? found->second : -1);
    }
    auto const root = index.find("Root");
    b.root = root != index.end() ? root->second : -1;
    int const found = static_cast<int>(std::count_if(b.mapped.begin(), b.mapped.end(), [](int i) { return i >= 0; }));
    log::Info("clip pose: {} of the clip's {} bones found on {:#x}'s skeleton ({} bones){}", found, b.mapped.size(),
        reinterpret_cast<std::uintptr_t>(body), count, b.root < 0 ? "; no Root bone" : "");
    return &gBindings.insert_or_assign(body, std::move(b)).first->second;
}

} // namespace

void Play(ue3::Object* body, std::shared_ptr<ik::clip::Clip const> clip, double rate) {
    gBody = body;
    gClip = std::move(clip);
    gRate = rate;
    gSeconds = 0.0;
    gLastCall = {};
    gBindings.clear();
    gStats = {};
    gStats.body = body;
    gStats.clip = gClip != nullptr ? gClip->name : std::string();
    gWanted.store(body != nullptr && gClip != nullptr && !gBroken.load() ? body : nullptr);
}

void SetTimeScale(double scale) {
    gTimeScale = std::clamp(scale, 0.0, 4.0);
}

void Seek(double seconds, double rate) {
    gSeconds = seconds;
    gRate = rate;
    gStats.seconds = seconds;
}

void Stop() {
    gWanted.store(nullptr);
    gBody = nullptr;
    gClip.reset();
    gBindings.clear();
}

bool Wants(ue3::Object const* component) {
    return component != nullptr && gWanted.load(std::memory_order_relaxed) == component;
}

void AfterPose(ue3::Object* component, float deltaTime) {
    if (component != gBody || gClip == nullptr || gBroken.load()) {
        return;
    }
    ++gStats.calls;
    // The step: the engine's, else the clock's (a component the engine updates without a delta).
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double dt = deltaTime;
    if (!(dt > 0.0f) && gLastCall.QuadPart != 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        dt = static_cast<double>(now.QuadPart - gLastCall.QuadPart) / static_cast<double>(freq.QuadPart);
    }
    gLastCall = now;
    gSeconds += std::clamp(dt, 0.0, 0.1) * gRate * gTimeScale;
    gStats.seconds = gSeconds;

    Binding const* const binding = Bind(component);
    if (binding == nullptr || binding->root < 0) {
        gStats.lastReject = "the body's skeleton can't be read, or has no Root";
        return;
    }
    gStats.mappedFound = static_cast<int>(std::count_if(binding->mapped.begin(), binding->mapped.end(), [](int i) { return i >= 0; }));
    if (gStats.mappedFound != static_cast<int>(binding->mapped.size())) {
        gStats.lastReject = "the body lacks some of the clip's bones";
        return;
    }
    thread_local std::array<skeleton::BoneAtom, kMaxBones> atoms;
    int count = 0;
    if (!ReadGuarded(component, atoms.data(), kMaxBones, &count) || count != binding->boneCount) {
        gStats.lastReject = "the pose can't be read (not composed yet, or another bone count)";
        return;
    }
    thread_local std::vector<ik::Transform> world;
    thread_local std::vector<ik::Transform> local;
    world.resize(static_cast<std::size_t>(count));
    local.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        world[static_cast<std::size_t>(i)] = skeleton::ToTransform(atoms[static_cast<std::size_t>(i)]);
    }
    for (int i = 0; i < count; ++i) {
        int const p = binding->parents[static_cast<std::size_t>(i)];
        local[static_cast<std::size_t>(i)] = p >= 0 ? ik::Relative(world[static_cast<std::size_t>(i)], world[static_cast<std::size_t>(p)])
                                                    : world[static_cast<std::size_t>(i)];
    }
    ik::clip::Sample const sample = ik::clip::At(*gClip, gSeconds);
    for (std::size_t k = 0; k < binding->mapped.size(); ++k) {
        local[static_cast<std::size_t>(binding->mapped[k])].rotation = sample.locals[k];
    }
    // The root: the game's place plus the clip's sway, at the clip's height (the bob), in component space.
    {
        std::size_t const r = static_cast<std::size_t>(binding->root);
        gStats.rootBefore = world[r].translation;
        gStats.pelvisBefore = world[static_cast<std::size_t>(binding->mapped.front())].translation;
        ik::Transform rootWorld = world[r];
        rootWorld.translation = {world[r].translation.x + sample.root.x, world[r].translation.y + sample.root.y, sample.root.z};
        int const p = binding->parents[r];
        local[r] = p >= 0 ? ik::Relative(rootWorld, world[static_cast<std::size_t>(p)]) : rootWorld;
        gStats.rootAfter = rootWorld.translation;
    }
    for (int i = 0; i < count; ++i) {
        int const p = binding->parents[static_cast<std::size_t>(i)];
        world[static_cast<std::size_t>(i)] = p >= 0 ? ik::Compose(local[static_cast<std::size_t>(i)], world[static_cast<std::size_t>(p)])
                                                    : local[static_cast<std::size_t>(i)];
        atoms[static_cast<std::size_t>(i)] = skeleton::ToAtom(world[static_cast<std::size_t>(i)]);
    }
    if (WriteGuarded(component, atoms.data(), count)) {
        ++gStats.written;
        gStats.lastReject = nullptr;
    } else {
        gStats.lastReject = "the pose can't be written";
    }
}

Stats GetStats() {
    Stats s = gStats;
    s.broken = gBroken.load();
    return s;
}

} // namespace mevr::engine::clip_pose
