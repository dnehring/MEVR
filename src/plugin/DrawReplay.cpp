// SPDX-License-Identifier: MIT
#include "plugin/DrawReplay.hpp"

#include "core/Hook.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"

#include <d3d11.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <intrin.h>
#include <map>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

namespace mevr::host::draw_replay {
namespace {

namespace fs = std::filesystem;

// ID3D11DeviceContext's vtable, in d3d11.h's order (checked against the SDK header; Halcyon's d3d_capture uses the
// same numbers for the entries it hooks).
enum Slot : unsigned {
    kVSSetConstantBuffers = 7, kPSSetShaderResources = 8, kPSSetShader = 9, kPSSetSamplers = 10, kVSSetShader = 11,
    kDrawIndexed = 12, kDraw = 13, kMap = 14, kUnmap = 15, kPSSetConstantBuffers = 16, kIASetInputLayout = 17,
    kIASetVertexBuffers = 18, kIASetIndexBuffer = 19, kDrawIndexedInstanced = 20, kDrawInstanced = 21,
    kGSSetConstantBuffers = 22, kGSSetShader = 23, kIASetPrimitiveTopology = 24, kVSSetShaderResources = 25,
    kVSSetSamplers = 26, kBegin = 27, kEnd = 28, kSetPredication = 30, kGSSetShaderResources = 31,
    kGSSetSamplers = 32, kOMSetRenderTargets = 33, kOMSetRenderTargetsAndUnorderedAccessViews = 34,
    kOMSetBlendState = 35, kOMSetDepthStencilState = 36, kSOSetTargets = 37, kDrawAuto = 38,
    kDrawIndexedInstancedIndirect = 39, kDrawInstancedIndirect = 40, kDispatch = 41, kDispatchIndirect = 42,
    kRSSetState = 43, kRSSetViewports = 44, kRSSetScissorRects = 45, kCopySubresourceRegion = 46, kCopyResource = 47,
    kUpdateSubresource = 48, kCopyStructureCount = 49, kClearRenderTargetView = 50, kClearUnorderedAccessViewUint = 51,
    kClearUnorderedAccessViewFloat = 52, kClearDepthStencilView = 53, kGenerateMips = 54, kSetResourceMinLOD = 55,
    kResolveSubresource = 57, kExecuteCommandList = 58, kHSSetShaderResources = 59, kHSSetShader = 60,
    kHSSetSamplers = 61, kHSSetConstantBuffers = 62, kDSSetShaderResources = 63, kDSSetShader = 64,
    kDSSetSamplers = 65, kDSSetConstantBuffers = 66, kCSSetShaderResources = 67, kCSSetUnorderedAccessViews = 68,
    kCSSetShader = 69, kCSSetSamplers = 70, kCSSetConstantBuffers = 71,
    kSlots = 72
};

enum Stage : std::uint8_t { VS, PS, GS, HS, DS, CS, kStages };
char const* const kStageNames[kStages] = {"VS", "PS", "GS", "HS", "DS", "CS"};

enum class Op : std::uint8_t {
    SetConstantBuffers, SetShaderResources, SetSamplers, SetShader,  // the stage in the header
    IASetInputLayout, IASetVertexBuffers, IASetIndexBuffer, IASetPrimitiveTopology,
    DrawIndexed, Draw, DrawIndexedInstanced, DrawInstanced, DrawAuto,
    Map, Unmap, UpdateSubresource,
    RSSetState, RSSetViewports, RSSetScissorRects,
    OMSetRenderTargets, OMSetBlendState, OMSetDepthStencilState,
    ClearRenderTargetView, ClearDepthStencilView,
    CopyResource, CopySubresourceRegion, ResolveSubresource, GenerateMips,
    Begin, End, SetPredication,
    CSSetUnorderedAccessViews,  // research/37: the engine unbinds compute UAVs a few times a frame
    Dispatch,                   // research/37, experiment 6: a few frames a second carry one in the tail
    Unsupported,  // the slot in `n`: the tail can't be replayed
    kCount
};
char const* const kOpNames[] = {"SetConstantBuffers", "SetShaderResources", "SetSamplers", "SetShader",
    "IASetInputLayout", "IASetVertexBuffers", "IASetIndexBuffer", "IASetPrimitiveTopology", "DrawIndexed", "Draw",
    "DrawIndexedInstanced", "DrawInstanced", "DrawAuto", "Map", "Unmap", "UpdateSubresource", "RSSetState",
    "RSSetViewports", "RSSetScissorRects", "OMSetRenderTargets", "OMSetBlendState", "OMSetDepthStencilState",
    "ClearRenderTargetView", "ClearDepthStencilView", "CopyResource", "CopySubresourceRegion", "ResolveSubresource",
    "GenerateMips", "Begin", "End", "SetPredication", "CSSetUnorderedAccessViews", "Dispatch", "Unsupported"};
static_assert(sizeof(kOpNames) / sizeof(kOpNames[0]) == static_cast<std::size_t>(Op::kCount));

#pragma pack(push, 1)
struct Header {
    Op op;
    std::uint8_t stage;
    std::uint16_t n;     // a count (pointers, viewports...) or the slot of an unsupported call
    std::uint32_t size;  // bytes that follow
};
struct Pointers {  // Set{ConstantBuffers, ShaderResources, Samplers}, OMSetRenderTargets (n targets + the DSV)
    std::uint32_t start;
    void* ptrs[1];
};
struct VertexBuffers {
    std::uint32_t start, count;
    // then void* buffers[count], UINT strides[count], UINT offsets[count]
};
struct IndexBuffer {
    void* buffer;
    std::uint32_t format, offset;
};
struct DrawIndexedArgs {
    std::uint32_t indexCount, startIndex;
    std::int32_t baseVertex;
};
struct DrawArgs {
    std::uint32_t vertexCount, start;
};
struct DrawIndexedInstancedArgs {
    std::uint32_t indexCountPerInstance, instanceCount, startIndex;
    std::int32_t baseVertex;
    std::uint32_t startInstance;
};
struct DrawInstancedArgs {
    std::uint32_t vertexCountPerInstance, instanceCount, startVertex, startInstance;
};
struct MapArgs {
    void* resource;
    std::uint32_t subresource, type, flags;
};
struct UnmapArgs {
    void* resource;
    std::uint32_t subresource, mapType, kind, dataSize;  // kind: 1 constant buffer, 0 other; dataSize bytes follow (0: none)
};
struct UpdateArgs {
    void* resource;
    std::uint32_t subresource, hasBox;
    D3D11_BOX box;
    std::uint32_t rowPitch, depthPitch;
    std::uint32_t kind;  // 1 constant buffer, 0 other buffer, 2 not a buffer (no data: skipped by the replay)
    std::uint32_t dataSize;  // bytes that follow
};
struct BlendArgs {
    void* state;
    float factor[4];
    std::uint32_t mask;
};
struct DepthStencilArgs {
    void* state;
    std::uint32_t reference;
};
struct ClearTargetArgs {
    void* view;
    float colour[4];
};
struct ClearDepthArgs {
    void* view;
    std::uint32_t flags;
    float depth;
    std::uint8_t stencil;
};
struct CopyArgs {
    void* dst;
    void* src;
};
struct CopyRegionArgs {
    void* dst;
    std::uint32_t dstSub, x, y, z;
    void* src;
    std::uint32_t srcSub, hasBox;
    D3D11_BOX box;
};
struct ResolveArgs {
    void* dst;
    std::uint32_t dstSub;
    void* src;
    std::uint32_t srcSub, format;
};
struct PredicationArgs {
    void* predicate;
    std::uint32_t value;
};
struct DispatchArgs {
    std::uint32_t x, y, z;
};
#pragma pack(pop)

constexpr std::size_t kMaxRecordedUpdate = 1u << 20;  // a buffer update larger than this isn't copied (the tail then isn't replayable)
constexpr std::size_t kMaxRecordedMap = 64u << 10;    // a mapped buffer up to this size is copied whole at Unmap
// The substitution looks only at constant updates up to this size: the per-view matrices live in a 96 or 128-byte
// buffer (the eye diff's tail scan, research/37 experiment 8: 96 B +48), and the per-draw 1-4 KB buffers carry only
// the view origin and pre-view translation, whose first-eye values the replay may keep (lighting 6 cm off, the
// geometry right). Scanning every chunk of the 5-7 MB a frame cost more than the re-issue (experiment 9).
constexpr std::size_t kSubstituteMaxBytes = 512;

// The recording's bytes: a buffer that grows without zeroing (std::vector::resize value-initialised the 5-6 MB a
// frame the recording appends: research/37, experiment 8).
struct Bytes {
    std::uint8_t* p = nullptr;
    std::size_t n = 0, cap = 0;
    Bytes() = default;
    Bytes(Bytes const&) = delete;
    Bytes& operator=(Bytes const&) = delete;
    ~Bytes() { std::free(p); }
    std::uint8_t* data() { return p; }
    std::uint8_t const* data() const { return p; }
    std::size_t size() const { return n; }
    std::size_t capacity() const { return cap; }
    void clear() { n = 0; }
    void reserve(std::size_t c) {
        if (c > cap) {
            auto* const q = static_cast<std::uint8_t*>(std::realloc(p, c));
            if (q == nullptr) {
                throw std::bad_alloc();
            }
            p = q;
            cap = c;
        }
    }
    void resize(std::size_t m) {
        if (m > cap) {
            reserve(std::max(m, cap * 2));
        }
        n = m;
    }
    void Free() {
        std::free(p);
        p = nullptr;
        n = cap = 0;
    }
};

struct Stream {
    Bytes bytes;
    std::size_t tail = SIZE_MAX;  // byte offset of the first entry the replay re-issues
    unsigned entries = 0, tailEntries = 0, draws = 0, updates = 0, textureUpdates = 0, mapsCopied = 0, mapsSkipped = 0;
    unsigned unsupported = 0, unsupportedSlot = 0, bigUpdates = 0;
    unsigned bigDiscardsInTail = 0;   // large dynamic buffers discarded after the tail mark (a ring that wrapped mid-tail)
    unsigned bigDiscardsUnshadowed = 0;   // of those, without a shadow copy: the tail isn't replayable
    std::unordered_map<void*, unsigned> bigDiscards;  // large dynamic buffers discarded, per buffer
    // Research/37 (the strobe): a large dynamic buffer discarded inside the tail. Its contents just before the discard
    // (what the tail's earlier draws read) were copied on the GPU into `shadow`; the replay binds the shadow for the
    // entries before `atEntry` and the buffer itself (or its next shadow) after.
    struct ShadowSwitch {
        void* resource;
        ID3D11Buffer* shadow;
        unsigned atEntry;
    };
    std::vector<ShadowSwitch> shadows;
    void Clear() {
        bytes.clear();
        tail = SIZE_MAX;
        entries = tailEntries = draws = updates = textureUpdates = mapsCopied = mapsSkipped = 0;
        unsupported = unsupportedSlot = bigUpdates = bigDiscardsInTail = bigDiscardsUnshadowed = 0;
        bigDiscards.clear();
        shadows.clear();
    }
};

struct BufferInfo {
    bool isBuffer = false;
    std::uint32_t byteWidth = 0;
    std::uint32_t bindFlags = 0;
    std::uint32_t usage = 0;
};
struct Mapped {
    void* data = nullptr;
    std::uint32_t type = 0;
};

void* gNext[kSlots]{};
void** gVtable = nullptr;
std::atomic<bool> gAttached{false};
std::atomic<int> gAttachLogged{0};
ID3D11DeviceContext* gContext = nullptr;
ID3D11DeviceContext* gContextSeen = nullptr;  // every context Attach was given, hooked or not

Stream gStreams[2];
thread_local Stream* t_recording = nullptr;
std::unordered_map<void*, BufferInfo> gBuffers;  // per frame (pointers can be reused after a release)
std::unordered_map<void*, Mapped> gMapped;       // the last Map of each resource this frame
// The recording's map copies, timed (research/37: 17 maps and 17 KB a frame at the heavy spot; the 7.7 MB a frame of
// recorded constants come through UpdateSubresource from the engine's own memory, so no mapped memory is read back).
std::atomic<std::uint64_t> gMapCopyTicks{0}, gMapCopyBytes{0}, gMapsCopied{0};
std::atomic<std::uint64_t> gTailDiscardFrames{0}, gTailDiscards{0}, gMapsSkipped{0};   // large dynamic buffers (research/37)
std::atomic<std::uint64_t> gShadowCopies{0};

// The shadow copies of large dynamic buffers (research/37, the strobe): one DEFAULT buffer per in-tail discard of a
// buffer, up to kShadowsPerBuffer a frame, kept for the session (the engine's rings live as long as the device).
constexpr unsigned kShadowsPerBuffer = 4;
struct ShadowSet {
    std::vector<ID3D11Buffer*> buffers;
    std::uint32_t byteWidth = 0;
    unsigned used = 0;   // this frame
};
std::unordered_map<void*, ShadowSet> gShadows;   // render thread
thread_local std::unordered_map<void*, ID3D11Buffer*> t_shadowSub;   // during a replay: buffer -> the shadow to bind instead

ID3D11Buffer* ShadowFor(ID3D11DeviceContext* c, ID3D11Resource* resource, BufferInfo const& info) {
    ShadowSet& set = gShadows[resource];
    if (set.byteWidth != info.byteWidth) {   // a different buffer at a reused pointer
        for (ID3D11Buffer* b : set.buffers) {
            b->Release();
        }
        set.buffers.clear();
        set.byteWidth = info.byteWidth;
        set.used = 0;
    }
    if (set.used >= kShadowsPerBuffer) {
        return nullptr;
    }
    while (set.buffers.size() <= set.used) {
        ID3D11Buffer* original = nullptr;
        if (FAILED(resource->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&original))) || original == nullptr) {
            return nullptr;
        }
        D3D11_BUFFER_DESC desc{};
        original->GetDesc(&desc);
        original->Release();
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;
        ID3D11Device* device = nullptr;
        c->GetDevice(&device);
        ID3D11Buffer* shadow = nullptr;
        HRESULT const hr = device != nullptr ? device->CreateBuffer(&desc, nullptr, &shadow) : E_FAIL;
        if (device != nullptr) {
            device->Release();
        }
        if (FAILED(hr) || shadow == nullptr) {
            static std::atomic<int> failures{0};
            if (failures.fetch_add(1, std::memory_order_relaxed) < 3) {
                log::Warn("draw replay: no shadow for a {} KB dynamic buffer (bind 0x{:X}): CreateBuffer 0x{:08X}", info.byteWidth >> 10, info.bindFlags,
                    static_cast<unsigned>(hr));
            }
            return nullptr;
        }
        set.buffers.push_back(shadow);
        static std::atomic<int> created{0};
        if (created.fetch_add(1, std::memory_order_relaxed) < 6) {
            log::Info("draw replay: shadow {} of a {} KB dynamic buffer (bind 0x{:X}, misc 0x{:X}) for its discards inside the tail (research/37)",
                set.buffers.size(), info.byteWidth >> 10, info.bindFlags, desc.MiscFlags);
        }
    }
    return set.buffers[set.used++];
}
std::vector<IUnknown*> gHeld;                    // AddRef'd for the frame
std::array<void*, 8192> gHeldCache{};

