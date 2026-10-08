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
    return age.count() < 0 ? 0 : static_cast<int>(age.count());
}

BaselineStatus BaselineTracker::status(Clock::time_point now) const {
    if (!committedAt_)
        return BaselineStatus::None;
    const auto age = now - *committedAt_;
    // The clock was set back: the true age is unknown, so treat it as too old.
    if (age < Clock::duration::zero())
        return BaselineStatus::Stale;
    if (age > std::chrono::minutes(blockMinutes_))
        return BaselineStatus::Stale;
    if (age > std::chrono::minutes(warnMinutes_))
        return BaselineStatus::Aging;
    return BaselineStatus::Fresh;
}

StartupBaseline startupBaseline(bool bridgeHasBaseline, std::optional<BaselineStatus> storedCommit) {
    const bool haveCommit = storedCommit && *storedCommit != BaselineStatus::None;
    if (bridgeHasBaseline)
        return haveCommit ? StartupBaseline::restoreAge : StartupBaseline::unknownAge;
    if (!haveCommit)
        return StartupBaseline::runWizard;
    return *storedCommit == BaselineStatus::Stale ? StartupBaseline::storedTooOld
                                                  : StartupBaseline::offerRecover;
}

} // namespace f20app
