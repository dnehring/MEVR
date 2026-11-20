// SPDX-License-Identifier: MIT
#include "core/ik/BodyStandIn.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <system_error>

namespace mevr::ik::body_stand_in {
namespace {

std::string Lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool EndsWith(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

// The path without its first part, if what is left still has a package ("a.b.c" -> "b.c"; "a.b" -> "").
std::string Shorter(std::string const& path) {
    std::size_t const dot = path.find('.');
    if (dot == std::string::npos || path.find('.', dot + 1) == std::string::npos) {
        return {};
    }
    return path.substr(dot + 1);
}

// A tiny reader for the index's JSON: the string after `"key":` from `at`, and where it ended. The rig tool writes paths and
// names without escapes but `\"` and `\\` are read.
std::optional<std::string> StringAfter(std::string_view text, std::string_view key, std::size_t& at) {
    std::string const quoted = "\"" + std::string(key) + "\"";
    std::size_t k = text.find(quoted, at);
    if (k == std::string_view::npos) return std::nullopt;
    std::size_t i = k + quoted.size();
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
    if (i >= text.size() || text[i] != ':') return std::nullopt;
    ++i;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
    if (i >= text.size() || text[i] != '"') return std::nullopt;
    std::string out;
    for (++i; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) ++i;
        out += text[i];
    }
    if (i >= text.size()) return std::nullopt;
    at = i + 1;
    return out;
}

// The number after `"key":` inside [from, to), or nullopt.
std::optional<double> NumberIn(std::string_view text, std::string_view key, std::size_t from, std::size_t to) {
    std::string const quoted = "\"" + std::string(key) + "\"";
    std::size_t const k = text.find(quoted, from);
    if (k == std::string_view::npos || k >= to) return std::nullopt;
    std::size_t i = text.find(':', k + quoted.size());
    if (i == std::string_view::npos || i >= to) return std::nullopt;
    ++i;
    while (i < to && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
    std::size_t j = i;
    while (j < to && (std::isdigit(static_cast<unsigned char>(text[j])) || text[j] == '.' || text[j] == '-' || text[j] == '+' ||
                         text[j] == 'e' || text[j] == 'E')) ++j;
    if (j == i) return std::nullopt;
    double value = 0.0;
    auto const [end, ec] = std::from_chars(text.data() + i, text.data() + j, value);
    if (ec != std::errc{} || end != text.data() + j) return std::nullopt;
    return value;
}

// The string after `"key":` inside [from, to), or nullopt.
std::optional<std::string> StringIn(std::string_view text, std::string_view key, std::size_t from, std::size_t to) {
    std::string const quoted = "\"" + std::string(key) + "\"";
    std::size_t const k = text.find(quoted, from);
    if (k == std::string_view::npos || k >= to) return std::nullopt;
    std::size_t at = k;
    std::optional<std::string> value = StringAfter(text.substr(0, to), key, at);
    return value;
}

} // namespace

std::string ObjectName(std::string_view path) {
    std::size_t const dot = path.find_last_of(". ");
    return Lower(dot == std::string_view::npos ? path : path.substr(dot + 1));
}

bool IsStandIn(std::string_view objectName) {
    return EndsWith(Lower(objectName), "_mevr");
}

std::string PackageName(std::string_view game) {
    return "mevr_bodies_" + Lower(game);
}

std::optional<Index> Index::Parse(std::string_view text, std::string* error) {
    auto fail = [&](char const* why) -> std::optional<Index> {
        if (error != nullptr) *error = why;
        return std::nullopt;
    };
    std::size_t at = 0;
    std::optional<std::string> const format = StringAfter(text, "format", at);
    if (!format || *format != "mevr-rig-index") return fail("not a mevr-rig-index");
    std::size_t const version = text.find("\"version\"");
    std::size_t const digit = version == std::string_view::npos ? version : text.find_first_of("0123456789", version);
    if (digit == std::string_view::npos || text[digit] != '1' ||
        (digit + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[digit + 1])))) {
        return fail("not version 1");
    }
    at = 0;
    std::optional<std::string> const package = StringAfter(text, "package", at);
    if (!package || package->empty()) return fail("no package");
    std::size_t const meshes = text.find("\"meshes\"");
    if (meshes == std::string_view::npos) return fail("no meshes");
    Index index;
    index.package_ = *package;
    at = meshes;
    while (true) {
        std::size_t probe = at;
        std::size_t const pathKey = text.find("\"path\"", at);
        std::optional<std::string> const path = StringAfter(text, "path", probe);
        if (!path) break;
        std::optional<std::string> const name = StringAfter(text, "name", probe);
        if (!name) return fail("a mesh without a name");
        at = probe;
        std::string const key = Lower(*path);
        std::string const rig = Lower(*name);
        index.exact_[key] = rig;
        // This entry's own fields lie between its braces, in any order.
        std::size_t const open = pathKey == std::string_view::npos ? pathKey : text.rfind('{', pathKey);
        std::size_t const close = text.find('}', probe);
        if (open != std::string_view::npos && close != std::string_view::npos) {
            if (std::optional<double> const cap = NumberIn(text, "breastMax", open, close)) {
                index.breastMax_[rig] = *cap;
            }
            if (std::optional<std::string> const club = StringIn(text, "club", open, close); club && !club->empty()) {
                index.club_[rig] = Lower(*club);
            }
            if (std::optional<double> const baked = NumberIn(text, "clubBreastA", open, close)) {
                index.clubBreastA_[rig] = *baked;
            }
        }
        if (std::string const shorter = Shorter(key); !shorter.empty()) {
            auto [it, fresh] = index.shorter_.emplace(shorter, rig);
            if (!fresh && it->second != rig) it->second.clear();  // two rigs: only exact paths find them
        }
    }
    if (index.exact_.empty()) return fail("no meshes");
    return index;
}