std::atomic<std::uint64_t> gReplays{0}, gFailures{0}, gRecorded{0}, gRecordedCalls{0}, gRecordedBytes{0},
    gReplayedCalls{0}, gSubstitutions{0}, gNotReplayable{0};
std::mutex gBlockerMutex;
std::string gLastBlocker;

std::mutex gDiffMutex;
bool gDiffPending = false;
std::string gDiffTag;

template <typename F>
F Next(unsigned slot) {
    return reinterpret_cast<F>(gNext[slot]);
}
// d3d11 keeps a per-context vtable and rewrites its draw, clear, copy and dispatch entries between variants of each
// function as the context's state changes (research/37, experiment 6: one set during Present, another while
// rendering, others possible). The entries Halcyon doesn't hook are hooked inline per variant, each variant through
// its own detour instantiation with its own trampoline.
constexpr unsigned kVariants = 3;
void* gNextV[kSlots][kVariants]{};
void* gVariantTarget[kSlots][kVariants]{};  // the d3d11 function each variant detour sits on
template <typename F>
F NextV(unsigned slot, unsigned v) {
    return reinterpret_cast<F>(gNextV[slot][v]);
}
// The replay calls whatever the vtable holds now: our own hooks pass a call through while nothing is recorded.
template <typename F>
F Live(unsigned slot) {
    return reinterpret_cast<F>(gVtable[slot]);
}

// ---- Recording ------------------------------------------------------------------------------------------------

std::uint8_t* Reserve(Stream& s, Op op, std::uint8_t stage, std::uint16_t n, std::size_t size) {
    std::size_t const at = s.bytes.size();
    s.bytes.resize(at + sizeof(Header) + size);
    auto* const header = reinterpret_cast<Header*>(s.bytes.data() + at);
    header->op = op;
    header->stage = stage;
    header->n = n;
    header->size = static_cast<std::uint32_t>(size);
    ++s.entries;
    return s.bytes.data() + at + sizeof(Header);
}

void Hold(void* p) {
    if (p == nullptr) {
        return;
    }
    std::size_t const index = (reinterpret_cast<std::uintptr_t>(p) >> 4) & (gHeldCache.size() - 1);
    if (gHeldCache[index] == p) {
        return;
    }
    gHeldCache[index] = p;
    static_cast<IUnknown*>(p)->AddRef();
    gHeld.push_back(static_cast<IUnknown*>(p));
}

void RecordPointers(Stream& s, Op op, std::uint8_t stage, UINT start, UINT count, void* const* ptrs, void* extra = nullptr,
    bool withExtra = false) {
    UINT const n = ptrs == nullptr ? 0 : count;
    std::size_t const total = static_cast<std::size_t>(n) + (withExtra ? 1 : 0);
    auto* const body = reinterpret_cast<Pointers*>(Reserve(s, op, stage, static_cast<std::uint16_t>(n),
        sizeof(std::uint32_t) + total * sizeof(void*)));
    body->start = start;
    for (UINT i = 0; i < n; ++i) {
        body->ptrs[i] = ptrs[i];
        Hold(ptrs[i]);
    }
    if (withExtra) {
        body->ptrs[n] = extra;
        Hold(extra);
    }
}

template <typename T>
T* RecordStruct(Stream& s, Op op, std::uint8_t stage = 0, std::uint16_t n = 0, std::size_t extra = 0) {
    return reinterpret_cast<T*>(Reserve(s, op, stage, n, sizeof(T) + extra));
}

void RecordUnsupported(Stream& s, unsigned slot) {
    Reserve(s, Op::Unsupported, 0, static_cast<std::uint16_t>(slot), 0);
    ++s.unsupported;
    if (s.unsupportedSlot == 0) {
        s.unsupportedSlot = slot;
    }
}

BufferInfo const& Lookup(ID3D11Resource* resource) {
    auto const found = gBuffers.find(resource);
    if (found != gBuffers.end()) {
        return found->second;
    }
    BufferInfo info;
    ID3D11Buffer* buffer = nullptr;
    if (resource != nullptr &&
        SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buffer))) && buffer != nullptr) {
        D3D11_BUFFER_DESC desc{};
        buffer->GetDesc(&desc);
        info.isBuffer = true;
        info.byteWidth = desc.ByteWidth;
        info.bindFlags = desc.BindFlags;
        info.usage = static_cast<std::uint32_t>(desc.Usage);
        buffer->Release();
    }
    return gBuffers.emplace(resource, info).first->second;
}

void RecordUpdate(Stream& s, ID3D11Resource* resource, UINT subresource, D3D11_BOX const* box, void const* data,
    UINT rowPitch, UINT depthPitch) {
    BufferInfo const& info = Lookup(resource);
    std::size_t size = 0;
    std::uint32_t kind = 2;
    if (info.isBuffer && subresource == 0 && data != nullptr) {
        size = box != nullptr ? (box->right > box->left ? box->right - box->left : 0) : info.byteWidth;
        kind = (info.bindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0 ? 1 : 0;
        if (size > kMaxRecordedUpdate) {
            ++s.bigUpdates;
            size = 0;
            kind = 3;  // too big to copy
        }
    } else {
        ++s.textureUpdates;
    }
    auto* const args = RecordStruct<UpdateArgs>(s, Op::UpdateSubresource, 0, 0, size);
    args->resource = resource;
    args->subresource = subresource;
    args->hasBox = box != nullptr ? 1u : 0u;
    args->box = box != nullptr ? *box : D3D11_BOX{};
    args->rowPitch = rowPitch;
    args->depthPitch = depthPitch;
    args->kind = kind;
    args->dataSize = static_cast<std::uint32_t>(size);
    if (size != 0) {
        std::memcpy(args + 1, data, size);
    }
    Hold(resource);
    ++s.updates;
}

// ---- The hooks (any thread; they record only on the thread that began recording) ------------------------------

// Set while one of our own hooks is inside its next entry: Halcyon's seam below it (Note*) then leaves the call
// alone, so a call is recorded once whichever hook saw it (research/37, experiment 6).
thread_local int t_viaOurs = 0;
struct ViaOurs {
    ViaOurs() { ++t_viaOurs; }
    ~ViaOurs() { --t_viaOurs; }
};
std::atomic<std::uint64_t> gNotedByHalcyon{0}, gSeenByOurs{0};

template <unsigned S, std::uint8_t Stage>
void STDMETHODCALLTYPE SetConstantBuffersHook(ID3D11DeviceContext* c, UINT start, UINT count, ID3D11Buffer* const* buffers) {
    if (t_recording != nullptr) {
        RecordPointers(*t_recording, Op::SetConstantBuffers, Stage, start, count, reinterpret_cast<void* const*>(buffers));
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*)>(S)(c, start, count, buffers);
}
template <unsigned S, std::uint8_t Stage>
void STDMETHODCALLTYPE SetShaderResourcesHook(ID3D11DeviceContext* c, UINT start, UINT count,
    ID3D11ShaderResourceView* const* views) {
    if (t_recording != nullptr) {
        RecordPointers(*t_recording, Op::SetShaderResources, Stage, start, count, reinterpret_cast<void* const*>(views));
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*)>(S)(c, start, count, views);
}
template <unsigned S, std::uint8_t Stage>
void STDMETHODCALLTYPE SetSamplersHook(ID3D11DeviceContext* c, UINT start, UINT count, ID3D11SamplerState* const* samplers) {
    if (t_recording != nullptr) {
        RecordPointers(*t_recording, Op::SetSamplers, Stage, start, count, reinterpret_cast<void* const*>(samplers));
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11SamplerState* const*)>(S)(c, start, count, samplers);
}
template <unsigned S, std::uint8_t Stage, typename Shader>
void STDMETHODCALLTYPE SetShaderHook(ID3D11DeviceContext* c, Shader* shader, ID3D11ClassInstance* const* instances, UINT count) {
    if (t_recording != nullptr) {
        if (count != 0 && instances != nullptr) {
            RecordUnsupported(*t_recording, S);
        } else {
            void* p = shader;
            RecordPointers(*t_recording, Op::SetShader, Stage, 0, 1, &p);
        }
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, Shader*, ID3D11ClassInstance* const*, UINT)>(S)(c, shader, instances, count);
}

void STDMETHODCALLTYPE DrawIndexedHook(ID3D11DeviceContext* c, UINT indexCount, UINT startIndex, INT baseVertex) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<DrawIndexedArgs>(*t_recording, Op::DrawIndexed);
        *a = {indexCount, startIndex, baseVertex};
        ++t_recording->draws;
        gSeenByOurs.fetch_add(1, std::memory_order_relaxed);
    }
    ViaOurs const via;
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT)>(kDrawIndexed)(c, indexCount, startIndex, baseVertex);
}
void STDMETHODCALLTYPE DrawHook(ID3D11DeviceContext* c, UINT vertexCount, UINT start) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<DrawArgs>(*t_recording, Op::Draw);
        *a = {vertexCount, start};
        ++t_recording->draws;
    }
    ViaOurs const via;
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT)>(kDraw)(c, vertexCount, start);
}
void STDMETHODCALLTYPE DrawIndexedInstancedHook(ID3D11DeviceContext* c, UINT indexCountPerInstance, UINT instanceCount,
    UINT startIndex, INT baseVertex, UINT startInstance) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<DrawIndexedInstancedArgs>(*t_recording, Op::DrawIndexedInstanced);
        *a = {indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance};
        ++t_recording->draws;
    }
    ViaOurs const via;
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT)>(kDrawIndexedInstanced)(
        c, indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance);
}
void STDMETHODCALLTYPE DrawInstancedHook(ID3D11DeviceContext* c, UINT vertexCountPerInstance, UINT instanceCount,
    UINT startVertex, UINT startInstance) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<DrawInstancedArgs>(*t_recording, Op::DrawInstanced);
        *a = {vertexCountPerInstance, instanceCount, startVertex, startInstance};
        ++t_recording->draws;
    }
    ViaOurs const via;
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT)>(kDrawInstanced)(
        c, vertexCountPerInstance, instanceCount, startVertex, startInstance);
}
void STDMETHODCALLTYPE DrawAutoHook(ID3D11DeviceContext* c) {
    if (t_recording != nullptr) {
        Reserve(*t_recording, Op::DrawAuto, 0, 0, 0);
        ++t_recording->draws;
    }
    ViaOurs const via;
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*)>(kDrawAuto)(c);
}

HRESULT STDMETHODCALLTYPE MapHook(ID3D11DeviceContext* c, ID3D11Resource* resource, UINT subresource, D3D11_MAP type,
    UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
    if (t_recording != nullptr && type == D3D11_MAP_WRITE_DISCARD && subresource == 0 && t_recording->tail != SIZE_MAX) {
        // A large dynamic buffer discarded inside the tail (a ring wrapping): the draws recorded before this point read
        // its contents as they are now, which the discard throws away. Copy them on the GPU (in order, before the
        // discard) into a shadow the replay binds for those draws. Our copy is not part of the recording.
        BufferInfo const& info = Lookup(resource);
        if (info.isBuffer && info.byteWidth > kMaxRecordedMap) {
            Stream& s = *t_recording;
            ID3D11Buffer* const shadow = ShadowFor(c, resource, info);
            if (shadow != nullptr) {
                t_recording = nullptr;
                c->CopyResource(shadow, resource);
                t_recording = &s;
                s.shadows.push_back({resource, shadow, s.entries});
                gShadowCopies.fetch_add(1, std::memory_order_relaxed);
            } else {
                ++s.bigDiscardsUnshadowed;
            }
        }
    }
    HRESULT const hr = Next<HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
        D3D11_MAPPED_SUBRESOURCE*)>(kMap)(c, resource, subresource, type, flags, mapped);
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<MapArgs>(*t_recording, Op::Map);
        *a = {resource, subresource, static_cast<std::uint32_t>(type), flags};
        if (SUCCEEDED(hr) && mapped != nullptr) {
            gMapped[resource] = {mapped->pData, static_cast<std::uint32_t>(type)};
        } else {
            gMapped.erase(resource);
        }
        Hold(resource);
    }
    return hr;
}
void STDMETHODCALLTYPE UnmapHook(ID3D11DeviceContext* c, ID3D11Resource* resource, UINT subresource) {
    if (t_recording != nullptr) {
        Stream& s = *t_recording;
        BufferInfo const& info = Lookup(resource);
        auto const found = gMapped.find(resource);
        bool const written = found != gMapped.end() && found->second.data != nullptr &&
            found->second.type != D3D11_MAP_READ;
        std::size_t size = 0;
        if (written && info.isBuffer && subresource == 0 && info.byteWidth <= kMaxRecordedMap) {
            size = info.byteWidth;
            ++s.mapsCopied;
        } else if (written) {
            ++s.mapsSkipped;
            if (info.isBuffer && found->second.type == D3D11_MAP_WRITE_DISCARD) {
                ++s.bigDiscards[resource];
                if (s.tail != SIZE_MAX) {
                    ++s.bigDiscardsInTail;   // a ring wrapped mid-tail (particles, dynamic meshes): the draws before it
                }                            // read contents the replay cannot bring back
            }
        }
        auto* const a = RecordStruct<UnmapArgs>(s, Op::Unmap, 0, 0, size);
        a->resource = resource;
        a->subresource = subresource;
        a->mapType = written ? found->second.type : 0;
        a->kind = (info.bindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0 ? 1u : 0u;
        a->dataSize = static_cast<std::uint32_t>(size);
        if (size != 0) {
            std::uint64_t const t0 = __rdtsc();
            std::memcpy(a + 1, found->second.data, size);  // the mapped memory, before it goes away
            gMapCopyTicks.fetch_add(__rdtsc() - t0, std::memory_order_relaxed);
            gMapCopyBytes.fetch_add(size, std::memory_order_relaxed);
            gMapsCopied.fetch_add(1, std::memory_order_relaxed);
        }
        if (found != gMapped.end()) {
            gMapped.erase(found);
        }
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT)>(kUnmap)(c, resource, subresource);
}
void STDMETHODCALLTYPE UpdateSubresourceHook(ID3D11DeviceContext* c, ID3D11Resource* resource, UINT subresource,
    D3D11_BOX const* box, void const* data, UINT rowPitch, UINT depthPitch) {
    if (t_recording != nullptr) {
        RecordUpdate(*t_recording, resource, subresource, box, data, rowPitch, depthPitch);
    }
    ViaOurs const via;
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_BOX const*, void const*, UINT, UINT)>(
        kUpdateSubresource)(c, resource, subresource, box, data, rowPitch, depthPitch);
}

