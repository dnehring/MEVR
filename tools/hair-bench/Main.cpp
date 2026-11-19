// SPDX-License-Identifier: MIT
// mevr-hair-bench: the hair simulation (core/ik/HairChain) run offline over one of our generated clips (tools/motion) on a
// body's skeleton, no game: parameter sweeps in seconds (docs/trilogy-hair-plan.md, tools/motion/README.md).
//
//   mevr-hair-bench --clip <name.mevrclip.json> --skeleton <mesh.tsv> [--fps 72] [--seconds 8] [--hold 2]
//                   [--stiffness 0.06,0.15] [--damping 0.1,0.2] [--inertia 1,0.6] [--bend 50] [--gravity 0]
//                   [--rates 30,72,120] [--csv out.csv]
//   mevr-hair-bench --jiggle 1 --clip ... --skeleton ... [--intensity 0.5,1] [--rates 30,60,120] [--trace out.csv]
//                   soft-tissue jiggle (core/ik/Jiggle) on stand-in bones instead: per bone the offset's range per axis,
//                   the peak, the settle after the clip stops, clamp hits, the RMS difference between frame rates
//
// The body walks the clip's loop for --seconds (travel included: the root moves forward at the clip's own pace), then stands
// still in its last pose for --hold seconds. Per parameter set: the strands' mean and peak swing while walking, the time to
// settle within 1 cm after it stops, the worst stretch, the jitter (the tip's second difference per step), the collision
// pushes, and how far the tips drift between frame rates (--rates: the same run at each, the tips' worst distance from the
// first rate's, as offsets from the animation). Skeleton TSVs are the player's game data: they stay local.

#include "core/Json.hpp"
#include "core/ik/Clip.hpp"
#include "core/ik/HairChain.hpp"
#include "core/ik/HandContact.hpp"
#include "core/ik/Jiggle.hpp"
#include "core/ik/Rigid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace ik = mevr::ik;
namespace hair = mevr::ik::hair;
namespace hc = mevr::ik::hand_contact;

struct Skeleton {
    std::vector<std::string> names;
    std::vector<int> parents;
    std::vector<ik::Transform> rest;  // locals, game units
};

bool LoadSkeleton(std::string const& path, Skeleton& out) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream f(line);
        std::string index, name, parent;
        double tx, ty, tz, qx, qy, qz, qw;
        std::getline(f, index, '\t');
        std::getline(f, name, '\t');
        std::getline(f, parent, '\t');
        f >> tx >> ty >> tz >> qx >> qy >> qz >> qw;
        out.names.push_back(name);
        out.parents.push_back(std::stoi(parent));
        out.rest.push_back({ik::Normalize(ik::Quat{qx, qy, qz, qw}), {tx, ty, tz}, 1.0});
    }
    return !out.names.empty();
}

std::vector<double> List(std::string const& text) {
    std::vector<double> v;
    std::stringstream s(text);
    std::string item;
    while (std::getline(s, item, ',')) {
        if (!item.empty()) {
            v.push_back(std::stod(item));
        }
    }
    return v;
}

struct Run {
    double meanSwingCm = 0, peakSwingCm = 0, settleS = -1, worstStretchMm = 0, jitterMm = 0;
    long long pushes = 0;
    std::vector<std::vector<ik::Vec3>> tips;  // per step, per chain: the tip's offset from the animation
};

