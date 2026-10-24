// SPDX-License-Identifier: MIT
// Offline tests for research/34 stage 4: the RVA table's checks (core/ExeCheck) over synthetic images and, when it's on
// this machine, the decrypted LE1 .text dump (%LOCALAPPDATA%\MEVR\dump, local research data: read, never copied), and
// the hook context (core/Hook) through a fake SPI interface and through our MinHook.

#include "core/ExeCheck.hpp"
#include "core/Hook.hpp"
#include "engine/Addresses.hpp"
#include "ue3/Pattern.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ExeTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

using namespace mevr;
using exe::Kind;
using exe::Status;

// A synthetic .text at RVA 0x1000 with a few "functions" and a data reference, .pdata-like bounds for them.
struct Synthetic {
    std::vector<std::uint8_t> text = std::vector<std::uint8_t>(0x400, 0xCC);
    struct Function {
        std::uint32_t begin;
        std::uint32_t end;
    };
    std::vector<Function> functions;

    void Put(std::uint32_t rva, std::vector<std::uint8_t> const& bytes) {
        std::memcpy(text.data() + (rva - 0x1000), bytes.data(), bytes.size());
    }
    exe::Image Image() const {
        exe::Image image;
        image.textRva = 0x1000;
        image.text = text;
        image.functionAt = [this](std::uint32_t rva) {
            exe::Bounds b;
            for (Function const& f : functions) {
                if (rva >= f.begin && rva < f.end) {
                    b.found = true;
                    b.begin = f.begin;
                    b.end = f.end;
                }
            }
            return b;
        };
        image.isData = [](std::uint32_t rva) { return rva >= 0x8000 && rva < 0x9000; };
        return image;
    }
};

// mov rax, [rip + disp] at `site`, referencing `target`.
std::vector<std::uint8_t> LoadFrom(std::uint32_t site, std::uint32_t target) {
    std::int32_t const disp = static_cast<std::int32_t>(target) - static_cast<std::int32_t>(site + 7);
    std::vector<std::uint8_t> bytes{0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x48, 0x85, 0xC0, 0x74, 0x05};
    std::memcpy(bytes.data() + 3, &disp, 4);
    return bytes;
}

