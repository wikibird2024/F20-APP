#pragma once
#include <chrono>
#include <optional>

namespace f20app {

// Lamp warm-up after power-on (spec 2.2 #4, 6.2): a baseline taken with a
// cold lamp drifts, so the baseline waits 15 min for thin films and n&k
// recipes, 5 min otherwise. The operator may skip it, with a reason that
// the app logs.
class WarmUpTimer {
public:
    // Same clock as BaselineTracker: wall-clock time, so a PC that sleeps
    // still counts.
    using Clock = std::chrono::system_clock;

    // Power-on time. The app starts with Windows, so its start time is the
    // best guess the software has.
    void start(Clock::time_point poweredOnAt = Clock::now());
    // From the selected recipe's limits; may change while warming up.
    void setRequiredMinutes(int minutes);
    // Skipped by the operator; stays skipped until start() again.
    void skip();

    bool isSkipped() const { return skipped_; }
    bool isDone(Clock::time_point now = Clock::now()) const;
    // Whole minutes left, rounded up; 0 when done.
    int minutesLeft(Clock::time_point now = Clock::now()) const;

private:
    std::optional<Clock::time_point> startedAt_;
    int requiredMinutes_ = 15;
    bool skipped_ = false;
};

} // namespace f20app
