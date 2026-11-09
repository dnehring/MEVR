// SPDX-License-Identifier: MIT
#include "core/vr/CameraDiagnostic.hpp"
#include "mod/ModApi.hpp"

#include <atomic>
#include <cstdio>
#include <limits>
#include <thread>

namespace {
int gFailures = 0;
void Check(bool ok, char const* expression, int line) {
    if (!ok) {
        std::printf("FAIL CameraDiagnosticTests.cpp:%d: %s\n", line, expression);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)
void Sink(double, double, double) {}
}

int RunCameraDiagnosticTests() {
    using mevr::vr::camera_diagnostic::Store;
    Store store;
    CHECK(!store.Latest(100).valid);
    store.Publish(10, -20, 30, 100);
    auto sample = store.Latest(105);
    CHECK(sample.valid && sample.forward == 10 && sample.right == -20 && sample.up == 30 && sample.sequence == 1);
    CHECK(store.Latest(15100).valid);
    CHECK(!store.Latest(15101).valid);
    CHECK(!store.Latest(99).valid);
    CHECK(!store.Latest(105, 100).valid);  // same timestamp is not a post-recenter observation
    store.Publish(10, -20, 30, 5100);  // unchanged observations must still refresh freshness
    CHECK(store.Latest(6100, 100).valid);
    CHECK(store.Latest(6100).sequence == 2);
    store.Reset();
    CHECK(!store.Latest(6100).valid);  // unload cannot leave another mod's sample behind
    store.Publish(std::numeric_limits<double>::infinity(), 0, 0, 6200);
    CHECK(!store.Latest(6200).valid);
    store.Publish(0, std::numeric_limits<double>::quiet_NaN(), 0, 6200);
    CHECK(!store.Latest(6200).valid);

    CHECK(mevr::CameraOffsetPublisher(nullptr) == nullptr);
    mevr::HostServices host;
    host.publishCameraOffset = &Sink;
    host.version = 37;
    CHECK(mevr::CameraOffsetPublisher(&host) == nullptr);
    host.version = 38;
    host.servicesSize = static_cast<std::uint32_t>(offsetof(mevr::HostServices, publishCameraOffset));
    CHECK(mevr::CameraOffsetPublisher(&host) == nullptr);
    host.servicesSize = sizeof(host);
    CHECK(mevr::CameraOffsetPublisher(&host) == &Sink);
    host.publishCameraOffset = nullptr;
    CHECK(mevr::CameraOffsetPublisher(&host) == nullptr);

    // The host's Present/menu reader must never see coordinates from different publications.
    store.Reset();
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int n = 1; n <= 10000; ++n) store.Publish(n, -n, n * 2, 100);
        done.store(true);
    });
    do {
        auto const s = store.Latest(100);
        CHECK(!s.valid || (s.right == -s.forward && s.up == s.forward * 2));
    } while (!done.load());
    writer.join();
    CHECK(store.Latest(100).forward == 10000);
    std::printf("Camera diagnostic freshness, ABI compatibility and publication checks: %s\n", gFailures ? "FAILED" : "passed");
    return gFailures;
}
