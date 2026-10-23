// SPDX-License-Identifier: MIT
#include "plugin/DrawReplay.hpp"

#include "core/Log.hpp"

#include <d3d11.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
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
    Unsupported,  // the slot in `n`: the tail can't be replayed
    kCount
};
char const* const kOpNames[] = {"SetConstantBuffers", "SetShaderResources", "SetSamplers", "SetShader",
    "IASetInputLayout", "IASetVertexBuffers", "IASetIndexBuffer", "IASetPrimitiveTopology", "DrawIndexed", "Draw",
    "DrawIndexedInstanced", "DrawInstanced", "DrawAuto", "Map", "Unmap", "UpdateSubresource", "RSSetState",
    "RSSetViewports", "RSSetScissorRects", "OMSetRenderTargets", "OMSetBlendState", "OMSetDepthStencilState",
    "ClearRenderTargetView", "ClearDepthStencilView", "CopyResource", "CopySubresourceRegion", "ResolveSubresource",
    "GenerateMips", "Begin", "End", "SetPredication", "Unsupported"};
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
#pragma pack(pop)

constexpr std::size_t kMaxRecordedUpdate = 1u << 20;  // a buffer update larger than this isn't copied (the tail then isn't replayable)
constexpr std::size_t kMaxRecordedMap = 64u << 10;    // a mapped buffer up to this size is copied whole at Unmap

struct Stream {
    std::vector<std::uint8_t> bytes;
    std::size_t tail = SIZE_MAX;  // byte offset of the first entry the replay re-issues
    unsigned entries = 0, tailEntries = 0, draws = 0, updates = 0, textureUpdates = 0, mapsCopied = 0, mapsSkipped = 0;
    unsigned unsupported = 0, unsupportedSlot = 0, bigUpdates = 0;
    std::unordered_map<void*, unsigned> bigDiscards;  // large dynamic buffers discarded in the tail, per buffer
    void Clear() {
        bytes.clear();
        tail = SIZE_MAX;
        entries = tailEntries = draws = updates = textureUpdates = mapsCopied = mapsSkipped = 0;
        unsupported = unsupportedSlot = bigUpdates = 0;
        bigDiscards.clear();
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

Stream gStreams[2];
thread_local Stream* t_recording = nullptr;
std::unordered_map<void*, BufferInfo> gBuffers;  // per frame (pointers can be reused after a release)
std::unordered_map<void*, Mapped> gMapped;       // the last Map of each resource this frame
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
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT)>(kDrawIndexed)(c, indexCount, startIndex, baseVertex);
}
void STDMETHODCALLTYPE DrawHook(ID3D11DeviceContext* c, UINT vertexCount, UINT start) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<DrawArgs>(*t_recording, Op::Draw);
        *a = {vertexCount, start};
        ++t_recording->draws;
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT)>(kDraw)(c, vertexCount, start);
}
void STDMETHODCALLTYPE DrawIndexedInstancedHook(ID3D11DeviceContext* c, UINT indexCountPerInstance, UINT instanceCount,
    UINT startIndex, INT baseVertex, UINT startInstance) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<DrawIndexedInstancedArgs>(*t_recording, Op::DrawIndexedInstanced);
        *a = {indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance};
        ++t_recording->draws;
    }
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
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT)>(kDrawInstanced)(
        c, vertexCountPerInstance, instanceCount, startVertex, startInstance);
}
void STDMETHODCALLTYPE DrawAutoHook(ID3D11DeviceContext* c) {
    if (t_recording != nullptr) {
        Reserve(*t_recording, Op::DrawAuto, 0, 0, 0);
        ++t_recording->draws;
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*)>(kDrawAuto)(c);
}