void STDMETHODCALLTYPE IASetInputLayoutHook(ID3D11DeviceContext* c, ID3D11InputLayout* layout) {
    if (t_recording != nullptr) {
        void* p = layout;
        RecordPointers(*t_recording, Op::IASetInputLayout, 0, 0, 1, &p);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11InputLayout*)>(kIASetInputLayout)(c, layout);
}
void STDMETHODCALLTYPE IASetVertexBuffersHook(ID3D11DeviceContext* c, UINT start, UINT count, ID3D11Buffer* const* buffers,
    UINT const* strides, UINT const* offsets) {
    if (t_recording != nullptr) {
        UINT const n = (buffers == nullptr || strides == nullptr || offsets == nullptr) ? 0 : count;
        auto* const a = RecordStruct<VertexBuffers>(*t_recording, Op::IASetVertexBuffers, 0, static_cast<std::uint16_t>(n),
            static_cast<std::size_t>(n) * (sizeof(void*) + 2 * sizeof(UINT)));
        a->start = start;
        a->count = n;
        auto* const ptrs = reinterpret_cast<void**>(a + 1);
        auto* const stridesOut = reinterpret_cast<UINT*>(ptrs + n);
        auto* const offsetsOut = stridesOut + n;
        for (UINT i = 0; i < n; ++i) {
            ptrs[i] = buffers[i];
            stridesOut[i] = strides[i];
            offsetsOut[i] = offsets[i];
            Hold(buffers[i]);
        }
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*, UINT const*, UINT const*)>(
        kIASetVertexBuffers)(c, start, count, buffers, strides, offsets);
}
void STDMETHODCALLTYPE IASetIndexBufferHook(ID3D11DeviceContext* c, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<IndexBuffer>(*t_recording, Op::IASetIndexBuffer);
        *a = {buffer, static_cast<std::uint32_t>(format), offset};
        Hold(buffer);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, DXGI_FORMAT, UINT)>(kIASetIndexBuffer)(c, buffer, format, offset);
}
void STDMETHODCALLTYPE IASetPrimitiveTopologyHook(ID3D11DeviceContext* c, D3D11_PRIMITIVE_TOPOLOGY topology) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<std::uint32_t>(*t_recording, Op::IASetPrimitiveTopology);
        *a = static_cast<std::uint32_t>(topology);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, D3D11_PRIMITIVE_TOPOLOGY)>(kIASetPrimitiveTopology)(c, topology);
}

void STDMETHODCALLTYPE BeginHook(ID3D11DeviceContext* c, ID3D11Asynchronous* async) {
    if (t_recording != nullptr) {
        void* p = async;
        RecordPointers(*t_recording, Op::Begin, 0, 0, 1, &p);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Asynchronous*)>(kBegin)(c, async);
}
void STDMETHODCALLTYPE EndHook(ID3D11DeviceContext* c, ID3D11Asynchronous* async) {
    if (t_recording != nullptr) {
        void* p = async;
        RecordPointers(*t_recording, Op::End, 0, 0, 1, &p);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Asynchronous*)>(kEnd)(c, async);
}
void STDMETHODCALLTYPE SetPredicationHook(ID3D11DeviceContext* c, ID3D11Predicate* predicate, BOOL value) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<PredicationArgs>(*t_recording, Op::SetPredication);
        *a = {predicate, value != 0 ? 1u : 0u};
        Hold(predicate);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Predicate*, BOOL)>(kSetPredication)(c, predicate, value);
}

void STDMETHODCALLTYPE OMSetRenderTargetsHook(ID3D11DeviceContext* c, UINT count, ID3D11RenderTargetView* const* views,
    ID3D11DepthStencilView* depth) {
    if (t_recording != nullptr) {
        RecordPointers(*t_recording, Op::OMSetRenderTargets, 0, 0, count, reinterpret_cast<void* const*>(views), depth, true);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*)>(
        kOMSetRenderTargets)(c, count, views, depth);
}
void STDMETHODCALLTYPE OMSetBlendStateHook(ID3D11DeviceContext* c, ID3D11BlendState* state, FLOAT const factor[4], UINT mask) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<BlendArgs>(*t_recording, Op::OMSetBlendState);
        a->state = state;
        if (factor != nullptr) {
            std::memcpy(a->factor, factor, sizeof(a->factor));
        } else {
            a->factor[0] = a->factor[1] = a->factor[2] = a->factor[3] = 1.0f;
        }
        a->mask = mask;
        Hold(state);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11BlendState*, FLOAT const*, UINT)>(kOMSetBlendState)(c, state, factor, mask);
}
void STDMETHODCALLTYPE OMSetDepthStencilStateHook(ID3D11DeviceContext* c, ID3D11DepthStencilState* state, UINT reference) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<DepthStencilArgs>(*t_recording, Op::OMSetDepthStencilState);
        *a = {state, reference};
        Hold(state);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilState*, UINT)>(kOMSetDepthStencilState)(c, state, reference);
}
void STDMETHODCALLTYPE RSSetStateHook(ID3D11DeviceContext* c, ID3D11RasterizerState* state) {
    if (t_recording != nullptr) {
        void* p = state;
        RecordPointers(*t_recording, Op::RSSetState, 0, 0, 1, &p);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RasterizerState*)>(kRSSetState)(c, state);
}
void STDMETHODCALLTYPE RSSetViewportsHook(ID3D11DeviceContext* c, UINT count, D3D11_VIEWPORT const* viewports) {
    if (t_recording != nullptr) {
        UINT const n = viewports == nullptr ? 0 : count;
        auto* const out = Reserve(*t_recording, Op::RSSetViewports, 0, static_cast<std::uint16_t>(n), n * sizeof(D3D11_VIEWPORT));
        if (n != 0) {
            std::memcpy(out, viewports, n * sizeof(D3D11_VIEWPORT));
        }
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, D3D11_VIEWPORT const*)>(kRSSetViewports)(c, count, viewports);
}
void STDMETHODCALLTYPE RSSetScissorRectsHook(ID3D11DeviceContext* c, UINT count, D3D11_RECT const* rects) {
    if (t_recording != nullptr) {
        UINT const n = rects == nullptr ? 0 : count;
        auto* const out = Reserve(*t_recording, Op::RSSetScissorRects, 0, static_cast<std::uint16_t>(n), n * sizeof(D3D11_RECT));
        if (n != 0) {
            std::memcpy(out, rects, n * sizeof(D3D11_RECT));
        }
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, D3D11_RECT const*)>(kRSSetScissorRects)(c, count, rects);
}

template <unsigned V>
void STDMETHODCALLTYPE ClearRenderTargetViewHook(ID3D11DeviceContext* c, ID3D11RenderTargetView* view, FLOAT const colour[4]) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<ClearTargetArgs>(*t_recording, Op::ClearRenderTargetView);
        a->view = view;
        if (colour != nullptr) {
            std::memcpy(a->colour, colour, sizeof(a->colour));
        } else {
            std::memset(a->colour, 0, sizeof(a->colour));
        }
        Hold(view);
    }
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, FLOAT const*)>(kClearRenderTargetView, V)(c, view, colour);
}
void STDMETHODCALLTYPE ClearDepthStencilViewHook(ID3D11DeviceContext* c, ID3D11DepthStencilView* view, UINT flags, FLOAT depth,
    UINT8 stencil) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<ClearDepthArgs>(*t_recording, Op::ClearDepthStencilView);
        *a = {view, flags, depth, stencil};
        Hold(view);
    }
    ViaOurs const via;
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8)>(kClearDepthStencilView)(
        c, view, flags, depth, stencil);
}
template <unsigned V>
void STDMETHODCALLTYPE CopyResourceHook(ID3D11DeviceContext* c, ID3D11Resource* dst, ID3D11Resource* src) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<CopyArgs>(*t_recording, Op::CopyResource);
        *a = {dst, src};
        Hold(dst);
        Hold(src);
    }
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*)>(kCopyResource, V)(c, dst, src);
}
template <unsigned V>
void STDMETHODCALLTYPE CopySubresourceRegionHook(ID3D11DeviceContext* c, ID3D11Resource* dst, UINT dstSub, UINT x, UINT y, UINT z,
    ID3D11Resource* src, UINT srcSub, D3D11_BOX const* box) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<CopyRegionArgs>(*t_recording, Op::CopySubresourceRegion);
        a->dst = dst;
        a->dstSub = dstSub;
        a->x = x;
        a->y = y;
        a->z = z;
        a->src = src;
        a->srcSub = srcSub;
        a->hasBox = box != nullptr ? 1u : 0u;
        a->box = box != nullptr ? *box : D3D11_BOX{};
        Hold(dst);
        Hold(src);
    }
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, D3D11_BOX const*)>(kCopySubresourceRegion, V)(c, dst, dstSub, x, y, z, src, srcSub, box);
}
template <unsigned V>
void STDMETHODCALLTYPE ResolveSubresourceHook(ID3D11DeviceContext* c, ID3D11Resource* dst, UINT dstSub, ID3D11Resource* src, UINT srcSub,
    DXGI_FORMAT format) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<ResolveArgs>(*t_recording, Op::ResolveSubresource);
        *a = {dst, dstSub, src, srcSub, static_cast<std::uint32_t>(format)};
        Hold(dst);
        Hold(src);
    }
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT)>(kResolveSubresource, V)(c, dst, dstSub, src, srcSub, format);
}
template <unsigned V>
void STDMETHODCALLTYPE GenerateMipsHook(ID3D11DeviceContext* c, ID3D11ShaderResourceView* view) {
    if (t_recording != nullptr) {
        void* p = view;
        RecordPointers(*t_recording, Op::GenerateMips, 0, 0, 1, &p);
    }
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11ShaderResourceView*)>(kGenerateMips, V)(c, view);
}

// Calls the replay can't repeat: recorded as such, passed through.
void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViewsHook(ID3D11DeviceContext* c, UINT count,
    ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth, UINT uavStart, UINT uavCount,
    ID3D11UnorderedAccessView* const* uavs, UINT const* initialCounts) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kOMSetRenderTargetsAndUnorderedAccessViews);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT,
        ID3D11UnorderedAccessView* const*, UINT const*)>(kOMSetRenderTargetsAndUnorderedAccessViews)(
        c, count, views, depth, uavStart, uavCount, uavs, initialCounts);
}
void STDMETHODCALLTYPE SOSetTargetsHook(ID3D11DeviceContext* c, UINT count, ID3D11Buffer* const* targets, UINT const* offsets) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kSOSetTargets);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11Buffer* const*, UINT const*)>(kSOSetTargets)(c, count, targets, offsets);
}
template <unsigned V>
void STDMETHODCALLTYPE DrawIndexedInstancedIndirectHook(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kDrawIndexedInstancedIndirect);
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT)>(kDrawIndexedInstancedIndirect, V)(c, args, offset);
}
template <unsigned V>
void STDMETHODCALLTYPE DrawInstancedIndirectHook(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kDrawInstancedIndirect);
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT)>(kDrawInstancedIndirect, V)(c, args, offset);
}
template <unsigned V>
void STDMETHODCALLTYPE DispatchHook(ID3D11DeviceContext* c, UINT x, UINT y, UINT z) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<DispatchArgs>(*t_recording, Op::Dispatch);
        *a = {x, y, z};
    }
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT)>(kDispatch, V)(c, x, y, z);
}
template <unsigned V>
void STDMETHODCALLTYPE DispatchIndirectHook(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kDispatchIndirect);
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT)>(kDispatchIndirect, V)(c, args, offset);
}
template <unsigned V>
void STDMETHODCALLTYPE CopyStructureCountHook(ID3D11DeviceContext* c, ID3D11Buffer* dst, UINT offset, ID3D11UnorderedAccessView* src) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kCopyStructureCount);
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT, ID3D11UnorderedAccessView*)>(kCopyStructureCount, V)(c, dst, offset, src);
}
template <unsigned V>
void STDMETHODCALLTYPE ClearUnorderedAccessViewUintHook(ID3D11DeviceContext* c, ID3D11UnorderedAccessView* view, UINT const values[4]) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kClearUnorderedAccessViewUint);
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11UnorderedAccessView*, UINT const*)>(kClearUnorderedAccessViewUint, V)(c, view, values);
}
template <unsigned V>
void STDMETHODCALLTYPE ClearUnorderedAccessViewFloatHook(ID3D11DeviceContext* c, ID3D11UnorderedAccessView* view, FLOAT const values[4]) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kClearUnorderedAccessViewFloat);
    NextV<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11UnorderedAccessView*, FLOAT const*)>(kClearUnorderedAccessViewFloat, V)(c, view, values);
}
void STDMETHODCALLTYPE SetResourceMinLODHook(ID3D11DeviceContext* c, ID3D11Resource* resource, FLOAT lod) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kSetResourceMinLOD);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, FLOAT)>(kSetResourceMinLOD)(c, resource, lod);
}
void STDMETHODCALLTYPE ExecuteCommandListHook(ID3D11DeviceContext* c, ID3D11CommandList* list, BOOL restore) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kExecuteCommandList);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL)>(kExecuteCommandList)(c, list, restore);
}
void STDMETHODCALLTYPE CSSetUnorderedAccessViewsHook(ID3D11DeviceContext* c, UINT start, UINT count,
    ID3D11UnorderedAccessView* const* views, UINT const* initialCounts) {
    if (t_recording != nullptr) {
        // Recorded as a plain pointer set (the initial counts, for append buffers, are not kept: the replay passes
        // none, which keeps the current counts, and the engine binds these only to unbind them).
        RecordPointers(*t_recording, Op::CSSetUnorderedAccessViews, 0, start, count, reinterpret_cast<void* const*>(views));
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11UnorderedAccessView* const*, UINT const*)>(
        kCSSetUnorderedAccessViews)(c, start, count, views, initialCounts);
}

struct HookEntry {
    unsigned slot;
    void* function;
};
// The entries an overlay (the EA app's, by its private copy of the context's vtable) puts back to d3d11's own at
// every Present (research/37, experiment 6: every draw, clear, copy and dispatch): hooked inline on d3d11's
// functions, below Halcyon's own inline hooks on seven of them, where nothing rewrites them.
constexpr unsigned kVolatileSlots[] = {kDrawIndexed, kDraw, kDrawIndexedInstanced, kDrawInstanced, kDrawAuto,
    kUpdateSubresource, kClearRenderTargetView, kClearDepthStencilView, kCopyResource, kCopySubresourceRegion,
    kResolveSubresource, kGenerateMips, kDrawIndexedInstancedIndirect, kDrawInstancedIndirect, kDispatch,
    kDispatchIndirect, kCopyStructureCount, kClearUnorderedAccessViewUint, kClearUnorderedAccessViewFloat};
