// SPDX-License-Identifier: MIT
#include "engine/Le2ControlHost.hpp"
#include "core/Json.hpp"
#include "core/qa/ControlProtocol.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
namespace host = mevr::engine::le2_control;
namespace wire = mevr::qa::control;
namespace json = mevr::json;
using json::Value;
void Require(bool ok, char const* why) { if (!ok) throw std::runtime_error(why); }
std::string Text(fs::path const& path) {
    auto text = path.u8string();
    return {reinterpret_cast<char const*>(text.data()), text.size()};
}
std::string Read(fs::path const& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void Put(fs::path const& path, std::string const& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    Require(file.good(), "fixture write failed");
}
std::string Hash(std::string text) {
    BCRYPT_ALG_HANDLE algorithm{};
    Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "test SHA unavailable");
    std::array<unsigned char, 32> digest{};
    auto status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(text.data()),
                            static_cast<ULONG>(text.size()), digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    Require(status >= 0, "test hash failed");
    std::string out;
    for (auto byte : digest) { out += "0123456789abcdef"[byte >> 4]; out += "0123456789abcdef"[byte & 15]; }
    return out;
}
struct Fixture {
    fs::path root, local, run, control, game, exe, module, invitation;
    Value context, invite;
    host::ProcessFiles process;
    wire::Identity identity;
    static constexpr std::uint64_t now = 1000000;
    Fixture() {
        std::array<wchar_t, 32768> temporary{};
        Require(GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data()) != 0, "temp path unavailable");
        static unsigned serial{};
        root = fs::path(temporary.data()) / (L"mevr-le2-control-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                             std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++serial));
        Require(!fs::exists(root), "test root collision");
        local = root / L"local"; run = root / L"run"; control = run / L"control"; game = root / L"game";
        exe = game / L"Game/ME2/Binaries/Win64/MassEffect2.exe";
        module = exe.parent_path() / L"ASI/LE2MEVR.asi";
        invitation = local / L"MEVR/le2-control.attach.json";
        fs::create_directories(control);
        Put(exe, "fake EXE, never executed"); Put(module, "fake ASI, never loaded");
        SYSTEMTIME system{}; system.wYear = 2026; system.wMonth = 11; system.wDay = 13; system.wHour = 12;
        FILETIME start{};
        Require(SystemTimeToFileTime(&system, &start) != FALSE, "test time failed");
        auto ticks = (static_cast<std::uint64_t>(start.dwHighDateTime) << 32) + start.dwLowDateTime + 1234567;
        process = {321, ticks, exe, module};
        identity = {"11111111-1111-4111-8111-111111111111", "22222222-2222-4222-8222-222222222222",
                    process.pid, ticks, Hash(Read(exe)), Hash(Read(module)), Hash(Read(module))};
        auto plan = Value::Object(); plan["schemaVersion"] = 1;
        plan["gameRoot"] = Text(game); plan["exeSha256"] = identity.exe;
        Put(run / L"plan.json", json::Write(plan));
        context = Value::Object(); context["schemaVersion"] = 1; context["phase"] = "game-bound";
        context["backupComplete"] = true; context["coordinatedOwner"] = "offline fake-files test";
        context["runId"] = identity.run; context["runDirectory"] = Text(run);
        context["lockPath"] = Text(local / L"MEVR/qa-cafeteria.lock");
        context["gameRoot"] = Text(game); context["exePath"] = Text(exe); context["exeSha256"] = identity.exe;
        context["planSha256"] = Hash(Read(run / L"plan.json"));
        auto bound = Value::Object(); bound["pid"] = static_cast<int>(process.pid);
        bound["name"] = "MassEffect2"; bound["path"] = Text(exe); bound["started"] = "2026-11-13T12:00:00.1234567Z";
        context["gameProcess"] = std::move(bound);
        SaveContext();
        Put(local / L"MEVR/qa-cafeteria.lock", Text(run));
        auto id = Value::Object();
        id["run"] = identity.run; id["lease"] = identity.lease; id["pid"] = std::to_string(identity.pid);
        id["start"] = std::to_string(identity.start); id["exe"] = identity.exe; id["host"] = identity.host;
        id["mod"] = identity.mod; id["game"] = "LE2";
        invite = Value::Object(); invite["schemaVersion"] = 1; invite["runDirectory"] = Text(run);
        invite["identity"] = std::move(id); invite["hostPath"] = Text(module); invite["modPath"] = Text(module);
        SaveInvite();
    }
    ~Fixture() {
        // Only the uniquely created test subtree, never the native profile/installation.
        if (root.filename().native().starts_with(L"mevr-le2-control-test-")) {
            std::error_code error; fs::remove_all(root, error);
        }
    }
    void SaveContext() { Put(run / L"context.json", json::Write(context)); }
    void SaveInvite() { Put(invitation, json::Write(invite)); }
    wire::Frame Request(std::string verb = "ping", std::uint64_t sequence = 1) const {
        return {"request", identity, "33333333-3333-4333-8333-333333333333", sequence,
                now + 10000, std::move(verb), "pending", {}};
    }
    void Send(wire::Frame const& frame) { auto bytes = wire::Encode(frame); Require(bytes.has_value(), "bad test frame"); Put(control / L"request.control", *bytes); }
    wire::Frame Result() const {
        auto value = wire::Decode(Read(control / L"result.control")); Require(value.has_value(), "result missing/invalid"); return *value;
    }
};