Run Simulate(Skeleton const& sk, ik::clip::Clip const& clip, hair::Settings const& s, double fps, double seconds, double hold) {
    Run run;
    std::vector<hair::Chain> chains = hair::Discover(sk.names, sk.parents);
    std::vector<hc::BonePart> parts;
    for (hc::BonePart const& p : hc::PartsFor(sk.names, sk.parents)) {
        if (p.part != hc::Part::Thigh && p.part != hc::Part::Shin && p.part != hc::Part::Foot) {
            parts.push_back(p);
        }
    }
    std::map<std::string, int> index;
    for (std::size_t i = 0; i < sk.names.size(); ++i) {
        index[sk.names[i]] = static_cast<int>(i);
    }
    std::vector<int> mapped;
    for (std::string const& b : clip.bones) {
        mapped.push_back(index.count(b) ? index[b] : -1);
    }
    int const root = index.count("Root") ? index["Root"] : -1;
    double const pace = clip.LoopSeconds() > 0 ? clip.TravelPerLoop() / clip.LoopSeconds() : 0.0;  // game units per second
    double const dt = 1.0 / fps;
    int const steps = static_cast<int>(std::lround((seconds + hold) * fps));
    std::vector<ik::Transform> local = sk.rest;
    std::vector<ik::Transform> world(sk.names.size());
    std::vector<ik::Transform> metres(sk.names.size());
    double swingSum = 0;
    long long swingCount = 0;
    double jitterSum = 0;
    long long jitterCount = 0;
    double lastMovingT = 0;
    std::vector<ik::Vec3> prevTip(chains.size()), prevPrevTip(chains.size());
    for (int k = 0; k <= steps; ++k) {
        double const t = k * dt;
        double const walkT = std::min(t, seconds);
        ik::clip::Sample const sample = ik::clip::At(clip, walkT);
        local = sk.rest;
        for (std::size_t m = 0; m < mapped.size(); ++m) {
            if (mapped[m] >= 0) {
                local[static_cast<std::size_t>(mapped[m])].rotation = sample.locals[m];
            }
        }
        for (std::size_t i = 0; i < sk.names.size(); ++i) {
            int const p = sk.parents[i];
            world[i] = p >= 0 ? ik::Compose(local[i], world[static_cast<std::size_t>(p)]) : local[i];
            if (static_cast<int>(i) == root) {
                // Root at the clip's height and sway, travelling forward at its pace (+X), as the driver walks it.
                world[i].translation = {sample.root.x + pace * walkT, world[i].translation.y + sample.root.y, sample.root.z};
                local[i] = p >= 0 ? ik::Relative(world[i], world[static_cast<std::size_t>(p)]) : world[i];
            }
        }
        std::vector<ik::Vec3> positions(sk.names.size());
        for (std::size_t i = 0; i < sk.names.size(); ++i) {
            metres[i] = {world[i].rotation, world[i].translation * 0.01, 1.0};
            positions[i] = metres[i].translation;
        }
        std::vector<hc::Capsule> body;
        for (hc::BonePart const& p : parts) {
            if (auto const c = hc::CapsuleOf(p, positions, 1.0)) {
                body.push_back(*c);
            }
        }
        hair::Input in;
        in.animated = metres;
        in.body = body;
        in.dt = k == 0 ? 0.0 : dt;
        std::vector<ik::Vec3> tips;
        double worstOff = 0;
        for (std::size_t c = 0; c < chains.size(); ++c) {
            hair::StepStats const st = hair::Step(chains[c], s, in);
            run.pushes += st.collisions;
            double const off = hair::TipOffset(chains[c]);
            worstOff = std::max(worstOff, off);
            if (t <= seconds) {
                swingSum += off;
                ++swingCount;
                run.peakSwingCm = std::max(run.peakSwingCm, off * 100);
            }
            for (std::size_t i = 1; i < chains[c].pos.size(); ++i) {
                double const ls = ik::Length(chains[c].pos[i] - chains[c].pos[i - 1]);
                double const la = ik::Length(chains[c].target[i] - chains[c].target[i - 1]);
                run.worstStretchMm = std::max(run.worstStretchMm, std::abs(ls - la) * 1000);
            }
            ik::Vec3 const tip = hair::Shown(chains[c], chains[c].pos.size() - 1) - chains[c].target.back();  // its offset, as drawn
            tips.push_back(tip);  // the offset, not the place: runs at different rates sample the walk at slightly other instants
            if (k >= 2) {
                ik::Vec3 const accel = tip - prevTip[c] * 2.0 + prevPrevTip[c];
                jitterSum += ik::Length(accel) * 1000;
                ++jitterCount;
            }
            prevPrevTip[c] = prevTip[c];
            prevTip[c] = tip;
        }
        run.tips.push_back(std::move(tips));
        if (t > seconds && worstOff >= 0.01) {
            lastMovingT = t;
        }
    }
    run.meanSwingCm = swingCount ? swingSum / swingCount * 100 : 0;
    run.jitterMm = jitterCount ? jitterSum / jitterCount : 0;
    run.settleS = lastMovingT > 0 ? lastMovingT - seconds : 0.0;
    return run;
}

