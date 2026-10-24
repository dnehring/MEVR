// SPDX-License-Identifier: MIT
#include "plugin/CrashBreadcrumbs.hpp"

#include "core/FixedText.hpp"
#include "core/Log.hpp"
#include "engine/GameAdapter.hpp"
#include "ue3/SafeNames.hpp"

#include <Windows.h>

#include <cstddef>
#include <cstring>
#include <utility>

namespace mevr::host::crash {

breadcrumbs::DispatchRing<kDispatches> gProcessEvents;
breadcrumbs::DispatchRing<kDispatches> gScriptCalls;
breadcrumbs::WriteRing<kWrites> gWrites;

namespace {

// ---- first-chance exceptions with fatal codes (most are handled: probes, the game's own guards) ------------

struct FirstChance {
    std::uint64_t stamp = 0;
    std::uintptr_t address = 0;  // where it happened
    std::uintptr_t target = 0;   // an access violation's address
    std::uint32_t code = 0;
    std::uint32_t thread = 0;
    std::uint32_t operation = 0;  // an access violation's: 0 read, 1 write, 8 execute
    std::uint32_t seq = 0;
};

class FirstChanceRing : public breadcrumbs::Ring<FirstChance, 16> {
public:
    void Record(EXCEPTION_RECORD const& r) {
        std::uint64_t const index = Claim();
        FirstChance& e = At(index);
        e.stamp = breadcrumbs::Stamp();
        e.address = reinterpret_cast<std::uintptr_t>(r.ExceptionAddress);
        bool const av = r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r.NumberParameters >= 2;
        e.target = av ? static_cast<std::uintptr_t>(r.ExceptionInformation[1]) : 0;
        e.operation = av ? static_cast<std::uint32_t>(r.ExceptionInformation[0]) : 0;
        e.code = r.ExceptionCode;
        e.thread = breadcrumbs::ThreadId();
        Publish(index, breadcrumbs::State::Done);
    }
};
FirstChanceRing gFirstChances;

std::atomic<bool> gInstalled{false};
std::atomic<bool> gReported{false};
std::atomic<bool> gReporting{false};
LPTOP_LEVEL_EXCEPTION_FILTER gPreviousFilter = nullptr;

// The timestamp rate: Stamp() and QueryPerformanceCounter at install, again at the crash.
std::uint64_t gInstallStamp = 0;
LARGE_INTEGER gInstallQpc{};
LARGE_INTEGER gQpcFrequency{};

wchar_t gReportPath[MAX_PATH] = {};
wchar_t gPreviousReportPath[MAX_PATH] = {};

// What name resolution needs (LE1); `named` false elsewhere: pointers only.
bool gNamed = false;
ue3::safe::Memory gMemory;
ue3::safe::FrameLayout gFrameLayout;
std::uintptr_t gExeBase = 0;

// The report: static, so writing it allocates nothing.
constexpr std::size_t kReportBytes = 512 * 1024;
char gReport[kReportBytes];

bool IsFatal(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
    case 0xC0000374u:  // STATUS_HEAP_CORRUPTION
    case 0xC0000409u:  // STATUS_STACK_BUFFER_OVERRUN (fast fail)
        return true;
    default:
        return false;
    }
}

char const* CodeName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "datatype misalignment";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "noncontinuable exception";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    case 0xC0000374u: return "heap corruption";
    case 0xC0000409u: return "stack buffer overrun (fast fail)";
    case 0xE06D7363u: return "C++ exception";
    default: return "exception";
    }
}