void TestSyntheticChecks() {
    Synthetic s;
    std::vector<std::uint8_t> const prologue{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xF9, 0xC3};
    s.Put(0x1100, prologue);
    s.functions.push_back({0x1100, 0x1180});
    s.Put(0x1200, LoadFrom(0x1200, 0x8010));
    s.functions.push_back({0x1200, 0x1240});
    s.Put(0x1300, {0x0F, 0xB6, 0xC1, 0xC3});  // a leaf: no bounds
    char const* const fn = "48 89 5C 24 08 57 48 83 EC 20 8B F9 C3";
    char const* const data = "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 05";
    exe::Image const image = s.Image();

    exe::Entry const function{"f", 0x1100, Kind::Function, fn, 0, 0, "test"};
    exe::Result r = exe::Check(image, function);
    CHECK(r.status == Status::AtRva && r.found == 0x1100);

    exe::Entry const global{"g", 0x8010, Kind::Data, data, 3, 0x1200, "test"};
    r = exe::Check(image, global);
    CHECK(r.status == Status::AtRva && r.found == 0x8010);

    exe::Entry const leaf{"l", 0x1300, Kind::Leaf, "0F B6 C1 C3", 0, 0, "test"};
    CHECK(exe::Check(image, leaf).status == Status::AtRva);
    exe::Entry const leafInside{"l2", 0x1104, Kind::Leaf, nullptr, 0, 0, "test"};
    CHECK(exe::Check(image, leafInside).status == Status::Missing);  // inside a function with unwind data

    exe::Entry const code{"c", 0x1105, Kind::Code, "08 57 48 83", 1, 0, "test"};
    CHECK(exe::Check(image, code).status == Status::AtRva);
    exe::Entry const end{"e", 0x1180, Kind::End, nullptr, 0, 0, "test"};
    CHECK(exe::Check(image, end).status == Status::AtRvaUnchecked);
    exe::Entry const notEnd{"e2", 0x1170, Kind::End, nullptr, 0, 0, "test"};
    CHECK(exe::Check(image, notEnd).status == Status::Missing);
    exe::Entry const notStart{"n", 0x1104, Kind::Function, nullptr, 0, 0, "test"};
    CHECK(exe::Check(image, notStart).status == Status::Missing);
    exe::Entry const dataInText{"d", 0x1100, Kind::Data, nullptr, 0, 0, "test"};
    CHECK(exe::Check(image, dataInText).status == Status::Missing);
    exe::Entry const dataOutside{"d2", 0x9100, Kind::Data, nullptr, 0, 0, "test"};
    CHECK(exe::Check(image, dataOutside).status == Status::Missing);
    exe::Entry const badPattern{"b", 0x1100, Kind::Function, "48 8", 0, 0, "test"};
    CHECK(exe::Check(image, badPattern).status == Status::AtRvaUnchecked);

    // Another module hooked the function: E9 rel32 over its first five bytes.
    Synthetic hooked = s;
    hooked.Put(0x1100, {0xE9, 0x11, 0x22, 0x33, 0x44});
    r = exe::Check(hooked.Image(), function);
    CHECK(r.status == Status::AtRvaHooked && r.found == 0x1100);
    // ...or more than its jump: the bytes differ, the structure holds, the pattern is nowhere else.
    hooked.Put(0x1105, {0x90});
    r = exe::Check(hooked.Image(), function);
    CHECK(r.status == Status::BytesDiffer && r.found == 0x1100 && exe::AtItsRva(r.status));

    // A patch moved the function and the global's reference.
    Synthetic moved;
    moved.Put(0x1150, prologue);
    moved.functions.push_back({0x1150, 0x11D0});
    moved.functions.push_back({0x1100, 0x1140});  // something else starts at the old RVA now
    moved.Put(0x1260, LoadFrom(0x1260, 0x8020));
    r = exe::Check(moved.Image(), function);
    CHECK(r.status == Status::Moved && r.found == 0x1150 && !exe::AtItsRva(r.status));
    r = exe::Check(moved.Image(), global);
    CHECK(r.status == Status::Moved && r.found == 0x8020);
    // The reference moved, the global didn't: still at its RVA.
    Synthetic sameGlobal;
    sameGlobal.Put(0x1260, LoadFrom(0x1260, 0x8010));
    CHECK(exe::Check(sameGlobal.Image(), global).status == Status::AtRva);
    // Two copies of a code pattern: ambiguous, so not "moved"; at a valid start it's kept with a warning.
    Synthetic twice = moved;
    twice.Put(0x1350, prologue);
    r = exe::Check(twice.Image(), function);
    CHECK(r.status == Status::BytesDiffer);
    // Gone entirely, and no function starts there.
    Synthetic gone;
    CHECK(exe::Check(gone.Image(), exe::Entry{"f", 0x1100, Kind::Function, fn, 0, 0, "test"}).status != Status::Moved);
    gone.functions.push_back({0x10F0, 0x1140});
    CHECK(exe::Check(gone.Image(), function).status == Status::Missing);

    CHECK(exe::HookJumpLength(std::vector<std::uint8_t>{0xE9, 0, 0, 0, 0}) == 5);
    CHECK(exe::HookJumpLength(std::vector<std::uint8_t>{0xEB, 0xF9}) == 2);
    CHECK(exe::HookJumpLength(std::vector<std::uint8_t>{0xFF, 0x25, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8}) == 14);
    CHECK(exe::HookJumpLength(std::vector<std::uint8_t>{0x48, 0x89, 0x5C}) == 0);
}

// Every table's entries: names, kinds, offsets that fit their patterns, no duplicate RVAs within a game.
template <std::size_t N>
void CheckTableShape(exe::Entry const (&table)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        exe::Entry const& e = table[i];
        CHECK(e.name != nullptr && e.name[0] != '\0' && e.rva != 0);
        if (e.pattern != nullptr) {
            auto const compiled = ue3::pattern::Compile(e.pattern);
            CHECK(compiled.has_value());
            if (compiled) {
                CHECK(e.offset <= compiled->bytes.size());
                if (e.kind == Kind::Data) {
                    CHECK(e.site != 0 && e.offset + 4 <= compiled->bytes.size());
                    for (std::size_t k = e.offset; k < e.offset + 4; ++k) {
                        CHECK(compiled->wild[k]);  // the displacement is a wildcard
                    }
                }
            }
        }
        for (std::size_t j = i + 1; j < N; ++j) {
            CHECK(table[j].rva != e.rva);
        }
    }
}

