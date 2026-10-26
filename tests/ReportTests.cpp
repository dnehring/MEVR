// SPDX-License-Identifier: MIT
// Offline tests for "Report a problem" (install/Report, install/Zip, install/ReportMarks; research/33 section 8): the
// zip writer (read back and inflated here), the redaction, the log extraction, the crash parse, the settings diff,
// REPORT.md's assembly and budget, the issue URL, the crash offer's rule. Called from tests/Main.cpp.

#include "install/Ini.hpp"
#include "install/Report.hpp"
#include "install/ReportMarks.hpp"
#include "install/Zip.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

namespace {

namespace report = mevr::install::report;
namespace zip = mevr::install::zip;

int* gReportFailures = nullptr;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL ReportTests.cpp line %d: %s\n", line, what);
        ++*gReportFailures;
    }
}
#define RCHECK(expr) Check((expr), #expr, __LINE__)

bool Has(std::string const& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

// ---- A small inflater (stored and fixed-Huffman blocks: what our writer makes), to check the deflate stream ----------
class Inflater {
public:
    explicit Inflater(std::string_view in) : in_(in) {}
    bool Run(std::string& out) {
        for (;;) {
            int const final = Bits(1);
            int const type = Bits(2);
            if (bad_) {
                return false;
            }
            if (type == 0) {
                bit_ = (bit_ + 7) & ~std::size_t{7};
                std::size_t const at = bit_ / 8;
                if (at + 4 > in_.size()) {
                    return false;
                }
                std::size_t const len = static_cast<unsigned char>(in_[at]) | static_cast<unsigned char>(in_[at + 1]) << 8;
                out.append(in_.substr(at + 4, len));
                bit_ = (at + 4 + len) * 8;
            } else if (type == 1) {
                if (!Fixed(out)) {
                    return false;
                }
            } else {
                return false;
            }
            if (final != 0) {
                return true;
            }
        }
    }

private:
    int Bits(int n) {
        int v = 0;
        for (int i = 0; i < n; ++i) {
            if (bit_ / 8 >= in_.size()) {
                bad_ = true;
                return 0;
            }
            v |= ((static_cast<unsigned char>(in_[bit_ / 8]) >> (bit_ % 8)) & 1) << i;
            ++bit_;
        }
        return v;
    }
    // A Huffman code, most significant bit first.
    int Code(int n) {
        int v = 0;
        for (int i = 0; i < n; ++i) {
            v = (v << 1) | Bits(1);
        }
        return v;
    }
    int LitLen() {
        int code = Code(7);
        if (code <= 0x17) {
            return 256 + code;
        }
        code = (code << 1) | Bits(1);
        if (code >= 0x30 && code <= 0xBF) {
            return code - 0x30;
        }
        if (code >= 0xC0 && code <= 0xC7) {
            return 280 + code - 0xC0;
        }
        code = (code << 1) | Bits(1);
        return 144 + code - 0x190;
    }
    bool Fixed(std::string& out) {
        static int const lbase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static int const lextra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static int const dbase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
            6145, 8193, 12289, 16385, 24577};
        static int const dextra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int const sym = LitLen();
            if (bad_) {
                return false;
            }
            if (sym < 256) {
                out += static_cast<char>(sym);
            } else if (sym == 256) {
                return true;
            } else {
                int const li = sym - 257;
                if (li >= 29) {
                    return false;
                }
                int const length = lbase[li] + Bits(lextra[li]);
                int const di = Code(5);
                if (di >= 30) {
                    return false;
                }
                std::size_t const distance = static_cast<std::size_t>(dbase[di] + Bits(dextra[di]));
                if (distance > out.size()) {
                    return false;
                }
                for (int k = 0; k < length; ++k) {
                    out += out[out.size() - distance];
                }
            }
        }
    }
    std::string_view in_;
    std::size_t bit_ = 0;
    bool bad_ = false;
};

