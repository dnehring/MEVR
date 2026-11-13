// SPDX-License-Identifier: MIT
#include "core/voice/ScriptMatch.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>

namespace mevr::voice::script_match {
namespace {

constexpr float kEpsilon = 1e-4f;

bool IsAsciiAlnum(unsigned char c) {
    return std::isalnum(c) != 0;
}

// One word of a text: its bytes [begin, end) and its lower-case form (typographic apostrophes as ').
struct Piece {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string lower;
};

// Splits a text into words. Word characters: ASCII letters and digits, the apostrophe (also U+2019 and U+2018, written as
// the UTF-8 bytes E2 80 99 / E2 80 98), and any other byte of 0x80 or more (an accented letter's bytes) except the rest of
// the E2 80 xx block (dashes, quotation marks, the ellipsis), which separates like punctuation. Apostrophes at a word's
// ends are dropped ("'em" is "em").
std::vector<Piece> Split(std::string_view text) {
    std::vector<Piece> out;
    Piece cur;
    bool open = false;
    auto close = [&](std::size_t at) {
        if (!open) return;
        open = false;
        cur.end = at;
        while (!cur.lower.empty() && cur.lower.front() == '\'') cur.lower.erase(cur.lower.begin());
        while (!cur.lower.empty() && cur.lower.back() == '\'') cur.lower.pop_back();
        if (!cur.lower.empty()) out.push_back(cur);
        cur = {};
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char const c = static_cast<unsigned char>(text[i]);
        bool word = false;
        char lower = 0;
        std::size_t step = 0;
        if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80) {
            unsigned char const third = static_cast<unsigned char>(text[i + 2]);
            if (third == 0x99 || third == 0x98) {
                word = true;
                lower = '\'';
            }
            step = 2;  // the sequence's other two bytes
        } else if (IsAsciiAlnum(c)) {
            word = true;
            lower = static_cast<char>(std::tolower(c));
        } else if (c == '\'') {
            word = true;
            lower = '\'';
        } else if (c >= 0x80) {
            word = true;
            lower = static_cast<char>(c);
        }
        if (word) {
            if (!open) {
                open = true;
                cur.begin = i;
            }
            cur.lower += lower;
        } else {
            close(i);
        }
        i += step;
    }
    close(text.size());
    return out;
}

// The form two spellings of one word share: apostrophes dropped ("don't" = "dont"), small numbers as words.
std::string Normal(std::string_view word) {
    static constexpr std::array<std::pair<char const*, char const*>, 31> kNumbers = {{{"0", "zero"}, {"1", "one"}, {"2", "two"},
        {"3", "three"}, {"4", "four"}, {"5", "five"}, {"6", "six"}, {"7", "seven"}, {"8", "eight"}, {"9", "nine"}, {"10", "ten"},
        {"11", "eleven"}, {"12", "twelve"}, {"13", "thirteen"}, {"14", "fourteen"}, {"15", "fifteen"}, {"16", "sixteen"},
        {"17", "seventeen"}, {"18", "eighteen"}, {"19", "nineteen"}, {"20", "twenty"}, {"30", "thirty"}, {"40", "forty"},
        {"50", "fifty"}, {"60", "sixty"}, {"70", "seventy"}, {"80", "eighty"}, {"90", "ninety"}, {"100", "hundred"},
        {"1000", "thousand"}, {"1st", "first"}}};
    std::string out;
    for (char c : word) {
        if (c != '\'') out += c;
    }
    for (auto const& [digits, name] : kNumbers) {
        if (out == digits) return name;
    }
    return out;
}

// A crude stem: plural and verb endings off, a final e off, a doubled last letter single. "rushed", "rushes", "rushing"
// and "rush" give "rush"; "moved" and "move" give "mov"; "running" and "run" give "run".
std::string Stem(std::string_view normal) {
    std::string s(normal);
    auto ends = [&](char const* tail) { return s.size() > std::char_traits<char>::length(tail) + 2 && s.ends_with(tail); };
    if (ends("ing")) {
        s.resize(s.size() - 3);
    } else if (ends("ed")) {
        s.resize(s.size() - 2);
    } else if (ends("es")) {
        s.resize(s.size() - 2);
    } else if (ends("s") && !s.ends_with("ss")) {
        s.resize(s.size() - 1);
    }
    if (s.size() > 3 && s.back() == 'e') s.pop_back();
    if (s.size() > 3 && s[s.size() - 1] == s[s.size() - 2]) s.pop_back();
    return s;
}

// Run-together spellings the recognizer writes for two words ("I'm outta here" for the line "I'm out of here"): the
// transcript's word is replaced by the words it stands for. A script that itself says "gonna" still matches: Same takes
// a script's run-together word for the first of its words.
struct RunTogether {
    char const* word;
    char const* first;
    char const* second;
};
constexpr RunTogether kRunTogether[] = {{"outta", "out", "of"}, {"gonna", "going", "to"}, {"wanna", "want", "to"}, {"gotta", "got", "to"},
    {"kinda", "kind", "of"}, {"sorta", "sort", "of"}, {"lemme", "let", "me"}, {"gimme", "give", "me"}, {"dunno", "dont", "know"},
    {"cannot", "can", "not"}};

bool IsVowel(char c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

int EditDistance(std::string_view a, std::string_view b) {
    std::vector<int> prev(b.size() + 1);
    std::vector<int> cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            int const substitute = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = (std::min)({prev[j] + 1, cur[j - 1] + 1, substitute});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// The game's names, sorted by their bytes (the generator's sort).
constexpr char const* kGameNames[] = {
#include "core/voice/GameNames.inc"
};

}  // namespace

bool IsGameName(std::string_view written) {
    auto const it = std::lower_bound(std::begin(kGameNames), std::end(kGameNames), written,
        [](char const* name, std::string_view w) { return std::string_view(name) < w; });
    return it != std::end(kGameNames) && std::string_view(*it) == written;
}

std::vector<std::string> Tokens(std::string_view text) {
    std::vector<std::string> out;
    for (Piece& p : Split(text)) out.push_back(std::move(p.lower));
    return out;
}

std::vector<std::string> DisplayWords(std::string_view text) {
    std::vector<std::string> out;
    for (Piece const& p : Split(text)) out.emplace_back(text.substr(p.begin, p.end - p.begin));
    return out;
}

std::vector<std::pair<std::size_t, std::size_t>> WordSpans(std::string_view text) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (Piece const& p : Split(text)) out.emplace_back(p.begin, p.end);
    return out;
}

bool IsStopWord(std::string_view word) {
    // Function words and fillers, in their normal form (no apostrophes). Not here on purpose: yes, no, not, never and the
    // negative contractions (they tell replies apart), and the question words (what, why, how, who, where, when).
    static constexpr char const* kStop[] = {"a", "an", "the", "and", "or", "but", "if", "so", "of", "to", "in", "on", "at", "by",
        "for", "from", "with", "about", "into", "over", "under", "up", "down", "out", "off", "as", "than", "then", "is", "are",
        "was", "were", "be", "been", "being", "am", "do", "does", "did", "have", "has", "had", "will", "would", "can", "could",
        "shall", "should", "may", "might", "must", "i", "you", "he", "she", "it", "we", "they", "me", "him", "her", "us", "them",
        "my", "your", "his", "its", "our", "their", "this", "that", "these", "those", "there", "here", "im", "ive", "ill", "id",
        "youre", "youve", "youll", "youd", "hes", "shes", "theyre", "theyve", "theyll", "weve", "well", "thats", "theres",
        "whats", "lets", "um", "uh", "er", "ah", "oh", "hmm", "mm", "hm", "okay", "ok", "yeah", "hey", "just", "really",
        "like", "got", "get", "some", "any", "all", "too", "very", "now"};
    std::string const normal = Normal(word);
    for (char const* s : kStop) {
        if (normal == s) return true;
    }
    return false;
}

std::string SoundKey(std::string_view word) {
    // A simplified Metaphone with voiced and unvoiced consonants merged (b/p, d/t, g/k, v/f, z/s): the recognizer's
    // misspellings of names keep the consonants' places and little else. Vowels count only at the start.
    std::string w;
    for (char c : word) {
        if (c >= 'a' && c <= 'z') w += c;
        else if (c >= 'A' && c <= 'Z') w += static_cast<char>(c - 'A' + 'a');
    }
    std::string key;
    auto put = [&](char c) {
        if (key.empty() || key.back() != c) key += c;
    };
    std::size_t i = 0;
    if (w.size() >= 2) {
        std::string_view const head(w.data(), 2);
        if (head == "kn" || head == "gn" || head == "pn" || head == "wr") i = 1;
    }
    for (; i < w.size(); ++i) {
        char const c = w[i];
        char const next = i + 1 < w.size() ? w[i + 1] : '\0';
        char const after = i + 2 < w.size() ? w[i + 2] : '\0';
        if (i > 0 && c == w[i - 1] && c != 'c') continue;  // doubled letters once
        switch (c) {
        case 'a': case 'e': case 'i': case 'o': case 'u':
            if (key.empty()) put('A');
            break;
        case 'b': case 'p':
            if (c == 'p' && next == 'h') {
                put('F');
                ++i;
            } else {
                put('P');
            }
            break;
        case 'c':
            if (next == 'h') {
                put('X');
                ++i;
            } else if (next == 'e' || next == 'i' || next == 'y') {
                put('S');
            } else {
                put('K');
            }
            break;
        case 'd':
            put('T');
            break;
        case 't':
            if (next == 'h') {
                put('0');
                ++i;
            } else if (next == 'i' && (after == 'o' || after == 'a')) {
                put('X');
            } else {
                put('T');
            }
            break;
        case 'g':
            if (next == 'h') {
                if (i == 0) put('K');  // "ghost"; elsewhere silent ("thought", "right")
                ++i;
            } else {
                put('K');
            }
            break;
        case 'k': case 'q':
            put('K');
            break;
        case 'f': case 'v':
            put('F');
            break;
        case 's': case 'z':
            if (c == 's' && next == 'h') {
                put('X');
                ++i;
            } else {
                put('S');
            }
            break;
        case 'x':
            if (i == 0) {
                put('S');
            } else {
                put('K');
                put('S');
            }
            break;
        case 'j':
            put('J');
            break;
        case 'l':
            put('L');
            break;
        case 'm':
            put('M');
            break;
        case 'n':
            put('N');
            break;
        case 'r':
            put('R');
            break;
        case 'w': case 'y':
            if (key.empty() && IsVowel(next)) put(c == 'w' ? 'W' : 'Y');
            break;
        default:  // h: silent here (its pairs are handled above)
            break;
        }
    }
    return key;
}

float Similarity(std::string_view a, std::string_view b) {
    if (a.empty() && b.empty()) return 1.0f;
    float const longer = static_cast<float>((std::max)(a.size(), b.size()));
    return 1.0f - static_cast<float>(EditDistance(a, b)) / longer;
}

Quality Compare(Word const& said, Word const& want, float similarity, float looseSpelling) {
    if (said.normal == want.normal) return Quality::Exact;
    for (RunTogether const& r : kRunTogether) {
        if (want.normal == r.word && said.normal == r.first) return Quality::Exact;
    }
    if (said.stem.size() >= 3 && said.stem == want.stem) return Quality::Exact;
    // Function words are matched as written only: "than", "then", "them" and "they" are one edit apart.
    if (said.stop || want.stop) return Quality::None;
    if (said.cut && said.normal.size() >= 3 && want.normal.size() > said.normal.size() && want.normal.starts_with(said.normal)) return Quality::Close;
    if (said.normal.size() >= 5 && want.normal.size() >= 5 && Similarity(said.normal, want.normal) >= similarity) return Quality::Close;
    if (said.normal.size() >= 4 && want.normal.size() >= 4 && said.sound.size() >= 2 && said.sound == want.sound) return Quality::Close;
    // One sound apart, spelled half alike: the recognizer's guesses at a name it doesn't know ("cairns" for "saren's",
    // "sister" for "history"). Never enough alone to tell two cards apart (the doubt rule).
    if (said.normal.size() >= 4 && want.normal.size() >= 4 && said.sound.size() >= 3 && want.sound.size() >= 3 &&
        EditDistance(said.sound, want.sound) <= 1 && Similarity(said.normal, want.normal) >= looseSpelling) {
        return Quality::Loose;
    }
    return Quality::None;
}

bool Same(Word const& said, Word const& want, float similarity) {
    return Compare(said, want, similarity) != Quality::None;
}

Quality WordQuality(std::string_view said, std::string_view want) {
    return Compare(MakeWord(said, 0.25f), MakeWord(want, 0.25f));
}

Word MakeWord(std::string_view token, float stopWeight) {
    Word word;
    word.normal = Normal(token);
    word.stem = Stem(word.normal);
    word.sound = SoundKey(word.normal);
    word.stop = IsStopWord(word.normal);
    word.weight = word.stop ? stopWeight : 1.0f;
    return word;
}

bool SameWord(std::string_view said, std::string_view want, float similarity) {
    return Same(MakeWord(said, 0.25f), MakeWord(want, 0.25f), similarity);
}

Wheel::Script Wheel::MakeScript(std::string_view text) const {
    Script script;
    for (std::string const& token : Tokens(text)) {
        script.words.push_back(MakeWord(token, settings_.stopWeight));
        script.total += script.words.back().weight;
    }
    return script;
}

Wheel::Wheel(std::vector<Card> cards, Settings settings) : settings_(settings), cards_(std::move(cards)) {
    for (Card const& card : cards_) {
        lines_.push_back(MakeScript(card.line));
        labels_.push_back(MakeScript(card.label));
        display_.push_back(DisplayWords(lines_.back().words.empty() ? card.label : card.line));
    }
    // Cards that are one choice are one group. With reply numbers: the same reply (ME1 offers one line under two or three
    // labels, all leading to one reply), or word for word the same line. Without: nearly the same line (a word's form or a
    // word or two apart: ME1 offers "I need to get to Peak 15" and "I needed to get to Peak 15" side by side), or for two
    // menu cards the same label. Cards with different replies and lines a word apart stay apart: reading can't tell them
    // apart, so neither is decided by its line and the player says the label.
    group_.resize(cards_.size());
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        group_[i] = static_cast<int>(i);
        for (std::size_t j = 0; j < i; ++j) {
            Script const& a = lines_[i].words.empty() ? labels_[i] : lines_[i];
            Script const& b = lines_[j].words.empty() ? labels_[j] : lines_[j];
            bool one = false;
            if (cards_[i].reply >= 0 && cards_[j].reply >= 0 && cards_[i].reply == cards_[j].reply) {
                one = true;
            } else if (lines_[i].words.empty() != lines_[j].words.empty() || a.words.empty() || b.words.empty()) {
                one = false;
            } else if (cards_[i].reply >= 0 && cards_[j].reply >= 0) {
                one = a.words.size() == b.words.size() &&
                    std::equal(a.words.begin(), a.words.end(), b.words.begin(), [](Word const& x, Word const& y) { return x.normal == y.normal; });
            } else {
                float const shared = Align(a.words, b, Quality::Close).matched;
                one = shared + kEpsilon >= settings_.sameLine * (std::max)(a.total, b.total);
            }
            if (one) {
                group_[i] = group_[j];
                break;
            }
        }
    }
}