HRESULT STDMETHODCALLTYPE MapHook(ID3D11DeviceContext* c, ID3D11Resource* resource, UINT subresource, D3D11_MAP type,
    UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
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
            }
        }
        auto* const a = RecordStruct<UnmapArgs>(s, Op::Unmap, 0, 0, size);
        a->resource = resource;
        a->subresource = subresource;
        a->mapType = written ? found->second.type : 0;
        a->kind = (info.bindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0 ? 1u : 0u;
        a->dataSize = static_cast<std::uint32_t>(size);
        if (size != 0) {
            std::memcpy(a + 1, found->second.data, size);  // the mapped memory, before it goes away
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
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, FLOAT const*)>(kClearRenderTargetView)(c, view, colour);
}
void STDMETHODCALLTYPE ClearDepthStencilViewHook(ID3D11DeviceContext* c, ID3D11DepthStencilView* view, UINT flags, FLOAT depth,
    UINT8 stencil) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<ClearDepthArgs>(*t_recording, Op::ClearDepthStencilView);
        *a = {view, flags, depth, stencil};
        Hold(view);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8)>(kClearDepthStencilView)(
        c, view, flags, depth, stencil);
}
void STDMETHODCALLTYPE CopyResourceHook(ID3D11DeviceContext* c, ID3D11Resource* dst, ID3D11Resource* src) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<CopyArgs>(*t_recording, Op::CopyResource);
        *a = {dst, src};
        Hold(dst);
        Hold(src);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*)>(kCopyResource)(c, dst, src);
}
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
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, D3D11_BOX const*)>(
        kCopySubresourceRegion)(c, dst, dstSub, x, y, z, src, srcSub, box);
}
void STDMETHODCALLTYPE ResolveSubresourceHook(ID3D11DeviceContext* c, ID3D11Resource* dst, UINT dstSub, ID3D11Resource* src, UINT srcSub,
    DXGI_FORMAT format) {
    if (t_recording != nullptr) {
        auto* const a = RecordStruct<ResolveArgs>(*t_recording, Op::ResolveSubresource);
        *a = {dst, dstSub, src, srcSub, static_cast<std::uint32_t>(format)};
        Hold(dst);
        Hold(src);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT)>(
        kResolveSubresource)(c, dst, dstSub, src, srcSub, format);
}
void STDMETHODCALLTYPE GenerateMipsHook(ID3D11DeviceContext* c, ID3D11ShaderResourceView* view) {
    if (t_recording != nullptr) {
        void* p = view;
        RecordPointers(*t_recording, Op::GenerateMips, 0, 0, 1, &p);
    }
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11ShaderResourceView*)>(kGenerateMips)(c, view);
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
void STDMETHODCALLTYPE DrawIndexedInstancedIndirectHook(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kDrawIndexedInstancedIndirect);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT)>(kDrawIndexedInstancedIndirect)(c, args, offset);
}
void STDMETHODCALLTYPE DrawInstancedIndirectHook(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kDrawInstancedIndirect);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT)>(kDrawInstancedIndirect)(c, args, offset);
}
void STDMETHODCALLTYPE DispatchHook(ID3D11DeviceContext* c, UINT x, UINT y, UINT z) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kDispatch);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT)>(kDispatch)(c, x, y, z);
}
void STDMETHODCALLTYPE DispatchIndirectHook(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kDispatchIndirect);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT)>(kDispatchIndirect)(c, args, offset);
}
void STDMETHODCALLTYPE CopyStructureCountHook(ID3D11DeviceContext* c, ID3D11Buffer* dst, UINT offset, ID3D11UnorderedAccessView* src) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kCopyStructureCount);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT, ID3D11UnorderedAccessView*)>(kCopyStructureCount)(c, dst, offset, src);
}
void STDMETHODCALLTYPE ClearUnorderedAccessViewUintHook(ID3D11DeviceContext* c, ID3D11UnorderedAccessView* view, UINT const values[4]) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kClearUnorderedAccessViewUint);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11UnorderedAccessView*, UINT const*)>(kClearUnorderedAccessViewUint)(c, view, values);
}
void STDMETHODCALLTYPE ClearUnorderedAccessViewFloatHook(ID3D11DeviceContext* c, ID3D11UnorderedAccessView* view, FLOAT const values[4]) {
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kClearUnorderedAccessViewFloat);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11UnorderedAccessView*, FLOAT const*)>(kClearUnorderedAccessViewFloat)(c, view, values);
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
    if (t_recording != nullptr) RecordUnsupported(*t_recording, kCSSetUnorderedAccessViews);
    Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11UnorderedAccessView* const*, UINT const*)>(
        kCSSetUnorderedAccessViews)(c, start, count, views, initialCounts);
}

