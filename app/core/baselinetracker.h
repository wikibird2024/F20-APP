#pragma once
#include <chrono>
#include <optional>

namespace f20app {

// Tracks baseline age against the per-recipe thresholds of spec §7.2:
// green = fresh, yellow = aging (warn), red = stale (measuring blocked).
enum class BaselineStatus { None, Fresh, Aging, Stale };

class BaselineTracker {
public:
    using Clock = std::chrono::steady_clock;

    void setThresholds(int warnMinutes, int blockMinutes);

    void committed(Clock::time_point when = Clock::now());
    void invalidate();

    bool hasBaseline() const { return committedAt_.has_value(); }
    std::optional<int> ageMinutes(Clock::time_point now = Clock::now()) const;
    BaselineStatus status(Clock::time_point now = Clock::now()) const;

private:
    std::optional<Clock::time_point> committedAt_;
    int warnMinutes_ = 20;  // defaults per §7.2 (thin-film rule)
    int blockMinutes_ = 30;
};

} // namespace f20app
