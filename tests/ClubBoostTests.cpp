// SPDX-License-Identifier: MIT
// The club dancers' boost (core/ik/ClubBoost): the hash is stable, the ratios lie in their ranges and differ between people,
// the club match, and a grown bone moves its vertices to Q + (1 + a) (v - Q) about its pivot, following the bone.
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "core/ik/BodyStandIn.hpp"
#include "core/ik/ClubBoost.hpp"

namespace club = mevr::ik::club;
namespace ik = mevr::ik;

namespace {

int gFail = 0;

void Check(bool ok, char const* what) {
    if (!ok) {
        std::printf("FAIL club boost: %s\n", what);
        ++gFail;
    }
}

bool Near(double a, double b, double e = 1e-9) { return std::abs(a - b) <= e; }

} // namespace

int RunClubBoostTests() {
    gFail = 0;
    // A fixed value: the hash must never change between builds (a dancer keeps her ratio across updates).
    Check(club::StableHash("") == club::StableHash(""), "the hash is deterministic");
    Check(club::StableHash("biod_omghub_230dendance.theworld.persistentlevel.biopawn_3|biog_hmf_arm_nkd_r.nkdb.hmf_arm_nkdb_mdl") ==
              club::StableHash("biod_omghub_230dendance.theworld.persistentlevel.biopawn_3|biog_hmf_arm_nkd_r.nkdb.hmf_arm_nkdb_mdl"),
        "the same key hashes the same");
    Check(club::StableHash("a") != club::StableHash("b"), "different keys hash differently");

    // Amounts: in range, the same for the same key, spread between people.
    club::SizeSettings settings;
    std::set<long long> breasts;
    std::set<long long> glutes;
    for (int i = 0; i < 40; ++i) {
        std::string const key = "biod_omghub_230dendance.theworld.persistentlevel.biopawn_" + std::to_string(i) + "|hmf_arm_nkdb_mdl";
        club::Sizes const s = club::SizesFor(key, 1.25, settings);
        club::Sizes const again = club::SizesFor(key, 1.25, settings);
        Check(s.breast == again.breast && s.glute == again.glute, "a person keeps her amounts");
        Check(s.breast >= 0.4 * 0.25 - 1e-12 && s.breast <= 0.25 + 1e-12, "breast amount in [0.4 (cap - 1), cap - 1]");
        Check(s.glute >= 0.10 - 1e-12 && s.glute <= 0.16 + 1e-12, "glute amount in [0.10, 0.16]");
        breasts.insert(std::llround(s.breast * 1000.0));
        glutes.insert(std::llround(s.glute * 1000.0));
    }
    Check(breasts.size() >= 20, "breast amounts differ between people");
    Check(glutes.size() >= 15, "glute amounts differ between people");
    // A body already as large as the cap is left as it is; a silly cap is held; no cap: the default.
    Check(Near(club::SizesFor("x", 0.9, settings).breast, 0.0), "a cap under 1 enlarges nothing");
    Check(club::SizesFor("x", 9.0, settings).breast <= 0.8 + 1e-12, "the cap is held at 1.8");
    Check(club::SizesFor("x", 0.0, settings).breast <= 0.15 + 1e-12 && club::SizesFor("x", 0.0, settings).breast > 0.0,
        "no breastMax: the default cap 1.15");
    club::SizeSettings flat;
    flat.gluteLow = flat.gluteMid = flat.gluteHigh = 0.0;
    Check(Near(club::SizesFor("x", 1.25, flat).glute, 0.0), "a zero glute range grows nothing");

    // The club match and who is female.
    std::vector<std::string> const parts{"bioa_sta60_05", "biod_omghub_230dendance", ""};
    Check(club::InClub("bioa_sta60_05women_dsg.theworld.persistentlevel.biopawn_2", parts), "Chora's Den matches");
    Check(club::InClub("biod_omghub_230dendance.theworld.persistentlevel.biopawn_7", parts), "Afterlife's dance floor matches");
    Check(!club::InClub("biod_omghub_450market.theworld.persistentlevel.biopawn_1", parts), "Omega's market doesn't");
    Check(!club::InClub("anything", std::vector<std::string>{""}), "an empty part never matches");
    Check(club::FemaleBody("biog_hmf_arm_cth_r.cthd.hmf_arm_cthd_mdl"), "a human female body");
    Check(club::FemaleBody("mevr_bodies_le3.asa_arm_liad_mdl_mevr"), "an asari stand-in");
    Check(!club::FemaleBody("biog_hmm_arm_cth_r.ctha.hmm_arm_ctha_mdl"), "a male body isn't");
    Check(!club::FemaleBody("biog_hmf_x.hmm_arm_mdl"), "the mesh's own name decides, not its package");

    // The growth about a pivot: a vertex skinned to the bone goes to Q + (1 + a) (v - Q), Q the pivot where it's drawn.
    ik::Transform bind;
    bind.rotation = ik::Normalize(ik::Quat{0.1, 0.2, 0.3, 0.9});
    bind.translation = {6.5, 7.5, 133.5};
    ik::Vec3 const offset{0.0, 0.0, 5.0};                 // the pivot 5 units above the bone in the body's frame
    ik::Vec3 const q = club::PivotLocal(bind, offset);
    ik::Vec3 const pivotAtBind = bind.translation + offset;
    Check(ik::Length(ik::Apply(bind, q) - pivotAtBind) < 1e-9, "the pivot's local point is the body-frame offset at bind");
    double const grow = 0.3;
    ik::Vec3 const v{13.0, 8.0, 131.0};                    // a vertex on the breast, below and in front of the pivot
    ik::Vec3 const local = ik::Apply(ik::Inverse(bind), v);
    ik::Vec3 const atBind = ik::Apply(club::Grow(bind, grow, q), local);
    ik::Vec3 const want = pivotAtBind + (v - pivotAtBind) * (1.0 + grow);
    Check(ik::Length(atBind - want) < 1e-9, "at bind a vertex lands at Q + (1 + a) (v - Q)");
    Check(atBind.z < v.z && atBind.x > v.x, "a vertex below and in front of the pivot grows forward and down");
    ik::Vec3 const top = pivotAtBind + ik::Vec3{2.0, 0.0, 0.0};
    Check(ik::Apply(club::Grow(bind, grow, q), ik::Apply(ik::Inverse(bind), top)).z <= top.z + 1e-9, "nothing grows above the pivot's height");
    Check(ik::Length(ik::Apply(club::Grow(bind, 0.0, q), local) - v) < 1e-9, "a = 0 draws the vertex where it was");

    // It follows the bone: posed elsewhere (turned and moved, any scale), the grown vertex is the posed vertex grown about the
    // posed pivot.
    ik::Transform posed;
    posed.rotation = ik::Normalize(ik::Quat{-0.3, 0.5, 0.1, 0.8});
    posed.translation = {40.0, -12.0, 90.0};
    posed.scale = 1.1;
    ik::Vec3 const vPosed = ik::Apply(posed, local);
    ik::Vec3 const qPosed = ik::Apply(posed, q);
    ik::Vec3 const grown = ik::Apply(club::Grow(posed, grow, q), local);
    Check(ik::Length(grown - (qPosed + (vPosed - qPosed) * (1.0 + grow))) < 1e-9, "the growth follows the bone");
    Check(Near(club::Grow(posed, grow, q).scale, 1.1 * 1.3), "the bone's scale grows by 1 + a");
    // The rig index's per-body cap, in either field order; none without the field.
    {
        std::string const text = R"({"format": "mevr-rig-index", "version": 1, "package": "MEVR_Bodies_LE2", "meshes": [
  {"path": "BIOG_HMF_ARM_NKD_R.NKDb.HMF_ARM_NKDb_MDL", "name": "HMF_ARM_NKDb_MDL_MEVR", "breastMax": 1.18},
  {"breastMax": 1.31, "path": "BIOG_HMF_ARM_CTH_R.CTHd.HMF_ARM_CTHd_MDL", "name": "HMF_ARM_CTHd_MDL_MEVR"},
  {"path": "BIOG_ASA_ARM_LIA_R.LIAd.ASA_ARM_LIAd_MDL", "name": "ASA_ARM_LIAd_MDL_MEVR"}
]})";
        auto const index = mevr::ik::body_stand_in::Index::Parse(text);
        Check(index.has_value(), "an index with breastMax parses");
        if (index) {
            auto const a = index->BreastMaxFor("biog_hmf_arm_nkd_r.nkdb.hmf_arm_nkdb_mdl");
            auto const b = index->BreastMaxFor("biog_hmf_arm_cth_r.cthd.hmf_arm_cthd_mdl");
            Check(a && Near(*a, 1.18), "breastMax after the name");
            Check(b && Near(*b, 1.31), "breastMax before the path");
            Check(!index->BreastMaxFor("biog_asa_arm_lia_r.liad.asa_arm_liad_mdl"), "no breastMax: none");
            Check(!index->BreastMaxFor("biog_nothing.x.y"), "an unlisted mesh: none");
        }
    }

    if (gFail == 0) std::printf("club boost: all passed\n");
    return gFail;
}