bool RoundTrip(std::string const& data) {
    std::string const packed = zip::Deflate(data);
    std::string unpacked;
    return Inflater(packed).Run(unpacked) && unpacked == data;
}

void TestZip() {
    RCHECK(zip::Crc32("123456789") == 0xCBF43926u);
    RCHECK(zip::Crc32("") == 0u);
    RCHECK(RoundTrip(""));
    RCHECK(RoundTrip("a"));
    RCHECK(RoundTrip("abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc"));
    std::string log;
    for (int i = 0; i < 6000; ++i) {
        log += std::format("[01:40:{:02}.{:03}] [warn] first-chance access violation 0xC0000005 on thread {} at LE1MEVR.asi+0x2C4866\n", i % 60,
            i % 1000, 96000 + i % 7);
    }
    RCHECK(RoundTrip(log));
    RCHECK(zip::Deflate(log).size() < log.size() / 4);
    std::string noise;
    std::uint32_t x = 12345;
    for (int i = 0; i < 100000; ++i) {
        x = x * 1664525u + 1013904223u;
        noise += static_cast<char>(x >> 24);
    }
    RCHECK(RoundTrip(noise));
    std::string longRun(70000, 'z');  // matches longer than 258 and distances up to the window
    RCHECK(RoundTrip(longRun + noise.substr(0, 40000) + longRun));

    zip::Writer w(1793000000);
    w.Add("REPORT.md", "# hello\n");
    w.Add("logs/LE1MEVR.log", log);
    w.Add("snapshot/x-left.png", noise.substr(0, 5000));
    w.Add("empty.txt", "");
    std::string const archive = w.Finish();
    std::vector<zip::ReadEntry> entries;
    RCHECK(zip::Read(archive, entries));
    RCHECK(entries.size() == 4);
    if (entries.size() == 4) {
        RCHECK(entries[0].entry.name == "REPORT.md");
        RCHECK(entries[1].entry.method == 8);
        RCHECK(entries[2].entry.method == 0);  // random bytes: stored
        std::string unpacked;
        RCHECK(Inflater(entries[1].data).Run(unpacked) && unpacked == log);
        RCHECK(entries[1].entry.crc == zip::Crc32(log));
        RCHECK(entries[2].data == noise.substr(0, 5000));
        RCHECK(entries[3].entry.size == 0);
    }
}

void TestRedaction() {
    report::Redactor const r({"daugu"}, {"DESKTOP-AB12CD"}, {"C:\\Users\\daugu"});
    report::Redactions n;
    std::string const out = r.Apply(
        "log: C:\\Users\\daugu\\AppData\\Local\\MEVR\n"
        "json: \"C:\\\\Users\\\\daugu\\\\AppData\"\n"
        "fwd: c:/users/DAUGU/Desktop\n"
        "word: hello daugu! and daugux and xdaugu stay\n"
        "pc: DESKTOP-AB12CD started; desktop-ab12cd.local\n"
        "mail: john.doe+mevr@example.co.uk, git@github.com.\n"
        "keep: LE1MEVR.asi+0x2C4866, Combat@3, a@b, user@localhost, 2 @ 3\n",
        &n);
    RCHECK(!Has(out, "daugu\\"));
    RCHECK(Has(out, "C:\\Users\\<user>\\AppData\\Local\\MEVR"));
    RCHECK(Has(out, "C:\\\\Users\\\\<user>\\\\AppData"));
    RCHECK(Has(out, "C:/Users/<user>/Desktop"));  // the spelling of the profile path replaces it
    RCHECK(Has(out, "hello <user>!"));
    RCHECK(Has(out, "daugux") && Has(out, "xdaugu"));
    RCHECK(Has(out, "pc: <computer> started; <computer>.local"));
    RCHECK(Has(out, "mail: <email>, <email>."));
    RCHECK(Has(out, "LE1MEVR.asi+0x2C4866, Combat@3, a@b, user@localhost, 2 @ 3"));
    RCHECK(n.profilePath == 3);
    RCHECK(n.userName == 1);
    RCHECK(n.computerName == 2);
    RCHECK(n.email == 2);
    RCHECK(n.Total() == 8);

    // A profile outside "Users": all of it goes.
    report::Redactor const other({"pat"}, {}, {"D:\\Profiles\\pat"});
    report::Redactions m;
    RCHECK(other.Apply("at D:\\Profiles\\pat\\x and pat", &m) == "at <profile>\\x and <user>");
    RCHECK(m.profilePath == 1 && m.userName == 1);
    // Too short a name isn't replaced as a word (it would eat the log); its profile path still is.
    report::Redactor const shortName({"al"}, {}, {"C:\\Users\\al"});
    RCHECK(shortName.Apply("C:\\Users\\al\\x al was here", nullptr) == "C:\\Users\\<user>\\x al was here");
    // Nothing to do: the text comes back as is.
    RCHECK(r.Apply("nothing here", nullptr) == "nothing here");
}

