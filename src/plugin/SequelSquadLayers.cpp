// SPDX-License-Identifier: MIT
#include "plugin/SequelSquadLayers.hpp"
#include "plugin/SquadPanel.hpp"
#include "plugin/SquadPlaceDraw.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/SquadPlace.hpp"
namespace mevr::host::sequel_squad_layers {
unsigned Place(MevrSeams::OverlayFrame const& f, ID3D11Device* device,
    void const** out, unsigned count, unsigned capacity) noexcept {
    if (!out || count >= capacity || !f.worldFrame || f.menuOpen || !f.headValid || !f.projectionValid) return count;
    squad_place_draw::Frame place;
    place.session = static_cast<XrSession>(f.session); place.appSpace = static_cast<XrSpace>(f.appSpace);
    place.context = f.context; place.device = device; place.headValid = true;
    place.head = {{f.headOrientation[0], f.headOrientation[1], f.headOrientation[2], f.headOrientation[3]},
        {f.headPosition[0], f.headPosition[1], f.headPosition[2]}};
    place.eyeWidth = f.projectionRect[0][2]; place.eyeHeight = f.projectionRect[0][3];
    for (int e = 0; e < 2; ++e) {
        auto const& q = f.projectionOrientation[e]; auto const& p = f.projectionPosition[e]; auto const& v = f.projectionFov[e];
        place.eyes[e] = {{q[0], q[1], q[2], q[3]}, {p[0], p[1], p[2]}};
        place.fov[e] = {v[0], v[1], v[2], v[3]};
    }
    if (auto const* layer = squad_place_draw::Layer(place)) out[count++] = layer;
    return count;
}
unsigned Panel(MevrSeams::OverlayFrame const& f, ID3D11Device* device,
    void const** out, unsigned count, unsigned capacity) noexcept {
    if (!out || count >= capacity) return count;
    squad_panel::Frame squad;
    squad.session = static_cast<XrSession>(f.session); squad.appSpace = static_cast<XrSpace>(f.appSpace);
    squad.context = f.context; squad.device = device;
    squad.worldFrame = f.worldFrame; squad.menuOpen = f.menuOpen; squad.headValid = f.headValid;
    squad.head = {{f.headOrientation[0], f.headOrientation[1], f.headOrientation[2], f.headOrientation[3]},
        {f.headPosition[0], f.headPosition[1], f.headPosition[2]}};
    if (auto const* layer = squad_panel::Layer(squad)) out[count++] = layer;
    return count;
}
void Recentered() {
    squad_panel::Recentered();
    vr::squad_place::Publish({}); // coordinates from the old app space cannot survive recenter
}
void ForgetSession() {
    squad_panel::ForgetSession();
    squad_place_draw::ForgetSession();
    vr::squad_place::Publish({});
    vr::squad_menu::Publish({});
}
}