struct HookEntry {
    unsigned slot;
    void* function;
};
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
    {kClearRenderTargetView, reinterpret_cast<void*>(&ClearRenderTargetViewHook)},
    {kClearDepthStencilView, reinterpret_cast<void*>(&ClearDepthStencilViewHook)},
    {kCopyResource, reinterpret_cast<void*>(&CopyResourceHook)},
    {kCopySubresourceRegion, reinterpret_cast<void*>(&CopySubresourceRegionHook)},
    {kResolveSubresource, reinterpret_cast<void*>(&ResolveSubresourceHook)},
    {kGenerateMips, reinterpret_cast<void*>(&GenerateMipsHook)},
    {kOMSetRenderTargetsAndUnorderedAccessViews, reinterpret_cast<void*>(&OMSetRenderTargetsAndUnorderedAccessViewsHook)},
    {kSOSetTargets, reinterpret_cast<void*>(&SOSetTargetsHook)},
    {kDrawIndexedInstancedIndirect, reinterpret_cast<void*>(&DrawIndexedInstancedIndirectHook)},
    {kDrawInstancedIndirect, reinterpret_cast<void*>(&DrawInstancedIndirectHook)},
    {kDispatch, reinterpret_cast<void*>(&DispatchHook)},
    {kDispatchIndirect, reinterpret_cast<void*>(&DispatchIndirectHook)},
    {kCopyStructureCount, reinterpret_cast<void*>(&CopyStructureCountHook)},
    {kClearUnorderedAccessViewUint, reinterpret_cast<void*>(&ClearUnorderedAccessViewUintHook)},
    {kClearUnorderedAccessViewFloat, reinterpret_cast<void*>(&ClearUnorderedAccessViewFloatHook)},
    {kSetResourceMinLOD, reinterpret_cast<void*>(&SetResourceMinLODHook)},
    {kExecuteCommandList, reinterpret_cast<void*>(&ExecuteCommandListHook)},
    {kCSSetUnorderedAccessViews, reinterpret_cast<void*>(&CSSetUnorderedAccessViewsHook)},
};

bool InOurModule(void const* address) {
    HMODULE ours = nullptr, theirs = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&InOurModule), &ours);
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        static_cast<LPCWSTR>(address), &theirs);
    return ours != nullptr && ours == theirs;
}

// ---- The substitution: first-eye view bytes -> second-eye view bytes, by 16-byte chunk -------------------------

