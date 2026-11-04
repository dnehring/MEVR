// SPDX-License-Identifier: MIT
// The galaxy map as a hologram we draw ourselves (core/vr/GalaxyHologram.hpp).

#include "core/vr/GalaxyHologram.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace mevr::vr::galaxy_holo {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;

XrVector3f FromGame(float const v[3]) { return {v[0], v[1], v[2]}; }

std::string LabelOf(char const (&label)[kLabelBytes]) {
    return std::string(label, strnlen(label, kLabelBytes));
}

// The kind's drawn radius at size 1, metres.
float KindRadius(Kind kind) {
    switch (kind) {
    case Kind::Cluster: return 0.010f;
    case Kind::System: return 0.009f;
    case Kind::Planet: return 0.016f;
    case Kind::Relay: return 0.014f;
    case Kind::Star: return 0.030f;
    default: return 0.008f;
    }
}

// The map's axes in game space: right, up, forward (unit, orthogonal), from the scene's (possibly sloppy) up and forward.
struct Basis {
    XrVector3f right{0.0f, 1.0f, 0.0f};
    XrVector3f up{0.0f, 0.0f, 1.0f};
    XrVector3f forward{1.0f, 0.0f, 0.0f};
};
Basis BasisOf(Scene const& scene) {
    Basis b;
    XrVector3f up = FromGame(scene.up);
    if (Length(up) < 1e-4f) {
        up = {0.0f, 0.0f, 1.0f};
    }
    up = Normalize(up);
    XrVector3f forward = FromGame(scene.forward);
    forward = Sub(forward, Mul(up, Dot(forward, up)));  // into the plane
    if (Length(forward) < 1e-4f) {
        // Any direction in the plane: the game's X, else its Y.
        XrVector3f const x{1.0f, 0.0f, 0.0f};
        forward = Sub(x, Mul(up, Dot(x, up)));
        if (Length(forward) < 1e-4f) {
            XrVector3f const y{0.0f, 1.0f, 0.0f};
            forward = Sub(y, Mul(up, Dot(y, up)));
        }
    }
    b.up = up;
    b.forward = Normalize(forward);
    b.right = Cross(b.up, b.forward);  // UE3: Z x X = Y (right)
    return b;
}

Colour Scaled(Colour c, float s) {
    c.a *= s;
    return c;
}
} // namespace

