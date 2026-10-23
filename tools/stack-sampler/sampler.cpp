// SPDX-License-Identifier: MIT
// Samples one thread's call stack in another process (research/22): where its time goes, by module and
// function, from outside the game (no redeploy). Suspends the thread for each sample (microseconds), walks
// its stack with dbghelp, and prints the top frames and what is on the stack. Names come from exports and
// our PDB; the game's exe has none (offsets).
// Usage: mevr-stack-sampler <pid> <tid> <seconds> [intervalUs] [raw-stacks.txt]; tools/sample-render-thread.ps1
// finds both and groups the raw stacks by function (tools/stack-functions.py).
#include <Windows.h>
#include <DbgHelp.h>
#include <timeapi.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: mevr-stack-sampler pid tid seconds [intervalUs] [raw-stacks.txt]\n"); return 1; }
    DWORD pid = std::strtoul(argv[1], nullptr, 10), tid = std::strtoul(argv[2], nullptr, 10);
    double seconds = std::atof(argv[3]);
    int intervalUs = argc > 4 ? std::atoi(argv[4]) : 1000;
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!proc || !th) { std::printf("open failed %lu\n", GetLastError()); return 1; }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    std::vector<std::vector<DWORD64>> stacks;
    LARGE_INTEGER f, t0, t; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    timeBeginPeriod(1);
    for (;;) {
        QueryPerformanceCounter(&t);
        if ((t.QuadPart - t0.QuadPart) / double(f.QuadPart) > seconds) break;
        if (SuspendThread(th) == (DWORD)-1) break;
        alignas(16) CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_FULL;
        std::vector<DWORD64> st;
        if (GetThreadContext(th, &ctx)) {
            STACKFRAME64 sf{}; sf.AddrPC.Offset = ctx.Rip; sf.AddrPC.Mode = AddrModeFlat;
            sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
            sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
            for (int i = 0; i < 64; ++i) {   // deep enough to keep the outer callers (24 cut them off)
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, th, &sf, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
                if (sf.AddrPC.Offset == 0) break;
                st.push_back(sf.AddrPC.Offset);
            }
        }
        ResumeThread(th);
        if (!st.empty()) stacks.push_back(std::move(st));
        // spin-sleep to the interval
        LARGE_INTEGER a; QueryPerformanceCounter(&a);
        for (;;) { LARGE_INTEGER b; QueryPerformanceCounter(&b); if ((b.QuadPart - a.QuadPart) * 1e6 / f.QuadPart >= intervalUs) break; Sleep(0); }
    }
    timeEndPeriod(1);
    auto name = [&](DWORD64 a) {
        char buf[sizeof(SYMBOL_INFO) + 512]{}; auto* s = reinterpret_cast<SYMBOL_INFO*>(buf);
        s->SizeOfStruct = sizeof(SYMBOL_INFO); s->MaxNameLen = 511; DWORD64 d = 0;
        IMAGEHLP_MODULE64 m{}; m.SizeOfStruct = sizeof(m);
        std::string mod = SymGetModuleInfo64(proc, a, &m) ? m.ModuleName : "?";
        if (SymFromAddr(proc, a, &d, s)) return mod + "!" + s->Name;
        return mod + "!" + (mod == "?" ? "" : std::to_string(a - m.BaseOfImage));
    };
    auto modOf = [&](DWORD64 a) { IMAGEHLP_MODULE64 m{}; m.SizeOfStruct = sizeof(m); return SymGetModuleInfo64(proc, a, &m) ? std::string(m.ModuleName) : std::string("?"); };
    std::map<std::string, int> top, topMod, incl;
    for (auto& st : stacks) {
        top[name(st[0])]++; topMod[modOf(st[0])]++;
        std::map<std::string, int> seen;
        for (auto a : st) { auto n = name(a); if (!seen[n]++) incl[n]++; }
    }
    auto print = [&](const char* title, std::map<std::string, int>& m, int n) {
        std::vector<std::pair<int, std::string>> v; for (auto& [k, c] : m) v.push_back({c, k});
        std::sort(v.rbegin(), v.rend());
        std::printf("\n%s (%zu samples)\n", title, stacks.size());
        for (int i = 0; i < n && i < (int)v.size(); ++i) std::printf("  %5.1f%%  %s\n", 100.0 * v[i].first / stacks.size(), v[i].second.c_str());
    };
    print("top frame by module", topMod, 12);
    print("top frame", top, 30);
    print("on the stack (inclusive)", incl, 45);
    // The raw stacks (one sample per line, innermost first, "module+0xoffset"), for tools/stack-functions.py.
    if (argc > 5) {
        FILE* out = nullptr;
        if (fopen_s(&out, argv[5], "w") == 0 && out != nullptr) {
            for (auto& st : stacks) {
                for (std::size_t i = 0; i < st.size(); ++i) {
                    IMAGEHLP_MODULE64 m{}; m.SizeOfStruct = sizeof(m);
                    bool const known = SymGetModuleInfo64(proc, st[i], &m) != FALSE;
                    std::fprintf(out, "%s%s+0x%llX", i ? ";" : "", known ? m.ModuleName : "?",
                        static_cast<unsigned long long>(known ? st[i] - m.BaseOfImage : st[i]));
                }
                std::fprintf(out, "\n");
            }
            std::fclose(out);
            std::printf("\nraw stacks: %s\n", argv[5]);
        }
    }
    return 0;
}
