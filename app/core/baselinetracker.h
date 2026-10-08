#pragma once
#include <chrono>
#include <optional>

namespace f20app {

// Tracks baseline age against the thresholds of spec §6.2:
// green = fresh, yellow = aging (> warn), red = stale (> block, measuring
// blocked).
enum class BaselineStatus { None, Fresh, Aging, Stale };

class BaselineTracker {
public:
    // system_clock, not steady_clock: the age must keep counting while the
    // PC sleeps and across app restarts, and the commit time is stored in
    // the database as a wall-clock time. NI-DAQmx keeps its calibration
    // time with the device the same way (self_cal_last_date_and_time).
    using Clock = std::chrono::system_clock;

    void setThresholds(int warnMinutes, int blockMinutes);

    // A real commit - or a commit time restored from the database. Never
    // call it for a baseline of unknown age: that would make it look fresh.
    void committed(Clock::time_point when = Clock::now());
    void invalidate();

    bool hasBaseline() const { return committedAt_.has_value(); }
    std::optional<Clock::time_point> committedAt() const { return committedAt_; }
    std::optional<int> ageMinutes(Clock::time_point now = Clock::now()) const;
    BaselineStatus status(Clock::time_point now = Clock::now()) const;

private:
    std::optional<Clock::time_point> committedAt_;
    int warnMinutes_ = 20;  // defaults per §6.2 (thin-film rule)
    int blockMinutes_ = 30;
};

// What to do with the baseline when the bridge (re)connects (spec 2.1 #4).
// FILMeasure knows whether it has an active baseline, not how old it is;
// the age always comes from our own stored commit, and an unknown age
// never passes as fresh.
enum class StartupBaseline {
    restoreAge,   // FILMeasure has one and we stored its commit: use that age
    unknownAge,   // FILMeasure has one, but we have no commit: run the baseline
    offerRecover, // FILMeasure lost it, our commit is within the limits: offer to recover
    storedTooOld, // FILMeasure lost it, our commit is stale: run the baseline
    runWizard,    // nothing to restore
};

// storedCommit: the status of our last valid commit under the current
// limits, nullopt when none is stored.
StartupBaseline startupBaseline(bool bridgeHasBaseline, std::optional<BaselineStatus> storedCommit);

} // namespace f20app
