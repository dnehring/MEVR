// SPDX-License-Identifier: MIT
// Offline tests for rigged bodies by path (core/ik/BodyStandIn): the rig tool's index read and matched (a path as written and
// without the census's file part, two rigs on one shorter path), and the cache that asks the engine once per mesh and level,
// never for a mesh the index lacks, and stops loading once the bodies' package is known to be missing. Made-up objects.

#include "core/ik/BodyStandIn.hpp"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/BodyStandInTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace bsi = mevr::ik::body_stand_in;

// As tools/rig's Rigging.cs WriteIndex writes it.
constexpr char kIndex[] = R"({
  "format": "mevr-rig-index",
  "version": 1,
  "package": "MEVR_Bodies_LE3",
  "meshes": [
    { "path": "BioH_Liara_00.BIOG_ASA_ARM_LIA_R.LIAa.ASA_ARM_LIAa_MDL", "name": "ASA_ARM_LIAa_MDL_MEVR" },
    { "path": "BioH_Liara_00_NC.BIOG_ASA_ARM_LIA_R.LIAa.ASA_ARM_LIAa_MDL", "name": "ASA_ARM_LIAa_MDL_MEVR" },
    { "path": "BioD_Nor_130RomLiara.ASA_ARM_LIAd_MDL", "name": "ASA_ARM_LIAd_MDL_MEVR" },
    { "path": "BIOG_HMF_ARM_CTH_R.CTHb.HMF_ARM_CTHb_MDL", "name": "HMF_ARM_CTHb_MDL_MEVR" },
    { "path": "BioH_Jack_00.BIOG_HMF_JCK_LGT_R.LGTa.HMF_JCK_LGTa_MDL", "name": "HMF_JCK_LGTa_MDL_8007_MEVR" },
    { "path": "BioH_Jack_01.BIOG_HMF_JCK_LGT_R.LGTa.HMF_JCK_LGTa_MDL", "name": "HMF_JCK_LGTa_MDL_7054_MEVR" }
  ]
}
)";

void TestIndex() {
    std::string error;
    auto const index = bsi::Index::Parse(kIndex, &error);
    CHECK(index.has_value());
    if (!index) return;
    CHECK(index->Package() == "MEVR_Bodies_LE3");
    CHECK(index->Size() == 6);
    // As written, any case.
    CHECK(index->RigFor("biod_nor_130romliara.asa_arm_liad_mdl") == "mevr_bodies_le3.asa_arm_liad_mdl_mevr");
    CHECK(index->RigFor("BIOG_HMF_ARM_CTH_R.CTHb.HMF_ARM_CTHb_MDL") == "mevr_bodies_le3.hmf_arm_cthb_mdl_mevr");
    // Without the census's file part: a forced export's path in the game's memory.
    CHECK(index->RigFor("biog_asa_arm_lia_r.liaa.asa_arm_liaa_mdl") == "mevr_bodies_le3.asa_arm_liaa_mdl_mevr");
    // Two rigs on one shorter path: only the exact paths find them.
    CHECK(index->RigFor("biog_hmf_jck_lgt_r.lgta.hmf_jck_lgta_mdl").empty());
    CHECK(index->RigFor("bioh_jack_01.biog_hmf_jck_lgt_r.lgta.hmf_jck_lgta_mdl") == "mevr_bodies_le3.hmf_jck_lgta_mdl_7054_mevr");
    // Never by the name alone, and never a shorter path that is only a name.
    CHECK(index->RigFor("asa_arm_liad_mdl").empty());
    CHECK(index->RigFor("biog_other_r.liad.asa_arm_liad_mdl").empty());

    CHECK(!bsi::Index::Parse("{}", &error));
    CHECK(!bsi::Index::Parse(R"({"format": "mevr-rig-index", "version": 2, "package": "P", "meshes": [{"path": "a.b", "name": "c"}]})", &error));
    CHECK(!bsi::Index::Parse(R"({"format": "mevr-rig-index", "version": 1, "package": "P", "meshes": []})", &error));
    CHECK(!bsi::Index::Parse(R"({"format": "mevr-rig-index", "version": 1, "package": "P", "meshes": [{"path": "a.b"}]})", &error));

    CHECK(bsi::ObjectName("SkeletalMesh BioD_Nor_130RomLiara.ASA_ARM_LIAd_MDL") == "asa_arm_liad_mdl");
    CHECK(bsi::IsStandIn("HMF_ARM_MIRa_MDL_MEVR") && !bsi::IsStandIn("hmf_arm_mira_mdl"));
    CHECK(bsi::PackageName("LE1") == "mevr_bodies_le1");
}

struct FakeEngine {
    std::map<std::string, int> inMemory;   // path -> object id
    std::map<std::string, int> inPackage;  // path -> object id (loaded on request)
    bool installed = true;
    bool packageInMemory = false;
    int finds = 0;
    int loads = 0;
    std::map<int, bool> dead;

    bsi::Engine Callbacks() {
        bsi::Engine e;
        e.findLoaded = [this](std::string const& path) -> void* {
            ++finds;
            auto it = inMemory.find(path);
            return it != inMemory.end() ? reinterpret_cast<void*>(static_cast<std::intptr_t>(it->second)) : nullptr;
        };
        e.load = [this](std::string const& path) -> void* {
            ++loads;
            if (!installed) return nullptr;
            packageInMemory = true;
            auto it = inPackage.find(path);
            return it != inPackage.end() ? reinterpret_cast<void*>(static_cast<std::intptr_t>(it->second)) : nullptr;
        };
        e.packageLoaded = [this] { return packageInMemory; };
        e.alive = [this](void* o) { return !dead[static_cast<int>(reinterpret_cast<std::intptr_t>(o))]; };
        return e;
    }
};