void TestTables() {
    CheckTableShape(engine::addresses::le1::kTable);
    CheckTableShape(engine::addresses::le2::kTable);
    CheckTableShape(engine::addresses::le3::kTable);
    // The named constants are entries (what the hooks resolve is what the gate checked).
    auto const listed = [](auto const& table, std::uint32_t rva) {
        for (exe::Entry const& e : table) {
            if (e.rva == rva) {
                return true;
            }
        }
        return rva == 0;  // not used for that game
    };
    namespace a1 = engine::addresses::le1;
    for (std::uint32_t rva : {a1::kProcessEvent, a1::kCallFunction, a1::kGameEngineTick, a1::kCalcSceneView,
             a1::kCalcSceneViewPrimaryReturn, a1::kViewportDraw, a1::kViewportDrawEnd, a1::kUpdateSkelPose,
             a1::kTryAcquireCover, a1::kCoverTrace, a1::kGObjects, a1::kNamePools, a1::kGMalloc, a1::kAppMalloc,
             a1::kAppFree, a1::kGNatives}) {
        CHECK(listed(a1::kTable, rva));
    }
    namespace a2 = engine::addresses::le2;
    for (std::uint32_t rva : {a2::kProcessEvent, a2::kCallFunction, a2::kGameEngineTick, a2::kCalcSceneView,
             a2::kViewportDraw, a2::kViewportDrawEnd}) {
        CHECK(listed(a2::kTable, rva));
    }
    namespace a3 = engine::addresses::le3;
    for (std::uint32_t rva : {a3::kProcessEvent, a3::kCallFunction, a3::kGameEngineTick, a3::kCalcSceneView,
             a3::kViewportDraw, a3::kViewportDrawEnd}) {
        CHECK(listed(a3::kTable, rva));
    }
}

// The LE1 table against the real decrypted .text (no .pdata or sections in the dump: bytes only).
void TestAgainstDump() {
    wchar_t local[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) == 0) {
        std::printf("exe tests: no LOCALAPPDATA; the LE1 dump check is skipped\n");
        return;
    }
    std::filesystem::path const path = std::filesystem::path(local) / L"MEVR" / L"dump" / L"MassEffect-60AEEFA2.text.bin";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::printf("exe tests: no LE1 .text dump at %s; checked against synthetic images only\n", path.string().c_str());
        return;
    }
    std::vector<std::uint8_t> const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.size() == 0xF9BB81);
    exe::Image image;
    image.textRva = 0x1000;  // MassEffect-60AEEFA2.txt: text_rva
    image.text = text;
    int patterned = 0;
    for (exe::Entry const& e : engine::addresses::le1::kTable) {
        exe::Result const r = exe::Check(image, e);
        bool const ok = e.pattern != nullptr ? r.status == Status::AtRva : r.status == Status::AtRvaUnchecked;
        if (!ok) {
            std::printf("FAIL exe tests: %s 0x%X against the dump: %s (%s)\n", e.name, e.rva, exe::StatusText(r.status),
                r.why.c_str());
            ++gFailures;
        }
        if (e.pattern == nullptr) {
            continue;
        }
        ++patterned;
        // Unique: one match for code, matches that all reference the RVA for data.
        auto const compiled = ue3::pattern::Compile(e.pattern);
        auto const hits = ue3::pattern::FindAll(image.text, *compiled, 16);
        if (e.kind != Kind::Data) {
            CHECK(hits.size() == 1);
        } else {
            CHECK(!hits.empty());
        }
    }
    // All code moved by 0x40 bytes, displacements unchanged (so each RIP-relative reference now points 0x40 further
    // too, unlike a real relink): every patterned entry is "moved" by exactly 0x40, found by its pattern.
    std::vector<std::uint8_t> shifted(text.size() + 0x40, 0xCC);
    std::memcpy(shifted.data() + 0x40, text.data(), text.size());
    exe::Image patched;
    patched.textRva = 0x1000;
    patched.text = shifted;
    int moved = 0;
    for (exe::Entry const& e : engine::addresses::le1::kTable) {
        if (e.pattern == nullptr) {
            continue;
        }
        exe::Result const r = exe::Check(patched, e);
        // Code moved 0x40; a RIP-relative reference moved with the code, so its target moved too.
        if (r.status == Status::Moved && r.found == e.rva + 0x40) {
            ++moved;
        } else {
            std::printf("FAIL exe tests: %s in the shifted image: %s 0x%X (%s)\n", e.name, exe::StatusText(r.status), r.found,
                r.why.c_str());
            ++gFailures;
        }
    }
    CHECK(moved == patterned);
    std::printf("exe tests: %d LE1 patterns checked against the dump\n", patterned);
}