std::string const kModLog =
    "[01:40:44.592] [info] MEVR 0.1.0 (hot reload host, commit 1a0622c) attaching to LE1\n"
    "[01:40:44.594] [info] game version 2.0.0.48602 OK; 42 of 42 addresses at their RVAs\n"
    "[01:40:55.654] [info] calibration: headset system \"SteamVR/OpenXR : null\"\n"
    "[01:40:55.654] [info] colour: headset: runtime 'SteamVR/OpenXR', system 'SteamVR/OpenXR : playstation_vr2', vendor 0x28DE -> "
    "[Colour.x]; automatic: neutral\n"
    "[01:40:58.947] [warn] first-chance access violation 0xC0000005 on thread 96732 at LE1MEVR.asi+0x2C4866 (reading 0x2E03C62F088)\n"
    "[01:40:59.000] [warn] first-chance access violation 0xC0000005 on thread 96732 at LE1MEVR.asi+0x2C4866 (reading 0x2E0484F3058)\n"
    "[01:40:59.001] [warn] first-chance access violation 0xC0000005 on thread 1234 at LE1MEVR.asi+0x2C4866 (reading 0x2E005E48048)\n"
    "[01:40:59.331] [warn] first-chance access violation 0xC0000005 on thread 96732 at LE1MEVR.asi+0x24C5AD (reading 0x48)\n"
    "[01:40:59.828] [info] colour: profile auto (= neutral) for 'SteamVR/OpenXR : null' [Colour.steamvr_openxr_null]: neutral grade\n"
    "[01:41:00.100] [error] reflect: Engine.Foo.Bar not found\n"
    "[01:41:02.000] [info] halcyon bridge: the runtime's interaction profile: left /interaction_profiles/sony/vr2_controller, right "
    "/interaction_profiles/sony/vr2_controller (xrGetCurrentInteractionProfile)\n"
    "garbage line without brackets [error] nope\n";

