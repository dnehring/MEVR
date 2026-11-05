// SPDX-License-Identifier: MIT
#include "core/vr/SquadParser.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace mevr::vr::squad_parser {

namespace sc = squad_context;

namespace {

enum class Tag : std::uint8_t {
    None, Stop, Name, All, Order, Power, Weapon, Deictic, Place, Sector, Flag, Elevation, Surface, Type, Generic, Aim, Gaze, Other,
    LastTarget, Conj, No, Cancel, Repeat, Equip, Until, Stay, NoRetreat, ComeBack, Support
};

struct Token {
    std::string text;
    Tag tag = Tag::None;
    int value = 0;       // Name: the letter; Order: the Order; Weapon: the type; Sector/Elevation/Flag: their values
    std::string code;    // Power: the code; Surface/Type: the word
    float similarity = 1.0f;
    bool head = true;    // the first token of a phrase (the rest are its tail)
};

struct Phrase {
    std::vector<std::string> words;
    Tag tag;
    int value = 0;
    std::string code;
};

// Flags for Tag::Flag.
enum Flag : int { Nearest = 1, Farthest = 2, Leftmost = 3, Rightmost = 4, Highest = 5, Biggest = 6 };

std::vector<std::string> Split(std::string_view phrase) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : phrase) {
        if (c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void Add(std::vector<Phrase>& v, std::initializer_list<char const*> phrases, Tag tag, int value = 0, std::string code = {}) {
    for (char const* p : phrases) v.push_back({Split(p), tag, value, code});
}

std::vector<Phrase> const& Vocabulary() {
    static std::vector<Phrase> const v = [] {
        std::vector<Phrase> p;
        Add(p, {"the", "a", "an", "to", "at", "on", "of", "in", "your", "you", "me", "my", "please", "now", "okay", "ok", "hey", "um", "uh",
                   "with", "for", "it's", "its", "some", "that's", "guy", "one", "ones", "up"}, Tag::Stop);
        Add(p, {"everyone", "everybody", "all of you", "you two", "both of you", "squad", "team", "all", "both"}, Tag::All);
        Add(p, {"you too", "you as well", "same", "same thing", "you also"}, Tag::Repeat);
        Add(p, {"again", "do it again", "one more time", "once more", "same again"}, Tag::Repeat);
        Add(p, {"belay that", "belay", "cancel", "cancel that", "never mind", "nevermind", "forget it", "as you were", "disregard", "stand down"},
            Tag::Cancel);
        Add(p, {"no", "no no", "not that", "wait no", "nope"}, Tag::No);
        Add(p, {"until it's clear", "until its clear", "until it's over", "until the fighting stops", "until it's quiet", "until it's safe",
                   "until it quiets down", "till it's clear", "until clear", "until it calms down", "until the area is clear", "while it's hot"},
            Tag::Until, 1);
        Add(p, {"until they're dead", "until they are dead", "until they're all dead", "until they're gone", "until that group is dead",
                   "until they're down", "till they're dead", "until they are gone", "until it's dead", "until he's dead"},
            Tag::Until, 2);
        Add(p, {"until i say otherwise", "until i say so", "until i tell you", "until further notice", "until i call you back",
                   "until i say", "till i say so"},
            Tag::Until, 3);
        Add(p, {"and stay there", "stay there", "and stay put", "stay put there", "don't come back", "and stay", "and hold there"},
            Tag::Stay);
        Add(p, {"no matter what", "whatever happens", "don't retreat", "don't fall back", "hold at all costs", "at all costs", "do not retreat",
                   "don't give it up", "never retreat"},
            Tag::NoRetreat);
        Add(p, {"and then come back", "then come back", "then regroup", "and come back", "then fall back", "then come back to me",
                   "then back to me", "and then regroup", "come back when it's done", "then on me", "and then fall back", "then return"},
            Tag::ComeBack);
        Add(p, {"and then", "and", "then"}, Tag::Conj);
        Add(p, {"support", "cover", "back up", "help", "protect", "assist", "stay with", "stick with", "watch", "guard", "cover for"},
            Tag::Support);
        Add(p, {"attack", "target", "take out", "take down", "kill", "shoot", "fire on", "fire at", "open fire", "engage", "focus on", "focus", "hit", "light up",
                   "waste", "deal with", "get rid of", "take care of", "drop", "neutralize", "neutralise", "eliminate", "destroy", "go after", "go for"},
            Tag::Order, static_cast<int>(sc::Order::Attack));
        Add(p, {"move", "move up", "move to", "move over", "go to", "go", "get to", "get over", "advance", "flank", "take position", "take cover",
                   "push up", "push", "head", "run", "get up"},
            Tag::Order, static_cast<int>(sc::Order::Move));
        Add(p, {"follow", "follow me", "on me", "with me", "regroup", "fall back", "come back", "come here", "come on", "get back", "rally", "to me",
                   "back to me", "stay close", "stick with me", "come to me", "get over here", "over here"},
            Tag::Order, static_cast<int>(sc::Order::Follow));
        // Not "hold there" / "stay there": "there" stays a place, so they hold at the pointed spot.
        Add(p, {"hold", "hold position", "hold here", "hold up", "stay", "stay here", "stay put", "stop", "wait", "wait here", "don't move",
                   "freeze"},
            Tag::Order, static_cast<int>(sc::Order::Hold));
        Add(p, {"use", "cast", "give him", "give them", "give it", "throw him", "hit him with", "hit them with", "hit it with"}, Tag::Order,
            static_cast<int>(sc::Order::Use));
        Add(p, {"equip", "switch to", "pull out", "draw", "use your", "get your", "grab your", "take your", "bring out", "ready your", "out with"},
            Tag::Equip);
        Add(p, {"pistol", "handgun", "sidearm"}, Tag::Weapon, 0);
        Add(p, {"shotgun", "scattergun", "scatter gun"}, Tag::Weapon, 1);
        Add(p, {"assault rifle", "rifle", "ar", "assault"}, Tag::Weapon, 2);
        Add(p, {"sniper rifle", "sniper", "sniper's", "long gun", "sr"}, Tag::Weapon, 3);
        Add(p, {"that one", "those ones", "those guys", "that guy", "that thing", "those two", "those three", "that", "those", "these", "this",
                   "them", "him", "her", "it"},
            Tag::Deictic);
        Add(p, {"over there", "up there", "down there", "right there", "back there", "there", "here"}, Tag::Place);
        Add(p, {"on the left", "to the left", "left side", "left ones", "leftmost", "left"}, Tag::Sector, static_cast<int>(sc::Sector::Left));
        Add(p, {"on the right", "to the right", "right side", "right ones", "rightmost", "right"}, Tag::Sector, static_cast<int>(sc::Sector::Right));
        Add(p, {"straight ahead", "in front", "ahead", "front", "in front of us", "dead ahead", "center", "centre", "middle"}, Tag::Sector,
            static_cast<int>(sc::Sector::Ahead));
        Add(p, {"behind", "behind us", "back there", "to the rear", "rear"}, Tag::Sector, static_cast<int>(sc::Sector::Behind));
        Add(p, {"nearest", "closest", "close ones", "near ones", "nearby", "close", "near"}, Tag::Flag, Nearest);
        Add(p, {"farthest", "furthest", "far ones", "far away", "far", "distant"}, Tag::Flag, Farthest);
        Add(p, {"biggest", "big group", "large group", "biggest group", "most of them", "big one", "bigger group"}, Tag::Flag, Biggest);
        Add(p, {"highest", "up top", "on top", "above", "above us", "up high", "high ones", "upstairs", "top", "high", "upper", "elevated"},
            Tag::Elevation, static_cast<int>(sc::Elevation::Above));
        Add(p, {"below", "down below", "below us", "downstairs", "low ones", "down low", "lower", "down"}, Tag::Elevation,
            static_cast<int>(sc::Elevation::Below));
        Add(p, {"enemy", "enemies", "hostiles", "hostile", "troops", "soldiers", "targets", "guys", "bad guys", "group", "squad of them", "units",
                   "contacts", "tangos"},
            Tag::Generic);
        Add(p, {"my target", "what i'm shooting", "what i'm shooting at", "what i'm aiming at", "the one i'm shooting", "where i'm aiming",
                   "this target"},
            Tag::Aim);
        Add(p, {"what i'm looking at", "the one i'm looking at", "where i'm looking", "what i see"}, Tag::Gaze);
        Add(p, {"the others", "the other ones", "the other one", "other ones", "the other group", "the rest", "the rest of them", "other group"},
            Tag::Other);
        Add(p, {"same target", "the same ones", "same one", "same ones", "that same one"}, Tag::LastTarget);
        std::stable_sort(p.begin(), p.end(), [](Phrase const& a, Phrase const& b) { return a.words.size() > b.words.size(); });
        return p;
    }();
    return v;
}

// bridge = catwalk = walkway: the surface synonyms, in groups.
std::vector<std::vector<std::string>> const& SurfaceSynonyms() {
    static std::vector<std::vector<std::string>> const s = {{"bridge", "catwalk", "walkway", "overpass", "gantry"}, {"stairs", "staircase", "steps"},
        {"ledge", "ridge", "cliff", "rocks"}, {"crates", "boxes", "containers", "cover"}, {"roof", "rooftop"}, {"platform", "deck"},
        {"balcony", "railing"}, {"ramp", "slope"}, {"door", "doorway", "entrance"}, {"wall"}, {"tower"}, {"hill"}};
    return s;
}

bool SameSurface(std::string_view a, std::string_view b) {
    if (a == b) return true;
    for (auto const& group : SurfaceSynonyms()) {
        bool const hasA = std::find(group.begin(), group.end(), a) != group.end();
        bool const hasB = std::find(group.begin(), group.end(), b) != group.end();
        if (hasA && hasB) return true;
    }
    return false;
}

// Short words match exactly: "there" is one letter from "these", "hold" from "hole"; the fuzz is for the long ones a recognizer
// mangles ("singularity", "overload", "destroyer").
bool WordMatches(std::string_view said, std::string_view want, float threshold) {
    if (said == want) return true;
    if (want.size() < 6 || said.size() < 4) return false;
    return Similarity(said, want) >= threshold;
}

}  // namespace

std::vector<std::string> Tokens(std::string_view text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        unsigned char const u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '\'') {
            cur += static_cast<char>(std::tolower(u));
        } else {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

float Similarity(std::string_view a, std::string_view b) {
    if (a.empty() && b.empty()) return 1.0f;
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            int const cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    float const longer = static_cast<float>(std::max(a.size(), b.size()));
    return 1.0f - static_cast<float>(prev[b.size()]) / longer;
}

std::string Singular(std::string_view word) {
    std::string w(word);
    if (w.size() > 4 && w.ends_with("ies")) return w.substr(0, w.size() - 3) + "y";
    if (w.size() > 3 && w.ends_with("s") && !w.ends_with("ss") && !w.ends_with("us")) return w.substr(0, w.size() - 1);
    return w;
}

Parsed Parse(std::string_view transcript, sc::Scene const& scene, std::vector<sc::Group> const& groups,
    std::vector<sc::RosterEntry> const& roster, Settings const& st) {
    Parsed parsed;
    std::vector<std::string> const words = Tokens(transcript);
    std::vector<Token> tokens(words.size());
    for (std::size_t i = 0; i < words.size(); ++i) tokens[i].text = words[i];
    if (tokens.empty()) return parsed;

    // 1. The present squadmates' names and aliases (fuzzy, against the present ones only).
    struct Alias {
        std::vector<std::string> words;
        char letter;
    };
    std::vector<Alias> aliases;
    for (sc::Member const& m : scene.party) {
        if (!m.present) continue;
        for (sc::RosterEntry const& e : roster) {
            if (e.letter != m.letter) continue;
            for (std::string const& w : Split(e.name)) aliases.push_back({{w}, e.letter});
            for (std::string const& a : e.aliases) aliases.push_back({Tokens(a), e.letter});
        }
        if (aliases.empty() || aliases.back().letter != m.letter) aliases.push_back({{std::string(1, static_cast<char>(std::tolower(m.letter)))}, m.letter});
    }
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].tag != Tag::None) continue;
        // The best alias at this position: multi-word aliases need every word; single words fuzzily.
        char bestLetter = 0, secondLetter = 0;
        float best = 0.0f, second = 0.0f;
        std::size_t bestLength = 1;
        for (Alias const& a : aliases) {
            if (a.words.empty() || i + a.words.size() > tokens.size()) continue;
            float score = 1.0f;
            for (std::size_t k = 0; k < a.words.size(); ++k) {
                // A shared first letter counts for a tenth: a recognizer's slip keeps it more often than the rest.
                float s = Similarity(tokens[i + k].text, a.words[k]);
                if (!a.words[k].empty() && !tokens[i + k].text.empty() &&
                    std::tolower(static_cast<unsigned char>(a.words[k][0])) == static_cast<unsigned char>(tokens[i + k].text[0])) {
                    s = std::min(1.0f, s + 0.1f);
                }
                score = std::min(score, s);
            }
            if (a.words.size() > 1 && score < st.wordSimilarity) continue;
            if (score > best && a.letter != bestLetter) {
                second = best;
                secondLetter = bestLetter;
                best = score;
                bestLetter = a.letter;
                bestLength = a.words.size();
            } else if (score > best) {
                best = score;
                bestLength = a.words.size();
            } else if (a.letter != bestLetter && score > second) {
                second = score;
                secondLetter = a.letter;
            }
        }
        (void)secondLetter;
        bool const stopWord = std::any_of(Vocabulary().begin(), Vocabulary().end(), [&](Phrase const& p) {
            return p.words.size() == 1 && p.words[0] == tokens[i].text && p.tag != Tag::Name;
        });
        if (bestLetter != 0 && best >= st.nameSimilarity && best - second >= st.nameMargin && !(stopWord && best < 1.0f)) {
            tokens[i].tag = Tag::Name;
            tokens[i].value = bestLetter;
            tokens[i].similarity = best;
            for (std::size_t k = 1; k < bestLength; ++k) {
                tokens[i + k].tag = Tag::Name;
                tokens[i + k].head = false;
            }
            if (best < 1.0f) parsed.notes += std::format("'{}' taken as {} ({:.2f}); ", tokens[i].text, bestLetter, best);
            i += bestLength - 1;
        }
    }
    // 2. The powers' spoken names (every roster entry's, so an absent squadmate's power is still a power).
    for (sc::RosterEntry const& e : roster) {
        for (auto const& [code, spoken] : e.powers) {
            std::vector<std::string> const pw = Tokens(spoken);
            if (pw.empty()) continue;
            for (std::size_t i = 0; i + pw.size() <= tokens.size(); ++i) {
                bool ok = true;
                for (std::size_t k = 0; k < pw.size() && ok; ++k) {
                    ok = tokens[i + k].tag == Tag::None && WordMatches(tokens[i + k].text, pw[k], st.wordSimilarity);
                }
                if (!ok) continue;
                tokens[i].tag = Tag::Power;
                tokens[i].code = code;
                for (std::size_t k = 1; k < pw.size(); ++k) {
                    tokens[i + k].tag = Tag::Power;
                    tokens[i + k].head = false;
                }
                i += pw.size() - 1;
            }
        }
    }
    // 3. The groups' surface and type words.
    for (sc::Group const& g : groups) {
        for (std::string const& w : {g.surface}) {
            if (w.empty()) continue;
            for (Token& t : tokens) {
                if (t.tag == Tag::None && (SameSurface(Singular(t.text), w) || WordMatches(t.text, w, st.wordSimilarity))) {
                    t.tag = Tag::Surface;
                    t.code = w;
                }
            }
        }
        for (std::string const& w : Tokens(g.type)) {
            for (Token& t : tokens) {
                if (t.tag == Tag::None && (Singular(t.text) == w || WordMatches(Singular(t.text), w, st.wordSimilarity))) {
                    t.tag = Tag::Type;
                    t.code = w;
                }
            }
        }
    }
    // 4. The vocabulary, longest phrases first.
    for (Phrase const& p : Vocabulary()) {
        for (std::size_t i = 0; i + p.words.size() <= tokens.size(); ++i) {
            bool ok = true;
            for (std::size_t k = 0; k < p.words.size() && ok; ++k) {
                ok = tokens[i + k].tag == Tag::None &&
                    (p.words.size() > 1 ? tokens[i + k].text == p.words[k] : WordMatches(tokens[i + k].text, p.words[k], st.wordSimilarity));
            }
            if (!ok) continue;
            tokens[i].tag = p.tag;
            tokens[i].value = p.value;
            tokens[i].code = p.code;
            for (std::size_t k = 1; k < p.words.size(); ++k) {
                tokens[i + k].tag = p.tag;
                tokens[i + k].head = false;
            }
            i += p.words.size() - 1;
        }
    }
    // 5. A leading "no": a correction (consumed).
    std::size_t start = 0;
    while (start < tokens.size() && (tokens[start].tag == Tag::Stop || (tokens[start].tag == Tag::No && tokens[start].head))) {
        if (tokens[start].tag == Tag::No) parsed.correction = true;
        ++start;
    }
    // 6. Clauses: a conjunction before an order, power, weapon or deictic starts a new one.
    std::vector<std::pair<std::size_t, std::size_t>> clauses;
    std::size_t from = start;
    auto const isVerbish = [](Tag t) {
        return t == Tag::Order || t == Tag::Power || t == Tag::Weapon || t == Tag::Equip || t == Tag::Repeat || t == Tag::Cancel || t == Tag::Support ||
            t == Tag::Deictic || t == Tag::Place;
    };
    bool orderSeen = false;
    for (std::size_t i = start; i < tokens.size(); ++i) {
        if (!tokens[i].head) continue;
        if (tokens[i].tag == Tag::Name && orderSeen && i > from) {
            // A vocative: a new clause, unless the name is the object of a support verb just before it.
            std::size_t j = i;
            while (j > from && (tokens[j - 1].tag == Tag::Stop || !tokens[j - 1].head)) --j;
            // ...or the actor named after "you too" / "same" ("you too, Wrex").
            bool const object = j > from && (tokens[j - 1].tag == Tag::Support || tokens[j - 1].tag == Tag::Repeat);
            if (!object) {
                clauses.emplace_back(from, i);
                from = i;
                orderSeen = false;
            }
            continue;
        }
        orderSeen = orderSeen || isVerbish(tokens[i].tag);
        if (tokens[i].tag != Tag::Conj) continue;
        std::size_t end = i;
        while (end < tokens.size() && tokens[end].tag == Tag::Conj) ++end;
        bool verbAhead = false;
        for (std::size_t j = end; j < tokens.size() && tokens[j].tag != Tag::Conj; ++j) {
            Tag const t = tokens[j].tag;
            verbAhead = verbAhead || isVerbish(t) || t == Tag::Name;
        }
        if (verbAhead && i > from) {
            clauses.emplace_back(from, i);
            from = end;
            i = end - 1;
            orderSeen = false;
        }
    }
    if (from < tokens.size()) clauses.emplace_back(from, tokens.size());
    // 7. Each clause.
    sc::Who carried;
    int deicticIndex = 0;
    for (auto const& [a, b] : clauses) {
        Clause clause;
        float confidence = 1.0f;
        std::string unresolved;
        sc::Action& act = clause.action;
        bool anyOrderWord = false;
        bool cancel = false, repeat = false, equip = false, support = false;
        std::optional<sc::Order> verb;
        int deictic = -1;
        bool deicticPlace = false;
        std::optional<sc::Sector> sector;
        std::optional<sc::Elevation> elevation;
        std::vector<int> flags;
        std::vector<std::string> surfaces, types;
        std::optional<sc::Target::Kind> explicitTarget;
        std::vector<std::string> unknown;
        for (std::size_t i = a; i < b; ++i) {
            Token const& t = tokens[i];
            if (!clause.words.empty()) clause.words += ' ';
            clause.words += t.text;
            if (!t.head) continue;
            switch (t.tag) {
            case Tag::Name:
                if (support && clause.supportOf == 0) {
                    clause.supportOf = static_cast<char>(t.value);
                } else {
                    act.who = {sc::Who::Member, static_cast<char>(t.value)};
                }
                confidence *= std::max(t.similarity, 0.5f);
                break;
            case Tag::Support:
                support = true;
                anyOrderWord = true;
                break;
            case Tag::All: act.who.kind = sc::Who::All; break;
            case Tag::Order:
                if (!verb) verb = static_cast<sc::Order>(t.value);
                anyOrderWord = true;
                break;
            case Tag::Power:
                act.power = t.code;
                anyOrderWord = true;
                break;
            case Tag::Weapon:
                act.weapon = t.value;
                anyOrderWord = true;
                break;
            case Tag::Equip:
                equip = true;
                anyOrderWord = true;
                break;
            case Tag::Deictic:
            case Tag::Place:
                if (deictic < 0) {
                    deictic = ++deicticIndex;
                    deicticPlace = t.tag == Tag::Place;
                    parsed.deictics.push_back({deictic, t.text, static_cast<int>(i), deicticPlace});
                    // A two-word deictic: its tail joins the word.
                    for (std::size_t k = i + 1; k < b && !tokens[k].head; ++k) parsed.deictics.back().word += " " + tokens[k].text;
                }
                break;
            case Tag::Sector: sector = static_cast<sc::Sector>(t.value); break;
            case Tag::Elevation: elevation = static_cast<sc::Elevation>(t.value); break;
            case Tag::Flag: flags.push_back(t.value); break;
            case Tag::Surface: surfaces.push_back(t.code); break;
            case Tag::Type: types.push_back(t.code); break;
            case Tag::Aim: explicitTarget = sc::Target::Aim; break;
            case Tag::Gaze: explicitTarget = sc::Target::Gaze; break;
            case Tag::Other: explicitTarget = sc::Target::Other; break;
            case Tag::LastTarget: explicitTarget = sc::Target::Last; break;
            case Tag::Cancel: cancel = true; break;
            case Tag::Until:
                clause.standing.until = t.value == 1 ? StandingHint::Until::Calm : t.value == 2 ? StandingHint::Until::GroupGone : StandingHint::Until::Relieved;
                break;
            case Tag::Stay: clause.standing.stay = true; break;
            case Tag::NoRetreat: clause.standing.noRetreat = true; break;
            case Tag::ComeBack: clause.standing.comeBack = true; break;
            case Tag::Repeat: repeat = true; break;
            case Tag::No: confidence *= 0.9f; break;
            case Tag::Generic:
            case Tag::Stop:
            case Tag::Conj:
                break;
            case Tag::None:
                unknown.push_back(t.text);
                break;
            }
        }
        if (cancel) {
            parsed.cancel = true;
            parsed.empty = false;
            act.order = sc::Order::Cancel;
            clause.confidence = 1.0f;
            parsed.clauses.push_back(clause);
            continue;
        }
        // Who: this clause's, else the one carried from the last, else everyone (a power or a weapon needs one).
        if (act.who.kind == sc::Who::None) act.who = carried;
        carried = act.who;
        // The order.
        if (repeat) {
            act.order = sc::Order::Repeat;
        } else if (support) {
            act.order = sc::Order::Attack;  // what they attack, or where they are: the dispatcher decides from the supported one's order
            if (clause.supportOf == 0) {
                unresolved += "whom to support; ";
                confidence *= 0.5f;
            }
        } else if (verb) {
            act.order = *verb;
            if (*verb == sc::Order::Use && act.power.empty() && act.weapon < 0) {
                unresolved += "what to use; ";
                confidence *= 0.5f;
            }
        } else if (!act.power.empty()) {
            act.order = sc::Order::Use;
        } else if (act.weapon >= 0 || equip) {
            act.order = sc::Order::None;  // an equip
        } else if (clause.standing.stay && deictic < 0 && !explicitTarget) {
            act.order = sc::Order::Hold;  // "stay there" alone: hold where they are
        } else if (deictic > 0 && deicticPlace) {
            act.order = sc::Order::Move;
        } else if (deictic > 0 || explicitTarget || sector || elevation || !flags.empty() || !surfaces.empty() || !types.empty()) {
            act.order = sc::Order::Attack;  // "Garrus, that one"; "the ones on the left"
        }
        bool const hasOrder = act.order != sc::Order::None || act.weapon >= 0 || !act.power.empty();
        if (!hasOrder) {
            if (!unknown.empty() || anyOrderWord) {
                unresolved += "the order; ";
                confidence *= 0.4f;
            } else {
                clause.confidence = 0.0f;
                clause.unresolved = "nothing to do";
                parsed.clauses.push_back(clause);
                continue;
            }
        }
        parsed.empty = false;
        if (equip && act.weapon < 0) {
            unresolved += "which weapon; ";
            confidence *= 0.5f;
        }
        if (act.who.kind == sc::Who::None && (act.weapon >= 0 || !act.power.empty())) {
            unresolved += "who; ";
            confidence *= 0.5f;
        }
        if (act.who.kind == sc::Who::None) act.who.kind = sc::Who::All;
        // The target: an explicit kind, else a description that fits one group, else the deictic, else the description's best.
        if (explicitTarget) {
            act.target.kind = *explicitTarget;
        } else {
            bool const described = sector || elevation || !flags.empty() || !surfaces.empty() || !types.empty();
            std::vector<sc::Group const*> fit;
            if (described) {
                for (sc::Group const& g : groups) {
                    bool ok = (!sector || g.sector == *sector) && (!elevation || g.elevation == *elevation);
                    for (int f : flags) {
                        ok = ok && (f == Nearest ? g.nearest : f == Farthest ? g.farthest : f == Leftmost ? g.leftmost
                                       : f == Rightmost ? g.rightmost : f == Highest ? g.highest : g.biggest);
                    }
                    for (std::string const& s : surfaces) ok = ok && SameSurface(s, g.surface);
                    for (std::string const& ty : types) {
                        bool found = false;
                        for (std::string const& w : Tokens(g.type)) found = found || w == ty;
                        ok = ok && found;
                    }
                    if (ok) fit.push_back(&g);
                }
            }
            if (fit.size() == 1) {
                act.target = {sc::Target::Group, fit[0]->id};
            } else if (deictic > 0) {
                act.target = {sc::Target::Pointed, deictic};
                if (described && fit.empty()) {
                    unresolved += "the description fits no group; ";
                    confidence *= 0.7f;
                }
            } else if (fit.size() > 1) {
                sc::Group const* nearest = fit[0];
                for (sc::Group const* g : fit) {
                    if (g->distanceM < nearest->distanceM) nearest = g;
                }
                act.target = {sc::Target::Group, nearest->id};
                unresolved += std::format("the description fits {} groups; ", fit.size());
                confidence *= 0.7f;
            } else if (described) {
                unresolved += "the description fits no group; ";
                confidence *= 0.5f;
            } else if ((act.order == sc::Order::Attack || act.order == sc::Order::Use) && !support) {
                confidence *= 0.9f;  // the gun's target, then the look (Validate's fallback)
            }
        }
        for (std::size_t i = 0; i < unknown.size(); ++i) {
            confidence *= 0.85f;
        }
        if (!unknown.empty()) {
            unresolved += "words: ";
            for (std::string const& u : unknown) unresolved += u + " ";
        }
        clause.confidence = std::clamp(confidence, 0.0f, 1.0f);
        clause.unresolved = unresolved;
        parsed.clauses.push_back(clause);
    }
    parsed.confidence = parsed.empty ? 0.0f : 1.0f;
    for (Clause const& c : parsed.clauses) {
        if (c.confidence > 0.0f || !parsed.empty) parsed.confidence = std::min(parsed.confidence, c.confidence);
    }
    if (parsed.empty) parsed.confidence = 0.0f;
    return parsed;
}

}  // namespace mevr::vr::squad_parser