// The first-chance recorder: logs nothing, decides nothing (CONTINUE_SEARCH), last in the handler list.
LONG CALLBACK FirstChanceHandler(EXCEPTION_POINTERS* info) {
    if (info != nullptr && info->ExceptionRecord != nullptr && IsFatal(info->ExceptionRecord->ExceptionCode) &&
        !gReporting.load(std::memory_order_relaxed)) {
        gFirstChances.Record(*info->ExceptionRecord);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---- guarded reads ----------------------------------------------------------------------------------------

constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
    PAGE_EXECUTE_WRITECOPY;

// The last region found readable, per thread (the report, and names for the log on the game thread).
thread_local std::uintptr_t gRegionLo = 0;
thread_local std::uintptr_t gRegionHi = 0;

bool Readable(std::uintptr_t address, std::size_t size) {
    std::uintptr_t const end = address + size;
    if (address >= gRegionLo && end <= gRegionHi) {
        return true;
    }
    std::uintptr_t at = address;
    while (at < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void const*>(at), &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 || (mbi.Protect & kReadable) == 0) {
            return false;
        }
        std::uintptr_t const lo = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        std::uintptr_t const hi = lo + mbi.RegionSize;
        if (at == address) {
            gRegionLo = lo;
            gRegionHi = hi;
        }
        at = hi;
    }
    return true;
}

bool CopyGuarded(void* out, void const* address, std::size_t size) {
    __try {
        std::memcpy(out, address, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeRead(void const* address, void* out, std::size_t size) {
    auto const a = reinterpret_cast<std::uintptr_t>(address);
    if (a < 0x10000 || a + size < a || a + size > 0x00007FFFFFFF0000ull) {
        return false;
    }
    return Readable(a, size) && CopyGuarded(out, address, size);
}

// ---- formatting -------------------------------------------------------------------------------------------

double gTicksPerMicro = 0.0;

void Calibrate(std::uint64_t nowStamp) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    double const micros = gQpcFrequency.QuadPart > 0
        ? static_cast<double>(now.QuadPart - gInstallQpc.QuadPart) * 1e6 / static_cast<double>(gQpcFrequency.QuadPart)
        : 0.0;
    gTicksPerMicro = micros > 1000.0 ? static_cast<double>(nowStamp - gInstallStamp) / micros : 0.0;
}

// "-12.345 ms" before `now` (or the raw tick difference when the rate is unknown).
void Age(text::Appender& out, std::uint64_t stamp, std::uint64_t now) {
    std::int64_t const ticks = static_cast<std::int64_t>(stamp - now);
    if (gTicksPerMicro > 0.0) {
        out.Milli(static_cast<std::int64_t>(static_cast<double>(ticks) / gTicksPerMicro)).Str(" ms");
    } else {
        out.Dec(ticks).Str(" ticks");
    }
}

wchar_t gModulePath[MAX_PATH];

// "module.dll+0x1234", or the bare address.
void Where(text::Appender& out, std::uintptr_t address) {
    HMODULE module = nullptr;
    if (address != 0 &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module) &&
        module != nullptr) {
        DWORD const n = GetModuleFileNameW(module, gModulePath, MAX_PATH);
        wchar_t const* name = gModulePath;
        for (DWORD i = 0; i < n; ++i) {
            if (gModulePath[i] == L'\\' || gModulePath[i] == L'/') {
                name = gModulePath + i + 1;
            }
        }
        out.Wide(n > 0 ? name : L"?").Char('+').Hex(address - reinterpret_cast<std::uintptr_t>(module));
        return;
    }
    out.Hex(address);
}

// An object as "Class Outer.Name [0x...]" (pointers only off LE1), flagged when it isn't a live object.
void Object(text::Appender& out, void const* object) {
    auto const p = reinterpret_cast<std::uintptr_t>(object);
    if (p == 0) {
        out.Str("None");
        return;
    }
    if (gNamed) {
        ue3::safe::AppendFull(gMemory, p, out);
        if (!ue3::safe::Listed(gMemory, p)) {
            out.Str(" (not in GObjects)");
        }
        out.Char(' ');
    }
    out.Char('[').Ptr(object).Char(']');
}

void Thread(text::Appender& out, std::uint32_t thread) {
    out.Str("tid ").UDec(thread);
    if (thread != 0 && thread == gGameThread.load(std::memory_order_relaxed)) {
        out.Str(" (game)");
    }
}

char const* StateName(breadcrumbs::State state) {
    switch (state) {
    case breadcrumbs::State::Engine: return "IN ENGINE";
    case breadcrumbs::State::Mod: return "IN MOD   ";
    case breadcrumbs::State::Done: return "returned ";
    }
    return "?        ";
}

template <std::size_t N>
void Dispatches(text::Appender& out, breadcrumbs::DispatchRing<N> const& ring, std::uint64_t now) {
    std::uint64_t const count = ring.Count();
    for (std::uint64_t i = ring.First(); i < count; ++i) {
        breadcrumbs::Dispatch const e = ring.At(i);
        out.Str("  ");
        if (!ring.Holds(i)) {
            out.Str("#").UDec(i).Str(" (overwritten while being read)\r\n");
            continue;
        }
        std::size_t const start = out.Size();
        Age(out, e.stamp, now);
        out.PadTo(start, 14);
        out.Str(StateName(ring.StateOf(i))).Char(' ');
        std::size_t const t = out.Size();
        Thread(out, e.thread);
        out.PadTo(t, 18);
        Object(out, e.function);
        out.Str("  on  ");
        Object(out, e.object);
        out.Str("\r\n");
    }
}

void Writes(text::Appender& out, std::uint64_t now) {
    // Coalesced entries keep their place in the ring but carry the latest time: sort by it.
    std::uint64_t order[kWrites];
    std::size_t n = 0;
    std::uint64_t const count = gWrites.Count();
    for (std::uint64_t i = gWrites.First(); i < count; ++i) {
        order[n++] = i;
    }
    for (std::size_t a = 1; a < n; ++a) {
        std::uint64_t const index = order[a];
        std::uint64_t const last = gWrites.At(index).last;
        std::size_t b = a;
        for (; b > 0 && static_cast<std::int64_t>(gWrites.At(order[b - 1]).last - last) > 0; --b) {
            order[b] = order[b - 1];
        }
        order[b] = index;
    }
    for (std::size_t k = 0; k < n; ++k) {
        std::uint64_t const i = order[k];
        breadcrumbs::Write const e = gWrites.At(i);
        out.Str("  ");
        if (!gWrites.Holds(i)) {
            out.Str("#").UDec(i).Str(" (overwritten while being read)\r\n");
            continue;
        }
        std::size_t const start = out.Size();
        Age(out, e.last, now);
        out.PadTo(start, 14);
        std::size_t const t = out.Size();
        Thread(out, e.thread);
        out.PadTo(t, 18);
        std::size_t const g = out.Size();
        out.Str(e.tag);
        out.PadTo(g, 28);
        out.Str(" at ").Ptr(e.address).Str(", ").UDec(e.size).Str(" bytes, first 8: ").Hex(e.value, 16);
        if (e.count > 1) {
            out.Str("  (x").UDec(e.count).Str(" since ");
            Age(out, e.first, now);
            out.Char(')');
        }
        out.Str("\r\n");
    }
}

// One step up the faulting thread's native stack; false at its end.
bool Unwind(CONTEXT& context) {
    __try {
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION const function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (function == nullptr) {
            // A leaf: the return address is on top of the stack.
            DWORD64 ret = 0;
            if (!SafeRead(reinterpret_cast<void const*>(context.Rsp), &ret, sizeof(ret))) {
                return false;
            }
            context.Rip = ret;
            context.Rsp += 8;
        } else {
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
        }
        return context.Rip != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void NativeStack(text::Appender& out, CONTEXT const& faulting) {
    static CONTEXT context;
    context = faulting;
    for (int i = 0; i < 40; ++i) {
        out.Str("  [").UDec(static_cast<std::uint64_t>(i)).Str("] ");
        Where(out, static_cast<std::uintptr_t>(context.Rip));
        out.Str("  (rsp ").Hex(context.Rsp).Str(")\r\n");
        if (!Unwind(context)) {
            break;
        }
    }
}

void ScriptFrames(text::Appender& out, std::uintptr_t rsp) {
    if (!gNamed) {
        out.Str("  (no reflection layout for this game: script frames not read)\r\n");
        return;
    }
    auto const* const tib = reinterpret_cast<NT_TIB const*>(NtCurrentTeb());
    std::uintptr_t const top = reinterpret_cast<std::uintptr_t>(tib->StackBase);
    std::uintptr_t const lo = rsp;
    std::uintptr_t const hi = top > lo && top - lo <= 8u * 1024 * 1024 ? top : lo + 1024 * 1024;
    static ue3::safe::Frame frames[64];
    std::size_t const n = ue3::safe::FindFrames(gMemory, gFrameLayout, lo, hi, frames, 64);
    if (n == 0) {
        out.Str("  (none found on this thread's stack)\r\n");
    }
    for (std::size_t i = 0; i < n; ++i) {
        ue3::safe::Frame const& f = frames[i];
        out.Str("  #").UDec(i).Str(" frame ").Ptr(reinterpret_cast<void const*>(f.at)).Str("  ");
        Object(out, reinterpret_cast<void const*>(f.node));
        out.Str("\r\n       on ");
        Object(out, reinterpret_cast<void const*>(f.object));
        out.Str("\r\n       code ").Ptr(reinterpret_cast<void const*>(f.code));
        if (f.codeOffset >= 0) {
            out.Str(" = script +").Hex(static_cast<std::uint64_t>(f.codeOffset)).Str(" of ").Hex(static_cast<std::uint32_t>(f.scriptSize));
        } else if (f.scriptSize >= 0) {
            out.Str(" (outside its script of ").Hex(static_cast<std::uint32_t>(f.scriptSize)).Str(" bytes)");
        }
        out.Str(", previous frame ").Ptr(reinterpret_cast<void const*>(f.previous)).Str("\r\n");
    }
}

void FirstChances(text::Appender& out, std::uint64_t now) {
    std::uint64_t const count = gFirstChances.Count();
    if (count == 0) {
        out.Str("  (none)\r\n");
    }
    for (std::uint64_t i = gFirstChances.First(); i < count; ++i) {
        FirstChance const e = gFirstChances.At(i);
        if (!gFirstChances.Holds(i)) {
            continue;
        }
        out.Str("  ");
        std::size_t const start = out.Size();
        Age(out, e.stamp, now);
        out.PadTo(start, 14);
        Thread(out, e.thread);
        out.Str("  ").Hex(e.code, 8).Char(' ').Str(CodeName(e.code)).Str(" at ");
        Where(out, e.address);
        if (e.code == EXCEPTION_ACCESS_VIOLATION) {
            out.Str(e.operation == 1 ? " writing " : e.operation == 8 ? " executing " : " reading ").Hex(e.target);
        }
        out.Str("\r\n");
    }
    if (count > 16) {
        out.Str("  (").UDec(count).Str(" fatal-code first-chance exceptions in this process, most handled)\r\n");
    }
}

void Registers(text::Appender& out, CONTEXT const& c) {
    struct Reg {
        char const* name;
        DWORD64 value;
    };
    Reg const regs[] = {{"rip", c.Rip}, {"rsp", c.Rsp}, {"rbp", c.Rbp}, {"rax", c.Rax}, {"rbx", c.Rbx}, {"rcx", c.Rcx},
        {"rdx", c.Rdx}, {"rsi", c.Rsi}, {"rdi", c.Rdi}, {"r8", c.R8}, {"r9", c.R9}, {"r10", c.R10}, {"r11", c.R11},
        {"r12", c.R12}, {"r13", c.R13}, {"r14", c.R14}, {"r15", c.R15}};
    int column = 0;
    for (Reg const& r : regs) {
        out.Str("  ").Str(r.name).Char('=').Hex(r.value, 16);
        if (++column == 4) {
            out.Str("\r\n");
            column = 0;
        }
    }
    if (column != 0) {
        out.Str("\r\n");
    }
}

void Build(text::Appender& out, EXCEPTION_POINTERS const& info) {
    std::uint64_t const now = breadcrumbs::Stamp();
    Calibrate(now);
    EXCEPTION_RECORD const& r = *info.ExceptionRecord;
    SYSTEMTIME t{};
    GetLocalTime(&t);
    out.Str("==== MEVR crash breadcrumbs (research/36) ====\r\n");
    out.UDec(t.wYear).Char('-').UDec(t.wMonth / 10).UDec(t.wMonth % 10).Char('-').UDec(t.wDay / 10).UDec(t.wDay % 10).Char(' ');
    out.UDec(t.wHour / 10).UDec(t.wHour % 10).Char(':').UDec(t.wMinute / 10).UDec(t.wMinute % 10).Char(':');
    out.UDec(t.wSecond / 10).UDec(t.wSecond % 10).Char('.').UDec(t.wMilliseconds / 100).UDec(t.wMilliseconds / 10 % 10)
        .UDec(t.wMilliseconds % 10);
    out.Str("  pid ").UDec(GetCurrentProcessId()).Str("  exe base ").Hex(gExeBase).Str("  host build " __DATE__ " " __TIME__ "\r\n");
    out.Str("exception ").Hex(r.ExceptionCode, 8).Str(" (").Str(CodeName(r.ExceptionCode)).Str(") at ");
    Where(out, reinterpret_cast<std::uintptr_t>(r.ExceptionAddress));
    if (r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r.NumberParameters >= 2) {
        out.Str(r.ExceptionInformation[0] == 1 ? ", writing " : r.ExceptionInformation[0] == 8 ? ", executing " : ", reading ")
            .Hex(r.ExceptionInformation[1]);
    }
    out.Str("\r\nfaulting thread: ");
    Thread(out, breadcrumbs::ThreadId());
    out.Str("; the game thread is ").UDec(gGameThread.load(std::memory_order_relaxed));
    out.Str("\r\ntimes below are before this report (timestamp rate ");
    out.Milli(static_cast<std::int64_t>(gTicksPerMicro * 1000.0)).Str(" ticks/us)\r\n\r\n");

    out.Str("registers:\r\n");
    if (info.ContextRecord != nullptr) {
        Registers(out, *info.ContextRecord);
        out.Str("\r\nnative stack (unwound from the fault):\r\n");
        NativeStack(out, *info.ContextRecord);
        out.Str("\r\nscript frames (FFrames on this thread's stack, innermost first):\r\n");
        ScriptFrames(out, static_cast<std::uintptr_t>(info.ContextRecord->Rsp));
    }
    out.Str("\r\nfatal-code first-chance exceptions before it (most were handled):\r\n");
    FirstChances(out, now);

    out.Str("\r\nProcessEvent: the last ").UDec(kDispatches).Str(" dispatches, oldest first (")
        .UDec(gProcessEvents.Count()).Str(" in all; IN ENGINE: no return seen, IN MOD: in the mod's handler):\r\n");
    Dispatches(out, gProcessEvents, now);
    out.Str("\r\nthe mod's CallFunction handlers: the last ").UDec(kDispatches).Str(", oldest first (").UDec(gScriptCalls.Count())
        .Str(" in all):\r\n");
    Dispatches(out, gScriptCalls, now);
    out.Str("\r\nthe mod's writes into engine memory, by latest write (").UDec(gWrites.Count())
        .Str(" entries in all; repeats of a recent tag+address folded):\r\n");
    Writes(out, now);
    if (out.Truncated()) {
        out.Rewind(out.Size() > 64 ? out.Size() - 64 : 0);
        out.Str("\r\n(report cut: its buffer is full)\r\n");
    }
    out.Str("==== end of crash breadcrumbs ====\r\n");
}

void WriteFileText(wchar_t const* path, text::Appender const& report) {
    if (path[0] == L'\0') {
        return;
    }
    MoveFileExW(path, gPreviousReportPath, MOVEFILE_REPLACE_EXISTING);
    HANDLE const file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, report.Data(), static_cast<DWORD>(report.Size()), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
}

void Report(EXCEPTION_POINTERS* info) {
    gReporting.store(true);
    text::Appender out(gReport, sizeof(gReport));
    Build(out, *info);
    WriteFileText(gReportPath, out);
    log::WriteUnformatted("error", "crash breadcrumbs (also in %LOCALAPPDATA%\\MEVR\\crash-breadcrumbs.txt):", out.View());
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info) {
    // Once per process; not on a stack overflow (no stack left to format on).
    if (info != nullptr && info->ExceptionRecord != nullptr && info->ExceptionRecord->ExceptionCode != EXCEPTION_STACK_OVERFLOW &&
        !gReported.exchange(true)) {
        __try {
            Report(info);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // A fault in the report itself: let the previous filter (Halcyon's [CRASH] line, WER's dump) still run.
        }
    }
    return gPreviousFilter != nullptr ? gPreviousFilter(info) : EXCEPTION_CONTINUE_SEARCH;
}

std::pair<std::uintptr_t, std::uintptr_t> ReadOnlyData(std::uintptr_t base) {
    auto const* const dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    auto const* const nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
    IMAGE_SECTION_HEADER const* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".rdata", 6) == 0) {
            std::uintptr_t const lo = base + section->VirtualAddress;
            return {lo, lo + section->Misc.VirtualSize};
        }
    }
    return {0, 0};
}

void Paths() {
    wchar_t dir[MAX_PATH] = {};
    DWORD const n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 40) {
        return;
    }
    wcscat_s(dir, L"\\MEVR");
    CreateDirectoryW(dir, nullptr);
    wcscpy_s(gReportPath, dir);
    wcscat_s(gReportPath, L"\\crash-breadcrumbs.txt");
    wcscpy_s(gPreviousReportPath, dir);
    wcscat_s(gPreviousReportPath, L"\\crash-breadcrumbs.prev.txt");
}

} // namespace

