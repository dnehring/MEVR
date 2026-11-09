// SPDX-License-Identifier: MIT
#include "core/vr/OmniRail.hpp"
#include "core/vr/OmniPower.hpp"
#include "core/vr/MenuPointer.hpp"
#include "core/vr/GamePointer.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <limits>

namespace {
int gFailures = 0;
void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/OmniRailMotionTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)
bool Close(float a, float b) { return std::fabs(a-b) < 1e-5f; }
namespace rail = mevr::vr::omni_rail;
namespace power = mevr::vr::omni_power;
namespace wu = mevr::vr::world_ui;
namespace mp = mevr::vr::menu_pointer;
namespace gp = mevr::vr::game_pointer;

void PlacementAndSlide() {
    wu::Panel screen{{{0,0,0,1},{0,1,-0.5f}}, 0.7f, 0.4f};
    auto const right = rail::Beside(screen);
    auto const left = rail::Beside(screen, -1.0f);
    CHECK(Close(right.pose.position.x, -left.pose.position.x));
    CHECK(Close(right.pose.position.x-right.widthM/2, screen.widthM/2+rail::kGapM));
    CHECK(Close(right.pose.position.y,screen.pose.position.y) && Close(right.pose.position.z,screen.pose.position.z));
    CHECK(Close(right.heightM,screen.heightM));
    auto const tucked = rail::Tucked(screen, 1);
    CHECK(Close(tucked.pose.position.x+tucked.widthM/2, screen.widthM/2));
    CHECK(tucked.pose.position.z < screen.pose.position.z);
    auto const tuckLeft = rail::Tucked(screen, -1);
    CHECK(Close(tucked.pose.position.x,-tuckLeft.pose.position.x));
    CHECK(!rail::Exposed(tucked,screen,1) && !rail::Exposed(tuckLeft,screen,-1));
    for (float side : {-1.0f,1.0f}) {
        auto const to = rail::Beside(screen,side);
        auto const halfway = rail::Interpolate(rail::Tucked(screen,side),to,0.04f);
        auto const face = rail::Exposed(halfway,screen,side);
        CHECK(face && face->rail.widthM < halfway.widthM && face->u1 > face->u0);
        if (face) {
            float const edge = face->rail.pose.position.x-side*face->rail.widthM/2;
            CHECK(Close(edge,side*screen.widthM/2));
            CHECK(Close(face->rail.widthM,(face->u1-face->u0)*halfway.widthM));
        }
        auto const wholeFace = rail::Exposed(to,screen,side);
        CHECK(wholeFace && Close(wholeFace->u0,0) && Close(wholeFace->u1,1));
    }
    CHECK(Close(rail::SlideEase(-1),0) && Close(rail::SlideEase(0),0));
    CHECK(Close(rail::SlideEase(rail::kSlideS),1) && Close(rail::SlideEase(100),1));
    CHECK(Close(rail::SlideEase(rail::kSlideS/2),0.5f));
    // Cubic in-out: zero endpoint speed, symmetric, monotonic, peak ~3d/(2T) < 1 m/s for this rail.
    float previous = 0;
    float peak = 0;
    float const distance = right.pose.position.x-tucked.pose.position.x;
    for (int i=1; i<=1000; ++i) {
        float const t = rail::kSlideS*static_cast<float>(i)/1000.0f;
        float const eased = rail::SlideEase(t);
        CHECK(eased >= previous && eased <= 1);
        CHECK(Close(eased+rail::SlideEase(rail::kSlideS-t),1));
        peak = std::max(peak,(eased-previous)*distance/(rail::kSlideS/1000));
        previous = eased;
    }
    CHECK(peak < 1.0f);
    CHECK(rail::SlideEase(0.001f) < 0.00002f);
    CHECK(rail::SlideEase(std::numeric_limits<float>::quiet_NaN()) == 0);
    rail::Rail from{{{0,0,0,1},{-0.1f,0.8f,-0.4f}},0.18f,0.32f};
    auto const start = rail::Interpolate(from,right,0);
    auto const end = rail::Interpolate(from,right,1);
    CHECK(Close(start.pose.position.x,from.pose.position.x) && Close(start.widthM,from.widthM));
    CHECK(Close(end.pose.position.x,right.pose.position.x) && Close(end.heightM,right.heightM));
    rail::Rail turn = right;
    turn.pose.orientation = {0,std::sqrt(0.5f),0,std::sqrt(0.5f)};
    auto const mid = rail::Interpolate(from,turn,rail::kSlideS/2);
    float const n = mid.pose.orientation.y*mid.pose.orientation.y+mid.pose.orientation.w*mid.pose.orientation.w;
    CHECK(Close(n,1) && mid.pose.orientation.y > 0 && mid.pose.orientation.w > 0);
    turn.pose.orientation = {0,0,0,-1};
    CHECK(Close(std::fabs(rail::Interpolate(from,turn,0.1f).pose.orientation.w),1));

    rail::Slide slide;
    slide.Step(from,false,1,0,std::nullopt);
    auto drawn = slide.Step(right,true,1,0.01f,tucked);
    CHECK(slide.Started() && slide.Moving() && Close(slide.Age(),0));
    CHECK(Close(drawn.pose.position.x,from.pose.position.x));
    drawn = slide.Step(right,true,1,0.07f,tucked);
    CHECK(!slide.Started() && Close(slide.Age(),0));
    CHECK(slide.FadingAlone() && Close(slide.Opacity(),0.3f));
    CHECK(Close(drawn.pose.position.x,from.pose.position.x)); // fading at its old place, never crossing the screen
    drawn = slide.Step(right,true,1,wu::kUnfoldS-0.07f,tucked);
    CHECK(!slide.FadingAlone() && Close(slide.Opacity(),1) && Close(slide.Age(),0));
    CHECK(Close(drawn.pose.position.x,tucked.pose.position.x));
    CHECK(!rail::Exposed(drawn,screen,1));
    drawn = slide.Step(right,true,1,rail::kSlideS/2,tucked);
    CHECK(Close(slide.Age(),rail::kSlideS/2));
    CHECK(Close(drawn.pose.position.x,(tucked.pose.position.x+right.pose.position.x)/2));
    // No tag/opening argument: another tab in the same opening cannot restart it.
    slide.Step(right,true,1,1.0f,tucked);
    CHECK(!slide.Started() && !slide.Moving() && Close(slide.Opacity(),1));
    slide.Step(left,true,-1,0,tuckLeft);
    CHECK(slide.Started() && slide.Moving());
    slide.Reset();
    drawn = slide.Step(right,true,1,0,tucked);
    CHECK(Close(drawn.pose.position.x,tucked.pose.position.x));
    auto moved = right;
    moved.pose.position.y += 0.3f;
    drawn = slide.Step(moved,true,1,0,std::nullopt);
    CHECK(Close(drawn.pose.position.y,tucked.pose.position.y+0.3f));
    // One long frame uses wall time without a 100 ms cap, even if it spans both phases.
    slide.Reset();
    slide.Step(right,true,1,0,tucked);
    drawn = slide.Step(right,true,1,wu::kUnfoldS+rail::kSlideS,tucked);
    CHECK(!slide.Moving() && Close(drawn.pose.position.x,right.pose.position.x));
    // A screen already partway through its unfold waits only for the remainder.
    slide.Reset();
    slide.Step(right,true,1,0,tucked,0.12f);
    slide.Step(right,true,1,0.06f,tucked);
    CHECK(Close(slide.Age(),0));
    slide.Step(right,true,1,rail::kSlideS,tucked);
    CHECK(!slide.Moving());
    for (int hz : {30,90,144}) {
        slide.Reset();
        slide.Step(right,true,1,0,tucked,wu::kUnfoldS);
        int frames = 0;
        while (slide.Moving() && frames<100) {
            slide.Step(right,true,1,1.0f/static_cast<float>(hz),tucked);
            ++frames;
        }
        CHECK(frames == static_cast<int>(std::ceil(rail::kSlideS*static_cast<float>(hz))));
        CHECK(Close(slide.Age(),rail::kSlideS));
    }
    // A rotated screen moves the rail on its local +X, not the room's X.
    screen.pose.orientation = {0,std::sqrt(0.5f),0,std::sqrt(0.5f)};
    CHECK(rail::Beside(screen).pose.position.z < screen.pose.position.z);
    CHECK(rail::Beside(screen,-1).pose.position.z > screen.pose.position.z);

    // Both hit rules follow only the exposed quad: the tucked portion never intercepts touch or ray.
    for (float side : {-1.0f,1.0f}) {
        screen.pose.orientation = {0,0,0,1};
        auto const destination = rail::Beside(screen,side);
        for (float t : {0.0f,0.07f,rail::kSlideS/2,rail::kSlideS}) {
            auto const moving = rail::Interpolate(rail::Tucked(screen,side),destination,t);
            auto const face = rail::Exposed(moving,screen,side);
            if (t==0) { CHECK(!face); continue; }
            CHECK(face.has_value());
            if (!face) continue;
            auto const at = face->rail;
            wu::Panel const panel{at.pose,at.widthM,at.heightM};
            XrVector3f const tip{at.pose.position.x,at.pose.position.y,at.pose.position.z+0.02f};
            CHECK(rail::GoingForPanel(tip,panel));
            CHECK(!rail::GoingForPanel({tip.x+at.widthM+2*rail::kTapPanelMarginM,tip.y,tip.z},panel));
            mp::Hit hit;
            CHECK(mp::Intersect({at.pose,at.widthM,at.heightM},tip,{0,0,-1},hit));
            CHECK(Close(hit.u,0.5f) && Close(hit.v,0.5f));
            gp::Settings settings;
            settings.touchClickOnContact = true;
            settings.touchScroll = false;
            gp::Pointer touch(settings);
            gp::Input input;
            input.active = true;
            input.width = 360;
            input.height = 640;
            input.dt = 0.01f;
            input.hands[1].tipValid = true;
            input.hands[1].tip = {tip.x,tip.y,at.pose.position.z+0.04f};
            gp::Quad const quad{at.pose,at.widthM,at.heightM};
            CHECK(touch.Step(quad,input).touch);
            input.hands[1].tip.z = at.pose.position.z+0.003f;
            auto const press = touch.Step(quad,input);
            CHECK(press.touch && press.pressed && press.pressEdge && press.hand == 1);
            CHECK(Close(press.u,0.5f) && Close(press.v,0.5f));
            gp::Pointer ray;
            input.hands[1].tipValid = false;
            input.hands[1].aimValid = true;
            input.hands[1].aim = {{0,0,0,1},tip};
            CHECK(ray.Step(quad,input).onQuad);
            input.hands[1].trigger = 0.8f;
            CHECK(ray.Step(quad,input).pressed);
        }
    }
}