struct VariantHook {
    unsigned slot;
    void* fn[kVariants];
};
#define MEVR_VARIANTS(slot, name) {slot, {reinterpret_cast<void*>(&name<0>), reinterpret_cast<void*>(&name<1>), reinterpret_cast<void*>(&name<2>)}}
VariantHook const kVariantHooks[] = {
    MEVR_VARIANTS(kClearRenderTargetView, ClearRenderTargetViewHook),
    MEVR_VARIANTS(kCopyResource, CopyResourceHook),
    MEVR_VARIANTS(kCopySubresourceRegion, CopySubresourceRegionHook),
    MEVR_VARIANTS(kResolveSubresource, ResolveSubresourceHook),
    MEVR_VARIANTS(kGenerateMips, GenerateMipsHook),
    MEVR_VARIANTS(kDrawIndexedInstancedIndirect, DrawIndexedInstancedIndirectHook),
    MEVR_VARIANTS(kDrawInstancedIndirect, DrawInstancedIndirectHook),
    MEVR_VARIANTS(kDispatch, DispatchHook),
    MEVR_VARIANTS(kDispatchIndirect, DispatchIndirectHook),
    MEVR_VARIANTS(kCopyStructureCount, CopyStructureCountHook),
    MEVR_VARIANTS(kClearUnorderedAccessViewUint, ClearUnorderedAccessViewUintHook),
    MEVR_VARIANTS(kClearUnorderedAccessViewFloat, ClearUnorderedAccessViewFloatHook),
};
#undef MEVR_VARIANTS
VariantHook const* FindVariant(unsigned slot) {
    for (VariantHook const& v : kVariantHooks) {
        if (v.slot == slot) {
            return &v;
        }
    }
    return nullptr;
}
hook::Context* gHooks = nullptr;

bool Volatile(unsigned slot) {
    for (unsigned const v : kVolatileSlots) {
        if (v == slot) {
            return true;
        }
    }
    return false;
}
bool gInlineSlot[kSlots]{};  // hooked inline: never in the vtable, never re-taken
void* gAttachSlot[kSlots]{};  // what the vtable held at Attach (research/37, experiment 6: which function the calls reach)
HookEntry const kHooks[] = {
    {kVSSetConstantBuffers, reinterpret_cast<void*>(&SetConstantBuffersHook<kVSSetConstantBuffers, VS>)},
    {kPSSetConstantBuffers, reinterpret_cast<void*>(&SetConstantBuffersHook<kPSSetConstantBuffers, PS>)},
    {kGSSetConstantBuffers, reinterpret_cast<void*>(&SetConstantBuffersHook<kGSSetConstantBuffers, GS>)},
    {kHSSetConstantBuffers, reinterpret_cast<void*>(&SetConstantBuffersHook<kHSSetConstantBuffers, HS>)},
    {kDSSetConstantBuffers, reinterpret_cast<void*>(&SetConstantBuffersHook<kDSSetConstantBuffers, DS>)},
    {kCSSetConstantBuffers, reinterpret_cast<void*>(&SetConstantBuffersHook<kCSSetConstantBuffers, CS>)},
    {kPSSetShaderResources, reinterpret_cast<void*>(&SetShaderResourcesHook<kPSSetShaderResources, PS>)},
    {kVSSetShaderResources, reinterpret_cast<void*>(&SetShaderResourcesHook<kVSSetShaderResources, VS>)},
    {kGSSetShaderResources, reinterpret_cast<void*>(&SetShaderResourcesHook<kGSSetShaderResources, GS>)},
    {kHSSetShaderResources, reinterpret_cast<void*>(&SetShaderResourcesHook<kHSSetShaderResources, HS>)},
    {kDSSetShaderResources, reinterpret_cast<void*>(&SetShaderResourcesHook<kDSSetShaderResources, DS>)},
    {kCSSetShaderResources, reinterpret_cast<void*>(&SetShaderResourcesHook<kCSSetShaderResources, CS>)},
    {kPSSetSamplers, reinterpret_cast<void*>(&SetSamplersHook<kPSSetSamplers, PS>)},
    {kVSSetSamplers, reinterpret_cast<void*>(&SetSamplersHook<kVSSetSamplers, VS>)},
    {kGSSetSamplers, reinterpret_cast<void*>(&SetSamplersHook<kGSSetSamplers, GS>)},
    {kHSSetSamplers, reinterpret_cast<void*>(&SetSamplersHook<kHSSetSamplers, HS>)},
    {kDSSetSamplers, reinterpret_cast<void*>(&SetSamplersHook<kDSSetSamplers, DS>)},
    {kCSSetSamplers, reinterpret_cast<void*>(&SetSamplersHook<kCSSetSamplers, CS>)},
    {kPSSetShader, reinterpret_cast<void*>(&SetShaderHook<kPSSetShader, PS, ID3D11PixelShader>)},
    {kVSSetShader, reinterpret_cast<void*>(&SetShaderHook<kVSSetShader, VS, ID3D11VertexShader>)},
    {kGSSetShader, reinterpret_cast<void*>(&SetShaderHook<kGSSetShader, GS, ID3D11GeometryShader>)},
    {kHSSetShader, reinterpret_cast<void*>(&SetShaderHook<kHSSetShader, HS, ID3D11HullShader>)},
    {kDSSetShader, reinterpret_cast<void*>(&SetShaderHook<kDSSetShader, DS, ID3D11DomainShader>)},
    {kCSSetShader, reinterpret_cast<void*>(&SetShaderHook<kCSSetShader, CS, ID3D11ComputeShader>)},
    {kDrawIndexed, reinterpret_cast<void*>(&DrawIndexedHook)},
    {kDraw, reinterpret_cast<void*>(&DrawHook)},
    {kDrawIndexedInstanced, reinterpret_cast<void*>(&DrawIndexedInstancedHook)},
    {kDrawInstanced, reinterpret_cast<void*>(&DrawInstancedHook)},
    {kDrawAuto, reinterpret_cast<void*>(&DrawAutoHook)},
    {kMap, reinterpret_cast<void*>(&MapHook)},
    {kUnmap, reinterpret_cast<void*>(&UnmapHook)},
    {kUpdateSubresource, reinterpret_cast<void*>(&UpdateSubresourceHook)},
    {kIASetInputLayout, reinterpret_cast<void*>(&IASetInputLayoutHook)},
    {kIASetVertexBuffers, reinterpret_cast<void*>(&IASetVertexBuffersHook)},
    {kIASetIndexBuffer, reinterpret_cast<void*>(&IASetIndexBufferHook)},
    {kIASetPrimitiveTopology, reinterpret_cast<void*>(&IASetPrimitiveTopologyHook)},
    {kBegin, reinterpret_cast<void*>(&BeginHook)},
    {kEnd, reinterpret_cast<void*>(&EndHook)},
    {kSetPredication, reinterpret_cast<void*>(&SetPredicationHook)},
    {kOMSetRenderTargets, reinterpret_cast<void*>(&OMSetRenderTargetsHook)},
    {kOMSetBlendState, reinterpret_cast<void*>(&OMSetBlendStateHook)},
    {kOMSetDepthStencilState, reinterpret_cast<void*>(&OMSetDepthStencilStateHook)},
    {kRSSetState, reinterpret_cast<void*>(&RSSetStateHook)},
    {kRSSetViewports, reinterpret_cast<void*>(&RSSetViewportsHook)},
    {kRSSetScissorRects, reinterpret_cast<void*>(&RSSetScissorRectsHook)},
    {kClearRenderTargetView, reinterpret_cast<void*>(&ClearRenderTargetViewHook<0>)},
    {kClearDepthStencilView, reinterpret_cast<void*>(&ClearDepthStencilViewHook)},
    {kCopyResource, reinterpret_cast<void*>(&CopyResourceHook<0>)},
    {kCopySubresourceRegion, reinterpret_cast<void*>(&CopySubresourceRegionHook<0>)},
    {kResolveSubresource, reinterpret_cast<void*>(&ResolveSubresourceHook<0>)},
    {kGenerateMips, reinterpret_cast<void*>(&GenerateMipsHook<0>)},
    {kOMSetRenderTargetsAndUnorderedAccessViews, reinterpret_cast<void*>(&OMSetRenderTargetsAndUnorderedAccessViewsHook)},
    {kSOSetTargets, reinterpret_cast<void*>(&SOSetTargetsHook)},
    {kDrawIndexedInstancedIndirect, reinterpret_cast<void*>(&DrawIndexedInstancedIndirectHook<0>)},
    {kDrawInstancedIndirect, reinterpret_cast<void*>(&DrawInstancedIndirectHook<0>)},
    {kDispatch, reinterpret_cast<void*>(&DispatchHook<0>)},
    {kDispatchIndirect, reinterpret_cast<void*>(&DispatchIndirectHook<0>)},
    {kCopyStructureCount, reinterpret_cast<void*>(&CopyStructureCountHook<0>)},
    {kClearUnorderedAccessViewUint, reinterpret_cast<void*>(&ClearUnorderedAccessViewUintHook<0>)},
    {kClearUnorderedAccessViewFloat, reinterpret_cast<void*>(&ClearUnorderedAccessViewFloatHook<0>)},
    {kSetResourceMinLOD, reinterpret_cast<void*>(&SetResourceMinLODHook)},
    {kExecuteCommandList, reinterpret_cast<void*>(&ExecuteCommandListHook)},
    {kCSSetUnorderedAccessViews, reinterpret_cast<void*>(&CSSetUnorderedAccessViewsHook)},
};

// The file name (lower case) of the module an address lies in; "" for none (a heap or a hook library's trampoline).
std::string ModuleNameOf(void const* address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(address), &module) || module == nullptr) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    DWORD const n = GetModuleFileNameW(module, path, MAX_PATH);
    if (n == 0) {
        return {};
    }
    std::wstring name(path, n);
    std::size_t const slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        name.erase(0, slash + 1);
    }
    std::string out;
    for (wchar_t const c : name) {
        out += static_cast<char>(c < 128 ? std::tolower(static_cast<int>(c)) : '?');
    }
    return out;
}

// ---- Halcyon's seams: the calls our hooks didn't see ---------------------------------------------------------

bool NoteWanted(ID3D11DeviceContext* c) {
    if (t_recording == nullptr || t_viaOurs != 0 || c != gContext) {
        return false;
    }
    gNotedByHalcyon.fetch_add(1, std::memory_order_relaxed);
    return true;
}

} // namespace

void NoteDrawIndexed(ID3D11DeviceContext* c, unsigned indexCount, unsigned startIndex, int baseVertex) {
    if (!NoteWanted(c)) return;
    auto* const a = RecordStruct<DrawIndexedArgs>(*t_recording, Op::DrawIndexed);
    *a = {indexCount, startIndex, baseVertex};
    ++t_recording->draws;
}
void NoteDraw(ID3D11DeviceContext* c, unsigned vertexCount, unsigned startVertex) {
    if (!NoteWanted(c)) return;
    auto* const a = RecordStruct<DrawArgs>(*t_recording, Op::Draw);
    *a = {vertexCount, startVertex};
    ++t_recording->draws;
}
void NoteDrawIndexedInstanced(ID3D11DeviceContext* c, unsigned indexCountPerInstance, unsigned instanceCount,
    unsigned startIndex, int baseVertex, unsigned startInstance) {
    if (!NoteWanted(c)) return;
    auto* const a = RecordStruct<DrawIndexedInstancedArgs>(*t_recording, Op::DrawIndexedInstanced);
    *a = {indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance};
    ++t_recording->draws;
}
void NoteDrawInstanced(ID3D11DeviceContext* c, unsigned vertexCountPerInstance, unsigned instanceCount,
    unsigned startVertex, unsigned startInstance) {
    if (!NoteWanted(c)) return;
    auto* const a = RecordStruct<DrawInstancedArgs>(*t_recording, Op::DrawInstanced);
    *a = {vertexCountPerInstance, instanceCount, startVertex, startInstance};
    ++t_recording->draws;
}
void NoteDrawAuto(ID3D11DeviceContext* c) {
    if (!NoteWanted(c)) return;
    Reserve(*t_recording, Op::DrawAuto, 0, 0, 0);
    ++t_recording->draws;
}
void NoteUpdateSubresource(ID3D11DeviceContext* c, ID3D11Resource* resource, unsigned subresource, D3D11_BOX const* box,
    void const* data, unsigned rowPitch, unsigned depthPitch) {
    if (!NoteWanted(c)) return;
    RecordUpdate(*t_recording, resource, subresource, box, data, rowPitch, depthPitch);
}
void NoteClearDepthStencilView(ID3D11DeviceContext* c, ID3D11DepthStencilView* view, unsigned flags, float depth,
    unsigned char stencil) {
    if (!NoteWanted(c)) return;
    auto* const a = RecordStruct<ClearDepthArgs>(*t_recording, Op::ClearDepthStencilView);
    *a = {view, flags, depth, stencil};
    Hold(view);
}

namespace {

// ---- The substitution: first-eye view bytes -> second-eye view bytes, by 16-byte chunk -------------------------

ViewLayout gLayout;
std::atomic<std::uint64_t> gMatrixSubstitutions{0};
std::atomic<std::uint64_t> gMatrixHits[4]{};   // per matrix kind: TVP, TVP transposed, inverse, inverse transposed
std::atomic<bool> gInversePair{true};
int gInverseChecked = 0;  // 0 not yet, 1 an inverse was found, -1 none (no inverse pair then)
std::size_t gInverseAt = SIZE_MAX;  // where it was found in the captured view bytes

// T(t) x M for row vectors: rows 0-2 of M unchanged, row 3 += t.x row0 + t.y row1 + t.z row2.
void TranslateRows(float const* m, float const* t, float* out) {
    std::memcpy(out, m, sizeof(float) * 16);
    for (int c = 0; c < 4; ++c) {
        out[12 + c] = m[12 + c] + t[0] * m[c] + t[1] * m[4 + c] + t[2] * m[8 + c];
    }
}
// M x T(u) for row vectors: every row r gets r.xyz += r.w * u.
void TranslateCols(float const* m, float const* u, float* out) {
    std::memcpy(out, m, sizeof(float) * 16);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) {
            out[r * 4 + c] = m[r * 4 + c] + m[r * 4 + 3] * u[c];
        }
    }
}
void Transpose(float const* m, float* out) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[c * 4 + r] = m[r * 4 + c];
        }
    }
}
bool NearIdentity(float const* a, float const* b) {  // a x b ~ I
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float v = 0.0f;
            for (int k = 0; k < 4; ++k) {
                v += a[r * 4 + k] * b[k * 4 + c];
            }
            if (std::abs(v - (r == c ? 1.0f : 0.0f)) > 1e-2f) {
                return false;
            }
        }
    }
    return true;
}

struct Substitution {
    struct Entry {
        std::uint8_t key[16];
        std::uint8_t value[16];
        bool used = false;
        std::uint16_t matrix = 0;  // 0: a 16-byte chunk; else 1 + the index of the 64-byte matrix this is the first row of
    };
    struct Matrix {
        std::uint8_t key[64];
        std::uint8_t value[64];
        unsigned used = 0;
        char const* what = "";
    };
    std::vector<Entry> entries;
    std::vector<Matrix> matrices;
    std::vector<std::uint16_t> index;  // open addressing over the first 12 bytes; 0 = empty, else entry + 1
    std::vector<std::uint8_t> filter;  // one bit per hash of the first 4 bytes

