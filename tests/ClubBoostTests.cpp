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
            Check(index->ClubRigFor("biog_hmf_arm_nkd_r.nkdb.hmf_arm_nkdb_mdl").empty(), "no club field: no club copy");
            Check(!index->ClubBreastAFor("biog_hmf_arm_nkd_r.nkdb.hmf_arm_nkdb_mdl"), "no clubBreastA field: none");
        }
    }

    // The club copy's fields (the owner's option 1, 2026-11-20): "club" and "clubBreastA" beside breastMax, any order, and an
    // entry without them beside one with them.
    {
        std::string const text = R"({"format": "mevr-rig-index", "version": 1, "package": "MEVR_Bodies_LE2", "meshes": [
  {"path": "BIOG_HMF_ARM_NKD_R.NKDb.HMF_ARM_NKDb_MDL", "name": "HMF_ARM_NKDb_MDL_MEVR", "breastMax": 1.18,
   "club": "HMF_ARM_NKDb_MDL_CLUB_MEVR", "clubBreastA": 0.126},
  {"clubBreastA": 0.5, "club": "HMF_ARM_CTHd_MDL_CLUB_MEVR", "path": "BIOG_HMF_ARM_CTH_R.CTHd.HMF_ARM_CTHd_MDL",
   "name": "HMF_ARM_CTHd_MDL_MEVR", "breastMax": 1.76},
  {"path": "BIOG_ASA_ARM_LIA_R.LIAd.ASA_ARM_LIAd_MDL", "name": "ASA_ARM_LIAd_MDL_MEVR", "breastMax": 1.10}
]})";
        auto const index = mevr::ik::body_stand_in::Index::Parse(text);
        Check(index.has_value(), "an index with club copies parses");
        if (index) {
            Check(index->Size() == 3, "three meshes, the club copies not counted as meshes");
            Check(index->ClubRigFor("biog_hmf_arm_nkd_r.nkdb.hmf_arm_nkdb_mdl") == "mevr_bodies_le2.hmf_arm_nkdb_mdl_club_mevr",
                "the club copy after the name");
            Check(index->RigFor("biog_hmf_arm_nkd_r.nkdb.hmf_arm_nkdb_mdl") == "mevr_bodies_le2.hmf_arm_nkdb_mdl_mevr",
                "the plain rig unchanged beside its club copy");
            Check(index->ClubRigFor("biog_hmf_arm_cth_r.cthd.hmf_arm_cthd_mdl") == "mevr_bodies_le2.hmf_arm_cthd_mdl_club_mevr",
                "the club copy before the path");
            auto const a = index->ClubBreastAFor("biog_hmf_arm_nkd_r.nkdb.hmf_arm_nkdb_mdl");
            auto const b = index->ClubBreastAFor("biog_hmf_arm_cth_r.cthd.hmf_arm_cthd_mdl");
            Check(a && Near(*a, 0.126), "clubBreastA after club");
            Check(b && Near(*b, 0.5), "clubBreastA first");
            auto const cap = index->BreastMaxFor("biog_hmf_arm_cth_r.cthd.hmf_arm_cthd_mdl");
            Check(cap && Near(*cap, 1.76), "breastMax still read after the club fields");
            Check(index->ClubRigFor("biog_asa_arm_lia_r.liad.asa_arm_liad_mdl").empty(), "an entry without club: no club copy");
            Check(!index->ClubBreastAFor("biog_asa_arm_lia_r.liad.asa_arm_liad_mdl"), "an entry without clubBreastA: none");
            Check(index->ClubRigFor("biog_nothing.x.y").empty(), "an unlisted mesh: no club copy");
        }
        // The club cache asks for the club copy, the plain cache for the rig.
        if (index) {
            namespace bsi = mevr::ik::body_stand_in;
            std::string asked;
            bsi::Engine engine;
            engine.findLoaded = [&](std::string const& path) -> void* { asked = path; return reinterpret_cast<void*>(0x10); };
            bsi::Cache plain;
            bsi::Cache clubCache(true);
            plain.SetIndex(index);
            clubCache.SetIndex(index);
            plain.Find("biog_hmf_arm_nkd_r.nkdb.hmf_arm_nkdb_mdl", engine);
            Check(asked == "mevr_bodies_le2.hmf_arm_nkdb_mdl_mevr", "the plain cache finds the rig");
            clubCache.Find("biog_hmf_arm_nkd_r.nkdb.hmf_arm_nkdb_mdl", engine);
            Check(asked == "mevr_bodies_le2.hmf_arm_nkdb_mdl_club_mevr", "the club cache finds the club copy");
            asked.clear();
            Check(clubCache.Find("biog_asa_arm_lia_r.liad.asa_arm_liad_mdl", engine) == nullptr && asked.empty(),
                "no club copy: the engine is never asked");
        }
    }

    // A dancer's variation on her club copy: within +-vary, the same for the same key, spread, and the breasts held at the cap.
    {
        std::set<long long> seen;
        for (int i = 0; i < 40; ++i) {
            std::string const key = "biod_cithub_wardsflux.theworld.persistentlevel.sfxskeletalmeshactor_" + std::to_string(i) +
                                    "|biog_hmf_arm_cth_r.cthc.hmf_arm_cthc_mdl";
            club::Sizes const var = club::VariationFor(key, 1.60, 0.40, 0.06, 0.03);
            club::Sizes const again = club::VariationFor(key, 1.60, 0.40, 0.06, 0.03);
            Check(var.breast == again.breast && var.glute == again.glute, "a dancer keeps her variation");
            Check(var.breast >= -0.06 - 1e-12 && var.breast <= 0.06 + 1e-12, "breast variation within +-0.06");
            Check(var.glute >= -0.03 - 1e-12 && var.glute <= 0.03 + 1e-12, "glute variation within +-0.03");
            Check((1.0 + 0.40) * (1.0 + var.breast) <= 1.60 + 1e-9, "baked and variation within the cap");
            seen.insert(std::llround(var.breast * 10000.0));
        }
        Check(seen.size() >= 20, "variations differ between dancers");
        // A club copy already at its cap: the variation never grows it, only shrinks.
        for (int i = 0; i < 40; ++i) {
            club::Sizes const var = club::VariationFor("k" + std::to_string(i), 1.20, 0.20, 0.06, 0.03);
            Check(var.breast <= 1e-9, "at the cap the variation only shrinks");
            Check((1.0 + 0.20) * (1.0 + var.breast) <= 1.20 + 1e-9, "at the cap: within it");
        }
        // Nothing baked listed (< 0): the variation against the cap alone.
        club::Sizes const none = club::VariationFor("k", 1.02, -1.0, 0.06, 0.03);
        Check(none.breast <= 0.02 + 1e-9, "no baked amount: the cap alone holds it");
        club::Sizes const zero = club::VariationFor("k", 1.6, 0.4, 0.0, 0.0);
        Check(Near(zero.breast, 0.0) && Near(zero.glute, 0.0), "no variation asked: none");
    }

    // The uniform fallback: gentler than SizesFor, the breasts at most 0.15, the glutes at most 0.08, still spread, never past
    // the cap.
    {
        club::SizeSettings settings2;
        std::set<long long> seen;
        for (int i = 0; i < 40; ++i) {
            std::string const key = "p" + std::to_string(i);
            club::Sizes const f = club::FallbackSizes(key, 1.60, settings2, 0.15, 0.08);
            club::Sizes const full = club::SizesFor(key, 1.60, settings2);
            Check(f.breast <= 0.15 + 1e-12 && f.breast > 0.0, "fallback breasts in (0, 0.15]");
            Check(f.glute <= 0.08 + 1e-12 && f.glute > 0.0, "fallback glutes in (0, 0.08]");
            Check(f.breast <= full.breast + 1e-12 && f.glute <= full.glute + 1e-12, "the fallback is gentler");
            seen.insert(std::llround(f.breast * 10000.0));
        }
        Check(seen.size() >= 20, "fallback amounts differ between people");
        Check(club::FallbackSizes("x", 1.05, settings2, 0.15, 0.08).breast <= 0.05 + 1e-12, "a small cap holds the fallback too");
    }

    if (gFail == 0) std::printf("club boost: all passed\n");
    return gFail;
}