void TestLogs() {
    RCHECK(report::ProblemKey("at LE1MEVR.asi+0x2C4866 (reading 0x2E03C62F088) thread 96732") ==
           report::ProblemKey("at LE1MEVR.asi+0x2C4866 (reading 0x48) thread 1"));
    RCHECK(report::ProblemKey("at X+0x2C4866") != report::ProblemKey("at X+0x24C5AD"));

    report::Problems const p = report::ExtractProblems(kModLog);
    RCHECK(p.errors == 1);
    RCHECK(p.warnings == 4);
    RCHECK(p.distinct == 3);
    RCHECK(p.items.size() == 3);
    if (p.items.size() == 3) {
        RCHECK(p.items[0].level == "error" && Has(p.items[0].text, "reflect: Engine.Foo.Bar"));
        RCHECK(p.items[1].count == 3 && Has(p.items[1].text, "+0x2C4866") && p.items[1].time == "01:40:58.947");
        RCHECK(p.items[2].count == 1 && Has(p.items[2].text, "+0x24C5AD"));
    }
    report::Problems const capped = report::ExtractProblems(kModLog, 1);
    RCHECK(capped.items.size() == 2 && capped.capped);

    std::string const halcyon =
        "2026-10-25 01:00:00.000 | old session line failed\n"
        "2026-10-26 01:40:44.597 | =====================================================\n"
        "2026-10-26 01:40:44.597 | MELEVR1 (Halcyon) inside ME VR Attempt's LE1MEVR.asi\n"
        "2026-10-26 01:40:44.600 | [XRRUNTIME] name='SteamVR/OpenXR' oculusFovQuirk=0\n"
        "2026-10-26 01:40:44.601 | [XR] recommended eye image 2824x2880; game render source 2400x2448; using eye swapchain\n"
        "2026-10-26 01:40:45.000 | [PANELDISC] class=BioSeqEvt_ArtPlaceableSkillGameFailed\n"
        "2026-10-26 01:40:46.000 | [XR] xrCreateSwapchain failed: -8\n"
        "2026-10-26 01:40:47.000 | [XR] xrCreateSwapchain failed: -9\n";
    std::string_view const session = report::LastHalcyonSession(halcyon);
    RCHECK(session.rfind("2026-10-26 01:40:44.597 | =====", 0) == 0);
    RCHECK(!Has(std::string(session), "old session"));
    RCHECK(report::LastHalcyonSession("no banner at all\n") == "no banner at all\n");
    RCHECK(report::LastHalcyonSession("a\nx | [XR] starting OpenXR session thread (y)\nb\n") == "x | [XR] starting OpenXR session thread (y)\nb\n");
    report::Problems const r = report::ExtractRendererProblems(session);
    RCHECK(r.errors == 2 && r.items.size() == 1 && r.items[0].count == 2);

    report::LogFacts const f = report::ReadLogFacts(kModLog, session);
    RCHECK(f.modVersion == "0.1.0");
    RCHECK(f.modBuild == "hot reload host, commit 1a0622c");
    RCHECK(f.commit == "1a0622c");
    RCHECK(f.gameVersion == "2.0.0.48602 OK");
    RCHECK(f.xrRuntime == "SteamVR/OpenXR");
    RCHECK(f.headsetSystem == "SteamVR/OpenXR : playstation_vr2");
    RCHECK(f.colourProfile == "auto (= neutral) for 'SteamVR/OpenXR : null' [Colour.steamvr_openxr_null]");
    RCHECK(f.interactionProfile == "left /interaction_profiles/sony/vr2_controller, right /interaction_profiles/sony/vr2_controller");
    RCHECK(f.eyeSize == "2824x2880");
    RCHECK(f.renderSize == "2400x2448");
}

std::string const kCrash =
    "==== MEVR crash breadcrumbs (research/36) ====\n"
    "2026-10-26 00:11:26.458  pid 20720  exe base 0x7FF709030000  host build Oct 25 2026 08:49:54\n"
    "exception 0xC0000005 (access violation) at ntdll.dll+0x1A12D, writing 0x24\n"
    "faulting thread: tid 8732; the game thread is 107332\n"
    "\n"
    "registers:\n"
    "  rip=0x00007FFD35E7A12D\n"
    "\n"
    "native stack (unwound from the fault):\n"
    "  [0] ntdll.dll+0x1A12D  (rsp 0x544498F610)\n"
    "  [1] MassEffect1.exe+0xD1C7A2  (rsp 0x544498F7D0)\n"
    "  [2] LE1MEVR.asi+0x2C4866  (rsp 0x544498F800)\n"
    "\n"
    "script frames (FFrames on this thread's stack, innermost first):\n"
    "  BioPawn.Tick at offset 12\n"
    "  SFXGame.Foo\n"
    "\n"
    "ProcessEvent: the last 128 dispatches\n";