void LightCurve() {
    CHECK(Close(power::Luminance(-1),0.15f) && Close(power::Luminance(0),0.15f));
    CHECK(Close(power::Luminance(power::kRiseS),1) && Close(power::Luminance(100),1));
    CHECK(power::Luminance(0.30f) < 0.5f); // ease in
    CHECK(power::Luminance(0.035f) > power::Luminance(0.060f)); // a small strike, then a dip
    CHECK(Close(power::Age(1000,1300),0.30f) && Close(power::Age(1000,900),0));
    CHECK(Close(power::Luminance(std::numeric_limits<float>::quiet_NaN()),power::kDim));
    bool flickered = false;
    float previous = power::Luminance(power::kFlickerS);
    for (int i=0; i<=1000; ++i) {
        float const t = static_cast<float>(i)/1000.0f;
        float const light = power::Luminance(t);
        CHECK(std::isfinite(light) && light >= power::kDim && light <= 1);
        CHECK(light == power::Luminance(t));
        if (t>0 && t<power::kFlickerS) {
            float const base = power::kDim+(1-power::kDim)*(t/power::kRiseS)*(t/power::kRiseS);
            flickered = flickered || light-base > 0.01f;
            CHECK(light-base <= 0.0351f);
        }
        if (t>=power::kFlickerS) {
            CHECK(light >= previous);
            previous = light;
        }
    }
    CHECK(flickered);
    CHECK(std::fabs(power::Luminance(0.14999f)-power::Luminance(0.15001f)) < 0.001f);
    CHECK(power::BrightnessParameter("Emissive_Intensity") && power::BrightnessParameter("BRIGHTNESS"));
    CHECK(!power::BrightnessParameter("SpawnRate") && !power::BrightnessParameter("Alpha") && !power::BrightnessParameter("Lifetime"));
    auto const dim = wu::BeamPixels(16,1,0.63f,0.35f,power::kDim);
    auto const full = wu::BeamPixels(16,1,0.63f,0.35f);
    for (std::size_t i=0; i<dim.size(); ++i) {
        CHECK((dim[i]>>24) <= (full[i]>>24));
        CHECK((dim[i]&255u) <= (full[i]&255u));
    }
    wu::State state;
    state.omniOnMs = 12345;
    wu::Publish(state); // an emitter-only update carries the power clock without pretending a screen is active
    CHECK(!wu::Latest(400) && wu::LatestOmniOnMs(400) == 12345);
    wu::Forget();
    CHECK(wu::LatestOmniOnMs(400) == 0);
}
} // namespace

int RunOmniRailMotionTests() {
    gFailures = 0;
    PlacementAndSlide();
    LightCurve();
    return gFailures;
}
