// SPDX-License-Identifier: MIT
#include "core/voice/Grammar.hpp"

#include <algorithm>
#include <format>
#include <set>
#include <string_view>

namespace mevr::voice::grammar {

namespace sc = vr::squad_context;

namespace {

std::string Alternatives(std::vector<std::string> const& words) {
    std::string out;
    for (std::string const& w : words) out += std::format("{}\"{}\"", out.empty() ? "" : " | ", w);
    return out.empty() ? "\"-\"" : out;
}

}  // namespace

std::string Gbnf(sc::Scene const& scene, std::vector<sc::Group> const& groups, Options const& options) {
    std::vector<std::string> who{"-", "ALL"};
    std::set<std::string> powers;
    for (sc::Member const& m : scene.party) {
        if (!m.present || m.letter == 0) continue;
        who.push_back(std::string(1, m.letter));
        for (sc::Power const& p : m.powers) powers.insert(p.code);
    }
    std::vector<std::string> weapon{"-"};
    for (int w = 0; w < sc::kWeapons; ++w) weapon.push_back(sc::WeaponCode(w));
    std::vector<std::string> power{"-"};
    power.insert(power.end(), powers.begin(), powers.end());
    std::vector<std::string> const order{"attack", "move", "follow", "hold", "regroup", "use", "cancel", "repeat"};
    std::vector<std::string> target{"-", "AIM", "GAZE", "LAST", "OTHER", "SELF", "AHEAD", "LEFT", "RIGHT", "BEHIND"};
    for (sc::Group const& g : groups) target.push_back(std::format("E{}", g.id));
    for (int i = 1; i <= std::clamp(options.deictics, 0, 9); ++i) target.push_back(std::format("PT{}", i));
    int const extra = std::clamp(options.maxClauses, 1, 5) - 1;
    // The newline ends the answer: without it greedy decoding never stops at one clause (zero-shot it filled all three).
    return std::format("root ::= (line (\" ; \" line){{0,{}}} | \"none\") \"\\n\"\n"
                       "line ::= who \"|\" weapon \"|\" power \"|\" order \"|\" target\n"
                       "who ::= {}\n"
                       "weapon ::= {}\n"
                       "power ::= {}\n"
                       "order ::= {}\n"
                       "target ::= {}\n",
        extra, Alternatives(who), Alternatives(weapon), Alternatives(power), Alternatives(order), Alternatives(target));
}

std::string GbnfFromBlock(std::string_view block, int maxClauses) {
    sc::Scene scene;
    std::vector<sc::Group> groups;
    Options options;
    options.maxClauses = maxClauses;
    std::size_t at = 0;
    while (at < block.size()) {
        std::size_t const end = block.find('\n', at);
        std::string_view const line = block.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        at = end == std::string_view::npos ? block.size() : end + 1;
        if (line.starts_with("PARTY ") && line.size() > 7 && line[7] == ':') {
            sc::Member m;
            m.letter = line[6];
            m.present = true;
            // "PARTY G:PI,AR* | OVR ok, DMP 9s": the codes after the bar.
            std::size_t const bar = line.find('|');
            if (bar != std::string_view::npos) {
                std::string_view rest = line.substr(bar + 1);
                while (!rest.empty()) {
                    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
                    std::size_t const sp = rest.find(' ');
                    std::string_view const code = rest.substr(0, sp);
                    if (code.size() >= 2 && code.size() <= 8 && code != "-" && code != "down") {
                        bool caps = true;
                        for (char c : code) caps = caps && c >= 'A' && c <= 'Z';
                        if (caps) m.powers.push_back({std::string(code), 0.0f});
                    }
                    std::size_t const comma = rest.find(',');
                    if (comma == std::string_view::npos) break;
                    rest.remove_prefix(comma + 1);
                }
            }
            scene.party.push_back(m);
        } else if (line.starts_with("E") && line.size() > 1 && line[1] >= '0' && line[1] <= '9') {
            sc::Group g;
            g.id = 0;
            for (std::size_t i = 1; i < line.size() && line[i] >= '0' && line[i] <= '9'; ++i) g.id = g.id * 10 + (line[i] - '0');
            if (g.id > 0) groups.push_back(g);
        } else if (line.starts_with("PT") && line.size() > 2 && line[2] >= '1' && line[2] <= '9') {
            options.deictics = std::max(options.deictics, line[2] - '0');
        }
    }
    return Gbnf(scene, groups, options);
}

std::string Prompt(std::string const& block) {
    return block + "\nACTION:";
}

}  // namespace mevr::voice::grammar