    static std::uint32_t Hash12(std::uint8_t const* p) {
        std::uint32_t a, b, c;
        std::memcpy(&a, p, 4);
        std::memcpy(&b, p + 4, 4);
        std::memcpy(&c, p + 8, 4);
        std::uint32_t h = a * 0x9E3779B1u;
        h ^= b + 0x7F4A7C15u + (h << 6) + (h >> 2);
        h ^= c + 0x85EBCA6Bu + (h << 6) + (h >> 2);
        return h;
    }
    static std::uint32_t Hash4(std::uint8_t const* p) {
        std::uint32_t a;
        std::memcpy(&a, p, 4);
        return (a * 0x9E3779B1u) >> 16;
    }
    static bool Trivial(std::uint8_t const* p) {  // all zero, or (0, 0, 0, 1)-like rows: never eye-dependent
        float f[3];
        std::memcpy(f, p, 12);
        return f[0] == 0.0f && f[1] == 0.0f && f[2] == 0.0f;
    }

    void Add(std::uint8_t const* key, std::uint8_t const* value) {
        if (Trivial(key) || std::memcmp(key, value, 12) == 0) {
            return;  // a difference in the 4th float only, or a key too common to be safe
        }
        for (Entry const& e : entries) {
            if (std::memcmp(e.key, key, 16) == 0) {
                return;  // a matrix's first row among them: the matrix takes precedence
            }
        }
        Entry e{};
        std::memcpy(e.key, key, 16);
        std::memcpy(e.value, value, 16);
        entries.push_back(e);
    }

    // A whole 4x4 (64 bytes, row-major as found) replaced by another where it is uploaded: the first row is the
    // index key, the rest is checked on a match.
    void AddMatrix(float const* key, float const* value, char const* what) {
        auto const* const k = reinterpret_cast<std::uint8_t const*>(key);
        if (Trivial(k)) {
            return;
        }
        for (Matrix const& m : matrices) {
            if (std::memcmp(m.key, k, 64) == 0) {
                return;
            }
        }
        Matrix m{};
        std::memcpy(m.key, k, 64);
        std::memcpy(m.value, value, 64);
        m.what = what;
        matrices.push_back(m);
        Entry e{};
        std::memcpy(e.key, k, 16);
        std::memcpy(e.value, k, 16);
        e.matrix = static_cast<std::uint16_t>(matrices.size());
        entries.push_back(e);
    }

    // The per-view matrices (research/37, experiment 6): the first eye's translated view-projection, wherever it is
    // uploaded, becomes T(pvt2 - pvt1) x TVP2: the first eye's translated geometry seen by the second eye's view;
    // its inverse becomes InvTVP2 x T(pvt1 - pvt2). Both as uploaded (rows) and transposed (columns).
    void BuildMatrices(std::uint8_t const* a, std::uint8_t const* b, std::size_t perView) {
        ViewLayout const& L = gLayout;
        if (L.translatedViewProjection == SIZE_MAX || L.preViewTranslation == SIZE_MAX ||
            L.translatedViewProjection + 64 > perView || L.preViewTranslation + 12 > perView) {
            return;
        }
        auto const* const tvp1 = reinterpret_cast<float const*>(a + L.translatedViewProjection);
        auto const* const tvp2 = reinterpret_cast<float const*>(b + L.translatedViewProjection);
        auto const* const pvt1 = reinterpret_cast<float const*>(a + L.preViewTranslation);
        auto const* const pvt2 = reinterpret_cast<float const*>(b + L.preViewTranslation);
        float const t[3] = {pvt2[0] - pvt1[0], pvt2[1] - pvt1[1], pvt2[2] - pvt1[2]};
        if (t[0] == 0.0f && t[1] == 0.0f && t[2] == 0.0f && std::memcmp(tvp1, tvp2, 64) == 0) {
            return;  // the same view: nothing to move
        }
        float sub[16], tvp1T[16], subT[16];
        TranslateRows(tvp2, t, sub);
        AddMatrix(tvp1, sub, "translated view-projection");
        Transpose(tvp1, tvp1T);
        Transpose(sub, subT);
        AddMatrix(tvp1T, subT, "translated view-projection, transposed");
        static unsigned sinceFailed = 0;
        if (gInverseChecked < 0 && ++sinceFailed >= 600) {
            gInverseChecked = 0;  // try again (the first view may have been the title screen's)
            sinceFailed = 0;
        }
        if (gInverseChecked == 0) {
            // The layout's guess first, then every 16-byte offset of the view: the matrix whose product with the
            // translated view-projection is the identity.
            gInverseAt = SIZE_MAX;
            if (L.invTranslatedViewProjection != SIZE_MAX && L.invTranslatedViewProjection + 64 <= perView &&
                NearIdentity(tvp1, reinterpret_cast<float const*>(a + L.invTranslatedViewProjection))) {
                gInverseAt = L.invTranslatedViewProjection;
            }
            for (std::size_t o = 0; gInverseAt == SIZE_MAX && o + 64 <= perView; o += 16) {
                if (NearIdentity(tvp1, reinterpret_cast<float const*>(a + o))) {
                    gInverseAt = o;
                }
            }
            gInverseChecked = gInverseAt != SIZE_MAX ? 1 : -1;
            if (gInverseAt != SIZE_MAX) {
                log::Info("draw replay: the inverse of the translated view-projection is at +{} of the captured view bytes{}",
                    gInverseAt, gInverseAt == L.invTranslatedViewProjection ? " (the layout's offset)" : " (found by search; the layout's offset was wrong)");
            } else {
                log::Info("draw replay: no matrix among the captured view bytes is the inverse of the translated view-projection: no inverse pair in the substitution");
            }
        }
        if (gInverseChecked < 0 || !gInversePair.load(std::memory_order_relaxed)) {
            return;
        }
        auto const* const inv1 = reinterpret_cast<float const*>(a + gInverseAt);
        auto const* const inv2 = reinterpret_cast<float const*>(b + gInverseAt);
        float const u[3] = {-t[0], -t[1], -t[2]};
        float invSub[16], inv1T[16], invSubT[16];
        TranslateCols(inv2, u, invSub);
        AddMatrix(inv1, invSub, "inverse translated view-projection");
        Transpose(inv1, inv1T);
        Transpose(invSub, invSubT);
        AddMatrix(inv1T, invSubT, "inverse translated view-projection, transposed");
    }

    void Build(ViewFields const& first, ViewFields const& second) {
        entries.clear();
        matrices.clear();
        if (first.bytes.size() != second.bytes.size() || first.perView == 0) {
            return;
        }
        std::size_t const n = first.bytes.size();
        std::uint8_t const* a = first.bytes.data();
        std::uint8_t const* b = second.bytes.data();
        for (int v = 0; v < first.views; ++v) {
            std::size_t const base = static_cast<std::size_t>(v) * first.perView;
            if (base + first.perView <= n) {
                BuildMatrices(a + base, b + base, first.perView);
            }
        }
        // Rows: every differing 16-byte chunk.
        for (std::size_t o = 0; o + 16 <= n; o += 16) {
            if (std::memcmp(a + o, b + o, 16) != 0) {
                Add(a + o, b + o);
            }
        }
        // Columns of any 4x4 that could start at a 16-byte boundary (shaders may take matrices transposed).
        for (std::size_t o = 0; o + 64 <= n; o += 16) {
            if (std::memcmp(a + o, b + o, 64) == 0) {
                continue;
            }
            for (std::size_t c = 0; c < 4; ++c) {
                std::uint8_t ca[16], cb[16];
                for (std::size_t r = 0; r < 4; ++r) {
                    std::memcpy(ca + r * 4, a + o + r * 16 + c * 4, 4);
                    std::memcpy(cb + r * 4, b + o + r * 16 + c * 4, 4);
                }
                if (std::memcmp(ca, cb, 16) != 0) {
                    Add(ca, cb);
                }
            }
        }
        std::size_t slots = 64;
        while (slots < entries.size() * 4) {
            slots *= 2;
        }
        index.assign(slots, 0);
        filter.assign(8192, 0);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            std::uint32_t h = Hash12(entries[i].key) & (slots - 1);
            while (index[h] != 0) {
                h = (h + 1) & (slots - 1);
            }
            index[h] = static_cast<std::uint16_t>(i + 1);
            std::uint32_t const bit = Hash4(entries[i].key);
            filter[bit >> 3] |= static_cast<std::uint8_t>(1u << (bit & 7));
        }
    }

    // The entry whose first 12 bytes match, or null.
    Entry* Find(std::uint8_t const* chunk) {
        if (entries.empty()) {
            return nullptr;
        }
        std::uint32_t const bit = Hash4(chunk);
        if ((filter[bit >> 3] & (1u << (bit & 7))) == 0) {
            return nullptr;
        }
        std::size_t const slots = index.size();
        std::uint32_t h = Hash12(chunk) & (slots - 1);
        while (index[h] != 0) {
            Entry& e = entries[index[h] - 1];
            if (std::memcmp(e.key, chunk, 12) == 0) {
                return &e;
            }
            h = (h + 1) & (slots - 1);
        }
        return nullptr;
    }

    // Replaces in place; returns the chunks changed. A full 16-byte match takes the value whole, a 12-byte one
    // keeps the chunk's own 4th float. A matrix's first row checks the whole 64 bytes and replaces them.
    unsigned Apply(std::uint8_t* data, std::size_t size, unsigned* matrixHits = nullptr) {
        unsigned changed = 0;
        for (std::size_t o = 0; o + 16 <= size; o += 16) {
            Entry* const e = Find(data + o);
            if (e == nullptr) {
                continue;
            }
            if (e->matrix != 0) {
                Matrix& m = matrices[e->matrix - 1];
                if (o + 64 <= size && std::memcmp(data + o, m.key, 64) == 0) {
                    std::memcpy(data + o, m.value, 64);
                    ++m.used;
                    gMatrixHits[(e->matrix - 1) & 3].fetch_add(1, std::memory_order_relaxed);
                    e->used = true;
                    ++changed;
                    if (matrixHits != nullptr) {
                        ++*matrixHits;
                    }
                    o += 48;  // past the matrix: its other rows are not chunk keys
                }
                continue;
            }
            std::memcpy(data + o, e->value, std::memcmp(e->key + 12, data + o + 12, 4) == 0 ? 16 : 12);
            e->used = true;
            ++changed;
        }
        return changed;
    }
};

Substitution gSubstitution;
std::vector<std::uint8_t> gScratch;

// The replay's rect shift (research/37): while a replay goes to the right half, the width of the bound render target
// is looked up (cached per view), and a viewport, scissor or copy that addresses the left part of a target at least
// twice as wide as it is moves right by its own width.
thread_local bool t_rightHalf = false;
thread_local std::uint32_t t_boundTargetWidth = 0;
std::unordered_map<void*, std::uint32_t> gTargetWidths;   // RTV -> the texture's width (render thread)
std::atomic<std::uint64_t> gReplayedDraws{0};
std::atomic<std::uint64_t> gShiftedRects{0};

std::uint32_t TargetWidth(ID3D11RenderTargetView* view) {
    if (view == nullptr) {
        return 0;
    }
    auto const found = gTargetWidths.find(view);
    if (found != gTargetWidths.end()) {
        return found->second;
    }
    std::uint32_t width = 0;
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    if (resource != nullptr) {
        ID3D11Texture2D* texture = nullptr;
        if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture != nullptr) {
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            width = desc.Width;
            texture->Release();
        }
        resource->Release();
    }
    gTargetWidths.emplace(view, width);
    return width;
}
std::uint32_t ResourceWidth(ID3D11Resource* resource) {
    if (resource == nullptr) {
        return 0;
    }
    ID3D11Texture2D* texture = nullptr;
    std::uint32_t width = 0;
    if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture != nullptr) {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        width = desc.Width;
        texture->Release();
    }
    return width;
}

// ---- Walking a stream -----------------------------------------------------------------------------------------

struct Cursor {
    std::uint8_t const* at;
    std::uint8_t const* end;
    Header const* header = nullptr;
    std::uint8_t const* body = nullptr;
    bool Next() {
        if (at == nullptr || at + sizeof(Header) > end) {
            return false;
        }
        header = reinterpret_cast<Header const*>(at);
        body = at + sizeof(Header);
        at = body + header->size;
        return at <= end;
    }
};

Cursor TailOf(Stream const& s) {
    if (s.tail == SIZE_MAX || s.tail > s.bytes.size()) {
        return {nullptr, nullptr};
    }
    return {s.bytes.data() + s.tail, s.bytes.data() + s.bytes.size()};
}

bool Blocked(Stream const& s, std::string* why) {
    if (s.tail == SIZE_MAX) {
        if (why) *why = "no tail mark (the world DPG's light targets weren't seen)";
        return true;
    }
    if (s.unsupported != 0) {
        if (why) *why = std::format("{} unsupported call(s), first at vtable slot {}", s.unsupported, s.unsupportedSlot);
        return true;
    }
    if (s.bigUpdates != 0) {
        if (why) *why = std::format("{} buffer update(s) over {} KB", s.bigUpdates, kMaxRecordedUpdate >> 10);
        return true;
    }
    if (s.bigDiscardsUnshadowed != 0) {
        if (why) *why = std::format("{} dynamic buffer(s) over {} KB discarded inside the tail without a shadow copy (more than {} a frame, or no buffer for it)",
            s.bigDiscardsUnshadowed, kMaxRecordedMap >> 10, kShadowsPerBuffer);
        return true;
    }
    return false;
}

