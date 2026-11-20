// SPDX-License-Identifier: MIT
#include "core/ik/BodySpheres.hpp"

#include "core/Json.hpp"

#include <cmath>
#include <unordered_map>

namespace mevr::ik::body_spheres {

std::optional<std::vector<Fitted>> Parse(std::string_view text, std::string* error) {
    auto const file = json::Parse(text, error);
    auto fail = [&](char const* why) -> std::optional<std::vector<Fitted>> {
        if (error != nullptr) *error = why;
        return std::nullopt;
    };
    if (!file || !file->IsObject()) return fail("not a JSON object");
    if (!file->Get("format").IsString() || file->Get("format").AsString() != "mevr-body-spheres") return fail("not a mevr-body-spheres file");
    if (file->Get("version").AsNumber(0.0) != 1.0) return fail("not version 1");
    json::Value const& list = file->Get("spheres");
    if (!list.IsArray()) return fail("no spheres");
    std::vector<Fitted> out;
    for (json::Value const& s : list.Items()) {
        json::Value const& c = s.Get("c");
        if (!s.Get("bone").IsString() || !c.IsArray() || c.Items().size() != 3) return fail("a sphere without bone or centre");
        Fitted f;
        f.bone = s.Get("bone").AsString();
        f.centre = {c.Items()[0].AsNumber(), c.Items()[1].AsNumber(), c.Items()[2].AsNumber()};
        f.radius = s.Get("r").AsNumber(0.0);
        json::Value const& n = s.Get("n");
        if (n.IsArray() && n.Items().size() == 3) {
            f.normal = Normalize(Vec3{n.Items()[0].AsNumber(), n.Items()[1].AsNumber(), n.Items()[2].AsNumber()}, Vec3{});
        }
        if (!(f.radius > 0.0) || !std::isfinite(f.centre.x) || !std::isfinite(f.centre.y) || !std::isfinite(f.centre.z)) {
            return fail("a sphere with no radius or a centre not finite");
        }
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<Sphere> Bind(std::span<Fitted const> fitted, std::span<std::string const> names, std::span<Transform const> bindWorld,
    int* missing) {
    std::unordered_map<std::string, int> index;
    for (std::size_t i = 0; i < names.size() && i < bindWorld.size(); ++i) index.emplace(names[i], static_cast<int>(i));
    std::vector<Sphere> out;
    int lost = 0;
    for (Fitted const& f : fitted) {
        auto const found = index.find(f.bone);
        if (found == index.end()) {
            ++lost;
            continue;
        }
        Transform const& bone = bindWorld[static_cast<std::size_t>(found->second)];
        out.push_back({found->second, Apply(Inverse(bone), f.centre), f.radius, Rotate(Conjugate(bone.rotation), f.normal)});
    }
    if (missing != nullptr) *missing = lost;
    return out;
}

void Place(std::span<Sphere const> spheres, std::span<Transform const> world, double metresPerUnit,
    std::vector<hand_contact::Capsule>& out, std::vector<Vec3>* normals) {
    for (Sphere const& s : spheres) {
        if (s.bone < 0 || static_cast<std::size_t>(s.bone) >= world.size()) continue;
        Transform const& bone = world[static_cast<std::size_t>(s.bone)];
        Vec3 const at = bone.translation + Rotate(bone.rotation, s.local * (bone.scale * metresPerUnit));
        if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z)) continue;
        hand_contact::Capsule c;
        c.a = at;
        c.b = at;
        c.radius = s.radius * bone.scale * metresPerUnit;
        out.push_back(c);
        if (normals != nullptr) normals->push_back(Rotate(bone.rotation, s.localNormal));
    }
}

} // namespace mevr::ik::body_spheres