void* Id(int id) { return reinterpret_cast<void*>(static_cast<std::intptr_t>(id)); }

void TestCache() {
    FakeEngine fake;
    fake.inPackage["mevr_bodies_le3.asa_arm_liaa_mdl_mevr"] = 7;
    fake.inPackage["mevr_bodies_le3.hmf_arm_cthb_mdl_mevr"] = 9;
    bsi::Engine const engine = fake.Callbacks();
    bsi::Cache cache;
    cache.SetIndex(bsi::Index::Parse(kIndex));

    // Found by the loader once, then from the cache.
    CHECK(cache.Find("biog_asa_arm_lia_r.liaa.asa_arm_liaa_mdl", engine) == Id(7));
    CHECK(cache.Find("biog_asa_arm_lia_r.liaa.asa_arm_liaa_mdl", engine) == Id(7));
    CHECK(fake.loads == 1 && fake.finds == 1);
    CHECK(cache.Entries().at("biog_asa_arm_lia_r.liaa.asa_arm_liaa_mdl").loaded);

    // A mesh the index lacks never reaches the engine.
    CHECK(cache.Find("biog_hmm_arm_shp_r.a.hmm_arm_shpa_mdl", engine) == nullptr);
    CHECK(cache.Find("biog_hmf_hir_pro.hair_promiranda.hmf_hir_promiranda_mdl", engine) == nullptr);
    CHECK(fake.loads == 1 && fake.finds == 1);
    CHECK(cache.Entries().at("biog_hmm_arm_shp_r.a.hmm_arm_shpa_mdl").outcome == bsi::Outcome::NotRigged);

    // Ours is never stood in for.
    CHECK(cache.Find("mevr_bodies_le3.asa_arm_liaa_mdl_mevr", engine) == nullptr);
    CHECK(cache.Entries().at("mevr_bodies_le3.asa_arm_liaa_mdl_mevr").outcome == bsi::Outcome::Skipped);

    // Listed but not in the package (an older build): asked once, the package being in memory.
    CHECK(cache.Find("biod_nor_130romliara.asa_arm_liad_mdl", engine) == nullptr);
    CHECK(cache.Find("biod_nor_130romliara.asa_arm_liad_mdl", engine) == nullptr);
    CHECK(fake.loads == 2);
    CHECK(cache.Entries().at("biod_nor_130romliara.asa_arm_liad_mdl").outcome == bsi::Outcome::NotLoaded);
    CHECK(!cache.PackageAbsent());

    // Already in memory: no load.
    fake.inMemory["mevr_bodies_le3.hmf_arm_cthb_mdl_mevr"] = 9;
    CHECK(cache.Find("biog_hmf_arm_cth_r.cthb.hmf_arm_cthb_mdl", engine) == Id(9));
    CHECK(fake.loads == 2 && !cache.Entries().at("biog_hmf_arm_cth_r.cthb.hmf_arm_cthb_mdl").loaded);

    // A found object gone: asked again.
    fake.dead[7] = true;
    fake.inPackage["mevr_bodies_le3.asa_arm_liaa_mdl_mevr"] = 8;
    CHECK(cache.Find("biog_asa_arm_lia_r.liaa.asa_arm_liaa_mdl", engine) == Id(8));

    // A new level: asked again, the index kept.
    cache.Forget();
    CHECK(cache.Entries().empty() && cache.Loads() == 0 && cache.GetIndex() != nullptr);
}

void TestNoPackage() {
    // No index: nothing asked.
    FakeEngine fake;
    bsi::Engine const engine = fake.Callbacks();
    bsi::Cache none;
    CHECK(none.Find("biog_asa_arm_lia_r.liaa.asa_arm_liaa_mdl", engine) == nullptr);
    CHECK(none.Entries().at("biog_asa_arm_lia_r.liaa.asa_arm_liaa_mdl").outcome == bsi::Outcome::NoPackage);
    CHECK(fake.finds == 0 && fake.loads == 0);

    // An index but the package not installed: the first failed load stops all loads this level.
    fake.installed = false;
    bsi::Cache cache;
    cache.SetIndex(bsi::Index::Parse(kIndex));
    CHECK(cache.Find("biog_asa_arm_lia_r.liaa.asa_arm_liaa_mdl", engine) == nullptr);
    CHECK(cache.PackageAbsent());
    CHECK(cache.Find("biog_hmf_arm_cth_r.cthb.hmf_arm_cthb_mdl", engine) == nullptr);
    CHECK(fake.loads == 1);
    CHECK(cache.Entries().at("biog_hmf_arm_cth_r.cthb.hmf_arm_cthb_mdl").outcome == bsi::Outcome::NoPackage);
    // The next level tries the loader again.
    cache.Forget();
    CHECK(cache.Find("biog_hmf_arm_cth_r.cthb.hmf_arm_cthb_mdl", engine) == nullptr);
    CHECK(fake.loads == 2);
}

} // namespace

int RunBodyStandInTests() {
    gFailures = 0;
    TestIndex();
    TestCache();
    TestNoPackage();
    std::printf("body stand-in tests: %d failure(s)\n", gFailures);
    return gFailures;
}