Wheel::Aligned Wheel::Align(std::vector<Word> const& said, Script const& script, Quality least) const {
    Aligned out;
    std::size_t const m = said.size();
    std::size_t const n = script.words.size();
    out.lit.assign(n, 0);
    out.pair.assign(m, -1);
    out.quality.assign(m, Quality::None);
    if (n == 0) {
        for (Word const& w : said) out.penalty += w.weight * settings_.extraPenalty;
        return out;
    }
    // best[i][j]: the heaviest alignment of said[i..] with script[j..] (words in order, any skipped on either side).
    std::vector<float> best((m + 1) * (n + 1), 0.0f);
    std::vector<Quality> same(m * n, Quality::None);
    auto at = [&](std::size_t i, std::size_t j) -> float& { return best[i * (n + 1) + j]; };
    for (std::size_t i = m; i-- > 0;) {
        for (std::size_t j = n; j-- > 0;) {
            float value = (std::max)(at(i + 1, j), at(i, j + 1));
            Quality const q = Compare(said[i], script.words[j], settings_.wordSimilarity, settings_.looseSpelling);
            if (q != Quality::None && q >= least) {
                same[i * n + j] = q;
                value = (std::max)(value, at(i + 1, j + 1) + script.words[j].weight);
            }
            at(i, j) = value;
        }
    }
    // Walk it from the start, taking the earliest script word each said word can have, so the lit words and the frontier
    // run from the line's beginning.
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < m && j < n) {
        if (same[i * n + j] != Quality::None && std::fabs(at(i, j) - (at(i + 1, j + 1) + script.words[j].weight)) < kEpsilon) {
            out.lit[j] = 1;
            out.pair[i] = static_cast<int>(j);
            out.quality[i] = same[i * n + j];
            out.matched += script.words[j].weight;
            out.frontier = static_cast<int>(j);
            ++i;
            ++j;
        } else if (std::fabs(at(i, j) - at(i + 1, j)) < kEpsilon) {
            ++i;
        } else {
            ++j;
        }
    }
    for (std::size_t k = 0; k < m; ++k) {
        if (out.pair[k] < 0) out.penalty += said[k].weight * settings_.extraPenalty;
    }
    return out;
}

