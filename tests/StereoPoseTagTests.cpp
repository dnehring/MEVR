// SPDX-License-Identifier: MIT
#include "core/vr/StereoPoseTag.hpp"
#include "core/vr/EyePublication.hpp"
#include <cstdio>
#include <limits>

namespace {
int failures = 0;
void Check(bool ok, int line) { if (!ok) { ++failures; std::printf("StereoPoseTagTests:%d\n", line); } }
#define CHECK(x) Check((x), __LINE__)
struct V { float x = 0, y = 0, z = 0; };
using History = mevr::vr::stereo_tag::EyeHistory<V, 4>;
struct Tag { unsigned orientation = 0; History::Pair eyes{}; float fov[2]{}; };
bool Near(float a, float b) { return std::abs(a-b) < 0.00001f; }
}
int main() {
    namespace st = mevr::vr::stereo_tag;
    History history;
    History::Pair const oldEyes{{{-0.032f, 1, 2}, {0.036f, 1.002f, 2.004f}}};
    // An asymmetric turned/translated sample: not a symmetric synthetic IPD.
    History::Pair const newEyes{{{1, 2, 3.032f}, {1.004f, 2.002f, 2.964f}}};
    history.Seed(oldEyes);
    history.Shift(newEyes);
    for (float frames : {0.f, .5f, 1.f, 1.5f, 3.f}) {
        auto d = st::FrameDelay(frames, 3);
        auto pair = history.At(d.newer, d.older, d.fraction);
        float const weight = frames < 1 ? frames : 1;
        for (unsigned eye = 0; eye != 2; ++eye) {
            CHECK(Near(pair[eye].x, newEyes[eye].x + (oldEyes[eye].x-newEyes[eye].x)*weight));
            CHECK(Near(pair[eye].z, newEyes[eye].z + (oldEyes[eye].z-newEyes[eye].z)*weight));
        }
    }
    CHECK(st::FrameDelay(-1,3).newer == 0);
    CHECK(st::FrameDelay(std::numeric_limits<float>::quiet_NaN(),3).newer == 0);
    CHECK(st::FrameDelay(std::numeric_limits<float>::infinity(),3).newer == 0);
    CHECK(st::FrameDelay(99,3).newer == 3 && st::FrameDelay(99,3).fraction == 0);
    // Circular LE3 ring and linear LE2 ring select the same runtime pair when
    // passed the matching head-ring slots; there is no independent sample clock.
    st::EyeHistory<V,16> circular;
    circular.Seed(oldEyes); circular.Write(7,newEyes);
    auto a = circular.At(7,6,.5f), b = history.At(0,1,.5f);
    CHECK(Near(a[0].x,b[0].x) && Near(a[1].z,b[1].z));

    st::SourceTag<Tag> source;
    st::CaptureEpoch epoch;
    epoch.Reset(10);
    CHECK(!epoch.Advance(10)); // context changed, old capture must remain mono
    CHECK(epoch.Advance(11) && epoch.Advance(11));
    source.BeginFrame(); CHECK(source.Enter(0));
    Tag first{10,oldEyes,{.7f,.8f}}, newer{20,newEyes,{1.1f,1.2f}};
    auto tag = source.Select(11,first,true);
    source.BeginFrame(); CHECK(!source.Enter(0));
    tag = source.Select(11,newer,true); // held capture freezes ALL declaration values
    CHECK(source.Held() && tag.orientation == 10 && tag.eyes[1].x == oldEyes[1].x && tag.fov[1] == .8f);

    mevr::vr::EyePublication<Tag> published;
    CHECK(published.Complete(0,true,true,true,&tag));
    CHECK(!published.Complete(1,true,true,false,&tag));
    CHECK(!published.CurrentPair()); // one failed release never publishes a stereo frame
    source.BeginFrame(); CHECK(!source.Enter(0));
    published.BeginFrame();
    auto retry = source.Select(11,newer,true);
    CHECK(retry.orientation == 10 && retry.eyes[1].x == oldEyes[1].x);
    CHECK(published.Complete(1,true,true,true,&retry));
    CHECK(!published.CurrentPair()); // eye0 succeeded only in the preceding frame
    CHECK(published.Complete(0,true,true,true,&retry));
    CHECK(published.CurrentPair() && published.Get(1)->tag.fov[1] == .8f);
    published.BeginFrame();
    tag = source.Select(12,newer,true); // newer source, but acquire fails: old receipt unchanged
    CHECK(!published.Complete(0,false,false,false,&tag));
    CHECK(published.Get(0)->tag.orientation == 10 && !published.CurrentPair());
    CHECK(published.Complete(0,true,true,true,&tag));
    CHECK(!published.CurrentPair());
    CHECK(published.Complete(1,true,true,true,&tag));
    CHECK(published.CurrentPair() && published.Get(1)->tag.orientation == 20);

    // Current-pose locked cinema / live mono bypass hold. Returning to stereo
    // cannot accidentally freeze that fallback declaration at the same sequence.
    CHECK(source.Select(12,first,false).orientation == 10);
    CHECK(source.Select(12,newer,true).orientation == 20);
    source.BeginFrame(); CHECK(source.Enter(1)); // mode/locked-cine/swap context
    CHECK(source.Select(12,first,true).orientation == 10);
    source.BeginFrame(); // leave SFR for flat, mono, AER or invalid tracking
    source.BeginFrame(); CHECK(source.Enter(1));
    CHECK(source.Select(12,newer,true).orientation == 20);
    source.Reset(); epoch.Reset(12); history.Seed(newEyes);
    CHECK(!epoch.Advance(12)); // recenter/session: same seq cannot bless old pixels
    source.BeginFrame(); CHECK(source.Enter(1));
    CHECK(source.Select(12,first,true).orientation == 10);
    CHECK(history.At(3,3,0)[1].x == newEyes[1].x);
    return failures ? 1 : 0;
}
