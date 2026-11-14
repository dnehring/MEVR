// SPDX-License-Identifier: MIT
#include "core/qa/ControlProtocol.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <utility>

namespace mevr::qa::control {
namespace {
bool Hex(std::string_view s, std::size_t length) {
    if (s.size() != length) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
bool Uuid(std::string_view s) {
    if (s.size() != 36) return false;
    std::string hex;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (s[i] != '-') return false; }
        else hex += s[i];
    }
    return Hex(hex, 32) && hex != std::string(32, '0');
}
bool Number(std::string_view s, std::uint64_t& out) {
    if (s.empty() || (s.size() > 1 && s.front() == '0')) return false;
    auto const [end, error] = std::from_chars(s.data(), s.data() + s.size(), out);
    return error == std::errc{} && end == s.data() + s.size();
}
bool ForbiddenConsole(std::string_view command) {
    std::string folded(command);
    for (char& c : folded) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    auto word = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; };
    constexpr std::string_view forbidden = "streamlevelin";
    for (auto pos = folded.find(forbidden); pos != std::string::npos; pos = folded.find(forbidden, pos + 1)) {
        auto const end = pos + forbidden.size();
        if ((pos == 0 || !word(folded[pos - 1])) && (end == folded.size() || !word(folded[end]))) return true;
    }
    return false;
}
bool Valid(Frame const& f) {
    if (!ValidIdentity(f.identity) || !Uuid(f.id) || f.sequence == 0 || f.deadline == 0 ||
        f.payload.size() > kMaxPayload || f.payload.find('\0') != std::string::npos) return false;
    if (f.verb != "ping" && f.verb != "observe" && f.verb != "observe-save-menu" &&
        f.verb != "console" && f.verb != "probe-fov" && f.verb != "advance-splash" && f.verb != "open-load-menu") return false;
    if (f.kind == "request") {
        if (f.status != "pending") return false;
        if (f.verb != "console") return f.payload.empty();
        // One native command only; no implicit batch or embedded control input.
        if (f.payload.empty() || f.payload.size() > 4096) return false;
        for (unsigned char c : f.payload) if (c < 32 || c == 127) return false;
        return true;
    }
    return f.kind == "result" && (f.status == "completed" || f.status == "dispatched" ||
        f.status == "rejected" || f.status == "error");
}
constexpr std::array<std::string_view, 15> kKeys{
    "kind", "run", "lease", "pid", "start", "exe", "host", "mod", "id", "sequence",
    "deadline", "verb", "status", "bytes", "game"};
constexpr std::string_view kMagic = "MEVR-CONTROL/1\n";
} // namespace

bool ValidIdentity(Identity const& i) {
    return Uuid(i.run) && Uuid(i.lease) && i.pid != 0 && i.start != 0 &&
        Hex(i.exe, 64) && Hex(i.host, 64) && Hex(i.mod, 64);
}
std::optional<Frame> Decode(std::string_view bytes, std::string* error) {
    auto fail = [&](char const* reason) -> std::optional<Frame> { if (error) *error = reason; return {}; };
    if (bytes.size() > kMaxFrame || !bytes.starts_with(kMagic)) return fail("size or protocol");
    bytes.remove_prefix(kMagic.size());
    std::array<std::string, kKeys.size()> values;
    for (std::size_t n = 0; n < kKeys.size(); ++n) {
        auto const end = bytes.find('\n');
        if (end == std::string_view::npos) return fail("incomplete header");
        auto line = bytes.substr(0, end);
        std::string const prefix = std::string(kKeys[n]) + "=";
        if (!line.starts_with(prefix)) return fail("unknown, duplicate or reordered field");
        auto const value = line.substr(prefix.size());
        if (value.find('\r') != std::string_view::npos || value.find('\0') != std::string_view::npos)
            return fail("header control character");
        values[n] = value;
        bytes.remove_prefix(end + 1);
    }
    if (bytes.empty() || bytes.front() != '\n' || values[14] != "LE2") return fail("separator or game");
    bytes.remove_prefix(1);
    std::uint64_t pid{}, start{}, sequence{}, deadline{}, size{};
    if (!Number(values[3], pid) || pid > std::numeric_limits<std::uint32_t>::max() ||
        !Number(values[4], start) || !Number(values[9], sequence) || !Number(values[10], deadline) ||
        !Number(values[13], size) || size != bytes.size()) return fail("number or payload length");
    Frame f{values[0], {values[1], values[2], static_cast<std::uint32_t>(pid), start,
        values[5], values[6], values[7]}, values[8], sequence, deadline, values[11], values[12], std::string(bytes)};
    if (!Valid(f)) return fail("invalid frame");
    return f;
}
std::optional<std::string> Encode(Frame const& f) {
    if (!Valid(f)) return {};
    std::array<std::string, kKeys.size()> const values{f.kind, f.identity.run, f.identity.lease,
        std::to_string(f.identity.pid), std::to_string(f.identity.start), f.identity.exe, f.identity.host,
        f.identity.mod, f.id, std::to_string(f.sequence), std::to_string(f.deadline), f.verb, f.status,
        std::to_string(f.payload.size()), "LE2"};
    std::string out(kMagic);
    for (std::size_t n = 0; n < kKeys.size(); ++n) out += std::string(kKeys[n]) + "=" + values[n] + "\n";
    out += "\n" + f.payload;
    return out;
}
Gate::Gate(Identity identity, bool allowConsole, bool allowProbeFov, bool allowMenuActions)
    : identity_(std::move(identity)), allowConsole_(allowConsole), allowProbeFov_(allowProbeFov),
      allowMenuActions_(allowMenuActions) {}
std::optional<std::string> Gate::Claim(Frame const& f, std::uint64_t now) {
    if (!Valid(f) || f.kind != "request") return "invalid request";
    if (!ValidIdentity(identity_) || !(f.identity == identity_)) return "identity mismatch";
    if (f.deadline <= now || f.deadline - now > kMaxLifetimeMs) return "expired or excessive deadline";
    if (f.sequence <= sequence_ || ids_.contains(f.id)) return "duplicate or stale request";
    if (ids_.size() >= kMaxRequests) return "lease request limit reached";
    if (f.verb == "probe-fov" && !allowProbeFov_) return "native FOV probe is disabled";
    if ((f.verb == "advance-splash" || f.verb == "open-load-menu") && !allowMenuActions_)
        return "native menu actions are disabled";
    if (f.verb == "console" && !allowConsole_) return "native console is disabled";
    if (f.verb == "console" && ForbiddenConsole(f.payload)) return "streamlevelin is prohibited";
    sequence_ = f.sequence;
    ids_.insert(f.id);
    return {};
}
} // namespace mevr::qa::control
