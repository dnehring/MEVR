#include "core/ik/BoneNames.hpp"

#include <algorithm>
#include <cctype>

namespace mevr::ik {
namespace {

bool Has(std::vector<std::string> const& tokens, std::string_view token) {
    return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
}

bool HasAny(std::vector<std::string> const& tokens, std::initializer_list<std::string_view> words) {
    return std::any_of(words.begin(), words.end(), [&](std::string_view word) { return Has(tokens, word); });
}

} // namespace

std::vector<std::string> NameTokens(std::string_view name) {
    std::vector<std::string> tokens;
    std::string current;
    auto flush = [&] {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };
    for (std::size_t i = 0; i < name.size(); ++i) {
        unsigned char const c = static_cast<unsigned char>(name[i]);
        if (!std::isalnum(c)) {
            flush();  // separators: _ - . space
            continue;
        }
        bool const upper = std::isupper(c) != 0;
        bool const digit = std::isdigit(c) != 0;
        if (!current.empty()) {
            unsigned char const previous = static_cast<unsigned char>(name[i - 1]);
            bool const camelBreak = upper && std::islower(previous);   // rightHand -> right | hand
            bool const digitBreak = digit != (std::isdigit(previous) != 0);  // Finger1 -> finger | 1
            if (camelBreak || digitBreak) {
                flush();
            }
        }
        current.push_back(static_cast<char>(std::tolower(c)));
    }
    flush();
    return tokens;
}

int HandSide(std::string_view boneName) {
    std::vector<std::string> const tokens = NameTokens(boneName);
    if (!HasAny(tokens, {"hand", "wrist"}) || HasAny(tokens, {"finger", "thumb", "index", "middle", "ring", "pinky", "end", "nub"})) {
        return -1;
    }
    bool const right = HasAny(tokens, {"r", "right", "rt"});
    bool const left = HasAny(tokens, {"l", "left", "lf", "lt"});
    if (right == left) {
        return -1;
    }
    return right ? 1 : 0;
}

bool IsHeadBone(std::string_view boneName) {
    std::vector<std::string> const tokens = NameTokens(boneName);
    return Has(tokens, "head") &&
        !HasAny(tokens, {"end", "nub", "top", "jaw", "eye", "brow", "lid", "lip", "tongue", "teeth", "neck", "gear"});
}

} // namespace mevr::ik