std::string Index::NameFor(std::string_view meshPath) const {
    std::string const key = Lower(meshPath);
    if (auto it = exact_.find(key); it != exact_.end()) {
        return it->second;
    }
    if (auto s = shorter_.find(key); s != shorter_.end() && !s->second.empty()) {
        return s->second;
    }
    return {};
}

std::string Index::RigFor(std::string_view meshPath) const {
    std::string const name = NameFor(meshPath);
    return name.empty() ? std::string() : Lower(package_) + "." + name;
}

std::optional<double> Index::BreastMaxFor(std::string_view meshPath) const {
    std::string const name = NameFor(meshPath);
    if (auto it = breastMax_.find(name); !name.empty() && it != breastMax_.end()) return it->second;
    return std::nullopt;
}

std::string Index::ClubRigFor(std::string_view meshPath) const {
    std::string const name = NameFor(meshPath);
    if (auto it = club_.find(name); !name.empty() && it != club_.end()) return Lower(package_) + "." + it->second;
    return {};
}

std::optional<double> Index::ClubBreastAFor(std::string_view meshPath) const {
    std::string const name = NameFor(meshPath);
    if (auto it = clubBreastA_.find(name); !name.empty() && it != clubBreastA_.end()) return it->second;
    return std::nullopt;
}

char const* Describe(Outcome outcome) {
    switch (outcome) {
    case Outcome::Found: return "rigged body found";
    case Outcome::NotRigged: return "not in the bodies' index (not a rigged body)";
    case Outcome::NotLoaded: return "in the index, but the package has no such rig";
    case Outcome::NoPackage: return "the bodies' package isn't installed";
    case Outcome::Skipped: return "one of ours";
    }
    return "?";
}

void* Cache::Find(std::string const& meshPath, Engine const& engine) {
    auto it = entries_.find(meshPath);
    if (it != entries_.end()) {
        Entry& e = it->second;
        if (e.outcome != Outcome::Found) {
            return nullptr;
        }
        if (e.object != nullptr && (!engine.alive || engine.alive(e.object))) {
            return e.object;
        }
        entries_.erase(it);  // the object went away (a level unloaded under us): ask again
    }
    Entry entry;
    if (IsStandIn(ObjectName(meshPath))) {
        entry.outcome = Outcome::Skipped;
    } else if (!index_) {
        entry.outcome = Outcome::NoPackage;
    } else {
        entry.rig = club_ ? index_->ClubRigFor(meshPath) : index_->RigFor(meshPath);
        if (!entry.rig.empty()) {
            entry.object = engine.findLoaded ? engine.findLoaded(entry.rig) : nullptr;
            if (entry.object == nullptr && !packageAbsent_ && engine.load) {
                ++loads_;
                entry.object = engine.load(entry.rig);
                entry.loaded = entry.object != nullptr;
                if (entry.object == nullptr && (!engine.packageLoaded || !engine.packageLoaded())) {
                    packageAbsent_ = true;  // not installed (or not in the TOC): no more loads until the level changes
                }
            }
            entry.outcome = entry.object != nullptr ? Outcome::Found : (packageAbsent_ ? Outcome::NoPackage : Outcome::NotLoaded);
        }
    }
    void* const found = entry.object;
    entries_[meshPath] = std::move(entry);
    return found;
}

void Cache::Forget() {
    entries_.clear();
    packageAbsent_ = false;
    loads_ = 0;
}

} // namespace mevr::ik::body_stand_in
