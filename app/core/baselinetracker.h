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

} // namespace f20app