// ---- Jiggle (research/trilogy-jiggle.md): stand-in bones on the body, springs over the clip --------------------------

namespace jiggle = mevr::ik::jiggle;

// A stand-in jiggle bone: its parent, its pivot at rest in component space (game units), the frame's turn from the body's
// axes (a glute's depth axis points backward: 180 degrees about up), which settings.
struct StandIn {
    char const* name;
    char const* parent;
    ik::Vec3 pivotRest;
    double yawDeg;
    bool glute;
};
constexpr StandIn kStandIns[] = {
    {"BreastLeft", "Chest2", {4.0, -9.0, 129.0}, 0.0, false},
    {"BreastRight", "Chest2", {4.0, 9.0, 129.0}, 0.0, false},
    {"GluteLeft", "Pelvis", {-7.0, -8.0, 88.0}, 180.0, true},
    {"GluteRight", "Pelvis", {-7.0, 8.0, 88.0}, 180.0, true},
};

struct JiggleRun {
    // Per stand-in: the shown offset's range per axis (cm) while the clip plays, its peak (cm), the time to settle within 5%
    // of that peak after the clip stops (s), clamp hits, the vertical offset per step (the rate check), the whole trace.
    std::vector<ik::Vec3> lo, hi;
    std::vector<double> peak, settle;
    std::vector<int> clampHits;
    std::vector<std::vector<double>> vertical;
    std::vector<std::vector<ik::Vec3>> trace;
};