// Re-issues one recorded entry through the vtable's next entries. False if the entry is malformed.
bool Issue(ID3D11DeviceContext* c, Header const& h, std::uint8_t const* body, bool& skipped) {
    skipped = false;
    switch (h.op) {
    case Op::SetConstantBuffers:
    case Op::SetShaderResources:
    case Op::SetSamplers: {
        auto const* const p = reinterpret_cast<Pointers const*>(body);
        static unsigned const slotsBy[3][kStages] = {
            {kVSSetConstantBuffers, kPSSetConstantBuffers, kGSSetConstantBuffers, kHSSetConstantBuffers, kDSSetConstantBuffers, kCSSetConstantBuffers},
            {kVSSetShaderResources, kPSSetShaderResources, kGSSetShaderResources, kHSSetShaderResources, kDSSetShaderResources, kCSSetShaderResources},
            {kVSSetSamplers, kPSSetSamplers, kGSSetSamplers, kHSSetSamplers, kDSSetSamplers, kCSSetSamplers}};
        if (h.stage >= kStages) return false;
        unsigned const slot = slotsBy[static_cast<unsigned>(h.op)][h.stage];
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, void* const*)>(slot)(c, p->start, h.n, p->ptrs);
        return true;
    }
    case Op::SetShader: {
        auto const* const p = reinterpret_cast<Pointers const*>(body);
        static unsigned const slots[kStages] = {kVSSetShader, kPSSetShader, kGSSetShader, kHSSetShader, kDSSetShader, kCSSetShader};
        if (h.stage >= kStages) return false;
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, void*, ID3D11ClassInstance* const*, UINT)>(slots[h.stage])(c, p->ptrs[0], nullptr, 0);
        return true;
    }
    case Op::IASetInputLayout:
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11InputLayout*)>(kIASetInputLayout)(
            c, static_cast<ID3D11InputLayout*>(reinterpret_cast<Pointers const*>(body)->ptrs[0]));
        return true;
    case Op::IASetVertexBuffers: {
        auto const* const a = reinterpret_cast<VertexBuffers const*>(body);
        auto const* ptrs = reinterpret_cast<ID3D11Buffer* const*>(a + 1);
        auto const* const strides = reinterpret_cast<UINT const*>(ptrs + a->count);
        auto const* const offsets = strides + a->count;
        ID3D11Buffer* shadowed[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        if (!t_shadowSub.empty() && a->count <= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) {
            for (UINT i = 0; i < a->count; ++i) {
                auto const found = t_shadowSub.find(ptrs[i]);
                shadowed[i] = found != t_shadowSub.end() ? found->second : ptrs[i];
            }
            ptrs = shadowed;
        }
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*, UINT const*, UINT const*)>(
            kIASetVertexBuffers)(c, a->start, a->count, ptrs, strides, offsets);
        return true;
    }
    case Op::IASetIndexBuffer: {
        auto const* const a = reinterpret_cast<IndexBuffer const*>(body);
        auto* buffer = static_cast<ID3D11Buffer*>(a->buffer);
        if (!t_shadowSub.empty()) {
            auto const found = t_shadowSub.find(buffer);
            if (found != t_shadowSub.end()) {
                buffer = found->second;
            }
        }
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, DXGI_FORMAT, UINT)>(kIASetIndexBuffer)(
            c, buffer, static_cast<DXGI_FORMAT>(a->format), a->offset);
        return true;
    }
    case Op::IASetPrimitiveTopology:
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, D3D11_PRIMITIVE_TOPOLOGY)>(kIASetPrimitiveTopology)(
            c, static_cast<D3D11_PRIMITIVE_TOPOLOGY>(*reinterpret_cast<std::uint32_t const*>(body)));
        return true;
    case Op::DrawIndexed: {
        auto const* const a = reinterpret_cast<DrawIndexedArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT)>(kDrawIndexed)(c, a->indexCount, a->startIndex, a->baseVertex);
        gReplayedDraws.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    case Op::Draw: {
        auto const* const a = reinterpret_cast<DrawArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT)>(kDraw)(c, a->vertexCount, a->start);
        gReplayedDraws.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    case Op::DrawIndexedInstanced: {
        auto const* const a = reinterpret_cast<DrawIndexedInstancedArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT)>(kDrawIndexedInstanced)(
            c, a->indexCountPerInstance, a->instanceCount, a->startIndex, a->baseVertex, a->startInstance);
        gReplayedDraws.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    case Op::DrawInstanced: {
        auto const* const a = reinterpret_cast<DrawInstancedArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT)>(kDrawInstanced)(
            c, a->vertexCountPerInstance, a->instanceCount, a->startVertex, a->startInstance);
        return true;
    }
    case Op::DrawAuto:
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*)>(kDrawAuto)(c);
        return true;
    case Op::CSSetUnorderedAccessViews: {
        auto const* const p = reinterpret_cast<Pointers const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11UnorderedAccessView* const*, UINT const*)>(
            kCSSetUnorderedAccessViews)(c, p->start, h.n, reinterpret_cast<ID3D11UnorderedAccessView* const*>(p->ptrs), nullptr);
        return true;
    }
    case Op::Map:
        skipped = true;  // the data a map wrote is still in the buffer; a copied one is re-written at its Unmap
        return true;
    case Op::Unmap: {
        auto const* const a = reinterpret_cast<UnmapArgs const*>(body);
        if (a->dataSize == 0) {
            skipped = true;
            return true;
        }
        auto* const resource = static_cast<ID3D11Resource*>(a->resource);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        D3D11_MAP const type = a->mapType == D3D11_MAP_WRITE_NO_OVERWRITE ? D3D11_MAP_WRITE_DISCARD : static_cast<D3D11_MAP>(a->mapType);
        HRESULT const hr = Live<HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
            D3D11_MAPPED_SUBRESOURCE*)>(kMap)(c, resource, a->subresource, type, 0, &mapped);
        if (FAILED(hr) || mapped.pData == nullptr) {
            return false;
        }
        auto* const data = const_cast<std::uint8_t*>(reinterpret_cast<std::uint8_t const*>(a + 1));
        if (a->kind == 1 && a->dataSize >= 16 && a->dataSize <= kSubstituteMaxBytes) {
            unsigned matrixHits = 0;  // in place: the stream is this frame's recording, replayed once
            gSubstitutions.fetch_add(gSubstitution.Apply(data, a->dataSize, &matrixHits), std::memory_order_relaxed);
            gMatrixSubstitutions.fetch_add(matrixHits, std::memory_order_relaxed);
        }
        std::memcpy(mapped.pData, data, a->dataSize);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT)>(kUnmap)(c, resource, a->subresource);
        return true;
    }
    case Op::UpdateSubresource: {
        auto const* const a = reinterpret_cast<UpdateArgs const*>(body);
        if (a->kind >= 2 || a->dataSize == 0) {
            skipped = true;  // a texture upload (one-time data), or nothing to write
            return true;
        }
        auto* const data = const_cast<std::uint8_t*>(reinterpret_cast<std::uint8_t const*>(a + 1));
        if (a->kind == 1 && a->dataSize >= 16 && a->dataSize <= kSubstituteMaxBytes) {
            unsigned matrixHits = 0;  // in place
            gSubstitutions.fetch_add(gSubstitution.Apply(data, a->dataSize, &matrixHits), std::memory_order_relaxed);
            gMatrixSubstitutions.fetch_add(matrixHits, std::memory_order_relaxed);
        }
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_BOX const*, void const*, UINT, UINT)>(
            kUpdateSubresource)(c, static_cast<ID3D11Resource*>(a->resource), a->subresource, a->hasBox ? &a->box : nullptr, data,
            a->rowPitch, a->depthPitch);
        return true;
    }
    case Op::RSSetState:
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RasterizerState*)>(kRSSetState)(
            c, static_cast<ID3D11RasterizerState*>(reinterpret_cast<Pointers const*>(body)->ptrs[0]));
        return true;
    case Op::RSSetViewports: {
        auto const* const recorded = reinterpret_cast<D3D11_VIEWPORT const*>(body);
        if (t_rightHalf && h.n >= 1 && t_boundTargetWidth != 0 && recorded[0].Width > 0.0f &&
            static_cast<float>(t_boundTargetWidth) >= 2.0f * recorded[0].Width &&
            recorded[0].TopLeftX + recorded[0].Width <= static_cast<float>(t_boundTargetWidth) / 2.0f + 0.5f) {
            D3D11_VIEWPORT shifted[16];
            UINT const n = h.n < 16 ? h.n : 16;
            std::memcpy(shifted, recorded, n * sizeof(D3D11_VIEWPORT));
            for (UINT i = 0; i < n; ++i) {
                shifted[i].TopLeftX += recorded[0].Width;
            }
            gShiftedRects.fetch_add(1, std::memory_order_relaxed);
            Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, D3D11_VIEWPORT const*)>(kRSSetViewports)(c, n, shifted);
            return true;
        }
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, D3D11_VIEWPORT const*)>(kRSSetViewports)(c, h.n, recorded);
        return true;
    }
    case Op::RSSetScissorRects: {
        auto const* const recorded = reinterpret_cast<D3D11_RECT const*>(body);
        if (t_rightHalf && h.n >= 1 && t_boundTargetWidth != 0) {
            LONG const width = recorded[0].right - recorded[0].left;
            if (width > 0 && static_cast<LONG>(t_boundTargetWidth) >= 2 * width && recorded[0].right <= static_cast<LONG>(t_boundTargetWidth / 2)) {
                D3D11_RECT shifted[16];
                UINT const n = h.n < 16 ? h.n : 16;
                std::memcpy(shifted, recorded, n * sizeof(D3D11_RECT));
                for (UINT i = 0; i < n; ++i) {
                    shifted[i].left += width;
                    shifted[i].right += width;
                }
                gShiftedRects.fetch_add(1, std::memory_order_relaxed);
                Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, D3D11_RECT const*)>(kRSSetScissorRects)(c, n, shifted);
                return true;
            }
        }
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, D3D11_RECT const*)>(kRSSetScissorRects)(c, h.n, recorded);
        return true;
    }
    case Op::OMSetRenderTargets: {
        auto const* const p = reinterpret_cast<Pointers const*>(body);
        if (t_rightHalf) {
            t_boundTargetWidth = h.n >= 1 ? TargetWidth(static_cast<ID3D11RenderTargetView*>(p->ptrs[0])) : 0;
        }
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*)>(
            kOMSetRenderTargets)(c, h.n, reinterpret_cast<ID3D11RenderTargetView* const*>(p->ptrs),
            static_cast<ID3D11DepthStencilView*>(p->ptrs[h.n]));
        return true;
    }
    case Op::OMSetBlendState: {
        auto const* const a = reinterpret_cast<BlendArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11BlendState*, FLOAT const*, UINT)>(kOMSetBlendState)(
            c, static_cast<ID3D11BlendState*>(a->state), a->factor, a->mask);
        return true;
    }
    case Op::OMSetDepthStencilState: {
        auto const* const a = reinterpret_cast<DepthStencilArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilState*, UINT)>(kOMSetDepthStencilState)(
            c, static_cast<ID3D11DepthStencilState*>(a->state), a->reference);
        return true;
    }
    case Op::ClearRenderTargetView: {
        auto const* const a = reinterpret_cast<ClearTargetArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, FLOAT const*)>(kClearRenderTargetView)(
            c, static_cast<ID3D11RenderTargetView*>(a->view), a->colour);
        return true;
    }
    case Op::ClearDepthStencilView: {
        auto const* const a = reinterpret_cast<ClearDepthArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8)>(kClearDepthStencilView)(
            c, static_cast<ID3D11DepthStencilView*>(a->view), a->flags, a->depth, a->stencil);
        return true;
    }
    case Op::CopyResource: {
        auto const* const a = reinterpret_cast<CopyArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*)>(kCopyResource)(
            c, static_cast<ID3D11Resource*>(a->dst), static_cast<ID3D11Resource*>(a->src));
        return true;
    }
    case Op::CopySubresourceRegion: {
        auto const* const a = reinterpret_cast<CopyRegionArgs const*>(body);
        UINT x = a->x;
        if (t_rightHalf && a->hasBox && a->box.right > a->box.left) {
            std::uint32_t const boxWidth = a->box.right - a->box.left;
            std::uint32_t const dstWidth = ResourceWidth(static_cast<ID3D11Resource*>(a->dst));
            if (dstWidth >= 2 * boxWidth && a->x + boxWidth <= dstWidth / 2) {
                x += boxWidth;
                gShiftedRects.fetch_add(1, std::memory_order_relaxed);
            }
        }
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, D3D11_BOX const*)>(
            kCopySubresourceRegion)(c, static_cast<ID3D11Resource*>(a->dst), a->dstSub, x, a->y, a->z,
            static_cast<ID3D11Resource*>(a->src), a->srcSub, a->hasBox ? &a->box : nullptr);
        return true;
    }
    case Op::ResolveSubresource: {
        auto const* const a = reinterpret_cast<ResolveArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT)>(
            kResolveSubresource)(c, static_cast<ID3D11Resource*>(a->dst), a->dstSub, static_cast<ID3D11Resource*>(a->src),
            a->srcSub, static_cast<DXGI_FORMAT>(a->format));
        return true;
    }
    case Op::GenerateMips:
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11ShaderResourceView*)>(kGenerateMips)(
            c, static_cast<ID3D11ShaderResourceView*>(reinterpret_cast<Pointers const*>(body)->ptrs[0]));
        return true;
    case Op::Begin:
    case Op::End:
        skipped = true;  // the first eye's queries keep their results; the second eye never reads any
        return true;
    case Op::SetPredication: {
        auto const* const a = reinterpret_cast<PredicationArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Predicate*, BOOL)>(kSetPredication)(
            c, static_cast<ID3D11Predicate*>(a->predicate), a->value != 0 ? TRUE : FALSE);
        return true;
    }
    case Op::Dispatch: {
        auto const* const a = reinterpret_cast<DispatchArgs const*>(body);
        Live<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT)>(kDispatch)(c, a->x, a->y, a->z);
        return true;
    }
    case Op::Unsupported:
    case Op::kCount:
        return false;
    }
    return false;
}

// ---- The diff -------------------------------------------------------------------------------------------------

fs::path PerfDir() {
    wchar_t buffer[MAX_PATH]{};
    DWORD const n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    std::error_code ec;
    fs::path const base = (n > 0 && n < MAX_PATH) ? fs::path(buffer) : fs::temp_directory_path(ec);
    return base / L"MEVR" / L"perf";
}

std::string Stamp() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    return std::format("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

std::string Floats(std::uint8_t const* p, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        float f;
        std::memcpy(&f, p + i * 4, 4);
        out += std::format("{}{:.4g}", i == 0 ? "" : ", ", f);
    }
    return out;
}

// The bytes of an entry that must agree between the eyes: everything but a copied payload.
std::size_t ComparableSize(Header const& h) {
    switch (h.op) {
    case Op::UpdateSubresource: return sizeof(UpdateArgs);
    case Op::Unmap: return sizeof(UnmapArgs);
    case Op::Map: return sizeof(MapArgs);
    default: return h.size;
    }
}

} // namespace

// ---- Public ---------------------------------------------------------------------------------------------------

std::atomic<std::uint64_t> gRetaken{0};