struct Substitution {
    struct Entry {
        std::uint8_t key[16];
        std::uint8_t value[16];
        bool used = false;
    };
    std::vector<Entry> entries;
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
                return;
            }
        }
        Entry e{};
        std::memcpy(e.key, key, 16);
        std::memcpy(e.value, value, 16);
        entries.push_back(e);
    }

    void Build(ViewFields const& first, ViewFields const& second) {
        entries.clear();
        if (first.bytes.size() != second.bytes.size() || first.perView == 0) {
            return;
        }
        std::size_t const n = first.bytes.size();
        std::uint8_t const* a = first.bytes.data();
        std::uint8_t const* b = second.bytes.data();
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
    // keeps the chunk's own 4th float.
    unsigned Apply(std::uint8_t* data, std::size_t size) {
        unsigned changed = 0;
        for (std::size_t o = 0; o + 16 <= size; o += 16) {
            Entry* const e = Find(data + o);
            if (e == nullptr) {
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
    for (auto const& [buffer, discards] : s.bigDiscards) {
        if (discards > 1) {
            if (why) *why = std::format("a dynamic buffer over {} KB discarded {} times (its earlier contents are gone)", kMaxRecordedMap >> 10, discards);
            return true;
        }
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
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, void* const*)>(slot)(c, p->start, h.n, p->ptrs);
        return true;
    }
    case Op::SetShader: {
        auto const* const p = reinterpret_cast<Pointers const*>(body);
        static unsigned const slots[kStages] = {kVSSetShader, kPSSetShader, kGSSetShader, kHSSetShader, kDSSetShader, kCSSetShader};
        if (h.stage >= kStages) return false;
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, void*, ID3D11ClassInstance* const*, UINT)>(slots[h.stage])(c, p->ptrs[0], nullptr, 0);
        return true;
    }
    case Op::IASetInputLayout:
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11InputLayout*)>(kIASetInputLayout)(
            c, static_cast<ID3D11InputLayout*>(reinterpret_cast<Pointers const*>(body)->ptrs[0]));
        return true;
    case Op::IASetVertexBuffers: {
        auto const* const a = reinterpret_cast<VertexBuffers const*>(body);
        auto const* const ptrs = reinterpret_cast<ID3D11Buffer* const*>(a + 1);
        auto const* const strides = reinterpret_cast<UINT const*>(ptrs + a->count);
        auto const* const offsets = strides + a->count;
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*, UINT const*, UINT const*)>(
            kIASetVertexBuffers)(c, a->start, a->count, ptrs, strides, offsets);
        return true;
    }
    case Op::IASetIndexBuffer: {
        auto const* const a = reinterpret_cast<IndexBuffer const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, DXGI_FORMAT, UINT)>(kIASetIndexBuffer)(
            c, static_cast<ID3D11Buffer*>(a->buffer), static_cast<DXGI_FORMAT>(a->format), a->offset);
        return true;
    }
    case Op::IASetPrimitiveTopology:
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, D3D11_PRIMITIVE_TOPOLOGY)>(kIASetPrimitiveTopology)(
            c, static_cast<D3D11_PRIMITIVE_TOPOLOGY>(*reinterpret_cast<std::uint32_t const*>(body)));
        return true;
    case Op::DrawIndexed: {
        auto const* const a = reinterpret_cast<DrawIndexedArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT)>(kDrawIndexed)(c, a->indexCount, a->startIndex, a->baseVertex);
        return true;
    }
    case Op::Draw: {
        auto const* const a = reinterpret_cast<DrawArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT)>(kDraw)(c, a->vertexCount, a->start);
        return true;
    }
    case Op::DrawIndexedInstanced: {
        auto const* const a = reinterpret_cast<DrawIndexedInstancedArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT)>(kDrawIndexedInstanced)(
            c, a->indexCountPerInstance, a->instanceCount, a->startIndex, a->baseVertex, a->startInstance);
        return true;
    }
    case Op::DrawInstanced: {
        auto const* const a = reinterpret_cast<DrawInstancedArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT)>(kDrawInstanced)(
            c, a->vertexCountPerInstance, a->instanceCount, a->startVertex, a->startInstance);
        return true;
    }
    case Op::DrawAuto:
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*)>(kDrawAuto)(c);
        return true;
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
        HRESULT const hr = Next<HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
            D3D11_MAPPED_SUBRESOURCE*)>(kMap)(c, resource, a->subresource, type, 0, &mapped);
        if (FAILED(hr) || mapped.pData == nullptr) {
            return false;
        }
        std::uint8_t const* data = reinterpret_cast<std::uint8_t const*>(a + 1);
        if (a->kind == 1) {
            gScratch.assign(data, data + a->dataSize);
            gSubstitutions.fetch_add(gSubstitution.Apply(gScratch.data(), gScratch.size()), std::memory_order_relaxed);
            data = gScratch.data();
        }
        std::memcpy(mapped.pData, data, a->dataSize);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT)>(kUnmap)(c, resource, a->subresource);
        return true;
    }
    case Op::UpdateSubresource: {
        auto const* const a = reinterpret_cast<UpdateArgs const*>(body);
        if (a->kind >= 2 || a->dataSize == 0) {
            skipped = true;  // a texture upload (one-time data), or nothing to write
            return true;
        }
        std::uint8_t const* data = reinterpret_cast<std::uint8_t const*>(a + 1);
        if (a->kind == 1) {
            gScratch.assign(data, data + a->dataSize);
            gSubstitutions.fetch_add(gSubstitution.Apply(gScratch.data(), gScratch.size()), std::memory_order_relaxed);
            data = gScratch.data();
        }
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_BOX const*, void const*, UINT, UINT)>(
            kUpdateSubresource)(c, static_cast<ID3D11Resource*>(a->resource), a->subresource, a->hasBox ? &a->box : nullptr, data,
            a->rowPitch, a->depthPitch);
        return true;
    }
    case Op::RSSetState:
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RasterizerState*)>(kRSSetState)(
            c, static_cast<ID3D11RasterizerState*>(reinterpret_cast<Pointers const*>(body)->ptrs[0]));
        return true;
    case Op::RSSetViewports:
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, D3D11_VIEWPORT const*)>(kRSSetViewports)(
            c, h.n, reinterpret_cast<D3D11_VIEWPORT const*>(body));
        return true;
    case Op::RSSetScissorRects:
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, D3D11_RECT const*)>(kRSSetScissorRects)(
            c, h.n, reinterpret_cast<D3D11_RECT const*>(body));
        return true;
    case Op::OMSetRenderTargets: {
        auto const* const p = reinterpret_cast<Pointers const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*)>(
            kOMSetRenderTargets)(c, h.n, reinterpret_cast<ID3D11RenderTargetView* const*>(p->ptrs),
            static_cast<ID3D11DepthStencilView*>(p->ptrs[h.n]));
        return true;
    }
    case Op::OMSetBlendState: {
        auto const* const a = reinterpret_cast<BlendArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11BlendState*, FLOAT const*, UINT)>(kOMSetBlendState)(
            c, static_cast<ID3D11BlendState*>(a->state), a->factor, a->mask);
        return true;
    }
    case Op::OMSetDepthStencilState: {
        auto const* const a = reinterpret_cast<DepthStencilArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilState*, UINT)>(kOMSetDepthStencilState)(
            c, static_cast<ID3D11DepthStencilState*>(a->state), a->reference);
        return true;
    }
    case Op::ClearRenderTargetView: {
        auto const* const a = reinterpret_cast<ClearTargetArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, FLOAT const*)>(kClearRenderTargetView)(
            c, static_cast<ID3D11RenderTargetView*>(a->view), a->colour);
        return true;
    }
    case Op::ClearDepthStencilView: {
        auto const* const a = reinterpret_cast<ClearDepthArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8)>(kClearDepthStencilView)(
            c, static_cast<ID3D11DepthStencilView*>(a->view), a->flags, a->depth, a->stencil);
        return true;
    }
    case Op::CopyResource: {
        auto const* const a = reinterpret_cast<CopyArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*)>(kCopyResource)(
            c, static_cast<ID3D11Resource*>(a->dst), static_cast<ID3D11Resource*>(a->src));
        return true;
    }
    case Op::CopySubresourceRegion: {
        auto const* const a = reinterpret_cast<CopyRegionArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, D3D11_BOX const*)>(
            kCopySubresourceRegion)(c, static_cast<ID3D11Resource*>(a->dst), a->dstSub, a->x, a->y, a->z,
            static_cast<ID3D11Resource*>(a->src), a->srcSub, a->hasBox ? &a->box : nullptr);
        return true;
    }
    case Op::ResolveSubresource: {
        auto const* const a = reinterpret_cast<ResolveArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT)>(
            kResolveSubresource)(c, static_cast<ID3D11Resource*>(a->dst), a->dstSub, static_cast<ID3D11Resource*>(a->src),
            a->srcSub, static_cast<DXGI_FORMAT>(a->format));
        return true;
    }
    case Op::GenerateMips:
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11ShaderResourceView*)>(kGenerateMips)(
            c, static_cast<ID3D11ShaderResourceView*>(reinterpret_cast<Pointers const*>(body)->ptrs[0]));
        return true;
    case Op::Begin:
    case Op::End:
        skipped = true;  // the first eye's queries keep their results; the second eye never reads any
        return true;
    case Op::SetPredication: {
        auto const* const a = reinterpret_cast<PredicationArgs const*>(body);
        Next<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Predicate*, BOOL)>(kSetPredication)(
            c, static_cast<ID3D11Predicate*>(a->predicate), a->value != 0 ? TRUE : FALSE);
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