JiggleRun SimulateJiggle(Skeleton const& sk, ik::clip::Clip const& clip, double intensity, double fps, double seconds, double hold) {
    std::map<std::string, int> index;
    for (std::size_t i = 0; i < sk.names.size(); ++i) {
        index[sk.names[i]] = static_cast<int>(i);
    }
    std::vector<int> mapped;
    for (std::string const& b : clip.bones) {
        mapped.push_back(index.count(b) ? index[b] : -1);
    }
    int const root = index.count("Root") ? index["Root"] : -1;
    double const pace = clip.LoopSeconds() > 0 ? clip.TravelPerLoop() / clip.LoopSeconds() : 0.0;
    // Rest: each stand-in's pivot in its parent's frame, and the frame's turn relative to the parent's rest rotation.
    std::vector<ik::Transform> rest(sk.names.size());
    for (std::size_t i = 0; i < sk.names.size(); ++i) {
        int const p = sk.parents[i];
        rest[i] = p >= 0 ? ik::Compose(sk.rest[i], rest[static_cast<std::size_t>(p)]) : sk.rest[i];
    }
    std::size_t const n = std::size(kStandIns);
    std::vector<int> parent(n);
    std::vector<ik::Vec3> pivotLocal(n);
    std::vector<ik::Quat> align(n);
    std::vector<jiggle::State> states(n);
    std::vector<jiggle::Settings> settings(n);
    for (std::size_t j = 0; j < n; ++j) {
        parent[j] = index.at(kStandIns[j].parent);
        ik::Transform const& pr = rest[static_cast<std::size_t>(parent[j])];
        pivotLocal[j] = ik::Rotate(ik::Conjugate(pr.rotation), (kStandIns[j].pivotRest - pr.translation) * 0.01);
        double const half = kStandIns[j].yawDeg * 3.14159265358979323846 / 360.0;
        align[j] = ik::Conjugate(pr.rotation) * ik::Quat{0.0, 0.0, std::sin(half), std::cos(half)};
        settings[j] = kStandIns[j].glute ? jiggle::Glute() : jiggle::Breast();
        settings[j].intensity = intensity;
    }
    JiggleRun run;
    run.lo.assign(n, {1e9, 1e9, 1e9});
    run.hi.assign(n, {-1e9, -1e9, -1e9});
    run.peak.assign(n, 0.0);
    run.settle.assign(n, 0.0);
    run.clampHits.assign(n, 0);
    run.vertical.assign(n, {});
    run.trace.assign(n, {});
    double const dt = 1.0 / fps;
    int const steps = static_cast<int>(std::lround((seconds + hold) * fps));
    std::vector<ik::Transform> local, world(sk.names.size());
    std::vector<double> lastBig(n, 0.0);
    for (int k = 0; k <= steps; ++k) {
        double const t = k * dt;
        double const walkT = std::min(t, seconds);
        ik::clip::Sample const sample = ik::clip::At(clip, walkT);
        local = sk.rest;
        for (std::size_t m = 0; m < mapped.size(); ++m) {
            if (mapped[m] >= 0) {
                local[static_cast<std::size_t>(mapped[m])].rotation = sample.locals[m];
            }
        }
        for (std::size_t i = 0; i < sk.names.size(); ++i) {
            int const p = sk.parents[i];
            world[i] = p >= 0 ? ik::Compose(local[i], world[static_cast<std::size_t>(p)]) : local[i];
            if (static_cast<int>(i) == root) {
                world[i].translation = {sample.root.x + pace * walkT, world[i].translation.y + sample.root.y, sample.root.z};
                local[i] = p >= 0 ? ik::Relative(world[i], world[static_cast<std::size_t>(p)]) : world[i];
            }
        }
        for (std::size_t j = 0; j < n; ++j) {
            ik::Transform const& pw = world[static_cast<std::size_t>(parent[j])];
            jiggle::Input in;
            in.frame.rotation = ik::Normalize(pw.rotation * align[j]);
            in.frame.translation = pw.translation * 0.01 + ik::Rotate(pw.rotation, pivotLocal[j]);
            in.dt = k == 0 ? 0.0 : dt;
            jiggle::Output const out = jiggle::Step(states[j], settings[j], in);
            ik::Vec3 const cm = out.shown * 100.0;
            run.trace[j].push_back(cm);
            run.vertical[j].push_back(cm.z);
            if (t <= seconds) {
                run.lo[j] = {std::min(run.lo[j].x, cm.x), std::min(run.lo[j].y, cm.y), std::min(run.lo[j].z, cm.z)};
                run.hi[j] = {std::max(run.hi[j].x, cm.x), std::max(run.hi[j].y, cm.y), std::max(run.hi[j].z, cm.z)};
            }
            double const mag = ik::Length(cm);
            run.peak[j] = std::max(run.peak[j], mag);
            if (t > seconds && mag > 0.05 * run.peak[j]) {
                lastBig[j] = t;
            }
        }
    }
    for (std::size_t j = 0; j < n; ++j) {
        run.settle[j] = lastBig[j] > 0 ? lastBig[j] - seconds : 0.0;
        run.clampHits[j] = states[j].clampHits;
    }
    return run;
}