// ---- core/Hook ------------------------------------------------------------------------------------------------

struct FakeSpi : loader::spi::Interface {
    std::vector<std::string> installed;
    std::vector<std::string> removed;
    bool refuse = false;
    loader::spi::Return GetVersion(unsigned long* out) override {
        *out = loader::spi::kVersion;
        return loader::spi::Return::Success;
    }
    loader::spi::Return GetBuildMode(bool* out) override {
        *out = true;
        return loader::spi::Return::Success;
    }
    loader::spi::Return GetHostGame(loader::spi::Game* out) override {
        *out = loader::spi::Game::LE1;
        return loader::spi::Return::Success;
    }
    loader::spi::Return FindPattern(void** out, char*) override {
        *out = nullptr;
        return loader::spi::Return::FailureGeneric;
    }
    loader::spi::Return InstallHook(char const* name, void* target, void*, void** original) override {
        if (refuse) {
            return loader::spi::Return::FailureHooking;
        }
        installed.emplace_back(name);
        *original = target;  // as the loader does: written before the detour goes live
        return loader::spi::Return::Success;
    }
    loader::spi::Return UninstallHook(char const* name) override {
        removed.emplace_back(name);
        return loader::spi::Return::Success;
    }
};

volatile int gSink = 0;

__declspec(noinline) int HookTarget(int x) {
    int y = x;
    for (int i = 0; i < 3; ++i) {
        y = y * 3 + gSink;
    }
    gSink = gSink + 1;
    return y + 7;
}

using tTarget = int(int);
tTarget* gOriginal = nullptr;

int HookDetour(int x) {
    return gOriginal(x) + 1000;
}

void TestHookContext() {
    FakeSpi fake;
    hook::Context viaSpi(&fake, "LE1MEVRmod");
    CHECK(viaSpi.FullName("UObject::CallFunction") == "LE1MEVRmod_UObject::CallFunction");
    tTarget* original = nullptr;
    CHECK(hook::Install(viaSpi, "Test", &HookTarget, &HookDetour, original));
    CHECK(original == &HookTarget);
    CHECK(fake.installed.size() == 1 && fake.installed[0] == "LE1MEVRmod_Test");
    CHECK(hook::Uninstall(viaSpi, "Test"));
    CHECK(fake.removed.size() == 1 && fake.removed[0] == "LE1MEVRmod_Test");
    fake.refuse = true;
    CHECK(!hook::Install(viaSpi, "Refused", &HookTarget, &HookDetour, original));
    CHECK(original == nullptr);
    CHECK(!hook::Install(viaSpi, "Null", static_cast<tTarget*>(nullptr), &HookDetour, original));

    // Our MinHook (no SPI interface).
    hook::Context own(nullptr, "LE1MEVRtest");
    CHECK(std::string(own.Backend()) == "our MinHook");
    gSink = 0;
    int const plain = HookTarget(2);
    gSink = 0;
    CHECK(hook::Install(own, "HookTarget", &HookTarget, &HookDetour, gOriginal));
    CHECK(gOriginal != nullptr);
    CHECK(HookTarget(2) == plain + 1000);
    tTarget* duplicate = &HookTarget;
    CHECK(!hook::Install(own, "HookTarget", &HookTarget, &HookDetour, duplicate));  // the name is taken
    CHECK(duplicate == nullptr);  // a refused install clears its out-parameter
    tTarget* trampoline = nullptr;
    CHECK(hook::Uninstall(own, "HookTarget"));
    CHECK(!hook::Uninstall(own, "HookTarget"));  // not installed any more
    gSink = 0;
    CHECK(HookTarget(2) == plain);
    // Installed again after removal, and the original is callable through it.
    CHECK(hook::Install(own, "HookTarget", &HookTarget, &HookDetour, trampoline));
    gOriginal = trampoline;
    gSink = 0;
    CHECK(HookTarget(2) == plain + 1000);
    CHECK(hook::Uninstall(own, "HookTarget"));
}

} // namespace

int RunExeTests() {
    TestSyntheticChecks();
    TestTables();
    TestAgainstDump();
    TestHookContext();
    return gFailures;
}
