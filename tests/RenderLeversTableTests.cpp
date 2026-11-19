// SPDX-License-Identifier: MIT
// plugin/RenderLeversTable's contract with engine/Addresses.hpp (research/sequel-render-levers.md "Shared module"): the
// render levers find their addresses by ROW NAME in the current game's table. This checks, for all three tables at once
// (no game is selected here), that every name the module asks for is a row of LE1's table with a nonzero RVA, that no
// table has two rows of one name (the lookup takes the first), and prints which levers each sequel's table can already
// serve, so a row that lands with a misspelt name shows here instead of as a lever that silently stays off.

#include "plugin/RenderLeversTable.hpp"

#include <cstdio>
#include <span>
#include <string_view>

namespace {

using mevr::exe::Entry;
namespace table = mevr::host::render_levers::table;
namespace addresses = mevr::engine::addresses;

int Count(std::span<Entry const> rows, std::string_view name) {
    int n = 0;
    for (Entry const& e : rows) {
        if (std::string_view(e.name) == name) {
            ++n;
        }
    }
    return n;
}

std::uint32_t Rva(std::span<Entry const> rows, std::string_view name) {
    for (Entry const& e : rows) {
        if (std::string_view(e.name) == name) {
            return e.rva;
        }
    }
    return 0;
}

// LE1's rows, at compile time too: the lookup the host uses, over LE1's table.
static_assert(table::RowIn(addresses::le1::kTable, table::row::kSceneRender) == 0x76EE20);
static_assert(table::RowIn(addresses::le1::kTable, table::row::kBoundShaderStateCtor) == 0x9A89F0);
static_assert(table::RowIn(addresses::le1::kTable, "no such row") == 0);
// ME3's Umbra command gate is a row of ME3's table only (LE1 and ME2 have no such gate).
static_assert(table::RowIn(addresses::le3::kTable, table::row::kUmbraCommandGate) == 0x1767208);
static_assert(table::RowIn(addresses::le1::kTable, table::row::kUmbraCommandGate) == 0);
static_assert(table::RowIn(addresses::le2::kTable, table::row::kUmbraCommandGate) == 0);

Entry const* Find(std::span<Entry const> rows, std::string_view name) {
    for (Entry const& e : rows) {
        if (std::string_view(e.name) == name) {
            return &e;
        }
    }
    return nullptr;
}

#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            std::printf("FAIL tests/RenderLeversTableTests.cpp line %d: %s\n", __LINE__, #expr); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

// The gate's row: a data global referenced by `cmp dword ptr [rip+X], 0`, the displacement 8 bytes into the pattern and
// one byte of immediate after it (exe::Entry::tail).
int CheckGateRow() {
    int failures = 0;
    Entry const* gate = Find(addresses::le3::kTable, table::row::kUmbraCommandGate);
    CHECK(gate != nullptr);
    if (gate != nullptr) {
        CHECK(gate->kind == mevr::exe::Kind::Data && gate->tail == 1 && gate->offset == 8 && gate->pattern != nullptr);
    }
    return failures;
}

// The current game's layout and lever defaults (compiled once per sequel: a flag or default that changes shows here, as
// LE1's do in RenderLeversTable.hpp's static_asserts).
int CheckGameLayout() {
    int failures = 0;
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    table::Layout const& l = table::kLayout;
    // Defaults: the renderer's own SFR state per pass; the query waits the game's own.
    CHECK(l.perEyeViewStateDefault == 1);
    CHECK(l.umbraQueryWaitDefault == 1);
    CHECK(l.ue3QueryWaitDefault == 1);
    // The groups that ran in the game (research/sequel-render-levers.md "ME2"/"ME3: the baseline and the A/Bs"), and the
    // ones that stand down until they do.
    CHECK(l.sceneRendererVerified && l.umbraCommanderVerified && l.boundShaderStateVerified && l.umbraQueriesVerified);
    CHECK(!l.umbraInstanceVerified && !l.occlusionHistoryVerified && !l.sharePassVerified && !l.sceneCaptureVerified);
    // The second eye (the owner's yes, 2026-11-17; research/sequel-render-levers.md "The batch before the headset test"):
    // the first eye's Umbra results stand in for the second eye's unready ones on both sequels.
    CHECK(l.umbraSharedVerified);
    CHECK(l.umbraShareResultsDefault == 1);
    // FSceneView's matrices umbraEyeMatrix reads and writes are LE1's offsets in all three games.
    CHECK(l.viewViewMatrix == 0x90 && l.viewTranslatedViewMatrix == 0x190 && l.viewTranslatedViewProj == 0x1D0 &&
          l.viewPreViewTranslation == 0x250 && l.viewViewProj == 0x260 && l.commanderView == 0x68 &&
          l.commanderViewParamsSet == 0x5C);
#endif
#if defined(MEVR_GAME_LE2)
    CHECK(std::string_view(table::kGameName) == "LE2");
    CHECK(l.viewStride == 0x11A0);
    CHECK(l.ineligibleShowFlagCount == 1 && l.ineligibleShowFlags[0] == 0x33);
    CHECK(!l.instanceCallAlsoKeepsBooks && !l.captureTakesUmbraBranch);
    CHECK(l.umbraCommandGate == 0);
    CHECK(l.ownsSceneCaptureHooks);
    // Halcyon ME2's same-frame retry of pass 1 is off (a third scene render on 1-3% of moving frames).
    CHECK(l.sfrReplayRetryDefault == 0);
#elif defined(MEVR_GAME_LE3)
    CHECK(std::string_view(table::kGameName) == "LE3");
    CHECK(l.viewStride == 0x11B0);
    CHECK(l.ineligibleShowFlagCount == 4 && l.ineligibleShowFlags[0] == 0x33 && l.ineligibleShowFlags[1] == 0x47 &&
          l.ineligibleShowFlags[2] == 0x15 && l.ineligibleShowFlags[3] == 0x1B);
    CHECK(l.instanceCallAlsoKeepsBooks && l.captureTakesUmbraBranch);
    CHECK(l.umbraCommandGate == 0x1767208 && l.umbraCommandGate == addresses::kUmbraCommandGate);
    CHECK(!l.ownsSceneCaptureHooks);   // plugin/Le3SceneCapture hooks the capture functions (the ledger's Forks 9)
    CHECK(l.sfrReplayRetryDefault == 1);   // ME2's renderer only: ME3's table keeps the Layout's default, never read
    // The rows umbraEyeMatrix (the lever the life-size stereo's mode 2 needs) hooks are ME3's own.
    static_assert(table::kUmbraIssueQueriesRva == 0x3D8EB0 && table::kCommanderCommandRva == 0x3E8B80);
#endif
    return failures;
}

#undef CHECK

} // namespace