// Takes back any of our vtable slots that another hooker overwrote since Attach (Halcyon installs its draw hooks into
// the same vtable from the swapchain's creation, after the first Present attached us: research/37, experiment 4, a
// replay with no draws). The overwriter becomes the next entry, so it still sees every call.
void Reattach() {
    if (!gAttached.load(std::memory_order_acquire) || gVtable == nullptr) {
        return;
    }
    {
        // The inline-hooked entries: whether the vtable still names the function we hooked (research/37, experiment 6:
        // the inline hooks saw no draws, so the calls reach another function).
        static unsigned logged = 0;
        for (unsigned const slot : kVolatileSlots) {
            if (gInlineSlot[slot] && gVtable[slot] != gAttachSlot[slot] && logged < 6) {
                ++logged;
                log::Info("draw replay: slot {} now holds {} ({}); at Attach it held {} ({}), where the inline hook is",
                    slot, gVtable[slot], ModuleNameOf(gVtable[slot]), gAttachSlot[slot], ModuleNameOf(gAttachSlot[slot]));
            }
        }
    }
    unsigned displaced = 0;
    std::string detail;
    for (HookEntry const& e : kHooks) {
        if (!gInlineSlot[e.slot] && gVtable[e.slot] != e.function) {
            ++displaced;
            if (detail.size() < 600) {
                std::string const module = ModuleNameOf(gVtable[e.slot]);
                detail += std::format("{}{} -> {}", detail.empty() ? "" : ", ", e.slot, module.empty() ? "no module" : module);
            }
        }
    }
    if (displaced == 0) {
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(gVtable, kSlots * sizeof(void*), PAGE_READWRITE, &old)) {
        return;
    }
    for (HookEntry const& e : kHooks) {
        if (!gInlineSlot[e.slot] && gVtable[e.slot] != e.function) {
            gNext[e.slot] = gVtable[e.slot];
            gVtable[e.slot] = e.function;
        }
    }
    VirtualProtect(gVtable, kSlots * sizeof(void*), old, &old);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    static std::uint64_t lastMs = 0;
    std::uint64_t const nowMs = GetTickCount64();
    std::uint64_t const n = gRetaken.fetch_add(1, std::memory_order_relaxed);
    if (n < 5 || n % 1000 == 0) {
        log::Info("draw replay: re-hook #{}: {} of our vtable entries had been overwritten since the last look ({} ms ago): slot -> new occupant: {}; taken back above them",
            n + 1, displaced, lastMs == 0 ? 0 : nowMs - lastMs, detail);
    }
    lastMs = nowMs;
}

bool Attach(ID3D11DeviceContext* context, hook::Context* hooks) {
    gContextSeen = context;
    gHooks = hooks;
    if (gAttached.load(std::memory_order_acquire)) {
        return true;
    }
    if (context == nullptr) {
        return false;
    }
    void** const vtable = *reinterpret_cast<void***>(context);
    // Halcyon's draw hooks (d3d_capture) sit below ours, so the recorder sees the game's calls and the replay runs
    // through them: they go in when the swapchain is captured, before any Present, and we're called from a
    // Present's end. They can't be seen in this vtable, though: on 2026-10-23 its DrawIndexed slot still held
    // d3d11's own entry with Halcyon's hooks installed and working (an overlay, the EA app's igo64.dll, had given
    // the context a private copy of the table before Halcyon patched the original; Halcyon's inline hooks on the
    // d3d11 entries carry its capture). So no waiting on the slot: whatever it holds, our entries go above it and
    // the calls pass down through it.
    std::string const occupant = ModuleNameOf(vtable[kDrawIndexed]);
    log::Info("draw replay: DrawIndexed's slot held an entry in {}", occupant.empty() ? "no module (a trampoline)" : occupant);
    DWORD old = 0;
    if (!VirtualProtect(vtable, kSlots * sizeof(void*), PAGE_READWRITE, &old)) {
        log::Warn("draw replay: VirtualProtect on the context's vtable failed ({}); off", GetLastError());
        return false;
    }
    for (unsigned i = 0; i < kSlots; ++i) {
        gNext[i] = vtable[i];
        gAttachSlot[i] = vtable[i];
    }
    log::Info("draw replay: context {} vtable {}: DrawIndexed {} ({}), UpdateSubresource {} ({}), ClearDepthStencilView {} ({}), VSSetShader {} ({})",
        static_cast<void*>(context), static_cast<void*>(vtable), vtable[kDrawIndexed], ModuleNameOf(vtable[kDrawIndexed]),
        vtable[kUpdateSubresource], ModuleNameOf(vtable[kUpdateSubresource]), vtable[kClearDepthStencilView],
        ModuleNameOf(vtable[kClearDepthStencilView]), vtable[kVSSetShader], ModuleNameOf(vtable[kVSSetShader]));
    unsigned hooked = 0, inlined = 0;
    std::string inlineFailures;
    for (HookEntry const& e : kHooks) {
        if (hooks != nullptr && Volatile(e.slot)) {
            void* original = nullptr;
            std::string const name = std::format("ID3D11DeviceContext[{}]", e.slot);
            VariantHook const* const variant = FindVariant(e.slot);
            void* const detour = variant != nullptr ? variant->fn[0] : e.function;
            if (hook::Install(*hooks, name.c_str(), vtable[e.slot], detour, &original) && original != nullptr) {
                if (variant != nullptr) {
                    gNextV[e.slot][0] = original;
                    gVariantTarget[e.slot][0] = vtable[e.slot];
                } else {
                    gNext[e.slot] = original;  // the trampoline: Halcyon's hook where it has one, else d3d11's body
                }
                gInlineSlot[e.slot] = true;
                ++inlined;
                continue;
            }
            inlineFailures += std::format("{} ", e.slot);
        }
        vtable[e.slot] = e.function;
        ++hooked;
    }
    VirtualProtect(vtable, kSlots * sizeof(void*), old, &old);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    gVtable = vtable;
    gContext = context;
    for (Stream& s : gStreams) {
        s.bytes.reserve(8u << 20);
    }
    gHeld.reserve(4096);
    gAttached.store(true, std::memory_order_release);
    log::Info("draw replay: hooked {} entries of the immediate context's vtable and {} of d3d11's functions inline (the entries an "
        "overlay restores at every Present){}", hooked, inlined,
        inlineFailures.empty() ? "" : std::format("; inline hooks refused for slots {}(in the vtable instead)", inlineFailures));
    return true;
}

ID3D11DeviceContext* Context() {
    return gContextSeen;
}

bool Attached() {
    return gAttached.load(std::memory_order_acquire);
}

// The table's slots: a function in the vtable that no variant detour sits on yet gets one (research/37, experiment 6).
void VariantSweep() {
    if (gHooks == nullptr || gVtable == nullptr) {
        return;
    }
    for (VariantHook const& v : kVariantHooks) {
        void* const target = gVtable[v.slot];
        if (target == nullptr) {
            continue;
        }
        unsigned free = kVariants;
        bool known = false;
        for (unsigned k = 0; k < kVariants; ++k) {
            if (gVariantTarget[v.slot][k] == target) {
                known = true;
                break;
            }
            if (gVariantTarget[v.slot][k] == nullptr && free == kVariants) {
                free = k;
            }
        }
        if (known) {
            continue;
        }
        if (free == kVariants) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                log::Warn("draw replay: slot {} holds a fourth variant ({}); not hooked (the replay may miss its calls)", v.slot, target);
            }
            continue;
        }
        void* original = nullptr;
        std::string const name = std::format("ID3D11DeviceContext[{}]v{}", v.slot, free);
        if (hook::Install(*gHooks, name.c_str(), target, v.fn[free], &original) && original != nullptr) {
            gNextV[v.slot][free] = original;
            gVariantTarget[v.slot][free] = target;
            log::Info("draw replay: slot {} variant {} at {} ({}) hooked", v.slot, free, target, ModuleNameOf(target));
        } else {
            gVariantTarget[v.slot][free] = target;  // don't retry every frame
            log::Warn("draw replay: slot {} variant {} at {}: the inline hook failed", v.slot, free, target);
        }
    }
}

void BeginRecording(int stream) {
    Reattach();
    VariantSweep();
    if (!Attached() || stream < 0 || stream > 1) {
        return;
    }
    Stream& s = gStreams[stream];
    s.Clear();
    for (auto& [resource, set] : gShadows) {
        set.used = 0;
    }
    t_recording = &s;
}

void EndRecording() {
    Stream* const s = t_recording;
    t_recording = nullptr;
    if (s == &gStreams[0]) {
        gRecorded.fetch_add(1, std::memory_order_relaxed);
        gRecordedCalls.fetch_add(s->entries, std::memory_order_relaxed);
        gRecordedBytes.fetch_add(s->bytes.size(), std::memory_order_relaxed);
        if (s->bigDiscardsInTail != 0) {
            gTailDiscardFrames.fetch_add(1, std::memory_order_relaxed);
            gTailDiscards.fetch_add(s->bigDiscardsInTail, std::memory_order_relaxed);
        }
        gMapsSkipped.fetch_add(s->mapsSkipped, std::memory_order_relaxed);
    }
}

void MarkTail() {
    if (t_recording != nullptr && t_recording->tail == SIZE_MAX) {
        t_recording->tail = t_recording->bytes.size();
        t_recording->tailEntries = t_recording->entries;  // entries before the tail; the tail's are the rest
    }
}

bool TailReplayable(std::string* why) {
    if (!Attached()) {
        if (why) *why = "not attached";
        return false;
    }
    if (Blocked(gStreams[0], why)) {
        gNotReplayable.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> const lock(gBlockerMutex);
        if (why) gLastBlocker = *why;
        return false;
    }
    return true;
}

int Replay(ViewFields const& first, ViewFields const& second, bool rightHalf) {
    if (!Attached()) {
        return 1;
    }
    t_rightHalf = rightHalf;
    t_boundTargetWidth = 0;
    Stream const& s = gStreams[0];
    if (s.tail == SIZE_MAX) {
        return 2;
    }
    gSubstitution.Build(first, second);
    {
        // Once a session: what the tail is made of (research/37: a replay that issued no draws).
        static std::uint64_t replaysSeen = 0;
        if (replaysSeen++ % 1500 == 0) {
            unsigned counts[static_cast<unsigned>(Op::kCount)]{};
            unsigned total = 0, drawsBeforeTail = 0;
            Cursor whole{s.bytes.data(), s.bytes.data() + s.bytes.size()};
            unsigned at = 0;
            while (whole.Next()) {
                bool const isDraw = whole.header->op == Op::DrawIndexed || whole.header->op == Op::Draw ||
                                    whole.header->op == Op::DrawIndexedInstanced || whole.header->op == Op::DrawInstanced;
                if (isDraw && whole.at <= s.bytes.data() + s.tail) {
                    ++drawsBeforeTail;
                }
                ++at;
            }
            Cursor t = TailOf(s);
            while (t.Next()) {
                ++counts[static_cast<unsigned>(t.header->op)];
                ++total;
            }
            std::string text;
            for (unsigned i = 0; i < static_cast<unsigned>(Op::kCount); ++i) {
                if (counts[i] != 0) {
                    text += std::format("{} {}, ", kOpNames[i], counts[i]);
                }
            }
            log::Info("draw replay: stream 0 has {} entries, tail at byte {} of {} ({} entries), {} draws before the tail; tail ops: {}",
                at, s.tail, s.bytes.size(), total, drawsBeforeTail, text);
        }
    }
    // Shadowed buffers (research/37, the strobe): until a buffer's first in-tail discard its draws bind the shadow
    // holding the contents of that time; past each discard, the next shadow, and past the last one the buffer itself.
    std::unordered_map<void*, std::vector<ID3D11Buffer*>> shadowLists;
    std::unordered_map<void*, std::size_t> shadowPos;
    t_shadowSub.clear();
    for (Stream::ShadowSwitch const& sw : s.shadows) {
        shadowLists[sw.resource].push_back(sw.shadow);
    }
    for (auto const& [resource, list] : shadowLists) {
        t_shadowSub[resource] = list.front();
        shadowPos[resource] = 0;
    }
    std::size_t nextSwitch = 0;
    unsigned entryIndex = s.tailEntries;
    Cursor cursor = TailOf(s);
    std::uint64_t issued = 0;
    while (cursor.Next()) {
        while (nextSwitch < s.shadows.size() && s.shadows[nextSwitch].atEntry <= entryIndex) {
            void* const resource = s.shadows[nextSwitch++].resource;
            std::size_t const pos = ++shadowPos[resource];
            std::vector<ID3D11Buffer*> const& list = shadowLists[resource];
            if (pos < list.size()) {
                t_shadowSub[resource] = list[pos];
            } else {
                t_shadowSub.erase(resource);   // past its last discard: the buffer's own contents
            }
        }
        bool skipped = false;
        if (!Issue(gContext, *cursor.header, cursor.body, skipped)) {
            gFailures.fetch_add(1, std::memory_order_relaxed);
            log::Warn("draw replay: entry {} ({}) couldn't be re-issued; the replay is off for the session",
                issued, kOpNames[static_cast<unsigned>(cursor.header->op)]);
            t_rightHalf = false;
            t_shadowSub.clear();
            return 3;
        }
        ++issued;
        ++entryIndex;
    }
    t_shadowSub.clear();
    t_rightHalf = false;
    gReplays.fetch_add(1, std::memory_order_relaxed);
    gReplayedCalls.fetch_add(issued, std::memory_order_relaxed);
    return 0;
}

