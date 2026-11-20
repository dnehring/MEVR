// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Rigged bodies by path (docs/trilogy-hair-plan.md section 12 step 3): tools/rig builds one package per game,
// MEVR_Bodies_<GAME>.pcc, holding every female body it rigged with soft-tissue bones, and beside it an index,
// MEVR_Bodies_<GAME>.json (`mevr-rig-index` version 1), mapping each game mesh's object path to its rig's name in the package.
// Names aren't unique (ME2 has two HMF_JCK_LGTa_MDL), so a body is looked up by its mesh's full path. game/Hair puts the rig
// on any body whose mesh the index lists (the hand table, Miranda's, first). Engine-agnostic: the game side passes the
// engine's lookups as callbacks. tests/BodyStandInTests.cpp.
namespace mevr::ik::body_stand_in {

// The object's own name in an object path ("biog_hmf_arm_mir_r.mira.hmf_arm_mira_mdl" -> "hmf_arm_mira_mdl"), lower case.
std::string ObjectName(std::string_view path);

// True for a mesh of ours (its name ends in "_mevr"): a stand-in is never stood in for.
bool IsStandIn(std::string_view objectName);

// The bodies' package for a game ("LE3" -> "mevr_bodies_le3"), lower case.
std::string PackageName(std::string_view game);

// The package's index. A path is matched lower case, as written, and also without its first part (the file the census found
// the copy in: "bioh_liara_00.biog_asa_arm_lia_r.liaa.asa_arm_liaa_mdl" is "biog_asa_arm_lia_r.liaa.asa_arm_liaa_mdl" in the
// game's memory, a forced export's own package first) unless two rigs claim that shorter path.
class Index {
public:
    // Nullopt (and `error`) if the text isn't a `mevr-rig-index` version 1.
    static std::optional<Index> Parse(std::string_view text, std::string* error = nullptr);

    std::string const& Package() const { return package_; }  // as written ("MEVR_Bodies_LE3")
    std::size_t Size() const { return exact_.size(); }
    // The rig's object path in the package (lower case: "mevr_bodies_le3.hmf_arm_mira_mdl_mevr"), or empty.
    std::string RigFor(std::string_view meshPath) const;

private:
    std::string package_;
    std::unordered_map<std::string, std::string> exact_;    // lower path -> rig name
    std::unordered_map<std::string, std::string> shorter_;  // lower path without its first part -> rig name ("" ambiguous)
};

// The engine's side of a lookup (game thread). Each returns an object or nullptr.
struct Engine {
    std::function<void*(std::string const& path)> findLoaded;  // a loaded skeletal mesh at this object path (lower case)
    std::function<void*(std::string const& path)> load;        // the engine's loader (DynamicLoadObject), may fail
    std::function<bool()> packageLoaded;                       // the bodies' package is in memory
    std::function<bool(void*)> alive;                          // a kept object is still the one we found
};

// What a lookup came to, per game mesh (`mevr hair bodies`).
enum class Outcome { Found, NotRigged, NotLoaded, NoPackage, Skipped };
char const* Describe(Outcome outcome);

struct Entry {
    Outcome outcome = Outcome::NotRigged;
    std::string rig;  // the rig's path in the package (empty: the index has none for this mesh)
    void* object = nullptr;
    bool loaded = false;  // found by the loader rather than already in memory
};

// The stand-ins found or missed in one level, per game mesh path: each mesh is asked once (a miss is kept until the level
// changes); a mesh the index doesn't list never reaches the engine; once the loader has failed with the bodies' package not
// in memory, nothing more is loaded this level.
class Cache {
public:
    // The index read (or none: every body NoPackage).
    void SetIndex(std::optional<Index> index) { index_ = std::move(index); }
    Index const* GetIndex() const { return index_ ? &*index_ : nullptr; }

    // The stand-in for the game mesh at `meshPath` (lower case), or nullptr.
    void* Find(std::string const& meshPath, Engine const& engine);
    // A new level: everything is asked again (the index is kept).
    void Forget();

    std::unordered_map<std::string, Entry> const& Entries() const { return entries_; }
    bool PackageAbsent() const { return packageAbsent_; }
    int Loads() const { return loads_; }

private:
    std::optional<Index> index_;
    std::unordered_map<std::string, Entry> entries_;
    bool packageAbsent_ = false;
    int loads_ = 0;
};

} // namespace mevr::ik::body_stand_in