bool Wheel::Doubtful(std::vector<Word> const& said, Result const& result, bool final, std::string& heard, std::string& word) const {
    int const leader = result.leader;
    int const runner = result.runnerUp;
    if (leader < 0 || runner < 0) return false;
    auto const l = static_cast<std::size_t>(leader);
    auto const r = static_cast<std::size_t>(runner);
    // The scripts the two scores came from (a menu card's line is empty: its label).
    bool const leadByLabel = result.cards[l].byLabel || lines_[l].words.empty();
    bool const runByLabel = result.cards[r].byLabel || lines_[r].words.empty();
    Script const& lead = leadByLabel ? labels_[l] : lines_[l];
    Script const& run = runByLabel ? labels_[r] : lines_[r];
    auto score = [&](Aligned const& aligned, Script const& script) {
        float value = aligned.matched - aligned.penalty;
        if (final) value -= settings_.missingPenalty * (std::min)(script.total - aligned.matched, settings_.missingCap);
        return value;
    };
    // The lead holds on the words heard as written: nothing to doubt.
    float const need = final ? settings_.finalMargin : settings_.leadMargin;
    if (score(Align(said, lead, Quality::Exact), lead) - score(Align(said, run, Quality::Exact), run) + kEpsilon >= need) return false;
    // It rests on words heard alike. Each of them that the runner-up doesn't use too must be heard Close and clearly nearer
    // the leader's word than any content word of the runner-up's.
    Aligned const withLead = Align(said, lead);
    Aligned const withRun = Align(said, run);
    for (std::size_t i = 0; i < said.size(); ++i) {
        Quality const q = withLead.quality[i];
        if (q == Quality::None || q == Quality::Exact || withRun.pair[i] >= 0) continue;
        Word const& want = lead.words[static_cast<std::size_t>(withLead.pair[i])];
        bool clear = q == Quality::Close;
        float const toLead = (std::max)(Similarity(said[i].normal, want.normal), said[i].sound == want.sound ? 0.75f : 0.0f);
        for (Word const& other : run.words) {
            if (!clear) break;
            if (other.stop) continue;
            float const toRun = (std::max)(Similarity(said[i].normal, other.normal), said[i].sound == other.sound ? 0.75f : 0.0f);
            if (toRun + settings_.clearGap > toLead || Compare(said[i], other, settings_.wordSimilarity, settings_.looseSpelling) != Quality::None) {
                clear = false;
            }
        }
        if (!clear) {
            heard = said[i].normal;
            std::vector<std::string> const shown = DisplayWords(leadByLabel ? cards_[l].label : cards_[l].line);
            auto const at = static_cast<std::size_t>(withLead.pair[i]);
            word = at < shown.size() ? shown[at] : want.normal;
            return true;
        }
    }
    return false;
}

