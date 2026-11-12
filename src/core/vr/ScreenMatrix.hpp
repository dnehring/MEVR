// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <optional>

// The second eye's screen-space matrices in the draw replay (research/37, "The right eye's shadows", 2026-11-11).
//
// UE3 builds every pass that reconstructs a position from the depth buffer (the shadow projections' ScreenToShadowMatrix,
// the fog's and modulated shadows' ScreenToWorld) as one matrix on the CPU:
//     M1 = S1 x Inv(TVP1) x R
// where S turns the shader's input (x_ndc z, y_ndc z, z, 1) into clip space (rows (1,0,0,0) (0,1,0,0) (0,0,P22,1)
// (0,0,P32,0) of the eye's projection), TVP1 is the first eye's translated view-projection and R the rest (the
// translation back to the world, a shadow's projection...). The replay re-issues the first eye's draws for the second
// eye with TVP1 replaced by Sub = T(pvt2 - pvt1) x TVP2, so the second eye's depth buffer holds the second eye's view;
// but M1 is not among the substituted keys, and the second eye's shadows were reconstructed through the first eye's
// view: they slid as the head moved (the owner's Citadel report).
//
// The second eye's version of any such composite is C x M1 with
//     C = S2 x Inv(Sub) x TVP1 x Inv(S1)
// (the second eye's input to its clip space, back into the first eye's translated space, into the first eye's clip
// space, back to the first eye's input), independent of R. Matrices are UE3's: row-major, row vectors (v x M).
namespace mevr::vr::screen_matrix {

using Mat4 = std::array<float, 16>;

// S for a translated view-projection: P22 and P32 read off its third and fourth columns (the projection's z column is
// (0, 0, P22, P32), its w column (0, 0, 1, 0), and the translated view has no translation). Empty when the matrix
// doesn't have that shape or S isn't invertible.
std::optional<Mat4> ScreenToClip(Mat4 const& translatedViewProjection);

// C, or empty when either view isn't usable. pvt: each eye's pre-view translation.
std::optional<Mat4> SecondEyeCorrection(Mat4 const& tvp1, Mat4 const& tvp2, float const pvt1[3], float const pvt2[3]);

// In place on a matrix as uploaded: `rows` as the engine's FMatrix (m = C x m), else stored transposed (m = m x C^T).
void Apply(Mat4 const& c, float* m, bool rows);

// Helpers, exposed for the tests.
Mat4 Multiply(Mat4 const& a, Mat4 const& b);
std::optional<Mat4> Inverse(Mat4 const& m);

} // namespace mevr::vr::screen_matrix
