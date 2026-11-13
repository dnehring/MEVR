// SPDX-License-Identifier: MIT
// mevr-thespian-bench (docs/thespian-mode-plan.md phase 0, research/70): Thespian mode's matcher (core/voice/ScriptMatch)
// over every dialogue wheel of the game, without the game and without audio.
//
//   mevr-thespian-bench <cases.tsv> [--noise 0.15] [--drop 0.05] [--seed 1] [--foreign 3] [--failures 20]
//                       [--utterances <file.tsv>] [--keyterms-out <file.tsv>]
//
// <cases.tsv> is tools/thespian-census.py --cases' output: one row per card of every wheel,
//   wheel id <TAB> card index <TAB> reply index <TAB> category <TAB> style <TAB> label <TAB> line <TAB> outcome key
// Every wheel holds all the replies its entry offers; the game shows fewer at a time (plot conditions hide some), so the
// bench's wheels are harder than the live ones.
//
// What it runs (each a section of its report):
//   read      every card's own line as the transcript, word by word: at how many words the right card is first decided,
//             whether a wrong card was ever decided on the way, and the outcome at the end of the line (final)
//   noisy     the same with a simulated recognizer: each word replaced by a random word with probability --noise and
//             dropped with probability --drop (only with --noise or --drop)
//   sound-alike --soundalike p: each content word, with probability p, heard as another word of the game's lines that only
//             sounds or looks like it (ScriptMatch's Close or Loose): the recognizer's guesses at names, at their worst
// Cards are grouped by their reply (column 3), as the game does; --replies 0 leaves the reply numbers out (grouped by
// their lines, as before 2026-11-13).
//   half      the player stops half-way through the line (final): right, undecided or wrong
//   label     the card's label alone as the transcript (final)
//   foreign   lines of other wheels as the transcript (--foreign per wheel; not ones that are a line or a label of this
//             wheel too): anything decided is a false commit. Harsh on purpose: the game's own lines share openings
//             ("Do you know why...") far more than a player's stray talk would.
//   heard     --utterances: rows "wheel id <TAB> card index <TAB> transcript" (a recognizer's text for that card's line; card
//             index -1: speech that is on no card): the outcome at the end, and how many of the transcript's words it took
// "Right" means the decided leader is in the expected card's group (cards with the same line are one group).
// --keyterms-out writes "wheel id <TAB> term,term,..." for every wheel (Wheel::Keyterms(12): the names on its cards), for
// tools/thespian-vo-bench.py --keyterms wheel.
#include "core/voice/ScriptMatch.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

namespace sm = mevr::voice::script_match;

struct WheelCase {
    std::string id;
    std::vector<sm::Card> cards;
};

struct Tally {
    int total = 0;
    int right = 0;
    int undecided = 0;
    int wrong = 0;
    int doubtful = 0;  // of the undecided: held back by the doubt rule (a word only heard alike)
    void Add(sm::Result const& r, sm::Wheel const& wheel, int expected) {
        ++total;
        if (!r.decided) {
            ++undecided;
            doubtful += r.doubtful ? 1 : 0;
        } else if (expected >= 0 && wheel.Group(r.leader) == wheel.Group(expected)) {
            ++right;
        } else {
            ++wrong;
        }
    }
    void Print(char const* name) const {
        double const n = total > 0 ? total : 1;
        std::printf("%-34s %6d   right %6d (%5.1f%%)   undecided %5d (%5.1f%%)   wrong %4d (%5.2f%%)   not sure which %d\n", name, total, right,
            100.0 * right / n, undecided, 100.0 * undecided / n, wrong, 100.0 * wrong / n, doubtful);
    }
};

std::vector<std::string> SplitTabs(std::string const& line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        std::size_t const tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return out;
}

std::string Join(std::vector<std::string> const& words, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count && i < words.size(); ++i) {
        if (i > 0) out += ' ';
        out += words[i];
    }
    return out;
}