void PingAndObserve() {
    Fixture f; host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; });
    auto before = Read(f.invitation);
    endpoint.Poll(1, 42);
    Require(endpoint.Attached() && !endpoint.Disabled(), endpoint.LastError().c_str());
    auto identity = json::Parse(Read(f.control / L"identity.json"));
    Require(identity && identity->String("source") == "diagnostic-host" && !identity->Bool("ready", true), "invalid descriptor claims");
    for (std::uint64_t sequence = 1; sequence <= 2; ++sequence) {
        auto request = f.Request(sequence == 1 ? "ping" : "observe", sequence);
        if (sequence == 2) request.id = "44444444-4444-4444-8444-444444444444";
        f.Send(request); endpoint.Poll(sequence + 1, 42);
        auto result = f.Result();
        Require(result.status == "completed" && result.identity == f.identity, "ping/observe not correlated");
        auto payload = json::Parse(result.payload);
        Require(payload && payload->String("source") == "diagnostic-host" && !payload->Bool("ready", true) &&
                payload->Get("unavailable").Get("map").IsString() && !payload->Get("capabilities").Bool("nativeConsole", true),
                "payload claims unvalidated engine state");
        for (auto const& [key, value] : payload->Get("capabilities").Members()) {
            (void)key; Require(value.GetKind() == Value::Kind::Bool, "capability is not boolean");
        }
        Require(!payload->Get("capabilities").Bool("observeWorld", true) &&
                !payload->Get("capabilities").Bool("observeStructuralWorld", true), "fixture advertised engine capability");
        Require(!fs::exists(f.control / L"request.control") && fs::exists(f.control / (L"consumed-" + std::to_wstring(sequence) + L".request")), "request not durably consumed");
        fs::remove(f.control / L"result.control");
    }
    Require(Read(f.invitation) == before && Read(f.exe) == "fake EXE, never executed", "producer modified inputs");
}
void WaitForInvitation() {
    Fixture f; auto invitation = Read(f.invitation); fs::remove(f.invitation); fs::remove(f.control);
    host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; }); endpoint.Poll(1, 42);
    Require(!endpoint.Disabled() && !endpoint.Attached() && !fs::exists(f.control), "uninvited producer mutated state");
    fs::create_directory(f.control); Put(f.invitation, invitation); endpoint.Poll(2, 42);
    Require(endpoint.Attached() && !endpoint.Disabled(), "late first invitation not accepted");
}
void RejectConsoleAndReplay() {
    Fixture f; host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; }); endpoint.Poll(1, 42);
    auto request = f.Request(); f.Send(request); endpoint.Poll(2, 42);
    Require(f.Result().status == "completed", "initial ping failed"); fs::remove(f.control / L"result.control");
    f.Send(request); endpoint.Poll(3, 42);
    Require(f.Result().status == "rejected", "duplicate dispatched"); fs::remove(f.control / L"result.control");
    request = f.Request("console", 2); request.id = "44444444-4444-4444-8444-444444444444"; request.payload = "exit";
    f.Send(request); endpoint.Poll(4, 42);
    Require(f.Result().status == "rejected", "native console was enabled");
}
void WrongLeaseAndDeadline() {
    Fixture f; host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; }); endpoint.Poll(1, 42);
    auto request = f.Request(); request.identity.lease = "99999999-9999-4999-8999-999999999999";
    f.Send(request); endpoint.Poll(2, 42);
    Require(f.Result().status == "rejected" && f.Result().identity == f.identity, "wrong lease was echoed/accepted");
    fs::remove(f.control / L"result.control");
    request = f.Request(); request.deadline = f.now;
    f.Send(request); endpoint.Poll(3, 42); Require(f.Result().status == "rejected", "expired request accepted");
}
void ClaimUsesFreshClock() {
    for (bool expiresDuringIo : {false, true}) {
        Fixture f;
        unsigned calls{};
        host::Endpoint endpoint(f.local, f.process, [&] {
            ++calls;
            Require(fs::exists(f.control / L"consumed-1.request") && !fs::exists(f.control / L"request.control"),
                    "UTC was read before ownership IO and request consumption");
            // Simulate the first ownership/hash reads carrying execution past the deadline.
            // A second clock query would expire even the admitted request; publication
            // must not retroactively reject a valid terminal result.
            return Fixture::now + (expiresDuringIo || calls > 1 ? 10001 : 1);
        });
        endpoint.Poll(1, 42);
        Require(calls == 0, "clock queried during attachment rather than request admission");
        f.Send(f.Request()); endpoint.Poll(2, 42);
        Require(calls == 1 && f.Result().status == (expiresDuringIo ? "rejected" : "completed"),
                "stale admission timestamp or late-result rejection");
    }
}
void InvalidAttach() {
    for (int test = 0; test < 6; ++test) {
        Fixture f;
        if (test == 0) { f.invite["identity"]["pid"] = "322"; f.SaveInvite(); }
        if (test == 1) Put(f.module, "different binary");
        if (test == 2) Put(f.local / L"MEVR/qa-cafeteria.lock", Text(f.root / L"other"));
        if (test == 3) Put(f.local / L"MEVR/game-slot/lock.json", "{}");
        if (test == 4) { f.context["backupComplete"] = false; f.SaveContext(); }
        if (test == 5) Put(f.run / L"plan.json", "{}");
        host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; }); endpoint.Poll(1, 42);
        Require(endpoint.Disabled() && !endpoint.Attached() && !fs::exists(f.control / L"identity.json"), "invalid attach accepted");
    }
}
void RevokedOwnerAndDescriptor() {
    for (int test = 0; test < 5; ++test) {
        Fixture f; host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; }); endpoint.Poll(1, 42); f.Send(f.Request());
        if (test == 0) { f.context["phase"] = "restoring"; f.SaveContext(); }
        if (test == 1) Put(f.control / L"identity.json", "{}");
        if (test == 2) Put(f.invitation, "{}");
        if (test == 3) Put(f.local / L"MEVR/qa-other.lock", "other");
        if (test == 4) Put(f.local / L"MEVR/qa-other.LOCK", "mixed-case owner");
        endpoint.Poll(2, 42);
        Require(endpoint.Disabled() && fs::exists(f.control / L"request.control") && !fs::exists(f.control / L"result.control"), "revoked owner request consumed");
    }
}
void MalformedAndInterrupted() {
    Fixture f; host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; }); endpoint.Poll(1, 42);
    Put(f.control / L"request.control", "bad frame"); endpoint.Poll(2, 42);
    Require(endpoint.Disabled() && fs::exists(f.control / L"consumed-1.request") && !fs::exists(f.control / L"result.control"), "malformed request lost");
    host::Endpoint restarted(f.local, f.process, [] { return Fixture::now + 1; }); restarted.Poll(3, 42);
    Require(restarted.Disabled(), "restart reused descriptor/lease");
}
void OutputCollisionAndOversize() {
    for (int test = 0; test < 2; ++test) {
        Fixture f; host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; }); endpoint.Poll(1, 42);
        f.Send(f.Request());
        if (test == 0) Put(f.control / L"result.control.tmp", "prior interrupted writer");
        else Put(f.control / L"request.control", std::string(wire::kMaxFrame + 1, 'x'));
        endpoint.Poll(2, 42);
        Require(endpoint.Disabled() && fs::exists(f.control / L"consumed-1.request") && !fs::exists(f.control / L"result.control"), "publication/size failure did not retain uncertainty");
        if (test == 0) Require(Read(f.control / L"result.control.tmp") == "prior interrupted writer", "temporary output overwritten");
    }
}
void ExistingResultAndReparse() {
    Fixture f; host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; }); endpoint.Poll(1, 42); f.Send(f.Request());
    Put(f.control / L"result.control", "other result"); endpoint.Poll(2, 42);
    Require(endpoint.Disabled() && Read(f.control / L"result.control") == "other result" && fs::exists(f.control / L"request.control"), "existing result clobbered");
    Fixture linked;
    auto target = linked.root / L"redirect.json"; Put(target, Read(linked.invitation)); fs::remove(linked.invitation);
    if (CreateSymbolicLinkW(linked.invitation.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        host::Endpoint probe(linked.local, linked.process); probe.Poll(1, 42);
        Require(probe.Disabled(), "reparse invitation accepted");
    } else std::cout << "SKIP reparse creation: Windows privilege/developer mode unavailable\n";
}
Value FixturePlan(Fixture& f) {
    auto plan = *json::Parse(Read(f.run / L"plan.json"));
    auto const profile = f.root / L"profile";
    std::string const relative = "Save/ME2/SyntheticCareer/Save99.pcsav";
    std::string const bytes = "private fake save content, never passed to an engine";
    Put(profile / relative, bytes); Put(f.run / L"fixture.pcsav", bytes);
    plan["profileRoot"] = Text(profile);
    auto file = Value::Object(); file["sha256"] = Hash(bytes); file["targetRelativePath"] = relative;
    plan["fixture"] = file;
    auto selection = Value::Object(); selection["schemaVersion"] = 1;
    auto descriptor = Value::Object(); descriptor["career"] = "SyntheticCareer"; descriptor["saveName"] = "Save99";
    descriptor["index"] = 42; descriptor["type"] = 0; // Index deliberately is NOT the filename number.
    selection["descriptor"] = descriptor; selection["fixtureSha256"] = Hash(bytes);
    selection["targetRelativePath"] = relative; selection["exeSha256"] = f.identity.exe;
    auto evidence = Value::Object(); evidence["runId"] = "55555555-5555-4555-8555-555555555555";
    evidence["observationSha256"] = Hash("archived observation"); evidence["captureSha256"] = Hash("archived screenshot");
    evidence["reviewedBy"] = "offline synthetic mapping reviewer";
    selection["evidence"] = evidence; plan["fixtureSelection"] = selection;
    f.context["profileRoot"] = Text(profile); f.context["fixtureTarget"] = Text(profile / relative);
    f.context["fixtureSha256"] = Hash(bytes);
    return plan;
}
void FreezePlan(Fixture& f, Value const& plan) {
    Put(f.run / L"plan.json", json::Write(plan));
    f.context["planSha256"] = Hash(Read(f.run / L"plan.json")); f.SaveContext();
}
void FrozenFixtureBinding() {
    for (int variant = 0; variant < 22; ++variant) {
        Fixture f; auto plan = FixturePlan(f);
        auto& selection = plan["fixtureSelection"];
        auto& descriptor = selection["descriptor"];
        if (variant == 1) plan.Erase("fixtureSelection");
        if (variant == 2) selection.Erase("evidence");
        if (variant == 3) selection["evidence"]["reviewedBy"] = "   ";
        if (variant == 4) selection["evidence"]["observationSha256"] = std::string(64, '0');
        if (variant == 5) selection["evidence"]["captureSha256"] = "unproven";
        if (variant == 6) selection["exeSha256"] = Hash("other executable");
        if (variant == 7) selection["fixtureSha256"] = Hash("other save");
        if (variant == 8) descriptor["index"] = -1;
        if (variant == 9) descriptor["index"] = 1.5;
        if (variant == 10) descriptor["type"] = 2;
        if (variant == 11) descriptor["type"] = "0";
        if (variant == 12) descriptor["career"] = "..";
        if (variant == 13) descriptor["saveName"] = "../Save99";
        if (variant == 14) descriptor["saveName"] = "Save99.";
        if (variant == 15) descriptor["saveName"] = "NUL";
        if (variant == 16) descriptor["career"] = std::string(256, 'a');
        if (variant == 17) descriptor["career"] = std::string("bad\xC0\xAF");
        if (variant == 18) selection["targetRelativePath"] = "Save/ME2/SyntheticCareer/Save42.pcsav";
        if (variant == 19) plan["fixture"]["targetRelativePath"] = "Save/ME2/SyntheticCareer/Other.pcsav";
        if (variant == 20) f.context["profileRoot"] = Text(f.root / L"wrong-profile");
        if (variant == 21) f.context["fixtureTarget"] = Text(f.root / L"other.pcsav");
        FreezePlan(f, plan);
        host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; }); endpoint.Poll(1, 42);
        Require(endpoint.Attached() && !endpoint.Disabled(), "optional invalid binding prevented safe attachment");
        f.Send(f.Request()); endpoint.Poll(2, 42);
        auto ping = json::Parse(f.Result().payload);
        Require(f.Result().status == "completed" && ping && ping->Get("fixtureBinding").Bool("available") == (variant == 0),
                "frozen binding validation incorrectly accepted/refused plan");
        Require(!ping->Get("capabilities").Bool("nativeFixtureAction", true) &&
                !ping->Get("capabilities").Bool("observeFixture", true), "offline fixture advertised native capability");
        fs::remove(f.control / L"result.control");
        auto request = f.Request("observe-fixture", 2); request.id = "66666666-6666-4666-8666-666666666666";
        f.Send(request); endpoint.Poll(3, 42);
        Require(f.Result().status == "rejected" && !endpoint.Disabled(), "fixture observation was not safely terminal rejected");
        fs::remove(f.control / L"result.control");
        request = f.Request("load-fixture", 3); request.id = "77777777-7777-4777-8777-777777777777";
        f.Send(request); endpoint.Poll(4, 42);
        Require(f.Result().status == "rejected" && !endpoint.Disabled(), "offline fixture entered native action");
    }
    for (bool frozen : {false, true}) {
        Fixture f; auto plan = FixturePlan(f); FreezePlan(f, plan);
        host::Endpoint endpoint(f.local, f.process, [] { return Fixture::now + 1; }); endpoint.Poll(1, 42);
        Put(frozen ? f.run / L"fixture.pcsav" : fs::path(f.context.String("fixtureTarget")), "changed after attachment");
        f.Send(f.Request("observe-fixture")); endpoint.Poll(2, 42);
        auto result = json::Parse(f.Result().payload);
        Require(f.Result().status == "rejected" && result && !result->Get("fixtureBinding").Bool("available", true) &&
                !endpoint.Disabled(), "changed staged/frozen save did not reject before entry");
    }
}
int FixtureLoop() {
    Fixture fixture;
    host::Endpoint endpoint(fixture.local, fixture.process);
    std::uint64_t tick = 1;
    endpoint.Poll(tick, 42);
    Require(endpoint.Attached() && !endpoint.Disabled(), endpoint.LastError().c_str());
    auto descriptor = json::Parse(Read(fixture.control / L"identity.json"));
    Require(descriptor.has_value(), "fixture identity missing");
    (*descriptor)["runDirectory"] = Text(fixture.run);
    (*descriptor)["fixtureOnly"] = true;
    std::cout << json::Write(*descriptor, 0) << std::endl;
    auto const end = GetTickCount64() + 30000;
    while (GetTickCount64() < end && !fs::exists(fixture.control / L"stop.fixture")) {
        endpoint.Poll(++tick, 42);
        if (endpoint.Disabled()) throw std::runtime_error(endpoint.LastError());
        Sleep(10);
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--fixture-loop") return FixtureLoop();
        Require(argc == 1, "unknown test argument");
        PingAndObserve(); WaitForInvitation(); RejectConsoleAndReplay(); WrongLeaseAndDeadline(); ClaimUsesFreshClock(); InvalidAttach();
        RevokedOwnerAndDescriptor(); MalformedAndInterrupted(); OutputCollisionAndOversize(); ExistingResultAndReparse(); FrozenFixtureBinding();
        std::cout << "11 LE2 control filesystem test groups passed (fake files/identity only)\n";
        return 0;
    } catch (std::exception const& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
