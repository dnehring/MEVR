// SPDX-License-Identifier: MIT
#include "core/vr/ManualCalibration.hpp"
#include "core/vr/MenuSettings.hpp"
#include "core/vr/LiveCalibration.hpp"
#include "core/vr/CalibrationMarkers.hpp"

#include <cstdio>
#include <limits>

namespace {
int failures = 0;
void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ManualCalibrationTests.cpp line %d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)
bool Near(float a, float b) { return std::abs(a - b) < 1e-5f; }
bool Near(XrVector3f a, XrVector3f b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }
bool SameRotation(XrQuaternionf a, XrQuaternionf b) {
    namespace c = mevr::vr::calibration;
    // Compare basis vectors: acos(dot(q,q)) has poor float precision near identity.
    return Near(c::Rotate(a, {0, 0, -1}), c::Rotate(b, {0, 0, -1})) &&
        Near(c::Rotate(a, {0, 1, 0}), c::Rotate(b, {0, 1, 0}));
}
}

int RunManualCalibrationTests() {
    namespace m = mevr::vr::manual_calibration;
    namespace c = mevr::vr::calibration;
    namespace ms = mevr::vr::menu_settings;
    mevr::vr::controllers::State raw;
    raw.active = true;
    raw.sequence = 42;
    raw.timeMs = 123;
    raw.left.gripValid = raw.left.aimValid = raw.right.gripValid = raw.right.aimValid = true;
    raw.left.grip = {{0, 0, 0, 1}, {-0.2f, 1.0f, -0.4f}};
    raw.right.grip = {{0, 0, 0, 1}, {0.2f, 1.0f, -0.4f}};
    raw.left.aim = raw.left.grip;
    raw.right.aim = {{0, 0, 0, 1}, {0.2f, 1.0f, -0.5f}};
    raw.right.trigger = 0.7f;
    m::Settings settings;
    auto same = m::Apply(raw, settings);
    CHECK(Near(same.right.grip.position, raw.right.grip.position));
    CHECK(same.right.grip.orientation.w == raw.right.grip.orientation.w);
    CHECK(same.sequence == 42 && same.timeMs == 123 && same.right.trigger == 0.7f);
    CHECK(m::Section("touch_quest") == "ManualCalibration.touch_quest");

    settings.values[1] = settings.values[7] = 0.02f;
    settings.values[2] = settings.values[8] = 0.03f;
    settings.values[3] = settings.values[9] = 0.04f;
    auto moved = m::Apply(raw, settings);
    CHECK(Near(moved.left.grip.position, {-0.22f, 1.03f, -0.44f}));
    CHECK(Near(moved.right.grip.position, {0.22f, 1.03f, -0.44f}));
    CHECK(Near(moved.right.aim.position, {0.22f, 1.03f, -0.54f}));
    // Controller-local correction follows a turned controller, not world axes.
    raw.right.grip.orientation = raw.right.aim.orientation = c::YawRotation(1.5707963268f);
    moved = m::Apply(raw, settings);
    CHECK(Near(moved.right.grip.position, {0.16f, 1.03f, -0.42f}));
    raw.right.grip.orientation = raw.right.aim.orientation = {0, 0, 0, 1};

    settings = {};
    settings.values[10] = 30;
    auto tilted = m::Apply(raw, settings);
    auto direction = c::Rotate(tilted.right.grip.orientation, {0, 0, -1});
    CHECK(direction.y < -0.49f && direction.z < -0.86f); // positive pitch down
    CHECK(Near(tilted.right.grip.position, raw.right.grip.position)); // rotates about the grip
    CHECK(Near(tilted.left.grip.position, raw.left.grip.position));
    CHECK(tilted.left.grip.orientation.w == 1);
    CHECK(Near(tilted.right.aim.position.y - tilted.right.grip.position.y, -0.05f));
    CHECK(SameRotation(tilted.right.aim.orientation, tilted.right.grip.orientation));
    // An arbitrary runtime aim/grip rotation remains unchanged by the common rigid correction.
    raw.right.aim.orientation = c::YawRotation(0.3f);
    tilted = m::Apply(raw, settings);
    auto relative = c::Multiply(c::Conjugate(tilted.right.grip.orientation), tilted.right.aim.orientation);
    CHECK(SameRotation(relative, raw.right.aim.orientation));
    settings = {};
    settings.values[5] = settings.values[11] = 20;
    auto turned = m::Apply(raw, settings);
    CHECK(c::Rotate(turned.right.grip.orientation, {0, 0, -1}).x > 0.3f);
    CHECK(c::Rotate(turned.left.grip.orientation, {0, 0, -1}).x < -0.3f);
    settings = {};
    settings.values[6] = settings.values[12] = 20;
    turned = m::Apply(raw, settings);
    CHECK(c::Rotate(turned.right.grip.orientation, {0, 1, 0}).x > 0.3f);
    CHECK(c::Rotate(turned.left.grip.orientation, {0, 1, 0}).x < -0.3f);

    settings.values[7] = 999;
    settings.values[8] = std::numeric_limits<float>::quiet_NaN();
    settings.values[0] = -999;
    CHECK(settings.Height() == -0.5f);
    moved = m::Apply(raw, settings);
    CHECK(Near(moved.right.grip.position.x, 0.35f) && Near(moved.right.grip.position.y, 1));
    raw.right.gripValid = false;
    CHECK(Near(m::Apply(raw, settings).right.aim.position, raw.right.aim.position));
    raw.active = false;
    CHECK(Near(m::Apply(raw, settings).left.grip.position, raw.left.grip.position));
    settings = {}; // reset is the identity and does not change measured calibration
    CHECK(settings.Height() == 0);
    for (auto const& field : m::kFields) {
        auto const& schema = ms::Schema();
        auto it = std::find_if(schema.begin(), schema.end(), [&](auto const& s) { return std::string_view(s.key) == field.menu; });
        CHECK(it != schema.end());
        if (it != schema.end()) {
            CHECK(it->number == 0 && it->min == -field.limit && it->max == field.limit && !it->local);
        }
    }
    CHECK(ms::FormatValue(ms::Format::SignedCentimeters1, 0.005f) == "+0.5 cm");
    CHECK(ms::FormatValue(ms::Format::SignedCentimeters1, -0.02f) == "-2.0 cm");
    // References follow the raw grip frame, including controller orientation; selection changes only their colour.
    {
        namespace markers = mevr::vr::calibration_markers;
        mevr::vr::controllers::State sample;
        sample.active = sample.left.gripValid = sample.right.gripValid = true;
        sample.left.grip = {{0,0,0,1}, {-0.2f,1.3f,-0.5f}};
        sample.right.grip = {c::YawRotation(0.6f), {0.2f,1.3f,-0.5f}};
        auto const a = markers::Build(sample, 1);
        auto const b = markers::Build(sample, 0);
        CHECK(!a.empty() && a.size() == b.size());
        bool forwardTip = false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(a[i].colour != b[i].colour);
            for (std::size_t j = 0; j < 4; ++j) {
                CHECK(Near(a[i].points[j], b[i].points[j]));
                if (a[i].colour == 0xFF3F8FFFu) {
                    auto const p = c::Rotate(c::Conjugate(sample.right.grip.orientation),
                        mevr::vr::calibration_weapon::Sub(a[i].points[j], sample.right.grip.position));
                    CHECK(p.z >= -0.091f && p.z <= 0.031f);
                    if (p.z < -0.089f) forwardTip = true;
                }
            }
        }
        CHECK(forwardTip);
        sample.left.gripValid = false;
        CHECK(markers::Build(sample, 1).size() * 2 == a.size());
        sample.right.grip.orientation.x = std::numeric_limits<float>::quiet_NaN();
        CHECK(markers::Build(sample, 1).empty());
        sample.active = false;
        CHECK(markers::Build(sample, 1).empty());
    }
    // Live editing never consumes the target hand's controls. The opposite hand owns a temporary draft.
    namespace live = mevr::vr::live_calibration;
    for (int target : {0, 1}) {
        live::Session session;
        m::Settings baseline;
        baseline.values[0] = 0.12f;
        baseline.values[1] = 0.02f;
        session.Begin(baseline, target);
        CHECK(session.Editor() == 1 - target && session.axis == 3);
        mevr::vr::controllers::State input;
        input.active = input.left.gripValid = input.right.gripValid = true;
        auto& editor = target == 0 ? input.right : input.left;
        auto& subject = target == 0 ? input.left : input.right;
        subject.primary = subject.secondary = true;
        subject.stickX = 1;
        editor.stickX = 1;
        CHECK(session.Tick(input, 0.05f, true) == live::Action::None && !session.armed);
        CHECK(session.draft.values == baseline.values); // opening gesture ignored
        editor.stickX = 0;
        session.Tick(input, 0.05f, true);
        CHECK(session.armed);
        editor.stickX = 1;
        session.Tick(input, 0.05f, true);
        CHECK(Near(session.draft.values[session.Index()], 0.6f));
        CHECK(session.draft.values[0] == baseline.values[0]);
        CHECK(session.draft.values[target == 0 ? 10 : 4] == 0); // other hand untouched
        editor.trigger = 1;
        session.Tick(input, 0.05f, true);
        CHECK(Near(session.draft.values[session.Index()], 0.72f)); // fine mode is one fifth speed
        auto const beforeLoss = session.draft.values;
        input.left.gripValid = false;
        session.Tick(input, 0.05f, true);
        input.left.gripValid = true;
        session.Tick(input, 0.05f, true);
        CHECK(session.draft.values == beforeLoss && !session.armed); // release required after tracking loss
        editor = {};
        editor.gripValid = true;
        session.Tick(input, 0.05f, true);
        editor.stickY = -1;
        session.Tick(input, 0.05f, true);
        CHECK(session.axis == 4);
        session.Tick(input, 0.05f, true);
        CHECK(session.axis == 4); // one axis per flick
        editor.stickY = 0;
        session.Tick(input, 0.05f, true);
        session.draft.values[session.Index()] = 44.9f;
        editor.stickX = 1;
        session.Tick(input, 100.0f, true);
        CHECK(session.draft.values[session.Index()] == 45); // bounded even after a long frame
        editor.stickClick = true;
        session.Tick(input, 0.05f, true);
        CHECK(session.draft.values[session.Index()] == 0);
        editor.stickClick = false;
        editor.secondary = true;
        CHECK(session.Tick(input, 0.05f, true) == live::Action::Cancel);
        CHECK(baseline.values[session.Index()] == 0); // cancellation never mutated saved values
        editor.secondary = false;
        editor.primary = true;
        CHECK(session.Tick(input, 0.05f, true) == live::Action::Done);
        auto const posed = m::Apply(input, session.draft);
        auto const filtered = live::PosesOnly(posed);
        CHECK(filtered.active && filtered.left.gripValid && filtered.right.gripValid);
        CHECK(Near(filtered.left.grip.position, posed.left.grip.position));
        CHECK(SameRotation(filtered.right.grip.orientation, posed.right.grip.orientation));
        CHECK(filtered.left.trigger == 0 && filtered.right.stickX == 0 && !filtered.left.primary && !filtered.right.secondary);
        editor.primary = false;
        editor.secondary = false;
        subject.gripValid = false;
        session.Tick(input, 0.05f, true);
        editor.secondary = true;
        CHECK(session.Tick(input, 0.05f, true) == live::Action::Cancel); // never trapped by a lost target controller
    }
    return failures;
}