Result Wheel::Match(std::string_view transcript, bool final) const {
    Result result;
    result.cards.resize(cards_.size());
    std::vector<Word> said;
    for (std::string const& token : Tokens(transcript)) {
        bool expanded = false;
        for (RunTogether const& r : kRunTogether) {
            if (token == r.word) {
                said.push_back(MakeWord(r.first, settings_.stopWeight));
                said.push_back(MakeWord(r.second, settings_.stopWeight));
                expanded = true;
                break;
            }
        }
        if (!expanded) said.push_back(MakeWord(token, settings_.stopWeight));
    }
    if (!said.empty()) said.back().cut = true;
    // What the decision needs per card besides the public state: the chosen script's size and how much of it was said.
    struct Chosen {
        float total = 0.0f;
        float coverage = 0.0f;
        int words = 0;
    };
    std::vector<Chosen> chosen(cards_.size());
    // A script's score: what was said of it, less the words said that it doesn't hold, less (once the player has stopped)
    // what of it was left unsaid, up to a cap.
    auto score = [&](Aligned const& aligned, Script const& script) {
        float value = aligned.matched - aligned.penalty;
        if (final) value -= settings_.missingPenalty * (std::min)(script.total - aligned.matched, settings_.missingCap);
        return value;
    };
    for (std::size_t c = 0; c < cards_.size(); ++c) {
        CardState& state = result.cards[c];
        state.group = group_[c];
        Script const& line = lines_[c];
        Script const& label = labels_[c];
        Aligned const byLine = Align(said, line);
        Aligned const byLabel = Align(said, label);
        bool const menu = line.words.empty();
        float const lineScore = menu ? -1e9f : score(byLine, line);
        float const labelScore = label.words.empty() ? -1e9f : score(byLabel, label);
        float const lineCoverage = line.total > 0.0f ? byLine.matched / line.total : 0.0f;
        float const labelCoverage = label.total > 0.0f ? byLabel.matched / label.total : 0.0f;
        // The better script; on a tie the one more of which was said (the label "The geth?" over the line that ends in it).
        state.byLabel = labelScore > lineScore + kEpsilon || (std::fabs(labelScore - lineScore) <= kEpsilon && labelCoverage > lineCoverage);
        Aligned const& picked = state.byLabel ? byLabel : byLine;
        Script const& script = state.byLabel ? label : line;
        state.score = state.byLabel ? labelScore : lineScore;
        state.matched = picked.matched;
        Aligned const& shown = menu ? byLabel : byLine;
        Script const& shownScript = menu ? label : line;
        state.coverage = shownScript.total > 0.0f ? shown.matched / shownScript.total : 0.0f;
        state.frontier = shown.frontier;
        state.lit = shown.lit;
        state.reachedEnd = shown.frontier >= 0 && shown.frontier + 2 >= static_cast<int>(shownScript.words.size());
        chosen[c] = {script.total, script.total > 0.0f ? picked.matched / script.total : 0.0f, static_cast<int>(script.words.size())};
    }
    // The best card of each group, then the best two groups: by score, a tie by coverage of the chosen script, then by index.
    auto better = [&](int a, int b) {
        CardState const& x = result.cards[static_cast<std::size_t>(a)];
        CardState const& y = result.cards[static_cast<std::size_t>(b)];
        if (std::fabs(x.score - y.score) > kEpsilon) return x.score > y.score;
        float const cx = chosen[static_cast<std::size_t>(a)].coverage;
        float const cy = chosen[static_cast<std::size_t>(b)].coverage;
        if (std::fabs(cx - cy) > kEpsilon) return cx > cy;
        return a < b;
    };
    std::map<int, int> bestOfGroup;
    for (int c = 0; c < static_cast<int>(cards_.size()); ++c) {
        auto const it = bestOfGroup.find(group_[static_cast<std::size_t>(c)]);
        if (it == bestOfGroup.end()) {
            bestOfGroup[group_[static_cast<std::size_t>(c)]] = c;
        } else if (better(c, it->second)) {
            it->second = c;
        }
    }
    int leader = -1;
    int runner = -1;
    for (auto const& [group, card] : bestOfGroup) {
        if (leader < 0 || better(card, leader)) {
            runner = leader;
            leader = card;
        } else if (runner < 0 || better(card, runner)) {
            runner = card;
        }
    }
    if (leader < 0 || result.cards[static_cast<std::size_t>(leader)].matched <= 0.0f) {
        return result;  // nothing of any card was said
    }
    CardState const& lead = result.cards[static_cast<std::size_t>(leader)];
    Chosen const& leadChosen = chosen[static_cast<std::size_t>(leader)];
    result.leader = leader;
    result.runnerUp = runner;
    result.margin = runner >= 0 ? lead.score - result.cards[static_cast<std::size_t>(runner)].score : lead.score;
    bool const evidence = lead.matched + kEpsilon >= (std::min)(settings_.minContent, settings_.minShare * leadChosen.total);
    bool const ahead = runner < 0 || result.margin + kEpsilon >= (final ? settings_.finalMargin : settings_.leadMargin);
    result.decided = evidence && ahead && lead.score + kEpsilon >= settings_.minScore;
    if (leadChosen.words <= settings_.shortScriptWords) {
        // "Goodbye.", "Yes.": only once the player has stopped, only said whole, and with at most one word around it.
        result.decided = result.decided && final && leadChosen.coverage + kEpsilon >= 1.0f &&
            static_cast<int>(said.size()) <= leadChosen.words + 1;
    }
    if (result.decided && Doubtful(said, result, final, result.doubtHeard, result.doubtWord)) {
        result.decided = false;
        result.doubtful = true;
    }
    return result;
}