bool Attach(ID3D11DeviceContext* context) {
    if (gAttached.load(std::memory_order_acquire)) {
        return true;
    }
    if (context == nullptr) {
        return false;
    }
    void** const vtable = *reinterpret_cast<void***>(context);
    // Halcyon's draw hooks (d3d_capture) must already sit in the vtable so the recorder sees the game's calls and
    // the replay runs through them. They go in at the first Present; we're called from a Present's end.
    if (!InOurModule(vtable[kDrawIndexed])) {
        if (gAttachLogged.exchange(1, std::memory_order_relaxed) == 0) {
            log::Info("draw replay: waiting for Halcyon's draw hooks before hooking the context");
        }
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kSlots * sizeof(void*), PAGE_READWRITE, &old)) {
        log::Warn("draw replay: VirtualProtect on the context's vtable failed ({}); off", GetLastError());
        return false;
    }
    for (unsigned i = 0; i < kSlots; ++i) {
        gNext[i] = vtable[i];
    }
    unsigned hooked = 0;
    for (HookEntry const& e : kHooks) {
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
    log::Info("draw replay: hooked {} entries of the immediate context's vtable, above Halcyon's", hooked);
    return true;
}

bool Attached() {
    return gAttached.load(std::memory_order_acquire);
}

void BeginRecording(int stream) {
    if (!Attached() || stream < 0 || stream > 1) {
        return;
    }
    Stream& s = gStreams[stream];
    s.Clear();
    t_recording = &s;
}

