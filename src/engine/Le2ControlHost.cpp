// SPDX-License-Identifier: MIT
#include "engine/Le2ControlHost.hpp"

#include "core/Json.hpp"
#include "core/qa/ControlProtocol.hpp"
#if !defined(MEVR_LE2_CONTROL_TEST)
#include "engine/Le2DiagnosticHost.hpp"
#include "engine/Le2DiagnosticObservation.hpp"
#include "plugin/ProcessEventHook.hpp"
#include "core/Log.hpp"
#endif

#include <Windows.h>
#include <ShlObj.h>
#include <bcrypt.h>

#include <array>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace mevr::engine::le2_control {
namespace {
namespace fs = std::filesystem;
namespace wire = qa::control;
using json::Value;
constexpr std::size_t kContextLimit = 16 * 1024 * 1024;

void Check(bool ok, char const* why) { if (!ok) throw std::runtime_error(why); }
std::string Utf8(fs::path const& path) {
    auto text = path.u8string();
    return {reinterpret_cast<char const*>(text.data()), text.size()};
}
fs::path Path(std::string const& text) {
    Check(!text.empty() && text.find('\0') == std::string::npos, "empty/NUL path");
    return fs::path(std::u8string_view(reinterpret_cast<char8_t const*>(text.data()), text.size()));
}
bool Equal(fs::path const& a, fs::path const& b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
fs::path Safe(fs::path const& input) {
    auto root = input.root_name().native();
    Check(input.is_absolute() && root.size() == 2 && root[1] == L':' &&
          ((root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z')),
          "only absolute local drive paths are supported");
    for (auto const& part : input.relative_path()) {
        auto text = part.native();
        Check(!text.empty() && text != L"." && text != L".." && text.back() != L'.' &&
              text.back() != L' ' && text.find(L':') == std::wstring::npos, "ambiguous path component");
    }
    auto path = input.lexically_normal();
    for (auto entry = path;; entry = entry.parent_path()) {
        auto const attributes = GetFileAttributesW(entry.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            auto const error = GetLastError();
            Check(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND, "path inspection failed");
        } else Check((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0, "reparse path refused");
        if (entry == entry.root_path()) break;
    }
    return path;
}
bool Exists(fs::path const& path) { return fs::exists(Safe(path)); }
struct File {
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
void OpenRead(File& file, fs::path const& path) {
    auto safe = Safe(path);
    file.handle = CreateFileW(safe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    Check(file.handle != INVALID_HANDLE_VALUE, "cannot open input exclusively against writers");
    BY_HANDLE_FILE_INFORMATION info{};
    Check(GetFileInformationByHandle(file.handle, &info) != FALSE &&
          (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0,
          "input is not an ordinary file");
}
std::string Read(fs::path const& path, std::size_t limit) {
    File file;
    OpenRead(file, path);
    LARGE_INTEGER size{};
    Check(GetFileSizeEx(file.handle, &size) != FALSE && size.QuadPart >= 0 &&
          static_cast<std::uint64_t>(size.QuadPart) <= limit, "input exceeds bounded size");
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD got{};
    Check(ReadFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) != FALSE &&
          got == bytes.size(), "short file read");
    return bytes;
}
Value Json(fs::path const& path, std::size_t limit = kContextLimit) {
    auto value = json::Parse(Read(path, limit));
    Check(value && value->IsObject(), "invalid JSON object");
    return std::move(*value);
}
std::string Hash(fs::path const& path) {
    File file;
    OpenRead(file, path);
    LARGE_INTEGER size{};
    Check(GetFileSizeEx(file.handle, &size) != FALSE && size.QuadPart >= 0 &&
          size.QuadPart <= 512ll * 1024 * 1024, "hash input exceeds 512 MiB limit");
    struct Algorithm {
        BCRYPT_ALG_HANDLE handle{};
        ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
    } algorithm;
    Check(BCryptOpenAlgorithmProvider(&algorithm.handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "SHA256 unavailable");
    DWORD length{}, got{};
    Check(BCryptGetProperty(algorithm.handle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length),
                           &got, 0) >= 0 && got == sizeof(length), "SHA256 object query failed");
    std::vector<unsigned char> object(length);
    struct HashState {
        BCRYPT_HASH_HANDLE handle{};
        ~HashState() { if (handle) BCryptDestroyHash(handle); }
    } state;
    Check(BCryptCreateHash(algorithm.handle, &state.handle, object.data(), length, nullptr, 0, 0) >= 0, "SHA256 create failed");
    std::array<unsigned char, 65536> buffer{};
    std::uint64_t count{};
    for (;;) {
        Check(ReadFile(file.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) != FALSE, "hash read failed");
        if (!got) break;
        count += got;
        Check(count <= static_cast<std::uint64_t>(size.QuadPart) &&
              BCryptHashData(state.handle, buffer.data(), got, 0) >= 0, "hash data changed or failed");
    }
    Check(count == static_cast<std::uint64_t>(size.QuadPart), "hash input size changed");
    std::array<unsigned char, 32> digest{};
    Check(BCryptFinishHash(state.handle, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0, "SHA256 finish failed");
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (auto byte : digest) { out += digits[byte >> 4]; out += digits[byte & 15]; }
    return out;
}
void Publish(fs::path const& path, std::string const& bytes) {
    auto target = Safe(path);
    auto temporary = Safe(fs::path(target.native() + L".tmp"));
    Check(!Exists(target), "output already exists; recovery required");
    {
        File file;
        file.handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        Check(file.handle != INVALID_HANDLE_VALUE, "output temporary already exists or cannot be created");
        DWORD wrote{};
        Check(bytes.size() <= wire::kMaxFrame && WriteFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote,
              nullptr) != FALSE && wrote == bytes.size() && FlushFileBuffers(file.handle) != FALSE, "output write failed");
    }
    // No REPLACE_EXISTING: another producer's output is never clobbered.
    Check(MoveFileExW(temporary.c_str(), Safe(target).c_str(), MOVEFILE_WRITE_THROUGH) != FALSE, "output publication failed");
}
std::uint64_t Number(std::string const& text) {
    std::uint64_t value{};
    auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    Check(!text.empty() && (text.size() == 1 || text.front() != '0') && error == std::errc{} &&
          end == text.data() + text.size(), "invalid canonical unsigned number");
    return value;
}
std::uint64_t Filetime(FILETIME value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
std::uint64_t UtcMs() {
    FILETIME utc{};
    GetSystemTimeAsFileTime(&utc);
    return (Filetime(utc) - 116444736000000000ull) / 10000;
}
std::uint64_t StartTime(std::string const& text) {
    Check(text.size() >= 20 && text[4] == '-' && text[7] == '-' && text[10] == 'T' &&
          text[13] == ':' && text[16] == ':' && text.back() == 'Z', "process start requires exact UTC ISO time");
    auto field = [&](std::size_t at, std::size_t count) {
        unsigned n{};
        for (std::size_t i = at; i < at + count; ++i) {
            Check(text[i] >= '0' && text[i] <= '9', "invalid process timestamp");
            n = n * 10 + static_cast<unsigned>(text[i] - '0');
        }
        return static_cast<WORD>(n);
    };
    SYSTEMTIME system{};
    system.wYear = field(0, 4); system.wMonth = field(5, 2); system.wDay = field(8, 2);
    system.wHour = field(11, 2); system.wMinute = field(14, 2); system.wSecond = field(17, 2);
    std::uint64_t fraction{};
    if (text.size() != 20) {
        Check(text[19] == '.' && text.size() >= 22 && text.size() <= 28, "invalid timestamp precision");
        auto decimals = text.substr(20, text.size() - 21);
        for (char c : decimals) { Check(c >= '0' && c <= '9', "invalid timestamp fraction"); fraction = fraction * 10 + c - '0'; }
        for (auto i = decimals.size(); i < 7; ++i) fraction *= 10;
    }
    FILETIME file{};
    Check(SystemTimeToFileTime(&system, &file) != FALSE, "invalid process date");
    return Filetime(file) + fraction;
}
Value Identity(wire::Identity const& id) {
    auto value = Value::Object();
    value["run"] = id.run; value["lease"] = id.lease; value["pid"] = std::to_string(id.pid);
    value["start"] = std::to_string(id.start); value["exe"] = id.exe; value["host"] = id.host;
    value["mod"] = id.mod; value["game"] = "LE2";
    return value;
}
wire::Identity Identity(Value const& value) {
    Check(value.IsObject() && value.Members().size() == 8 && value.String("game") == "LE2", "invalid invitation identity");
    for (auto const& [key, entry] : value.Members()) { (void)key; Check(entry.IsString(), "identity values must be strings"); }
    auto const pid = Number(value.String("pid"));
    Check(pid <= std::numeric_limits<std::uint32_t>::max(), "PID out of range");
    wire::Identity id{value.String("run"), value.String("lease"), static_cast<std::uint32_t>(pid),
        Number(value.String("start")), value.String("exe"), value.String("host"), value.String("mod")};
    Check(wire::ValidIdentity(id), "invalid identity values");
    return id;
}
[[maybe_unused]] fs::path KnownLocal() {
    PWSTR raw{};
    // NO_PACKAGE_REDIRECTION, also used by session.py's real-host guard.
    Check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0x00010000, nullptr, &raw)), "known LocalAppData unavailable");
    fs::path known(raw);
    CoTaskMemFree(raw);
    std::array<wchar_t, 32768> env{};
    DWORD const count = GetEnvironmentVariableW(L"LOCALAPPDATA", env.data(), static_cast<DWORD>(env.size()));
    Check(count > 0 && count < env.size() && Equal(Safe(known), Safe(env.data())), "redirected LOCALAPPDATA refused");
    return Safe(known);
}
[[maybe_unused]] fs::path ModulePath(HMODULE module) {
    std::array<wchar_t, 32768> buffer{};
    DWORD const count = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    Check(count > 0 && count < buffer.size(), "module path unavailable");
    return Safe(fs::path(buffer.data()));
}
} // namespace

struct Endpoint::Impl {
    fs::path local, directory, run, invitation;
    ProcessFiles actual;
    UtcClock clock;
    wire::Identity identity;
    std::unique_ptr<wire::Gate> gate;
    std::string error, invitationBytes, descriptorBytes;
    bool attempted{}, attached{}, disabled{};
    std::uint64_t consumed{};

    Impl(fs::path root, ProcessFiles process, UtcClock utc)
        : local(Safe(root)), actual(std::move(process)), clock(utc ? std::move(utc) : UtcClock(UtcMs)) {
        actual.exe = Safe(actual.exe); actual.module = Safe(actual.module);
        Check(Equal(actual.exe.filename(), L"MassEffect2.exe") && Equal(actual.module.extension(), L".asi") &&
              actual.pid != 0 && actual.startFiletime != 0, "producer requires an LE2 monolithic ASI identity");
        invitation = local / L"MEVR/le2-control.attach.json";
    }
    void Ownership() {
        auto const shared = Safe(local / L"MEVR");
        auto const lock = shared / L"qa-cafeteria.lock";
        Check(Equal(Safe(Path(Read(lock, 32768))), run), "shared lock no longer owns this run");
        Check(!Exists(shared / L"game-slot/lock.json"), "game-slot ownership conflict");
        std::size_t entries{};
        for (auto const& entry : fs::directory_iterator(shared)) {
            Check(++entries <= 4096, "shared ownership directory exceeds bounded scan");
            if (Equal(entry.path().extension(), L".lock")) Check(Equal(Safe(entry.path()), lock), "another session lock is present");
        }
        auto context = Json(run / L"context.json");
        Check(context.Number("schemaVersion") == 1 && context.String("phase") == "game-bound" &&
              context.Bool("backupComplete") && !context.String("coordinatedOwner").empty() &&
              context.String("runId") == identity.run && Equal(Safe(Path(context.String("runDirectory"))), run) &&
              Equal(Safe(Path(context.String("lockPath"))), lock), "session ownership/phase differs");
        Check(Hash(run / L"plan.json") == context.String("planSha256"), "frozen session plan changed");
        auto plan = Json(run / L"plan.json");
        auto game = Safe(Path(plan.String("gameRoot")));
        Check(Equal(Safe(game / L"Game/ME2/Binaries/Win64/MassEffect2.exe"), actual.exe) &&
              Equal(Safe(Path(context.String("gameRoot"))), game) &&
              Equal(Safe(Path(context.String("exePath"))), actual.exe) &&
              context.String("exeSha256") == identity.exe && plan.String("exeSha256") == identity.exe,
              "session executable differs");
        auto const& process = context.Get("gameProcess");
        Check(process.Number("pid") == actual.pid && process.String("name") == "MassEffect2" &&
              Equal(Safe(Path(process.String("path"))), actual.exe) &&
              StartTime(process.String("started")) == actual.startFiletime, "bound process differs");
        Check(Hash(actual.exe) == identity.exe && Hash(actual.module) == identity.host,
              "loaded-module file or executable changed on disk");
        Check(Read(invitation, 16384) == invitationBytes, "attach invitation changed");
        if (attached) Check(Read(directory / L"identity.json", wire::kMaxFrame) == descriptorBytes, "runtime descriptor changed");
    }
    void Attach() {
        attempted = true;  // No second attach attempt or lease in this process lifetime.
        invitationBytes = Read(invitation, 16384);
        auto parsed = json::Parse(invitationBytes);
        Check(parsed && parsed->IsObject() && parsed->Number("schemaVersion") == 1, "invalid attach invitation");
        auto requested = Identity(parsed->Get("identity"));
        Check(requested.pid == actual.pid && requested.start == actual.startFiletime, "invitation targets another process");
        Check(Equal(Safe(Path(parsed->String("hostPath"))), actual.module) &&
              Equal(Safe(Path(parsed->String("modPath"))), actual.module), "invitation is not the loaded monolithic ASI");
        identity = {requested.run, requested.lease, actual.pid, actual.startFiletime,
                    Hash(actual.exe), Hash(actual.module), {}};
        identity.mod = identity.host;
        Check(identity == requested, "invitation hashes differ from actual process files");
        run = Safe(Path(parsed->String("runDirectory")));
        directory = Safe(run / L"control");
        Check(fs::is_directory(directory), "control directory must be created by protected bootstrap");
        Check(!Exists(directory / L"request.control") && !Exists(directory / L"result.control"), "pre-attachment traffic refused");
        Ownership();
        auto descriptor = Value::Object();
        descriptor["schemaVersion"] = 1; descriptor["identity"] = Identity(identity);
        descriptor["hostPath"] = Utf8(actual.module); descriptor["modPath"] = Utf8(actual.module);
        descriptor["source"] = "diagnostic-host"; descriptor["ready"] = false;
        descriptor["hashScope"] = "on-disk files at paths of loaded modules; not memory hashes";
        descriptorBytes = json::Write(descriptor);
        Publish(directory / L"identity.json", descriptorBytes);
#if defined(MEVR_LE2_CONTROL_TEST)
        gate = std::make_unique<wire::Gate>(identity, false);
#else
        // An explicit empty probe request may attempt one guarded scalar query.
        // General console dispatch remains disabled independently.
        gate = std::make_unique<wire::Gate>(identity, false, true);
#endif
        attached = true;
    }
    void Request(std::uint64_t tick, std::uint32_t thread) {
        auto request = Safe(directory / L"request.control");
        if (!Exists(request)) return;
        Ownership();
        Check(!Exists(directory / L"result.control"), "uncollected result prevents another request");
        Check(consumed < wire::kMaxRequests, "consumed request limit reached");
        auto claimed = Safe(directory / (L"consumed-" + std::to_wstring(++consumed) + L".request"));
        // Consume atomically BEFORE Claim/dispatch. Never replace a prior claim.
        Check(MoveFileExW(request.c_str(), claimed.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE, "request claim move failed");
        auto decoded = wire::Decode(Read(claimed, wire::kMaxFrame));
        Check(decoded.has_value(), "malformed consumed request; outcome retained for recovery");
        // Re-read true UTC at admission, after potentially slow ownership/hash IO.
        // A later publication does not invalidate a request already admitted here.
        auto failure = gate->Claim(*decoded, clock());
        wire::Frame result = *decoded;
        result.kind = "result"; result.identity = identity;
        auto payload = Value::Object();
        payload["source"] = "diagnostic-host"; payload["ready"] = false;
        if (failure) {
            result.status = "rejected";
            payload["reason"] = *failure;
        } else {
            result.status = "completed";
            payload["game"] = "LE2"; payload["readOnly"] = true;
            payload["gameTick"] = std::to_string(tick); payload["gameThreadId"] = std::to_string(thread);
            payload["sampleMonotonicMs"] = std::to_string(GetTickCount64());
            auto capabilities = Value::Object();
            capabilities["ping"] = true; capabilities["observeDiagnostic"] = true;
            capabilities["observeWorld"] = false; capabilities["nativeConsole"] = false;
            capabilities["observeStructuralWorld"] = false;
            capabilities["probeFov"] = false;
#if !defined(MEVR_LE2_CONTROL_TEST)
            capabilities["observeStructuralWorld"] = true;
            capabilities["probeFov"] = true;
            payload["observationScope"] = "sdk-structural-diagnostic";
            le2_diagnostic::ObserverServices services{};
            services.inspectTarget = &host::process_event::InspectDiagnosticTarget;
            services.observeController = &host::process_event::ObserveValidatedController;
            services.readHookSnapshot = &host::process_event::ReadDiagnosticSnapshot;
            // Tick is entered only through DiagnosticTickGate on its hook-owned
            // game thread. Ping never traverses engine memory.
            if (decoded->verb == "observe") {
                auto const provenance = host::process_event::ReadDiagnosticSnapshot();
                payload["observation"] = le2_diagnostic::Observe(thread, &provenance, &services);
            } else if (decoded->verb == "probe-fov") {
                // This invokes script and is not labelled a read-only sample.
                payload["readOnly"] = false;
                auto probe = le2_diagnostic::ProbeFov(thread, services);
                result.status = probe.status;
                payload["probe"] = std::move(probe.payload);
            }
#endif
            payload["capabilities"] = std::move(capabilities);
            auto unavailable = Value::Object();
            for (auto name : {"map", "world", "player", "weapon", "saveSelection"})
                unavailable[name] = "LE2 engine fields/lifetime are not validated by this producer";
            payload["unavailable"] = std::move(unavailable);
        }
        // Match the observer's compact payload budget. Pretty indentation can
        // exceed the wire cap even when the structural sample itself fits.
        result.payload = json::Write(payload, 0);
        auto encoded = wire::Encode(result);
        if (!encoded) throw std::runtime_error("result encoding failed after claim: payload bytes=" +
            std::to_string(result.payload.size()) + ", limit=" + std::to_string(wire::kMaxPayload));
        // Revalidate before publishing any claimed observation. Failure leaves uncertainty.
        Ownership();
        Publish(directory / L"result.control", *encoded);
        if (decoded->verb == "probe-fov" && result.status == "error") {
            // Preserve the correlated uncertainty result, then stop this lease.
            disabled = true;
            error = "native FOV probe outcome uncertain; no further requests admitted";
        }
    }
};

Endpoint::Endpoint(fs::path localAppData, ProcessFiles actual, UtcClock clock)
    : impl_(std::make_unique<Impl>(std::move(localAppData), std::move(actual), std::move(clock))) {}
Endpoint::~Endpoint() = default;
void Endpoint::Poll(std::uint64_t tick, std::uint32_t thread) noexcept {
    if (impl_->disabled) return;
    try {
        Check(tick && thread, "missing tick/thread identity");
        if (!impl_->attached) {
            if (impl_->attempted || !Exists(impl_->invitation)) return;
            impl_->Attach();
        } else impl_->Request(tick, thread);
    } catch (std::exception const& error) {
        impl_->disabled = true;
        try { impl_->error = error.what(); } catch (...) {}
        OutputDebugStringA("MEVR LE2 control disabled; preserve invitation and consumed traffic for recovery.\n");
    } catch (...) {
        impl_->disabled = true;
        try { impl_->error = "unknown producer failure"; } catch (...) {}
    }
}
bool Endpoint::Attached() const { return impl_->attached; }
bool Endpoint::Disabled() const { return impl_->disabled; }
std::string const& Endpoint::LastError() const { return impl_->error; }

#if !defined(MEVR_LE2_CONTROL_TEST)
namespace {
std::unique_ptr<Endpoint> gEndpoint;
bool gStartedOnce{};
bool gErrorLogged{};
std::uint64_t gTicks{}, gNextPoll{};
void Tick(float) {
    if (!gEndpoint || gTicks == std::numeric_limits<std::uint64_t>::max()) return;
    ++gTicks;
    auto monotonic = GetTickCount64();
    if (monotonic < gNextPoll) return;
    gNextPoll = monotonic + 250;
    gEndpoint->Poll(gTicks, GetCurrentThreadId());
    if (gEndpoint->Disabled() && !gErrorLogged) {
        gErrorLogged = true;
        try { log::Error("LE2 control disabled: {} (preserve attach/consumed files for recovery)", gEndpoint->LastError()); }
        catch (...) {} // diagnostic logging must never escape the game tick
    }
}
} // namespace
bool Start() noexcept {
#if defined(MEVR_LE2_DIAGNOSTIC) && defined(MEVR_MONOLITHIC)
    if (gStartedOnce) return false;
    gStartedOnce = true;
    try {
        FILETIME created{}, exited{}, kernel{}, user{};
        Check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE, "process time unavailable");
        HMODULE module{};
        Check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
              reinterpret_cast<LPCWSTR>(&Start), &module) != FALSE, "loaded ASI module unavailable");
        gEndpoint = std::make_unique<Endpoint>(KnownLocal(), ProcessFiles{GetCurrentProcessId(), Filetime(created),
                                             ModulePath(nullptr), ModulePath(module)});
        if (!le2_diagnostic::SetTickCallback(&Tick)) { gEndpoint.reset(); return false; }
        return true;
    } catch (std::exception const& error) {
        gEndpoint.reset();
        try { log::Error("LE2 control registration refused: {}", error.what()); } catch (...) {}
        return false;
    } catch (...) { gEndpoint.reset(); return false; }
#else
    return false;
#endif
}
void Shutdown() noexcept { gEndpoint.reset(); } // host has already stopped its serialized tick
#else
bool Start() noexcept { return false; }
void Shutdown() noexcept {}
#endif
} // namespace mevr::engine::le2_control