void TestCrash() {
    report::Crash const c = report::ParseCrash(kCrash);
    RCHECK(c.present);
    RCHECK(c.when == "2026-10-26 00:11:26.458");
    RCHECK(c.exception == "0xC0000005 (access violation) at ntdll.dll+0x1A12D, writing 0x24");
    RCHECK(c.location == "ntdll.dll+0x1A12D");
    RCHECK(c.thread == "faulting thread: tid 8732; the game thread is 107332");
    RCHECK(c.nativeFrames.size() == 3 && c.nativeFrames[2] == "LE1MEVR.asi+0x2C4866");
    RCHECK(c.scriptFrames.size() == 2 && c.scriptFrames[0] == "BioPawn.Tick at offset 12");
    RCHECK(report::ParseCrash(kCrash, 2).nativeFrames.size() == 2);
    RCHECK(!report::ParseCrash("").present);
}

void TestSettings() {
    std::string const mod = "[Hands]\r\naimAssist = 1\r\nphysicalCrouch=1.0\r\n[Comfort]\r\nturn=snap\r\nsnapDeg=45.000\r\n[Sights]\r\nsniperRifle = scope 4\r\n";
    auto const d = report::DiffModIni(mod);
    RCHECK(d.size() == 2);
    if (d.size() == 2) {
        RCHECK(d[0].section == "Comfort" && d[0].key == "turn" && d[0].value == "snap" && d[0].defaultValue == "game");
        RCHECK(d[1].section == "Hands" && d[1].key == "aimAssist" && d[1].value == "1");
    }
    std::string const templateIni = mevr::install::ini::HalcyonReleaseIni();
    // The template itself: nothing differs.
    RCHECK(report::DiffHalcyonIni(templateIni, templateIni).empty());
    RCHECK(report::DiffModIni("[Menu]\nassets=\n").empty());  // empty is the default, not a difference
    std::string const halcyon = "[active]\nprofile=Custom 1\n[profile:Default]\nvrMode = 0\n[profile:Custom 1]\nvrMode = 2\nsfr2ResX = 2824\n";
    auto const h = report::DiffHalcyonIni(halcyon, templateIni);
    bool sawRes = false, sawMissing = false, sawMode = false;
    for (auto const& x : h) {
        sawRes = sawRes || (x.key == "sfr2ResX" && x.value == "2824" && x.defaultValue == "4096" && x.section == "profile:Custom 1");
        sawMissing = sawMissing || (x.key == "menuKey" && Has(x.value, "not in the file"));
        sawMode = sawMode || x.key == "vrMode";
    }
    RCHECK(sawRes && sawMissing && !sawMode);
}

report::Bundle SampleBundle() {
    report::Bundle b;
    b.system.setupVersion = "0.1.0 (commit 1a0622c)";
    b.system.runningMod = "0.1.0 (monolithic, commit 1a0622c)";
    b.system.gameVersion = "2.0.0.48602 (supported)";
    b.system.store = "Steam";
    b.system.windows = "Windows 11 Home 24H2 (build 26100.1)";
    b.system.gpu = "NVIDIA GeForce RTX 4090 (24 GB)";
    b.system.gpuDriver = "32.0.15.6094 (NVIDIA 560.94)";
    b.system.xrRuntime = "SteamVR (SteamVR/OpenXR)";
    b.system.headset = "SteamVR/OpenXR : playstation_vr2 on SteamVR/OpenXR";
    b.problems = report::ExtractProblems(kModLog);
    b.crash = report::ParseCrash(kCrash);
    b.diffs = report::DiffModIni("[Hands]\naimAssist=1\n");
    report::File f;
    f.name = "logs/LE1MEVR.log";
    f.bytes = kModLog;
    b.files.push_back(f);
    b.missing.push_back("logs/MEVRLoader.log");
    return b;
}