// The words of a line with some content words heard as another word of the game that only sounds or looks like it (Close
// or Loose, never the word itself): the recognizer's guesses at names, at their worst.
std::vector<std::string> SoundAlike(std::vector<std::string> const& words, std::vector<std::string> const& vocabulary, double rate,
    std::map<std::string, std::vector<std::string>>& cache, std::mt19937& rng) {
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::vector<std::string> out;
    for (std::string const& w : words) {
        if (sm::IsStopWord(w) || chance(rng) >= rate) {
            out.push_back(w);
            continue;
        }
        auto it = cache.find(w);
        if (it == cache.end()) {
            std::vector<std::string> alike;
            for (std::string const& v : vocabulary) {
                sm::Quality const q = sm::WordQuality(v, w);
                if (q == sm::Quality::Close || q == sm::Quality::Loose) alike.push_back(v);
            }
            it = cache.emplace(w, std::move(alike)).first;
        }
        if (it->second.empty()) {
            out.push_back(w);
        } else {
            std::uniform_int_distribution<std::size_t> pick(0, it->second.size() - 1);
            out.push_back(it->second[pick(rng)]);
        }
    }
    return out;
}

// The words of a line through a simulated recognizer.
std::vector<std::string> Noisy(std::vector<std::string> const& words, std::vector<std::string> const& pool, double noise, double drop,
    std::mt19937& rng) {
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
    std::vector<std::string> out;
    for (std::string const& w : words) {
        double const roll = chance(rng);
        if (roll < drop) continue;
        out.push_back(roll < drop + noise ? pool[pick(rng)] : w);
    }
    return out;
}

struct ReadStats {
    Tally atEnd;
    int wrongOnTheWay = 0;         // cards for which another group was decided at some prefix
    std::map<int, int> wordsNeeded;  // words -> cards first decided right at that many (9 = nine or more)
    int neverWhileSpeaking = 0;    // right only at the end, or not at all
    std::vector<std::string> failures;
};

void Read(sm::Wheel const& wheel, WheelCase const& wc, int card, std::vector<std::string> const& said, ReadStats& stats, int keep) {
    int first = -1;
    bool wrongLead = false;
    for (std::size_t k = 1; k <= said.size(); ++k) {
        sm::Result const r = wheel.Match(Join(said, k), false);
        if (!r.decided) continue;
        if (wheel.Group(r.leader) == wheel.Group(card)) {
            if (first < 0) first = static_cast<int>(k);
        } else {
            wrongLead = true;
        }
    }
    sm::Result const end = wheel.Match(Join(said, said.size()), true);
    stats.atEnd.Add(end, wheel, card);
    stats.wrongOnTheWay += wrongLead ? 1 : 0;
    if (first > 0) {
        ++stats.wordsNeeded[(std::min)(first, 9)];
    } else {
        ++stats.neverWhileSpeaking;
    }
    bool const failed = !end.decided || wheel.Group(end.leader) != wheel.Group(card) || wrongLead;
    if (failed && static_cast<int>(stats.failures.size()) < keep) {
        std::string note = std::format("{} card {}: said \"{}\" -> {}", wc.id, card, Join(said, said.size()),
            !end.decided ? "undecided" : wheel.Group(end.leader) == wheel.Group(card) ? "right at the end" : "WRONG");
        if (end.leader >= 0) note += std::format(" (leader {} \"{}\", margin {:.2f})", end.leader, wc.cards[static_cast<std::size_t>(end.leader)].line, end.margin);
        if (wrongLead) note += " [a wrong card was decided on the way]";
        stats.failures.push_back(note);
    }
}

