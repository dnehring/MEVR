// SPDX-License-Identifier: MIT
#include "core/vr/EyePublication.hpp"
#include <cstdio>

namespace {
int failures = 0;
void Check(bool ok, int line) { if (!ok) { ++failures; std::printf("EyePublicationTests:%d\n", line); } }
#define CHECK(x) Check((x), __LINE__)
struct Tag { unsigned sequence, pose, fov; };
}
int main() {
    mevr::vr::EyePublication<Tag> images;
    Tag left{10,100,70}, right{11,101,71};
    images.BeginFrame();
    CHECK(images.Complete(0, true, true, true, &left));
    CHECK(!images.CurrentPair()); // partial pair cannot become a current stereo frame
    CHECK(!images.Complete(1, true, false, true, &right));
    CHECK(!images.Get(1) && !images.TaggedPair());
    CHECK(!images.Complete(1, true, true, false, &right)); // copy without release is not publication
    CHECK(!images.Get(1));
    CHECK(images.Complete(1, true, true, true, &right));
    CHECK(images.CurrentPair() && images.TaggedPair());

    images.BeginFrame();
    left = {12,200,90}; // history advances without an image upload
    CHECK(!images.CurrentPair() && images.TaggedPair());
    CHECK(images.Get(0)->tag.sequence == 10 && images.Get(0)->tag.pose == 100 && images.Get(0)->tag.fov == 70);
    CHECK(!images.Complete(0, false, false, false, &left)); // failed acquire touched no released image
    CHECK(images.Get(0)->tag.sequence == 10 && images.TaggedPair());
    CHECK(images.Complete(0, true, true, true, &left)); // AER replaces only the fresh eye
    CHECK(!images.CurrentPair() && images.TaggedPair());
    CHECK(images.Get(0)->tag.pose == 200 && images.Get(1)->tag.pose == 101);
    CHECK(!images.Complete(1, true, false, true, &right)); // acquired failed attempt invalidates the old eye
    CHECK(!images.TaggedPair() && !images.Get(1));
    CHECK(images.Complete(1, true, true, true, &right)); // catch-up restores a complete current pair
    CHECK(images.CurrentPair());

    images.Reset(); // new session/swapchain/history generation
    CHECK(!images.CurrentPair() && !images.TaggedPair() && !images.Get(0) && !images.Get(1));
    CHECK(images.Complete(0, true, true, true));
    CHECK(images.Complete(1, true, true, true));
    CHECK(images.CurrentPair() && !images.TaggedPair()); // current SFR pair has no AER-reuse tag
    CHECK(!images.Complete(-1, true, true, true) && !images.Complete(2, true, true, true));
    return failures ? 1 : 0;
}