void Install() {
    if (gInstalled.exchange(true)) {
        return;
    }
    Paths();
    gExeBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto const layout = engine::adapter::ReflectionLayout();
    auto const globals = engine::adapter::ReflectionGlobalsForGame();
    auto const [rdataLo, rdataHi] = ReadOnlyData(gExeBase);
    if (layout && globals && rdataLo != 0) {
        gMemory.layout = *layout;
        gMemory.poolArray = gExeBase + globals->namePoolsRva;
        gMemory.poolCount = globals->namePoolCount;
        gMemory.objects = gExeBase + globals->objectsRva;
        gMemory.read = &SafeRead;
        gFrameLayout.node = static_cast<std::uint32_t>(offsetof(FFrame, Node));
        gFrameLayout.object = static_cast<std::uint32_t>(offsetof(FFrame, Object));
        gFrameLayout.code = static_cast<std::uint32_t>(offsetof(FFrame, Code));
        gFrameLayout.previous = static_cast<std::uint32_t>(offsetof(FFrame, PreviousFrame));
        gFrameLayout.structScript = static_cast<std::uint32_t>(offsetof(UStruct, Script));  // the SDK's [verify]
        gFrameLayout.vtableLo = rdataLo;
        gFrameLayout.vtableHi = rdataHi;
        gNamed = true;
    }
    QueryPerformanceFrequency(&gQpcFrequency);
    QueryPerformanceCounter(&gInstallQpc);
    gInstallStamp = breadcrumbs::Stamp();
    AddVectoredExceptionHandler(0, &FirstChanceHandler);  // last in line: it only records
    gPreviousFilter = SetUnhandledExceptionFilter(&UnhandledFilter);
    log::Info("crash breadcrumbs: on (the last {} ProcessEvents, {} script handler calls, {} mod writes; names {}); written to {} "
              "and this log if the process crashes; previous unhandled-exception filter {}",
        kDispatches, kDispatches, kWrites, gNamed ? "resolved at the crash" : "not resolved (pointers only)",
        gReportPath[0] != L'\0' ? "%LOCALAPPDATA%\\MEVR\\crash-breadcrumbs.txt" : "(no path)",
        reinterpret_cast<void const*>(gPreviousFilter));
}

void AppendObject(text::Appender& out, void const* object) {
    Object(out, object);
}

void RecordWrite(char const* tag, void const* address, std::uint32_t size, std::uint64_t value) {
    gWrites.Record(tag, address, size, value);
}

std::uint64_t BeginScriptCall(ue3::Function* function, ue3::Object* object) {
    return gScriptCalls.Begin(function, object, breadcrumbs::State::Mod);
}

void EndScriptCall(std::uint64_t token) {
    gScriptCalls.Advance(token, breadcrumbs::State::Done);
}

} // namespace mevr::host::crash
