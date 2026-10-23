// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A small JSON value with a parser and a writer: enough for the installer's manifest (MEVR-install.json), the
// setup request file and the GitHub Releases answer. UTF-8 in and out; \u escapes become UTF-8 (surrogate pairs
// joined). Objects keep their keys sorted (std::map), so a written file is stable. Numbers are doubles.
namespace mevr::json {

class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : kind_(Kind::Bool), bool_(b) {}
    Value(double n) : kind_(Kind::Number), number_(n) {}
    Value(int n) : kind_(Kind::Number), number_(n) {}
    Value(std::int64_t n) : kind_(Kind::Number), number_(static_cast<double>(n)) {}
    Value(std::uint64_t n) : kind_(Kind::Number), number_(static_cast<double>(n)) {}
    Value(char const* s) : kind_(Kind::String), string_(s) {}
    Value(std::string s) : kind_(Kind::String), string_(std::move(s)) {}
    Value(std::string_view s) : kind_(Kind::String), string_(s) {}

    static Value Array() {
        Value v;
        v.kind_ = Kind::Array;
        return v;
    }
    static Value Object() {
        Value v;
        v.kind_ = Kind::Object;
        return v;
    }

    Kind GetKind() const { return kind_; }
    bool IsNull() const { return kind_ == Kind::Null; }
    bool IsObject() const { return kind_ == Kind::Object; }
    bool IsArray() const { return kind_ == Kind::Array; }
    bool IsString() const { return kind_ == Kind::String; }

    // Typed reads with a fallback for the wrong kind.
    bool AsBool(bool fallback = false) const { return kind_ == Kind::Bool ? bool_ : fallback; }
    double AsNumber(double fallback = 0.0) const { return kind_ == Kind::Number ? number_ : fallback; }
    std::string const& AsString() const;

    // Arrays.
    std::vector<Value> const& Items() const { return array_; }
    std::vector<Value>& Items() { return array_; }
    void Push(Value v);

    // Objects. Get returns a null value for a missing key (or a non-object).
    Value const& Get(std::string const& key) const;
    bool Has(std::string const& key) const;
    Value& operator[](std::string const& key);  // makes this an object if it's null
    std::map<std::string, Value> const& Members() const { return object_; }
    void Erase(std::string const& key) { object_.erase(key); }

    // Shortcuts: a member's string / number / bool with a fallback.
    std::string String(std::string const& key, std::string const& fallback = {}) const;
    double Number(std::string const& key, double fallback = 0.0) const;
    bool Bool(std::string const& key, bool fallback = false) const;

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::map<std::string, Value> object_;
};

// Parses `text`; nullopt (and `error`, if given) when it isn't valid JSON.
std::optional<Value> Parse(std::string_view text, std::string* error = nullptr);

// Writes `value`; `indent` > 0 pretty-prints with that many spaces per level.
std::string Write(Value const& value, int indent = 2);

} // namespace mevr::json