void PrintRead(char const* name, ReadStats const& stats) {
    stats.atEnd.Print(name);
    int decidedWhileSpeaking = 0;
    for (auto const& [words, count] : stats.wordsNeeded) decidedWhileSpeaking += count;
    std::printf("    decided while speaking: %d of %d; first at word", decidedWhileSpeaking, stats.atEnd.total);
    int running = 0;
    for (auto const& [words, count] : stats.wordsNeeded) {
        running += count;
        std::printf("  %d%s: %d (%.0f%%)", words, words == 9 ? "+" : "", count, 100.0 * running / (std::max)(1, stats.atEnd.total));
    }
    std::printf("\n    a wrong card decided on the way: %d (%.2f%%)\n", stats.wrongOnTheWay,
        100.0 * stats.wrongOnTheWay / (std::max)(1, stats.atEnd.total));
    for (std::string const& f : stats.failures) std::printf("      %s\n", f.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "mevr-thespian-bench <cases.tsv> [--noise p] [--drop p] [--seed n] [--foreign n] [--failures n] [--utterances file.tsv]\n");
        return 2;
    }
    double noise = 0.0;
    double drop = 0.0;
    double soundAlike = 0.0;
    bool replies = true;
    unsigned seed = 1;
    int foreign = 3;
    int keep = 12;
    std::string utterances;
    std::string keytermsOut;
    std::string alikeOutPath;  // --soundalike-out: the sound-alike transcripts, as --utterances rows (for an A/B)
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string const key = argv[i];
        if (key == "--noise") noise = std::atof(argv[i + 1]);
        else if (key == "--soundalike") soundAlike = std::atof(argv[i + 1]);
        else if (key == "--soundalike-out") alikeOutPath = argv[i + 1];
        else if (key == "--replies") replies = std::atoi(argv[i + 1]) != 0;
        else if (key == "--drop") drop = std::atof(argv[i + 1]);
        else if (key == "--seed") seed = static_cast<unsigned>(std::atoi(argv[i + 1]));
        else if (key == "--foreign") foreign = std::atoi(argv[i + 1]);
        else if (key == "--failures") keep = std::atoi(argv[i + 1]);
        else if (key == "--utterances") utterances = argv[i + 1];
        else if (key == "--keyterms-out") keytermsOut = argv[i + 1];
    }
    std::vector<WheelCase> cases;
    std::map<std::string, std::size_t> byId;
    {
        std::ifstream in(argv[1]);
        if (!in) {
            std::fprintf(stderr, "can't read %s\n", argv[1]);
            return 2;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> const f = SplitTabs(line);
            if (f.size() < 7) continue;
            auto const it = byId.find(f[0]);
            if (it == byId.end()) {
                byId[f[0]] = cases.size();
                cases.push_back({f[0], {}});
            }
            cases[byId[f[0]]].cards.push_back({f[5], f[6], replies ? std::atoi(f[2].c_str()) : -1});
        }
    }
    std::vector<sm::Wheel> wheels;
    std::vector<std::string> pool;       // every line's words: the simulated recognizer's wrong words
    std::vector<std::string> everyLine;  // for the foreign test
    for (WheelCase const& wc : cases) {
        wheels.emplace_back(wc.cards);
        for (sm::Card const& card : wc.cards) {
            if (card.line.empty()) continue;
            everyLine.push_back(card.line);
            for (std::string& w : sm::Tokens(card.line)) pool.push_back(std::move(w));
        }
    }
    if (cases.empty() || pool.empty()) {
        std::fprintf(stderr, "no cards in %s\n", argv[1]);
        return 2;
    }
    if (!keytermsOut.empty()) {
        std::ofstream out(keytermsOut, std::ios::binary);
        for (std::size_t w = 0; w < cases.size(); ++w) {
            std::string terms;
            for (std::string const& term : wheels[w].Keyterms(12)) terms += (terms.empty() ? "" : ",") + term;
            out << cases[w].id << '\t' << terms << '\n';
        }
        std::printf("key terms of %zu wheels -> %s\n", cases.size(), keytermsOut.c_str());
        return 0;
    }
    std::mt19937 rng(seed);
    std::vector<std::string> vocabulary(pool.begin(), pool.end());
    std::sort(vocabulary.begin(), vocabulary.end());
    vocabulary.erase(std::unique(vocabulary.begin(), vocabulary.end()), vocabulary.end());
    std::map<std::string, std::vector<std::string>> alikeCache;
    std::ofstream alikeOut;
    if (!alikeOutPath.empty()) alikeOut.open(alikeOutPath, std::ios::binary);
    ReadStats read;
    ReadStats noisy;
    ReadStats alike;
    Tally half;
    Tally label;
    Tally foreignTally;
    std::vector<std::string> foreignFailures;
    std::size_t cards = 0;
    for (std::size_t w = 0; w < cases.size(); ++w) {
        WheelCase const& wc = cases[w];
        sm::Wheel const& wheel = wheels[w];
        for (int c = 0; c < wheel.Count(); ++c) {
            ++cards;
            sm::Card const& card = wheel.At(c);
            if (!card.label.empty()) label.Add(wheel.Match(card.label, true), wheel, c);
            if (card.line.empty() || wheel.Group(c) != c) continue;  // a group's line once
            std::vector<std::string> const said = sm::Tokens(card.line);
            Read(wheel, wc, c, said, read, keep);
            half.Add(wheel.Match(Join(said, (said.size() + 1) / 2), true), wheel, c);
            if (noise > 0.0 || drop > 0.0) {
                std::vector<std::string> const heard = Noisy(said, pool, noise, drop, rng);
                if (!heard.empty()) Read(wheel, wc, c, heard, noisy, keep);
            }
            if (soundAlike > 0.0) {
                std::vector<std::string> const heard = SoundAlike(said, vocabulary, soundAlike, alikeCache, rng);
                Read(wheel, wc, c, heard, alike, keep);
                if (alikeOut.is_open() && heard != said) alikeOut << wc.id << '\t' << c << '\t' << Join(heard, heard.size()) << '\n';
            }
        }
        std::uniform_int_distribution<std::size_t> pick(0, everyLine.size() - 1);
        for (int k = 0; k < foreign; ++k) {
            std::string const& other = everyLine[pick(rng)];
            std::vector<std::string> const words = sm::Tokens(other);
            bool onThisWheel = false;
            for (int c = 0; c < wheel.Count(); ++c) onThisWheel = onThisWheel || sm::Tokens(wheel.At(c).line) == words || sm::Tokens(wheel.At(c).label) == words;
            if (onThisWheel) continue;
            sm::Result const r = wheel.Match(other, true);
            foreignTally.Add(r, wheel, -1);
            if (r.decided && static_cast<int>(foreignFailures.size()) < keep) {
                foreignFailures.push_back(std::format("{}: \"{}\" taken for card {} \"{}\" (margin {:.2f}{})", wc.id, other, r.leader,
                    r.cards[static_cast<std::size_t>(r.leader)].byLabel ? wheel.At(r.leader).label : wheel.At(r.leader).line, r.margin,
                    r.cards[static_cast<std::size_t>(r.leader)].byLabel ? ", by its label" : ""));
            }
        }
    }
    std::printf("%zu wheels, %zu cards, %zu lines read (one per group)\n\n", cases.size(), cards, static_cast<std::size_t>(read.atEnd.total));
    PrintRead("read: the whole line, at its end", read);
    if (noisy.atEnd.total > 0) {
        std::printf("\n");
        PrintRead(std::format("noisy ({:.0f}% wrong, {:.0f}% dropped)", noise * 100.0, drop * 100.0).c_str(), noisy);
    }
    if (alike.atEnd.total > 0) {
        std::printf("\n");
        PrintRead(std::format("sound-alike ({:.0f}% of content words)", soundAlike * 100.0).c_str(), alike);
    }
    std::printf("\n");
    half.Print("half: stopped half-way");
    label.Print("label: the label alone");
    foreignTally.Print("foreign: another wheel's line");
    for (std::string const& f : foreignFailures) std::printf("      %s\n", f.c_str());

    if (!utterances.empty()) {
        std::ifstream in(utterances);
        if (!in) {
            std::fprintf(stderr, "can't read %s\n", utterances.c_str());
            return 2;
        }
        ReadStats heard;
        Tally none;
        int unknown = 0;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> const f = SplitTabs(line);
            if (f.size() < 3) continue;
            auto const it = byId.find(f[0]);
            if (it == byId.end()) {
                ++unknown;
                continue;
            }
            int const card = std::atoi(f[1].c_str());
            sm::Wheel const& wheel = wheels[it->second];
            if (card < 0 || card >= wheel.Count()) {
                none.Add(wheel.Match(f[2], true), wheel, -1);
                continue;
            }
            Read(wheel, cases[it->second], card, sm::Tokens(f[2]), heard, keep);
        }
        std::printf("\n");
        PrintRead("heard: a recognizer's transcripts", heard);
        if (none.total > 0) none.Print("heard: speech on no card");
        if (unknown > 0) std::printf("    %d rows name a wheel the cases don't have\n", unknown);
    }
    return 0;
}
