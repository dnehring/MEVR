// SPDX-License-Identifier: MIT
// mevr-hair-bench: the hair simulation (core/ik/HairChain) run offline over one of our generated clips (tools/motion) on a
// body's skeleton, no game: parameter sweeps in seconds (docs/trilogy-hair-plan.md, tools/motion/README.md).
//
//   mevr-hair-bench --clip <name.mevrclip.json> --skeleton <mesh.tsv> [--fps 72] [--seconds 8] [--hold 2]
//                   [--stiffness 0.06,0.15] [--damping 0.1,0.2] [--inertia 1,0.6] [--bend 50] [--gravity 0]
//                   [--rates 30,72,120] [--csv out.csv]
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