std::vector<std::string> Wheel::Keyterms(int count, std::string_view context) const {
    // The names the cards write (people and places as written; a name of several words as one term, "Eden Prime"; a
    // possessive's name without its 's), and the setting's own words that are written small ("rachni", "geth": the
    // recognizer wrote "Ragnar" and "guest"). Counted over the cards they appear on; the rarest first (the ones that tell
    // the cards apart), then in order of appearance.
    static constexpr char const* kLore[] = {"geth", "rachni", "krogan", "turian", "turians", "asari", "salarian", "salarians",
        "quarian", "quarians", "volus", "hanar", "elcor", "batarian", "batarians", "keeper", "keepers", "biotic", "biotics",
        "thresher", "husk", "husks", "omni", "eezo", "mako", "spectre", "spectres"};
    // One word of a text as the key terms see it.
    struct Written {
        std::string full;      // as written
        std::string bare;      // as written, a possessive's 's off ("Saren's" is "Saren")
        std::string lower;     // lower case, the 's off
        bool capital = false;
        bool start = false;    // the first word of a sentence
        bool joined = false;   // only a space between it and the word before
    };
    auto split = [](std::string_view text) {
        std::vector<Written> out;
        bool sentenceStart = true;
        std::size_t cursor = 0;
        for (Piece const& piece : Split(text)) {
            bool spaceOnly = !out.empty() && piece.begin == cursor + 1 && text[cursor] == ' ';
            for (std::size_t k = cursor; k < piece.begin; ++k) {
                char const ch = text[k];
                if (ch == '.' || ch == '?' || ch == '!' || ch == ':') sentenceStart = true;
            }
            Written w;
            w.full = std::string(text.substr(piece.begin, piece.end - piece.begin));
            w.bare = w.full;
            w.lower = piece.lower;
            if (piece.lower.ends_with("'s")) {
                // The apostrophe is one byte as typed and three as the typographic one.
                w.bare.resize(w.bare.size() - (w.bare.ends_with("'s") ? 2 : (std::min)(w.bare.size(), std::size_t{4})));
                w.lower.resize(w.lower.size() - 2);
            }
            w.capital = !w.bare.empty() && w.bare[0] >= 'A' && w.bare[0] <= 'Z';
            w.start = sentenceStart;
            w.joined = spaceOnly;
            out.push_back(std::move(w));
            cursor = piece.end;
            sentenceStart = false;
        }
        return out;
    };
    // The wheel's own names: capitalised inside a sentence on any card or in the other person's line.
    std::vector<std::string> known;
    auto learn = [&](std::string_view text) {
        for (Written const& w : split(text)) {
            if (w.capital && !w.start && !IsStopWord(w.lower)) known.push_back(w.bare);
        }
    };
    for (Card const& card : cards_) {
        learn(card.label);
        learn(card.line);
    }
    learn(context);
    auto isName = [&](Written const& w) {
        return w.capital && !IsStopWord(w.lower) &&
            (!w.start || std::find(known.begin(), known.end(), w.bare) != known.end() || IsGameName(w.bare));
    };
    struct Term {
        std::string word;
        int cards = 0;
        int order = 0;
    };
    std::vector<Term> terms;
    for (std::size_t c = 0; c < cards_.size(); ++c) {
        std::vector<std::string> onCard;
        auto add = [&](std::string term) {
            if (std::find(onCard.begin(), onCard.end(), term) == onCard.end()) onCard.push_back(std::move(term));
        };
        for (std::string const* text : {&cards_[c].label, &cards_[c].line}) {
            std::vector<Written> const words = split(*text);
            for (std::size_t i = 0; i < words.size();) {
                Written const& w = words[i];
                if (isName(w)) {
                    // A run of names with only spaces between: one term, the last word's possessive off ("Zhu's Hope").
                    std::size_t end = i + 1;
                    while (end < words.size() && words[end].joined && isName(words[end])) ++end;
                    std::string term;
                    for (std::size_t k = i; k < end; ++k) {
                        term += (k == i ? "" : " ") + (k + 1 == end ? words[k].bare : words[k].full);
                    }
                    if (end > i + 1 || term.size() >= 3) add(std::move(term));
                    i = end;
                    continue;
                }
                bool const lore = std::any_of(std::begin(kLore), std::end(kLore), [&](char const* l) { return w.lower == l; });
                if (lore) add(w.bare);
                ++i;
            }
        }
        for (std::string const& word : onCard) {
            auto const it = std::find_if(terms.begin(), terms.end(), [&](Term const& t) { return t.word == word; });
            if (it == terms.end()) {
                terms.push_back({word, 1, static_cast<int>(terms.size())});
            } else {
                ++it->cards;
            }
        }
    }
    std::stable_sort(terms.begin(), terms.end(), [](Term const& a, Term const& b) { return a.cards != b.cards ? a.cards < b.cards : a.order < b.order; });
    std::vector<std::string> out;
    for (Term const& term : terms) {
        if (static_cast<int>(out.size()) >= count) break;
        out.push_back(term.word);
    }
    return out;
}

}  // namespace mevr::voice::script_match
