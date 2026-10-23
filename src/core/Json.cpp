#include "core/Json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mevr::json {
namespace {

std::string const kEmptyString;
Value const kNull;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::optional<Value> Run(std::string* error) {
        Skip();
        Value v;
        if (!ParseValue(v, 0)) {
            if (error != nullptr) {
                *error = error_.empty() ? "invalid JSON" : error_;
                *error += " at offset " + std::to_string(pos_);
            }
            return std::nullopt;
        }
        Skip();
        if (pos_ != text_.size()) {
            if (error != nullptr) {
                *error = "trailing characters at offset " + std::to_string(pos_);
            }
            return std::nullopt;
        }
        return v;
    }

private:
    void Skip() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r' || text_[pos_] == '\n')) {
            ++pos_;
        }
    }
    bool Fail(char const* what) {
        if (error_.empty()) {
            error_ = what;
        }
        return false;
    }
    bool Literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) {
            return Fail("unknown literal");
        }
        pos_ += word.size();
        return true;
    }
    static void AppendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool Hex4(std::uint32_t& out) {
        if (pos_ + 4 > text_.size()) {
            return Fail("short \\u escape");
        }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char const c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') {
                out |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                out |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                out |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return Fail("bad \\u escape");
            }
        }
        return true;
    }
    bool ParseString(std::string& out) {
        ++pos_;  // the opening quote
        while (pos_ < text_.size()) {
            char const c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return Fail("control character in a string");
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                break;
            }
            char const e = text_[pos_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t cp = 0;
                if (!Hex4(cp)) {
                    return false;
                }
                if (cp >= 0xD800 && cp < 0xDC00 && text_.substr(pos_, 2) == "\\u") {
                    pos_ += 2;
                    std::uint32_t low = 0;
                    if (!Hex4(low)) {
                        return false;
                    }
                    if (low >= 0xDC00 && low < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        AppendUtf8(out, cp);
                        cp = low;
                    }
                }
                AppendUtf8(out, cp);
                break;
            }
            default:
                return Fail("bad escape");
            }
        }
        return Fail("unterminated string");
    }
    bool ParseNumber(Value& out) {
        std::size_t const start = pos_;
        if (text_[pos_] == '-') {
            ++pos_;
        }
        bool digits = false;
        while (pos_ < text_.size() && ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '.' || text_[pos_] == 'e' ||
                                          text_[pos_] == 'E' || text_[pos_] == '+' || text_[pos_] == '-')) {
            digits = digits || (text_[pos_] >= '0' && text_[pos_] <= '9');
            ++pos_;
        }
        if (!digits) {
            return Fail("bad number");
        }
        std::string const number(text_.substr(start, pos_ - start));
        char* end = nullptr;
        double const value = std::strtod(number.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(value)) {
            return Fail("bad number");
        }
        out = Value(value);
        return true;
    }
    bool ParseValue(Value& out, int depth) {
        if (depth > 64) {
            return Fail("nested too deeply");
        }
        if (pos_ >= text_.size()) {
            return Fail("unexpected end");
        }
        char const c = text_[pos_];
        if (c == '{') {
            ++pos_;
            out = Value::Object();
            Skip();
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            for (;;) {
                Skip();
                if (pos_ >= text_.size() || text_[pos_] != '"') {
                    return Fail("expected a key");
                }
                std::string key;
                if (!ParseString(key)) {
                    return false;
                }
                Skip();
                if (pos_ >= text_.size() || text_[pos_] != ':') {
                    return Fail("expected ':'");
                }
                ++pos_;
                Skip();
                Value member;
                if (!ParseValue(member, depth + 1)) {
                    return false;
                }
                out[key] = std::move(member);
                Skip();
                if (pos_ < text_.size() && text_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < text_.size() && text_[pos_] == '}') {
                    ++pos_;
                    return true;
                }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++pos_;
            out = Value::Array();
            Skip();
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            for (;;) {
                Skip();
                Value item;
                if (!ParseValue(item, depth + 1)) {
                    return false;
                }
                out.Push(std::move(item));
                Skip();
                if (pos_ < text_.size() && text_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < text_.size() && text_[pos_] == ']') {
                    ++pos_;
                    return true;
                }
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string s;
            if (!ParseString(s)) {
                return false;
            }
            out = Value(std::move(s));
            return true;
        }
        if (c == 't') {
            out = Value(true);
            return Literal("true");
        }
        if (c == 'f') {
            out = Value(false);
            return Literal("false");
        }
        if (c == 'n') {
            out = Value();
            return Literal("null");
        }
        return ParseNumber(out);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::string error_;
};

void WriteString(std::string& out, std::string const& s) {
    out += '"';
    for (char const c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void WriteValue(std::string& out, Value const& v, int indent, int depth) {
    auto newline = [&](int level) {
        if (indent > 0) {
            out += '\n';
            out.append(static_cast<std::size_t>(indent * level), ' ');
        }
    };
    switch (v.GetKind()) {
    case Value::Kind::Null: out += "null"; break;
    case Value::Kind::Bool: out += v.AsBool() ? "true" : "false"; break;
    case Value::Kind::Number: {
        double const n = v.AsNumber();
        char buffer[40];
        if (std::fabs(n) < 9.0e15 && n == std::floor(n)) {
            std::snprintf(buffer, sizeof buffer, "%lld", static_cast<long long>(n));
        } else {
            std::snprintf(buffer, sizeof buffer, "%.17g", n);
        }
        out += buffer;
        break;
    }
    case Value::Kind::String: WriteString(out, v.AsString()); break;
    case Value::Kind::Array: {
        if (v.Items().empty()) {
            out += "[]";
            break;
        }
        out += '[';
        bool first = true;
        for (Value const& item : v.Items()) {
            if (!first) {
                out += ',';
            }
            first = false;
            newline(depth + 1);
            WriteValue(out, item, indent, depth + 1);
        }
        newline(depth);
        out += ']';
        break;
    }
    case Value::Kind::Object: {
        if (v.Members().empty()) {
            out += "{}";
            break;
        }
        out += '{';
        bool first = true;
        for (auto const& [key, member] : v.Members()) {
            if (!first) {
                out += ',';
            }
            first = false;
            newline(depth + 1);
            WriteString(out, key);
            out += indent > 0 ? ": " : ":";
            WriteValue(out, member, indent, depth + 1);
        }
        newline(depth);
        out += '}';
        break;
    }
    }
}

} // namespace

std::string const& Value::AsString() const {
    return kind_ == Kind::String ? string_ : kEmptyString;
}

void Value::Push(Value v) {
    if (kind_ == Kind::Null) {
        kind_ = Kind::Array;
    }
    array_.push_back(std::move(v));
}

Value const& Value::Get(std::string const& key) const {
    if (kind_ != Kind::Object) {
        return kNull;
    }
    auto const it = object_.find(key);
    return it != object_.end() ? it->second : kNull;
}

bool Value::Has(std::string const& key) const {
    return kind_ == Kind::Object && object_.contains(key);
}

Value& Value::operator[](std::string const& key) {
    if (kind_ == Kind::Null) {
        kind_ = Kind::Object;
    }
    return object_[key];
}

std::string Value::String(std::string const& key, std::string const& fallback) const {
    Value const& v = Get(key);
    return v.IsString() ? v.AsString() : fallback;
}

double Value::Number(std::string const& key, double fallback) const {
    return Get(key).AsNumber(fallback);
}

bool Value::Bool(std::string const& key, bool fallback) const {
    return Get(key).AsBool(fallback);
}

std::optional<Value> Parse(std::string_view text, std::string* error) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);  // a UTF-8 byte order mark
    }
    return Parser(text).Run(error);
}

std::string Write(Value const& value, int indent) {
    std::string out;
    WriteValue(out, value, indent, 0);
    if (indent > 0) {
        out += '\n';
    }
    return out;
}

} // namespace mevr::json
