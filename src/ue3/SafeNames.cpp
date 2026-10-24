// SPDX-License-Identifier: MIT
#include "ue3/SafeNames.hpp"

namespace mevr::ue3::safe {
namespace {

constexpr std::size_t kMaxNameChars = 256;
constexpr int kMaxOuters = 8;

char Fold(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool ReadPtr(Memory const& m, std::uintptr_t address, std::uintptr_t& out) {
    out = 0;
    return m.Read(address, out);
}

bool NameOf(Memory const& m, std::uintptr_t object, Name& out) {
    return object != 0 && m.Read(object + m.layout.objectName, out);
}

// The name's base text, without its number.
bool AppendBase(Memory const& m, std::int32_t index, text::Appender& out) {
    if (index == 0) {
        out.Str("None");
        return true;
    }
    std::uint32_t const raw = static_cast<std::uint32_t>(index);
    std::uint32_t const pool = (raw >> m.layout.namePoolShift) & m.layout.namePoolMask;
    std::uintptr_t base = 0;
    if (pool >= m.poolCount || !ReadPtr(m, m.poolArray + pool * sizeof(std::uintptr_t), base) || base == 0) {
        return false;
    }
    std::uintptr_t const entry = base + (raw & m.layout.nameOffsetMask);
    std::uint32_t header = 0;
    if (!m.Read(entry, header)) {
        return false;
    }
    bool const wide = (header & m.layout.nameWideFlag) != 0;
    std::uintptr_t const chars = entry + m.layout.nameChars;
    std::size_t const start = out.Size();
    for (std::size_t i = 0; i < kMaxNameChars; ++i) {
        std::uint32_t c = 0;
        if (wide) {
            std::uint16_t w = 0;
            if (!m.Read(chars + i * 2, w)) {
                out.Rewind(start);
                return false;
            }
            c = w;
        } else {
            std::uint8_t b = 0;
            if (!m.Read(chars + i, b)) {
                out.Rewind(start);
                return false;
            }
            c = b;
        }
        if (c == 0) {
            return true;
        }
        out.Char(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?');
    }
    return true;  // cut at kMaxNameChars
}

} // namespace

bool AppendName(Memory const& m, Name name, text::Appender& out) {
    if (!AppendBase(m, name.index, out)) {
        out.Str("<name?>");
        return false;
    }
    if (name.number != 0) {
        out.Char('_').Dec(static_cast<std::int64_t>(name.number) - 1);
    }
    return true;
}

bool Listed(Memory const& m, std::uintptr_t object) {
    if (object == 0 || (object & 7) != 0 || m.objects == 0) {
        return false;
    }
    std::int32_t index = -1;
    TArray<Object*> objects;
    if (!m.Read(object + m.layout.objectIndex, index) || !m.Read(m.objects, objects) || index < 0 || index >= objects.count ||
        objects.data == nullptr) {
        return false;
    }
    std::uintptr_t slot = 0;
    return ReadPtr(m, reinterpret_cast<std::uintptr_t>(objects.data) + static_cast<std::uintptr_t>(index) * sizeof(void*), slot) &&
        slot == object;
}

bool AppendObjectName(Memory const& m, std::uintptr_t object, text::Appender& out) {
    if (object == 0) {
        out.Str("None");
        return true;
    }
    Name name;
    if (!NameOf(m, object, name)) {
        out.Str("<object?>");
        return false;
    }
    return AppendName(m, name, out);
}

bool AppendPath(Memory const& m, std::uintptr_t object, text::Appender& out) {
    if (object == 0) {
        out.Str("None");
        return true;
    }
    // Outermost first: collect the chain, then print it backwards.
    std::uintptr_t chain[kMaxOuters + 1];
    int count = 0;
    std::uintptr_t at = object;
    bool cut = false;
    while (at != 0) {
        if (count == kMaxOuters + 1) {
            cut = true;
            break;
        }
        chain[count++] = at;
        std::uintptr_t outer = 0;
        if (!ReadPtr(m, at + m.layout.objectOuter, outer)) {
            break;
        }
        at = outer;
    }
    if (cut) {
        out.Str("...");
    }
    bool ok = true;
    for (int i = count - 1; i >= 0; --i) {
        ok = AppendObjectName(m, chain[i], out) && ok;
        if (i > 0) {
            out.Char('.');
        }
    }
    return ok;
}

bool AppendFull(Memory const& m, std::uintptr_t object, text::Appender& out) {
    if (object == 0) {
        out.Str("None");
        return true;
    }
    std::uintptr_t cls = 0;
    bool ok = ReadPtr(m, object + m.layout.objectClass, cls);
    if (ok) {
        ok = AppendObjectName(m, cls, out);
    } else {
        out.Str("<class?>");
    }
    out.Char(' ');
    return AppendPath(m, object, out) && ok;
}

bool NameIs(Memory const& m, std::uintptr_t object, std::string_view text) {
    Name name;
    if (!NameOf(m, object, name) || name.number != 0) {
        return false;
    }
    char buffer[kMaxNameChars + 1];
    text::Appender a(buffer, sizeof(buffer));
    if (!AppendBase(m, name.index, a) || a.Size() != text.size()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (Fold(buffer[i]) != Fold(text[i])) {
            return false;
        }
    }
    return true;
}

std::size_t FindFrames(Memory const& m, FrameLayout const& layout, std::uintptr_t lo, std::uintptr_t hi, Frame* out,
    std::size_t max) {
    std::size_t found = 0;
    lo = (lo + 7) & ~std::uintptr_t{7};
    // A page at a time: one read each instead of one per slot.
    constexpr std::size_t kChunk = 4096;
    std::uintptr_t words[kChunk / sizeof(std::uintptr_t)];
    for (std::uintptr_t page = lo; page < hi && found < max;) {
        std::uintptr_t const pageEnd = (page & ~std::uintptr_t{kChunk - 1}) + kChunk;
        std::uintptr_t const end = pageEnd < hi ? pageEnd : hi;
        std::size_t const bytes = static_cast<std::size_t>(end - page) & ~std::size_t{7};
        if (bytes == 0 || m.read == nullptr || !m.read(reinterpret_cast<void const*>(page), words, bytes)) {
            page = end;
            continue;
        }
        for (std::size_t i = 0; i < bytes / sizeof(std::uintptr_t) && found < max; ++i) {
            std::uintptr_t const vtable = words[i];
            if (vtable < layout.vtableLo || vtable >= layout.vtableHi) {
                continue;
            }
            std::uintptr_t const at = page + i * sizeof(std::uintptr_t);
            Frame f;
            f.at = at;
            if (!ReadPtr(m, at + layout.node, f.node) || !ReadPtr(m, at + layout.object, f.object) ||
                !Listed(m, f.node) || !Listed(m, f.object)) {
                continue;
            }
            std::uintptr_t nodeClass = 0;
            if (!ReadPtr(m, f.node + m.layout.objectClass, nodeClass) ||
                !(NameIs(m, nodeClass, "Function") || NameIs(m, nodeClass, "State") || NameIs(m, nodeClass, "Class"))) {
                continue;
            }
            ReadPtr(m, at + layout.code, f.code);
            ReadPtr(m, at + layout.previous, f.previous);
            TArray<std::uint8_t> script;
            if (layout.structScript != 0 && m.Read(f.node + layout.structScript, script) && script.data != nullptr &&
                script.count >= 0) {
                f.scriptSize = script.count;
                auto const data = reinterpret_cast<std::uintptr_t>(script.data);
                if (f.code >= data && f.code <= data + static_cast<std::uintptr_t>(script.count)) {
                    f.codeOffset = static_cast<std::int64_t>(f.code - data);
                }
            }
            out[found++] = f;
        }
        page = end;
    }
    return found;
}

} // namespace mevr::ue3::safe