XrVector3f Add(XrVector3f const& a, XrVector3f const& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XrVector3f Sub(XrVector3f const& a, XrVector3f const& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XrVector3f Mul(XrVector3f const& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(XrVector3f const& a, XrVector3f const& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
XrVector3f Cross(XrVector3f const& a, XrVector3f const& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(XrVector3f const& a) { return std::sqrt(Dot(a, a)); }
XrVector3f Normalize(XrVector3f const& a) {
    float const l = Length(a);
    return l > 1e-9f ? Mul(a, 1.0f / l) : XrVector3f{0.0f, 0.0f, 0.0f};
}

Table PlaceTable(XrPosef const& head, TableSettings const& settings) {
    Table t;
    float const yaw = overlay::HeadingOf(head.orientation);
    t.pose.orientation = overlay::YawRotation(yaw);
    t.pose.position = Add(head.position, overlay::Rotate(t.pose.orientation, {0.0f, -settings.belowM, -settings.distanceM}));
    t.radiusM = settings.radiusM;
    return t;
}

float FitScale(Scene const& scene, float radiusM) {
    float extent = scene.extent;
    if (!(extent > 0.0f)) {
        Basis const b = BasisOf(scene);
        XrVector3f const c = FromGame(scene.centre);
        for (int i = 0; i < std::min<int>(scene.objectCount, kMaxObjects); ++i) {
            XrVector3f const d = Sub(FromGame(scene.objects[i].position), c);
            float const across = std::hypot(Dot(d, b.right), Dot(d, b.forward));
            extent = std::max(extent, across);
        }
        extent *= 1.15f;  // a margin: the farthest object inside the rim, with room for its label
    }
    if (!(extent > 1e-3f)) {
        extent = 1.0f;
    }
    return radiusM / extent;
}

XrVector3f ToRoom(Scene const& scene, Table const& table, float scale, float const gamePosition[3], float heightScale) {
    Basis const b = BasisOf(scene);
    XrVector3f const d = Sub(FromGame(gamePosition), FromGame(scene.centre));
    XrVector3f const local{Dot(d, b.right) * scale, Dot(d, b.up) * scale * heightScale, -Dot(d, b.forward) * scale};
    return Add(table.pose.position, overlay::Rotate(table.pose.orientation, local));
}

std::vector<Placed> PlaceObjects(Scene const& scene, Table const& table, TableSettings const& settings) {
    std::vector<Placed> out;
    float const scale = FitScale(scene, table.radiusM);
    int const n = std::min<int>(scene.objectCount, kMaxObjects);
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        Object const& o = scene.objects[i];
        Placed p;
        p.index = i;
        p.position = ToRoom(scene, table, scale, o.position, settings.heightScale);
        float const size = o.size > 0.0f ? std::clamp(o.size, 0.4f, 3.0f) : 1.0f;
        p.radiusM = KindRadius(static_cast<Kind>(o.kind)) * size * (table.radiusM / 0.40f);
        out.push_back(p);
    }
    return out;
}

int Pick(XrVector3f const& origin, XrVector3f const& direction, std::span<Placed const> placed, Scene const& scene,
         float minToleranceDeg) {
    XrVector3f const dir = Normalize(direction);
    if (Length(dir) < 0.5f) {
        return -1;
    }
    int best = -1;
    float bestRatio = 1.0f;
    for (Placed const& p : placed) {
        if (p.index < 0 || p.index >= scene.objectCount || (scene.objects[p.index].flags & kSelectable) == 0) {
            continue;
        }
        XrVector3f const v = Sub(p.position, origin);
        float const along = Dot(v, dir);
        if (along <= 0.01f) {
            continue;
        }
        float const off = Length(Sub(v, Mul(dir, along)));
        float const angle = std::atan2(off, along);
        float const tolerance = std::max(minToleranceDeg * kDegToRad, std::atan2(p.radiusM * 1.5f, along));
        float const ratio = angle / tolerance;
        if (ratio < bestRatio) {
            bestRatio = ratio;
            best = p.index;
        }
    }
    return best;
}

std::optional<int> Pointer::Step(int candidate, float trigger, bool valid) {
    if (!valid) {
        hovered_ = -1;
        down_ = false;
        return std::nullopt;
    }
    hovered_ = candidate;
    bool const wasDown = down_;
    down_ = down_ ? trigger > 0.4f : trigger > 0.6f;
    if (down_ && !wasDown && hovered_ >= 0) {
        return hovered_;
    }
    return std::nullopt;
}

void Pointer::Reset() {
    hovered_ = -1;
    down_ = false;
}

std::optional<std::array<float, 2>> ToGameScreen(GameCamera const& camera, float const gamePosition[3]) {
    if (!camera.valid || !(camera.fovXDeg > 1.0f) || !(camera.aspect > 0.1f)) {
        return std::nullopt;
    }
    float const sp = std::sin(camera.pitch), cp = std::cos(camera.pitch);
    float const sy = std::sin(camera.yaw), cy = std::cos(camera.yaw);
    float const sr = std::sin(camera.roll), cr = std::cos(camera.roll);
    // UE3's FRotationMatrix: X forward, Y right, Z up.
    XrVector3f const forward{cp * cy, cp * sy, sp};
    XrVector3f const right{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    XrVector3f const up{-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
    XrVector3f const d = Sub(FromGame(gamePosition), FromGame(camera.location));
    float const z = Dot(d, forward);
    if (z <= 1e-3f) {
        return std::nullopt;
    }
    float const tanX = std::tan(camera.fovXDeg * 0.5f * kDegToRad);
    float const tanY = tanX / camera.aspect;
    float const u = 0.5f + 0.5f * Dot(d, right) / (z * tanX);
    float const v = 0.5f - 0.5f * Dot(d, up) / (z * tanY);
    return std::array<float, 2>{u, v};
}

DrawList Build(Scene const& scene, Table const& table, TableSettings const& settings, std::span<Placed const> placed, int hovered,
               float timeS) {
    DrawList list;
    XrQuaternionf const q = table.pose.orientation;
    XrVector3f const tableUp = overlay::Rotate(q, {0.0f, 1.0f, 0.0f});
    XrVector3f const tableAway = overlay::Rotate(q, {0.0f, 0.0f, -1.0f});
    XrVector3f const centre = table.pose.position;
    float const r = table.radiusM;
    Colour const holo{0.35f, 0.75f, 1.0f, 1.0f};
    Colour const warm{1.0f, 0.62f, 0.25f, 1.0f};
    Colour const green{0.35f, 1.0f, 0.55f, 1.0f};
    float const pulse = 0.5f + 0.5f * std::sin(timeS * 3.0f);

    // The projector's base: a faint grid under the map and two rims.
    XrVector3f const base = Sub(centre, Mul(tableUp, 0.004f));
    list.discs.push_back({base, tableUp, tableAway, r * 1.10f, Scaled(holo, 0.16f), DiscStyle::Grid});
    list.rings.push_back({base, tableUp, r * 1.10f, 0.0025f, Scaled(holo, 0.55f)});
    list.rings.push_back({base, tableUp, r * 1.14f, 0.0012f, Scaled(holo, 0.30f)});

    Level const level = static_cast<Level>(scene.level);
    if (level == Level::Galaxy) {
        list.discs.push_back({centre, tableUp, tableAway, r * 1.02f, Colour{0.85f, 0.80f, 1.0f, 0.85f}, DiscStyle::Galaxy});
    }

    // Relay routes.
    auto const placedAt = [&](int index) -> Placed const* {
        for (Placed const& p : placed) {
            if (p.index == index) {
                return &p;
            }
        }
        return nullptr;
    };
    for (int i = 0; i < std::min<int>(scene.linkCount, kMaxLinks); ++i) {
        Placed const* a = placedAt(scene.links[i].a);
        Placed const* b = placedAt(scene.links[i].b);
        if (a != nullptr && b != nullptr) {
            list.lines.push_back({a->position, b->position, 0.0016f, Scaled(holo, 0.55f)});
        }
    }

    // A system's orbits: a ring through each planet, round the star (or the table's centre).
    if (level == Level::System) {
        XrVector3f sun = centre;
        for (Placed const& p : placed) {
            if (static_cast<Kind>(scene.objects[p.index].kind) == Kind::Star) {
                sun = p.position;
                break;
            }
        }
        for (Placed const& p : placed) {
            if (static_cast<Kind>(scene.objects[p.index].kind) != Kind::Planet) {
                continue;
            }
            XrVector3f const d = Sub(p.position, sun);
            float const across = Length(Sub(d, Mul(tableUp, Dot(d, tableUp))));
            if (across > 0.01f) {
                list.rings.push_back({sun, tableUp, across, 0.0016f, Scaled(holo, 0.75f)});
            }
        }
    }

    // The objects: a stalk down to the plane, the body, markers, the label.
    XrVector3f sunForLight = centre;
    for (Placed const& p : placed) {
        if (static_cast<Kind>(scene.objects[p.index].kind) == Kind::Star) {
            sunForLight = p.position;
        }
    }
    for (Placed const& p : placed) {
        Object const& o = scene.objects[p.index];
        Kind const kind = static_cast<Kind>(o.kind);
        bool const isHovered = p.index == hovered;
        float const height = Dot(Sub(p.position, centre), tableUp);
        if (std::abs(height) > 0.004f) {
            list.lines.push_back({p.position, Sub(p.position, Mul(tableUp, height)), 0.0008f, Scaled(holo, 0.30f)});
        }
        Sprite s;
        s.centre = p.position;
        s.radiusM = p.radiusM;
        switch (kind) {
        case Kind::Star:
            s.style = SpriteStyle::Star;
            s.colour = Colour{1.0f, 0.85f, 0.55f, 1.0f};
            break;
        case Kind::Planet:
            s.style = SpriteStyle::Planet;
            s.colour = Colour{0.55f, 0.70f, 0.85f, 1.0f};
            s.light = Normalize(Sub(sunForLight, p.position));
            if (Length(s.light) < 0.5f) {
                s.light = tableUp;
            }
            break;
        case Kind::Relay:
            s.style = SpriteStyle::Relay;
            s.colour = holo;
            break;
        default:
            s.style = SpriteStyle::Star;
            s.colour = Colour{0.80f, 0.92f, 1.0f, 1.0f};
            break;
        }
        list.sprites.push_back(s);
        if (isHovered) {
            list.sprites.push_back({p.position, p.radiusM * 2.6f, warm, SpriteStyle::Halo, {}});
        }
        if ((o.flags & kSelected) != 0) {
            list.rings.push_back({p.position, tableUp, p.radiusM * 1.9f, 0.0016f, warm});
        }
        if ((o.flags & kHere) != 0) {
            list.rings.push_back({p.position, tableUp, p.radiusM * (2.2f + 0.6f * pulse), 0.0014f, Scaled(green, 0.5f + 0.5f * pulse)});
        }
        if ((o.flags & kMarked) != 0) {
            list.rings.push_back({p.position, tableUp, p.radiusM * 1.6f, 0.0012f, Scaled(Colour{1.0f, 0.9f, 0.3f, 1.0f}, 0.4f + 0.6f * pulse)});
        }
        std::string const text = LabelOf(o.label);
        if (!text.empty()) {
            Label l;
            l.anchor = Add(p.position, Mul(tableUp, p.radiusM + 0.008f));
            l.heightM = isHovered ? 0.022f : 0.016f;
            l.colour = isHovered ? warm : Colour{0.82f, 0.92f, 1.0f, (o.flags & kSelectable) != 0 ? 1.0f : 0.6f};
            l.text = text;
            list.labels.push_back(l);
        }
    }

    // The level's title over the far rim.
    std::string const title(scene.title, strnlen(scene.title, kLabelBytes));
    if (!title.empty()) {
        Label t;
        t.anchor = Add(Add(centre, Mul(tableAway, r * 1.15f)), Mul(tableUp, 0.07f));
        t.heightM = 0.040f;
        t.colour = Colour{0.85f, 0.95f, 1.0f, 1.0f};
        t.text = title;
        t.plate = false;
        list.labels.push_back(t);
    }
    (void)settings;
    return list;
}

std::string Describe(Scene const& scene) {
    int selectable = 0;
    for (int i = 0; i < std::min<int>(scene.objectCount, kMaxObjects); ++i) {
        selectable += (scene.objects[i].flags & kSelectable) != 0 ? 1 : 0;
    }
    char const* const names[] = {"unknown", "galaxy", "cluster", "system", "planet"};
    return std::format("level {} ({}), '{}', {} objects ({} selectable), {} links, centre ({:.0f}, {:.0f}, {:.0f}), extent {:.0f}, "
                       "camera {}",
        scene.level, scene.level <= 4 ? names[scene.level] : "?", std::string(scene.title, strnlen(scene.title, kLabelBytes)),
        scene.objectCount, selectable, scene.linkCount, scene.centre[0], scene.centre[1], scene.centre[2], scene.extent,
        scene.camera.valid ? std::format("at ({:.0f}, {:.0f}, {:.0f}) fov {:.1f}", scene.camera.location[0], scene.camera.location[1],
                                         scene.camera.location[2], scene.camera.fovXDeg)
                           : std::string("none"));
}

} // namespace mevr::vr::galaxy_holo