int main() {
    int failures = 0;
    struct Game {
        char const* name;
        std::span<Entry const> rows;
    };
    Game const games[] = {{"LE1", addresses::le1::kTable}, {"LE2", addresses::le2::kTable}, {"LE3", addresses::le3::kTable}};
    for (char const* const name : table::kRowNames) {
        if (Rva(games[0].rows, name) == 0) {
            std::printf("FAIL: LE1's table has no row '%s' (plugin/RenderLevers asks for it)\n", name);
            ++failures;
        }
        for (Game const& game : games) {
            if (Count(game.rows, name) > 1) {
                std::printf("FAIL: %s's table has %d rows named '%s' (the lookup takes the first)\n", game.name,
                    Count(game.rows, name), name);
                ++failures;
            }
        }
    }
    for (Game const& game : games) {
        int have = 0;
        std::printf("%s:", game.name);
        for (char const* const name : table::kRowNames) {
            if (Rva(game.rows, name) != 0) {
                ++have;
            } else {
                std::printf(" [no '%s']", name);
            }
        }
        std::printf(" %d of %d render-lever rows\n", have, static_cast<int>(std::size(table::kRowNames)));
    }
    failures += CheckGateRow();
    failures += CheckGameLayout();
    if (failures == 0) {
        std::printf("render levers table: every row the module asks for is in LE1's table; no duplicate names%s\n",
#if defined(MEVR_GAME_LE2)
            "; LE2's layout and lever defaults as measured"
#elif defined(MEVR_GAME_LE3)
            "; LE3's layout, lever defaults and command gate as measured"
#else
            ""
#endif
        );
    }
    return failures == 0 ? 0 : 1;
}
