#include "warmup.h"

namespace f20app {

void WarmUpTimer::start(Clock::time_point poweredOnAt) {
    startedAt_ = poweredOnAt;
    skipped_ = false;
}

void WarmUpTimer::setRequiredMinutes(int minutes) {
    requiredMinutes_ = minutes < 0 ? 0 : minutes;
}

void WarmUpTimer::skip() {
    skipped_ = true;
}

int WarmUpTimer::minutesLeft(Clock::time_point now) const {
    if (skipped_ || requiredMinutes_ == 0)
        return 0;
    if (!startedAt_)
        return requiredMinutes_; // never started: the safe side
    // The clock was set back: the time since power-on is unknown, so the
    // full warm-up is left (the same rule as BaselineTracker::status).
    const auto elapsed = now < *startedAt_ ? Clock::duration::zero() : now - *startedAt_;
    const auto left = std::chrono::minutes(requiredMinutes_) - elapsed;
    if (left <= Clock::duration::zero())
        return 0;
    const auto wholeMinutes = std::chrono::ceil<std::chrono::minutes>(left);
    return static_cast<int>(wholeMinutes.count());
}

bool WarmUpTimer::isDone(Clock::time_point now) const {
    return minutesLeft(now) == 0;
}

} // namespace f20app
