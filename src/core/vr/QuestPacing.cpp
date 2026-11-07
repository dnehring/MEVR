// SPDX-License-Identifier: MIT
#include "core/vr/QuestPacing.hpp"
#include "core/Ini.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace mevr::vr::quest_pacing {
std::vector<Action> Actions(std::string_view value, bool streamed) {
    std::string text = ini::Lower(ini::Trim(value));
    if (text.empty() || text == "auto")
        return streamed ? std::vector<Action>{Action::Refresh, Action::RefreshCycle, Action::Performance} : std::vector<Action>{};
    if (text == "off") return {};
    std::vector<Action> out;
    while (!text.empty()) {
        auto const split = text.find(',');
        auto const token = ini::Trim(text.substr(0, split));
        Action a;
        if (token == "refresh") a = Action::Refresh;
        else if (token == "refresh-cycle") a = Action::RefreshCycle;
        else if (token == "performance") a = Action::Performance;
        else return {};
        if (std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
        if (split == std::string::npos) break;
        text.erase(0, split + 1);
        if (ini::Trim(text).empty()) return {};
    }
    return out;
}
char const* Name(Action a) {
    switch (a) {
    case Action::Refresh: return "refresh";
    case Action::RefreshCycle: return "refresh-cycle";
    default: return "performance";
    }
}
void Detector::Suspend() {
    readings_.clear();
    healthySince_ = 0;
    // A focus change is not proof of recovery. Keep an episode until healthy frames confirm it.
}
Result Detector::Step(Sample const& s, unsigned maxTries, bool force) {
    if (s.level != 0 && s.level != level_) {
        level_ = s.level;
        tries_ = 0;
        stuck_ = gaveUp_ = false;
        Suspend(); // retain lastTry_: even changing levels cannot bypass the 10 s cooldown
    }
    Result r;
    r.tries = tries_;
    bool const valid = std::isfinite(s.now) && std::isfinite(s.interval) && std::isfinite(s.work) &&
        std::isfinite(s.period) && s.interval > 0 && s.interval <= .250 && s.work >= 0 && s.period > .001 && s.period < .050;
    if (!valid || !s.eligible) {
        Suspend();
        if (!force) return r;
    } else {
        if (!readings_.empty() && s.now <= readings_.back().time) Suspend();
        readings_.push_back({s.now, s.interval, s.work / s.period, s.period});
        while (readings_.size() > 1 && s.now - readings_[1].time >= 3.0) readings_.pop_front();
        if (readings_.size() > 4096) { Suspend(); return r; }
        double intervals = 0, periods = 0;
        std::vector<double> work;
        work.reserve(readings_.size());
        for (auto const& v : readings_) { intervals += v.interval; periods += v.period; work.push_back(v.work); }
        r.rate = periods / intervals;
        auto const p90 = static_cast<std::size_t>(std::ceil(.9 * static_cast<double>(work.size()))) - 1;
        std::nth_element(work.begin(), work.begin() + static_cast<std::ptrdiff_t>(p90), work.end());
        r.workP90 = work[p90];
        bool const window = s.now - readings_.front().time >= 3.0 - 1e-6;
        if (stuck_ && window && r.rate >= .85) {
            if (healthySince_ == 0) healthySince_ = s.now;
            if (s.now - healthySince_ >= 1.0) {
                stuck_ = false;
                r.event = Event::Recovered;
                r.elapsed = s.now - entered_;
                return r;
            }
        } else healthySince_ = 0;
        if (!force && (!window || r.rate > .6 || r.workP90 > .5)) return r;
    }
    if (!stuck_) {
        stuck_ = true;
        entered_ = s.now;
        r.event = Event::Entering;
        return r;
    }
    r.elapsed = s.now - entered_;
    if (s.now - lastTry_ < 10.0) return r;
    if (tries_ >= maxTries) {
        if (!gaveUp_) { gaveUp_ = true; r.event = Event::GaveUp; }
        return r;
    }
    lastTry_ = s.now;
    ++tries_;
    r.tries = tries_;
    r.event = Event::Recovering;
    return r;
}
} // namespace mevr::vr::quest_pacing
