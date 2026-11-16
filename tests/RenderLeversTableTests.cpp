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
    if (failures == 0) {
        std::printf("render levers table: every row the module asks for is in LE1's table; no duplicate names\n");
    }
    return failures == 0 ? 0 : 1;
}