void TestReportMd() {
    report::Bundle b = SampleBundle();
    std::string const md = report::BuildReportMd(b);
    RCHECK(md.rfind("# MEVR problem report", 0) == 0);
    RCHECK(Has(md, report::kTroubleshootingUrl));
    RCHECK(Has(md, report::kRepoUrl));
    RCHECK(Has(md, report::kIssuesUrl));
    RCHECK(Has(md, "| GPU | NVIDIA GeForce RTX 4090 (24 GB) |"));
    RCHECK(Has(md, "### The game crashed"));
    RCHECK(Has(md, "Faulting module+offset: ntdll.dll+0x1A12D"));
    RCHECK(Has(md, "LE1MEVR.asi+0x2C4866"));
    RCHECK(Has(md, "BioPawn.Tick at offset 12"));
    RCHECK(Has(md, "[warn] x3 (first 01:40:58.947)"));
    RCHECK(Has(md, "LE1MEVR.ini [Hands] aimAssist = 1 (default 0)"));
    RCHECK(Has(md, "- logs/LE1MEVR.log ("));
    RCHECK(Has(md, "Not found on this PC"));
    // The prompt closes it.
    std::size_t const prompt = md.find("I'm reporting a problem with MEVR, a VR mod for Mass Effect Legendary Edition.");
    RCHECK(prompt != std::string::npos && md.find("Ask me what I was doing when it happened.", prompt) != std::string::npos);
    RCHECK(md.size() < 8 * 1024);

    // Thousands of distinct lines still fit the budget, prompt included.
    std::string big;
    for (int i = 0; i < 3000; ++i) {
        big += std::format("[01:00:00.000] [error] distinct problem number {} in module {}\n", i, std::string(1, static_cast<char>('a' + i % 26)) +
                                                                                                std::string(200, static_cast<char>('a' + (i / 26) % 26)));
    }
    b.problems = report::ExtractProblems(big, 1000);
    b.previousProblems = report::ExtractProblems(big, 1000);
    std::string const bigMd = report::BuildReportMd(b);
    RCHECK(bigMd.size() <= report::kReportMdBudget);
    RCHECK(Has(bigMd, "Ask me what I was doing when it happened."));

    b = SampleBundle();
    RCHECK(report::Headline(b) == "Crash: 0xC0000005 (access violation) at ntdll.dll+0x1A12D");
    b.crash.stale = true;  // a crash before the last two sessions isn't the headline, and is labelled as earlier
    RCHECK(report::Headline(b) == "Error: reflect: Engine.Foo.Bar not found");
    RCHECK(Has(report::BuildReportMd(b), "### An earlier crash"));
    b.crash = {};
    RCHECK(report::Headline(b) == "Error: reflect: Engine.Foo.Bar not found");
    std::string const url = report::IssueUrl(b);
    RCHECK(url.rfind("https://github.com/dnehring/MEVR/issues/new?template=bug.yml&title=", 0) == 0);
    RCHECK(Has(url, "&version=0.1.0%20%28monolithic%2C%20commit%201a0622c%29"));
    RCHECK(Has(url, "&summary=MEVR%3A%20"));
    RCHECK(url.size() < 8000);
    RCHECK(report::UrlEncode("a b/c?d=é") == "a%20b%2Fc%3Fd%3D%C3%A9");
    b.system.gpu = std::string(20000, 'x');  // a summary too long for a URL is cut
    RCHECK(report::IssueUrl(b).size() < 8000);
}

void TestMarks() {
    namespace marks = mevr::install::report_marks;
    using namespace std::chrono_literals;
    marks::Time const t0{};
    RCHECK(!marks::CrashNewer(std::nullopt, std::nullopt, std::nullopt));
    RCHECK(marks::CrashNewer(t0 + 10s, std::nullopt, std::nullopt));
    RCHECK(!marks::CrashNewer(t0 + 10s, t0 + 11s, std::nullopt));
    RCHECK(!marks::CrashNewer(t0 + 10s, std::nullopt, t0 + 10s));
    RCHECK(marks::CrashNewer(t0 + 10s, t0 + 5s, t0 + 6s));
}

} // namespace

void RunReportTests(int& failures) {
    gReportFailures = &failures;
    TestZip();
    TestRedaction();
    TestLogs();
    TestCrash();
    TestSettings();
    TestReportMd();
    TestMarks();
}