int JiggleReport(Skeleton const& sk, ik::clip::Clip const& clip, std::map<std::string, std::string>& a) {
    double const fps = std::stod(a["--fps"]), seconds = std::stod(a["--seconds"]), hold = std::stod(a["--hold"]);
    std::vector<double> const rates = List(a["--rates"]);
    for (double intensity : List(a["--intensity"])) {
        JiggleRun const r = SimulateJiggle(sk, clip, intensity, fps, seconds, hold);
        std::vector<JiggleRun> others;
        for (double rate : rates) {
            others.push_back(SimulateJiggle(sk, clip, intensity, rate, seconds, hold));
        }
        std::printf("intensity %.2f, %.0f Hz:\n%12s | %8s %8s %8s | %7s %8s %7s %9s\n", intensity, fps, "bone", "depth cm", "side cm",
                    "up cm", "peak cm", "settle s", "clamps", "rate rms%");
        for (std::size_t j = 0; j < std::size(kStandIns); ++j) {
            // Frame rates: the vertical offset at common tenths of a second, RMS against the first rate, in % of its peak.
            double rms = -1;
            if (others.size() >= 2) {
                double sum = 0, peakRef = 1e-9;
                int count = 0;
                for (int tenth = 0; tenth <= static_cast<int>((seconds + hold) * 10); ++tenth) {
                    std::size_t const i0 = static_cast<std::size_t>(std::lround(tenth / 10.0 * rates[0]));
                    if (i0 >= others[0].vertical[j].size()) {
                        continue;
                    }
                    peakRef = std::max(peakRef, std::abs(others[0].vertical[j][i0]));
                    for (std::size_t o = 1; o < others.size(); ++o) {
                        std::size_t const io = static_cast<std::size_t>(std::lround(tenth / 10.0 * rates[o]));
                        if (io < others[o].vertical[j].size()) {
                            double const d = others[o].vertical[j][io] - others[0].vertical[j][i0];
                            sum += d * d;
                            ++count;
                        }
                    }
                }
                rms = count ? std::sqrt(sum / count) / peakRef * 100 : -1;
            }
            std::printf("%12s | %8.2f %8.2f %8.2f | %7.2f %8.2f %7d %9.1f\n", kStandIns[j].name, r.hi[j].x - r.lo[j].x,
                        r.hi[j].y - r.lo[j].y, r.hi[j].z - r.lo[j].z, r.peak[j], r.settle[j], r.clampHits[j], rms);
        }
        if (a.count("--trace")) {
            std::ofstream out(a["--trace"]);
            out << "t,bone,depth,side,up\n";
            for (std::size_t j = 0; j < std::size(kStandIns); ++j) {
                for (std::size_t k = 0; k < r.trace[j].size(); ++k) {
                    out << k / fps << ',' << kStandIns[j].name << ',' << r.trace[j][k].x << ',' << r.trace[j][k].y << ','
                        << r.trace[j][k].z << '\n';
                }
            }
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::map<std::string, std::string> a{{"--fps", "72"}, {"--seconds", "8"}, {"--hold", "2"}, {"--stiffness", "0.06"},
        {"--damping", "0.10"}, {"--inertia", "1.0"}, {"--bend", "50"}, {"--gravity", "0"}, {"--rates", ""}};
    for (int i = 1; i + 1 < argc; i += 2) {
        a[argv[i]] = argv[i + 1];
    }
    if (!a.count("--clip") || !a.count("--skeleton")) {
        std::fprintf(stderr, "mevr-hair-bench --clip <name.mevrclip.json> --skeleton <mesh.tsv> [--fps 72] [--seconds 8] [--hold 2]\n"
                             "  [--stiffness a,b] [--damping a,b] [--inertia a,b] [--bend 50] [--gravity 0] [--rates 30,72,120] [--csv out.csv]\n");
        return 2;
    }
    Skeleton sk;
    if (!LoadSkeleton(a["--skeleton"], sk)) {
        std::fprintf(stderr, "no skeleton in %s\n", a["--skeleton"].c_str());
        return 1;
    }
    std::ifstream in(a["--clip"], std::ios::binary);
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto const parsed = mevr::json::Parse(text, &error);
    auto const clip = parsed ? ik::clip::Load(*parsed, &error) : std::nullopt;
    if (!clip) {
        std::fprintf(stderr, "%s: %s\n", a["--clip"].c_str(), error.c_str());
        return 1;
    }
    if (a.count("--jiggle")) {
        if (!a.count("--intensity")) {
            a["--intensity"] = "0.5";
        }
        std::printf("%s: jiggle on stand-in bones (breasts on Chest2, glutes on Pelvis); loop %.2f s\n", clip->name.c_str(), clip->LoopSeconds());
        return JiggleReport(sk, *clip, a);
    }
    std::vector<hair::Chain> const chains = hair::Discover(sk.names, sk.parents);
    std::printf("%s on %zu bones, %zu hair chains; loop %.2f s, %.2f m/s\n", clip->name.c_str(), sk.names.size(), chains.size(),
                clip->LoopSeconds(), clip->LoopSeconds() > 0 ? clip->TravelPerLoop() / clip->LoopSeconds() / 100 : 0.0);
    double const fps = std::stod(a["--fps"]), seconds = std::stod(a["--seconds"]), hold = std::stod(a["--hold"]);
    std::vector<double> const rates = List(a["--rates"]);
    std::ofstream csv;
    if (a.count("--csv")) {
        csv.open(a["--csv"]);
        csv << "stiffness,damping,inertia,bend,gravity,meanSwingCm,peakSwingCm,settleS,worstStretchMm,jitterMm,pushes,rateDriftCm\n";
    }
    std::printf("%9s %8s %8s | %10s %10s %8s %10s %9s %8s %10s\n", "stiffness", "damping", "inertia", "mean cm", "peak cm",
                "settle s", "stretch mm", "jitter mm", "pushes", "rate cm");
    for (double stiffness : List(a["--stiffness"])) {
        for (double damping : List(a["--damping"])) {
            for (double inertia : List(a["--inertia"])) {
                hair::Settings s;
                s.stiffness = stiffness;
                s.damping = damping;
                s.inertia = inertia;
                s.maxBendDeg = std::stod(a["--bend"]);
                s.gravity = std::stod(a["--gravity"]);
                Run const r = Simulate(sk, *clip, s, fps, seconds, hold);
                double drift = -1;
                if (rates.size() >= 2) {
                    std::vector<Run> runs;
                    for (double rate : rates) {
                        runs.push_back(Simulate(sk, *clip, s, rate, seconds, hold));
                    }
                    drift = 0;
                    // Compare at whole tenths of a second, the tips of each chain.
                    for (int tenth = 0; tenth <= static_cast<int>((seconds + hold) * 10); ++tenth) {
                        for (std::size_t j = 1; j < runs.size(); ++j) {
                            std::size_t const i0 = static_cast<std::size_t>(std::lround(tenth / 10.0 * rates[0]));
                            std::size_t const ij = static_cast<std::size_t>(std::lround(tenth / 10.0 * rates[j]));
                            if (i0 >= runs[0].tips.size() || ij >= runs[j].tips.size()) {
                                continue;
                            }
                            for (std::size_t c = 0; c < runs[0].tips[i0].size(); ++c) {
                                drift = std::max(drift, ik::Length(runs[0].tips[i0][c] - runs[j].tips[ij][c]) * 100);
                            }
                        }
                    }
                }
                std::printf("%9.3f %8.3f %8.2f | %10.2f %10.2f %8.2f %10.2f %9.3f %8lld %10.2f\n", stiffness, damping, inertia,
                            r.meanSwingCm, r.peakSwingCm, r.settleS, r.worstStretchMm, r.jitterMm, r.pushes, drift);
                if (csv) {
                    csv << stiffness << ',' << damping << ',' << inertia << ',' << s.maxBendDeg << ',' << s.gravity << ','
                        << r.meanSwingCm << ',' << r.peakSwingCm << ',' << r.settleS << ',' << r.worstStretchMm << ',' << r.jitterMm
                        << ',' << r.pushes << ',' << drift << '\n';
                }
            }
        }
    }
    return 0;
}
