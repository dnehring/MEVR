// SPDX-License-Identifier: MIT
#include "core/d3d11/ShaderReflect.hpp"

#include <cstring>

namespace mevr::d3d11::reflect {
namespace {

// A view of bytes that never reads past its end.
struct Bytes {
    std::uint8_t const* p = nullptr;
    std::size_t n = 0;

    bool U32(std::size_t at, std::uint32_t& out) const {
        if (at > n || n - at < 4) {
            return false;
        }
        std::memcpy(&out, p + at, 4);
        return true;
    }
    bool U16(std::size_t at, std::uint16_t& out) const {
        if (at > n || n - at < 2) {
            return false;
        }
        std::memcpy(&out, p + at, 2);
        return true;
    }
    // A NUL-terminated name at `at`, at most 256 characters.
    bool Name(std::size_t at, std::string& out) const {
        out.clear();
        for (std::size_t i = at; i < n && i - at < 256; ++i) {
            if (p[i] == 0) {
                return true;
            }
            out.push_back(static_cast<char>(p[i]));
        }
        return false;
    }
};

constexpr std::uint32_t FourCC(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24);
}

// The RDEF chunk's data, or an empty view.
Bytes FindRdef(Bytes const& all) {
    std::uint32_t magic = 0, chunks = 0;
    if (!all.U32(0, magic) || magic != FourCC('D', 'X', 'B', 'C') || !all.U32(28, chunks) || chunks > 64) {
        return {};
    }
    for (std::uint32_t i = 0; i < chunks; ++i) {
        std::uint32_t at = 0, fourcc = 0, size = 0;
        if (!all.U32(32 + std::size_t{i} * 4, at) || !all.U32(at, fourcc) || !all.U32(std::size_t{at} + 4, size)) {
            return {};
        }
        if (fourcc == FourCC('R', 'D', 'E', 'F')) {
            std::size_t const start = std::size_t{at} + 8;
            if (start > all.n || all.n - start < size) {
                return {};
            }
            return {all.p + start, size};
        }
    }
    return {};
}

} // namespace

std::vector<Variable> Variables(void const* bytecode, std::size_t size, NameFilter want) {
    std::vector<Variable> out;
    if (bytecode == nullptr || size < 32) {
        return out;
    }
    Bytes const all{static_cast<std::uint8_t const*>(bytecode), size};
    Bytes const rdef = FindRdef(all);
    std::uint32_t cbCount = 0, cbAt = 0, bindCount = 0, bindAt = 0, target = 0;
    if (rdef.p == nullptr || !rdef.U32(0, cbCount) || !rdef.U32(4, cbAt) || !rdef.U32(8, bindCount) ||
        !rdef.U32(12, bindAt) || !rdef.U32(16, target) || cbCount > 64 || bindCount > 512) {
        return out;
    }
    // Shader model 5 adds an "RD11" block after the header with the sizes of each record; 4.x uses the short ones.
    std::uint32_t cbSize = 24, bindSize = 32, varSize = 24;
    std::uint32_t rd11 = 0;
    if (rdef.U32(28, rd11) && rd11 == FourCC('R', 'D', '1', '1')) {
        std::uint32_t s = 0;
        cbSize = rdef.U32(36, s) && s >= 24 ? s : 24;
        bindSize = rdef.U32(40, s) && s >= 32 ? s : 32;
        varSize = rdef.U32(44, s) && s >= 24 ? s : 40;
    }
    // The constant buffers' bind points come from the resource bindings of the same name.
    struct Binding {
        std::string name;
        std::uint32_t slot;
    };
    std::vector<Binding> bindings;
    for (std::uint32_t i = 0; i < bindCount; ++i) {
        std::size_t const at = std::size_t{bindAt} + std::size_t{i} * bindSize;
        std::uint32_t nameAt = 0, type = 0, slot = 0;
        if (!rdef.U32(at, nameAt) || !rdef.U32(at + 4, type) || !rdef.U32(at + 20, slot)) {
            return {};
        }
        if (type != 0) {  // D3D_SIT_CBUFFER
            continue;
        }
        Binding b{{}, slot};
        if (!rdef.Name(nameAt, b.name)) {
            return {};
        }
        bindings.push_back(std::move(b));
    }
    for (std::uint32_t i = 0; i < cbCount; ++i) {
        std::size_t const at = std::size_t{cbAt} + std::size_t{i} * cbSize;
        std::uint32_t nameAt = 0, varCount = 0, varAt = 0, type = 0;
        if (!rdef.U32(at, nameAt) || !rdef.U32(at + 4, varCount) || !rdef.U32(at + 8, varAt) || !rdef.U32(at + 20, type) ||
            varCount > 4096) {
            return {};
        }
        if (type != 0) {  // D3D_CT_CBUFFER (not a tbuffer or a structured buffer's layout)
            continue;
        }
        std::string cbName;
        if (!rdef.Name(nameAt, cbName)) {
            return {};
        }
        std::uint32_t slot = UINT32_MAX;
        for (Binding const& b : bindings) {
            if (b.name == cbName) {
                slot = b.slot;
                break;
            }
        }
        if (slot == UINT32_MAX) {
            continue;  // declared but not bound: nothing the shader reads
        }
        for (std::uint32_t v = 0; v < varCount; ++v) {
            std::size_t const va = std::size_t{varAt} + std::size_t{v} * varSize;
            std::uint32_t vNameAt = 0, start = 0, bytes = 0, typeAt = 0;
            if (!rdef.U32(va, vNameAt) || !rdef.U32(va + 4, start) || !rdef.U32(va + 8, bytes) || !rdef.U32(va + 16, typeAt)) {
                return {};
            }
            Variable var;
            if (!rdef.Name(vNameAt, var.name)) {
                return {};
            }
            if (want != nullptr && !want(var.name)) {
                continue;
            }
            var.slot = slot;
            var.offset = start;
            var.size = bytes;
            if (!rdef.U16(typeAt, var.varClass) || !rdef.U16(std::size_t{typeAt} + 4, var.rows) ||
                !rdef.U16(std::size_t{typeAt} + 6, var.columns)) {
                return {};
            }
            out.push_back(std::move(var));
        }
    }
    return out;
}

} // namespace mevr::d3d11::reflect
