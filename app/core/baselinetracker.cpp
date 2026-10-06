#include "baselinetracker.h"

namespace f20app {

void BaselineTracker::setThresholds(int warnMinutes, int blockMinutes) {
    warnMinutes_ = warnMinutes;
    blockMinutes_ = blockMinutes;
}

void BaselineTracker::committed(Clock::time_point when) {
    committedAt_ = when;
}

void BaselineTracker::invalidate() {
    committedAt_.reset();
}

std::optional<int> BaselineTracker::ageMinutes(Clock::time_point now) const {
    if (!committedAt_)
        return std::nullopt;
    const auto age = std::chrono::duration_cast<std::chrono::minutes>(now - *committedAt_);
    return static_cast<int>(age.count());
}

BaselineStatus BaselineTracker::status(Clock::time_point now) const {
    const auto age = ageMinutes(now);
    if (!age)
        return BaselineStatus::None;
    if (*age >= blockMinutes_)
        return BaselineStatus::Stale;
    if (*age >= warnMinutes_)
        return BaselineStatus::Aging;
    return BaselineStatus::Fresh;
}

} // namespace f20app
