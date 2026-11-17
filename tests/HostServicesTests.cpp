// SPDX-License-Identifier: MIT
#include "mod/ModApi.hpp"
#include <cstdio>

int RunHostServicesTests() {
    int failures = 0;
    auto check = [&](bool ok) { if (!ok) ++failures; };
    mevr::HostServices host;
    host.version = 42;
    host.servicesSize = sizeof host;
    check(mevr::HostBlocksThespian(nullptr));
    check(mevr::HostBlocksThespian(&host));
    host.thespianInputBlocked = [] { return false; };
    check(!mevr::HostBlocksThespian(&host));
    host.version = 41;
    check(mevr::HostBlocksThespian(&host));
    host.version = 42;
    host.servicesSize = static_cast<std::uint32_t>(offsetof(mevr::HostServices, thespianInputBlocked));
    check(mevr::HostBlocksThespian(&host));
    host.servicesSize = sizeof host;
    host.thespianInputBlocked = [] { return true; };
    check(mevr::HostBlocksThespian(&host));
    // v44: the menu's offers (research/28 "Leave"): only from a v44 host whose struct reaches the field.
    mevr::HostServices offers;
    offers.servicesSize = sizeof offers;
    check(mevr::GameMenuOffersPublisher(nullptr) == nullptr);
    check(mevr::GameMenuOffersPublisher(&offers) == nullptr);  // no publisher set
    offers.setGameMenuOffers = [](std::uint32_t) {};
    check(mevr::GameMenuOffersPublisher(&offers) == offers.setGameMenuOffers);
    offers.version = 43;
    check(mevr::GameMenuOffersPublisher(&offers) == nullptr);
    offers.version = 44;
    offers.servicesSize = static_cast<std::uint32_t>(offsetof(mevr::HostServices, setGameMenuOffers));
    check(mevr::GameMenuOffersPublisher(&offers) == nullptr);
    if (failures) std::printf("HostServices admission: %d failures\n", failures);
    return failures;
}