bool WriteDiff(ViewFields const& first, ViewFields const& second, std::string const& tag) {
    Stream const& a = gStreams[0];
    Stream const& b = gStreams[1];
    gSubstitution.Build(first, second);
    std::string blocker;
    bool const replayable = !Blocked(a, &blocker);

    std::string report;
    report += std::format("eye diff {}{}\n", Stamp(), tag.empty() ? "" : " " + tag);
    report += std::format("first eye: {} calls recorded ({} KB), {} draws, {} buffer updates, {} texture updates (skipped by a "
        "replay), {} maps copied, {} maps too big to copy; tail from entry {}: {} calls\n",
        a.entries, a.bytes.size() >> 10, a.draws, a.updates, a.textureUpdates, a.mapsCopied, a.mapsSkipped,
        a.tail == SIZE_MAX ? -1 : static_cast<int>(a.tailEntries), a.tail == SIZE_MAX ? 0u : a.entries - a.tailEntries);
    report += std::format("second eye (the engine's replay): {} calls, {} draws, {} buffer updates\n", b.entries, b.draws, b.updates);
    report += std::format("tail replayable: {}{}\n", replayable ? "yes" : "no", replayable ? "" : " (" + blocker + ")");
    report += std::format("view fields: {} view(s) x {} bytes; substitution keys {} (16-byte chunks that differ, rows and columns; "
        "{} of them the first rows of 64-byte matrices)\n", first.views, first.perView, gSubstitution.entries.size(),
        gSubstitution.matrices.size());
    for (Substitution::Entry const& e : gSubstitution.entries) {
        if (e.matrix == 0) {
            report += std::format("  key ({}) -> ({})\n", Floats(e.key, 4), Floats(e.value, 4));
        }
    }
    for (Substitution::Matrix const& m : gSubstitution.matrices) {
        report += std::format("  matrix {}: rows ({}) ({}) ({}) ({}) -> ({}) ({}) ({}) ({})\n", m.what,
            Floats(m.key, 4), Floats(m.key + 16, 4), Floats(m.key + 32, 4), Floats(m.key + 48, 4),
            Floats(m.value, 4), Floats(m.value + 16, 4), Floats(m.value + 32, 4), Floats(m.value + 48, 4));
    }
    {
        // Where the matrices are in the first eye's tail: every constant-buffer update, through the substitution.
        Cursor t = TailOf(a);
        unsigned constantUpdates = 0, withMatrix = 0;
        std::map<std::pair<std::uint32_t, std::uint32_t>, unsigned> matrixAt;
        for (Substitution::Matrix& m : gSubstitution.matrices) {
            m.used = 0;
        }
        while (t.Next()) {
            std::uint8_t const* payload = nullptr;
            std::size_t size = 0;
            if (t.header->op == Op::UpdateSubresource) {
                auto const* const u = reinterpret_cast<UpdateArgs const*>(t.body);
                if (u->kind == 1 && u->dataSize != 0) {
                    payload = reinterpret_cast<std::uint8_t const*>(u + 1);
                    size = u->dataSize;
                }
            } else if (t.header->op == Op::Unmap) {
                auto const* const u = reinterpret_cast<UnmapArgs const*>(t.body);
                if (u->kind == 1 && u->dataSize != 0) {
                    payload = reinterpret_cast<std::uint8_t const*>(u + 1);
                    size = u->dataSize;
                }
            }
            if (payload == nullptr) {
                continue;
            }
            ++constantUpdates;
            gScratch.assign(payload, payload + size);
            unsigned hits = 0;
            gSubstitution.Apply(gScratch.data(), gScratch.size(), &hits);
            withMatrix += hits != 0 ? 1 : 0;
            if (hits != 0) {
                for (std::size_t o = 0; o + 16 <= size; o += 16) {
                    if (std::memcmp(gScratch.data() + o, payload + o, 16) != 0) {
                        ++matrixAt[{static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(o)}];
                        o += 48;
                    }
                }
            }
        }
        report += std::format("the first eye's tail: {} constant-buffer updates, {} of them carrying a matrix key:", constantUpdates, withMatrix);
        for (Substitution::Matrix& m : gSubstitution.matrices) {
            report += std::format(" {} x{};", m.what, m.used);
            m.used = 0;
        }
        report += "\n  the matrices sit at (update size, offset): ";
        for (auto const& [where, count] : matrixAt) {
            report += std::format("{} B +{} x{}; ", where.first, where.second, count);
        }
        for (Substitution::Entry& e : gSubstitution.entries) {
            e.used = false;
        }
        report += "\n";
    }

    // Walk both in step.
    Cursor ca = TailOf(a);
    Cursor cb = b.tail != SIZE_MAX ? TailOf(b) : Cursor{b.bytes.data(), b.bytes.data() + b.bytes.size()};  // tails both (research/37)
    unsigned index = 0, structural = 0, pairs = 0, updatesCompared = 0, updatesDiffering = 0, chunksDiffering = 0,
        chunksCovered = 0, chunksUncovered = 0;
    std::map<std::pair<void*, std::uint32_t>, unsigned> uncoveredBy;  // (the bound PS or VS, offset) -> count
    std::vector<std::string> samples;
    void* boundShader[kStages]{};
    std::string firstMismatch;
    for (;;) {
        bool const hasA = ca.Next();
        bool const hasB = cb.Next();
        if (!hasA || !hasB) {
            if (hasA != hasB) {
                ++structural;
                if (firstMismatch.empty()) {
                    firstMismatch = std::format("entry {}: the {} stream ends first", index, hasA ? "second" : "first");
                }
            }
            break;
        }
        ++pairs;
        Header const& ha = *ca.header;
        Header const& hb = *cb.header;
        if (ha.op == Op::SetShader && ha.stage < kStages) {
            boundShader[ha.stage] = reinterpret_cast<Pointers const*>(ca.body)->ptrs[0];
        }
        std::size_t const cmpA = ComparableSize(ha);
        if (ha.op != hb.op || ha.stage != hb.stage || ha.n != hb.n || cmpA != ComparableSize(hb) ||
            std::memcmp(ca.body, cb.body, std::min<std::size_t>(cmpA, std::min<std::size_t>(ha.size, hb.size))) != 0) {
            ++structural;
            if (firstMismatch.empty()) {
                firstMismatch = std::format("entry {}: first {} (stage {}, n {}, {} bytes) vs second {} (stage {}, n {}, {} bytes)",
                    index, kOpNames[static_cast<unsigned>(ha.op)], ha.stage, ha.n, ha.size,
                    kOpNames[static_cast<unsigned>(hb.op)], hb.stage, hb.n, hb.size);
            }
            if (structural > 50) {
                break;  // out of step: no point going on
            }
            ++index;
            continue;
        }
        std::uint8_t const* payloadA = nullptr;
        std::uint8_t const* payloadB = nullptr;
        std::size_t payloadSize = 0;
        std::uint32_t kind = 0;
        void* resource = nullptr;
        if (ha.op == Op::UpdateSubresource) {
            auto const* const ua = reinterpret_cast<UpdateArgs const*>(ca.body);
            auto const* const ub = reinterpret_cast<UpdateArgs const*>(cb.body);
            if (ua->dataSize == ub->dataSize && ua->dataSize != 0) {
                payloadA = reinterpret_cast<std::uint8_t const*>(ua + 1);
                payloadB = reinterpret_cast<std::uint8_t const*>(ub + 1);
                payloadSize = ua->dataSize;
                kind = ua->kind;
                resource = ua->resource;
            }
        } else if (ha.op == Op::Unmap) {
            auto const* const ua = reinterpret_cast<UnmapArgs const*>(ca.body);
            auto const* const ub = reinterpret_cast<UnmapArgs const*>(cb.body);
            if (ua->dataSize == ub->dataSize && ua->dataSize != 0) {
                payloadA = reinterpret_cast<std::uint8_t const*>(ua + 1);
                payloadB = reinterpret_cast<std::uint8_t const*>(ub + 1);
                payloadSize = ua->dataSize;
                kind = ua->kind;
                resource = ua->resource;
            }
        }
        if (payloadA != nullptr) {
            ++updatesCompared;
            bool differs = false;
            gScratch.assign(payloadA, payloadA + payloadSize);
            gSubstitution.Apply(gScratch.data(), gScratch.size());  // the whole update: matrices span four chunks
            for (std::size_t o = 0; o + 16 <= payloadSize; o += 16) {
                if (std::memcmp(payloadA + o, payloadB + o, 16) == 0) {
                    continue;
                }
                differs = true;
                ++chunksDiffering;
                if (std::memcmp(gScratch.data() + o, payloadB + o, 16) == 0) {
                    ++chunksCovered;
                } else {
                    ++chunksUncovered;
                    void* const shader = boundShader[PS] != nullptr ? boundShader[PS] : boundShader[VS];
                    ++uncoveredBy[{shader, static_cast<std::uint32_t>(o)}];
                    if (samples.size() < 60) {
                        samples.push_back(std::format("  entry {} {} {} buffer {} ({} B) +{}: first ({}) second ({}) [VS {} PS {}]",
                            index, kOpNames[static_cast<unsigned>(ha.op)], kind == 1 ? "constant" : "other", resource,
                            payloadSize, o, Floats(payloadA + o, 4), Floats(payloadB + o, 4), boundShader[VS], boundShader[PS]));
                    }
                }
            }
            if (differs) {
                ++updatesDiffering;
            }
        }
        ++index;
    }
    report += std::format("\naligned pairs {}; structural mismatches {}{}\n", pairs, structural,
        firstMismatch.empty() ? "" : "; first: " + firstMismatch);
    report += std::format("buffer updates compared {}; differing {}; differing 16-byte chunks {}: covered by the substitution {}, "
        "not covered {}\n", updatesCompared, updatesDiffering, chunksDiffering, chunksCovered, chunksUncovered);
    unsigned keysUsed = 0;
    for (Substitution::Entry const& e : gSubstitution.entries) {
        keysUsed += e.used ? 1 : 0;
    }
    report += std::format("substitution keys used {} of {}; matrix keys used in the aligned updates:", keysUsed, gSubstitution.entries.size());
    for (Substitution::Matrix const& m : gSubstitution.matrices) {
        report += std::format(" {} x{};", m.what, m.used);
    }
    report += "\n";
    if (!uncoveredBy.empty()) {
        report += "\nnot covered, by (bound shader, offset): count\n";
        std::vector<std::pair<unsigned, std::pair<void*, std::uint32_t>>> sorted;
        for (auto const& [key, count] : uncoveredBy) {
            sorted.emplace_back(count, key);
        }
        std::sort(sorted.begin(), sorted.end(), [](auto const& x, auto const& y) { return x.first > y.first; });
        for (std::size_t i = 0; i < sorted.size() && i < 40; ++i) {
            report += std::format("  shader {} +{}: {}\n", sorted[i].second.first, sorted[i].second.second, sorted[i].first);
        }
        report += "\nsamples:\n";
        for (std::string const& s : samples) {
            report += s + "\n";
        }
    }
    bool const covered = structural == 0 && chunksUncovered == 0;
    report += std::format("\nverdict: {}\n", !replayable ? "the tail can't be replayed (" + blocker + ")"
        : structural != 0 ? "the streams don't align: the replay would issue the wrong calls"
        : chunksUncovered != 0 ? "aligned; some constant bytes differ beyond the view fields (listed): the replay would get those wrong"
        : "aligned and fully covered: the replay reproduces the engine's second eye");

    std::error_code ec;
    fs::path const dir = PerfDir();
    fs::create_directories(dir, ec);
    std::string safeTag;
    for (char const ch : tag) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '_') safeTag += ch;
    }
    fs::path const path = dir / (Stamp() + (safeTag.empty() ? "" : "-" + safeTag) + "-eyediff.txt");
    {
        std::ofstream out(path, std::ios::binary);
        out << report;
    }
    log::Info("draw replay: eye diff: {} pairs, {} structural mismatches, {} differing chunks ({} covered, {} not), tail {}: {}",
        pairs, structural, chunksDiffering, chunksCovered, chunksUncovered, replayable ? "replayable" : "not replayable",
        mevr::ini::Utf8(path));
    return replayable && covered;
}

void RequestDiff(std::string const& tag) {
    std::lock_guard<std::mutex> const lock(gDiffMutex);
    gDiffPending = true;
    gDiffTag = tag;
}

bool DiffPending() {
    std::lock_guard<std::mutex> const lock(gDiffMutex);
    return gDiffPending;
}

std::string TakeDiffTag() {
    std::lock_guard<std::mutex> const lock(gDiffMutex);
    gDiffPending = false;
    return std::move(gDiffTag);
}

void OnPresentEnd() {
    if (!Attached()) {
        return;
    }
    t_recording = nullptr;
    for (IUnknown* p : gHeld) {
        p->Release();
    }
    gHeld.clear();
    gHeldCache.fill(nullptr);
    gBuffers.clear();
    gMapped.clear();
    for (Stream& s : gStreams) {
        if (s.bytes.capacity() > (64u << 20)) {
            s.bytes.Free();  // an unusual frame: give the memory back
            s.bytes.reserve(8u << 20);
        }
        s.Clear();
    }
}

void SetInversePair(bool on) {
    gInversePair.store(on, std::memory_order_relaxed);
}

void SetViewLayout(ViewLayout const& layout) {
    gLayout = layout;
    gInverseChecked = 0;
    log::Info("draw replay: view layout: translated view-projection at +{}, its inverse at +{}, the pre-view translation at +{} of the captured view bytes",
        layout.translatedViewProjection == SIZE_MAX ? -1 : static_cast<int>(layout.translatedViewProjection),
        layout.invTranslatedViewProjection == SIZE_MAX ? -1 : static_cast<int>(layout.invTranslatedViewProjection),
        layout.preViewTranslation == SIZE_MAX ? -1 : static_cast<int>(layout.preViewTranslation));
}

unsigned RecordedDraws(int stream) {
    return stream >= 0 && stream < 2 ? gStreams[stream].draws : 0u;
}

// Time-stamp counter ticks as milliseconds, the rate measured against the performance counter since the first call.
double TicksToMs(std::uint64_t ticks) {
    static std::uint64_t const tsc0 = __rdtsc();
    static LARGE_INTEGER const qpc0 = [] { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q; }();
    static LARGE_INTEGER const freq = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q; }();
    std::uint64_t const tsc = __rdtsc();
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    double const seconds = static_cast<double>(qpc.QuadPart - qpc0.QuadPart) / static_cast<double>(freq.QuadPart);
    if (seconds < 0.5 || tsc <= tsc0 || ticks == 0) {
        return 0.0;
    }
    double const rate = static_cast<double>(tsc - tsc0) / seconds;   // ticks a second
    return static_cast<double>(ticks) / rate * 1000.0;
}

Stats TakeStats() {
    Stats s;
    s.replays = gReplays.exchange(0, std::memory_order_relaxed);
    s.failures = gFailures.exchange(0, std::memory_order_relaxed);
    s.recorded = gRecorded.exchange(0, std::memory_order_relaxed);
    s.recordedCalls = gRecordedCalls.exchange(0, std::memory_order_relaxed);
    s.recordedBytes = gRecordedBytes.exchange(0, std::memory_order_relaxed);
    s.replayedCalls = gReplayedCalls.exchange(0, std::memory_order_relaxed);
    s.substitutions = gSubstitutions.exchange(0, std::memory_order_relaxed);
    s.notReplayable = gNotReplayable.exchange(0, std::memory_order_relaxed);
    s.replayedDraws = gReplayedDraws.exchange(0, std::memory_order_relaxed);
    s.shiftedRects = gShiftedRects.exchange(0, std::memory_order_relaxed);
    s.matrixSubstitutions = gMatrixSubstitutions.exchange(0, std::memory_order_relaxed);
    s.rehooks = gRetaken.exchange(0, std::memory_order_relaxed);
    s.notedByHalcyon = gNotedByHalcyon.exchange(0, std::memory_order_relaxed);
    for (int i = 0; i < 4; ++i) {
        s.matrixHits[i] = gMatrixHits[i].exchange(0, std::memory_order_relaxed);
    }
    s.seenByOurs = gSeenByOurs.exchange(0, std::memory_order_relaxed);
    s.mapsCopied = gMapsCopied.exchange(0, std::memory_order_relaxed);
    s.mapCopyBytes = gMapCopyBytes.exchange(0, std::memory_order_relaxed);
    s.mapCopyMs = TicksToMs(gMapCopyTicks.exchange(0, std::memory_order_relaxed));
    s.tailDiscardFrames = gTailDiscardFrames.exchange(0, std::memory_order_relaxed);
    s.tailDiscards = gTailDiscards.exchange(0, std::memory_order_relaxed);
    s.mapsSkipped = gMapsSkipped.exchange(0, std::memory_order_relaxed);
    s.shadowCopies = gShadowCopies.exchange(0, std::memory_order_relaxed);
    std::lock_guard<std::mutex> const lock(gBlockerMutex);
    s.lastBlocker = gLastBlocker;
    return s;
}

std::string StatsJson() {
    return std::format("{{\"attached\": {}, \"replays\": {}, \"replayFailures\": {}}}", Attached() ? "true" : "false",
        gReplays.load(std::memory_order_relaxed), gFailures.load(std::memory_order_relaxed));
}

} // namespace mevr::host::draw_replay