void EndRecording() {
    Stream* const s = t_recording;
    t_recording = nullptr;
    if (s == &gStreams[0]) {
        gRecorded.fetch_add(1, std::memory_order_relaxed);
        gRecordedCalls.fetch_add(s->entries, std::memory_order_relaxed);
        gRecordedBytes.fetch_add(s->bytes.size(), std::memory_order_relaxed);
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

int Replay(ViewFields const& first, ViewFields const& second) {
    if (!Attached()) {
        return 1;
    }
    Stream const& s = gStreams[0];
    if (s.tail == SIZE_MAX) {
        return 2;
    }
    gSubstitution.Build(first, second);
    Cursor cursor = TailOf(s);
    std::uint64_t issued = 0;
    while (cursor.Next()) {
        bool skipped = false;
        if (!Issue(gContext, *cursor.header, cursor.body, skipped)) {
            gFailures.fetch_add(1, std::memory_order_relaxed);
            log::Warn("draw replay: entry {} ({}) couldn't be re-issued; the replay is off for the session",
                issued, kOpNames[static_cast<unsigned>(cursor.header->op)]);
            return 3;
        }
        ++issued;
    }
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
    report += std::format("view fields: {} view(s) x {} bytes; substitution keys {} (16-byte chunks that differ, rows and columns)\n",
        first.views, first.perView, gSubstitution.entries.size());
    for (Substitution::Entry const& e : gSubstitution.entries) {
        report += std::format("  key ({}) -> ({})\n", Floats(e.key, 4), Floats(e.value, 4));
    }

    // Walk both in step.
    Cursor ca = TailOf(a);
    Cursor cb{b.bytes.data(), b.bytes.data() + b.bytes.size()};
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
            for (std::size_t o = 0; o + 16 <= payloadSize; o += 16) {
                if (std::memcmp(payloadA + o, payloadB + o, 16) == 0) {
                    continue;
                }
                differs = true;
                ++chunksDiffering;
                std::uint8_t chunk[16];
                std::memcpy(chunk, payloadA + o, 16);
                gSubstitution.Apply(chunk, 16);
                if (std::memcmp(chunk, payloadB + o, 16) == 0) {
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
    report += std::format("substitution keys used {} of {}\n", keysUsed, gSubstitution.entries.size());
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
        path.string());
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
            std::vector<std::uint8_t>().swap(s.bytes);  // an unusual frame: give the memory back
            s.bytes.reserve(8u << 20);
        }
        s.Clear();
    }
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
    std::lock_guard<std::mutex> const lock(gBlockerMutex);
    s.lastBlocker = gLastBlocker;
    return s;
}

std::string StatsJson() {
    return std::format("{{\"attached\": {}, \"replays\": {}, \"replayFailures\": {}}}", Attached() ? "true" : "false",
        gReplays.load(std::memory_order_relaxed), gFailures.load(std::memory_order_relaxed));
}

} // namespace mevr::host::draw_replay
