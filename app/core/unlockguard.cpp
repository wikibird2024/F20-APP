#include "unlockguard.h"

namespace f20app {

int UnlockGuard::waitSeconds(Clock::time_point now) const {
    if (wrongTries_ < kMaxWrongTries)
        return 0;
    const auto left = lastWrong_ + kWaitAfterWrong - now;
    if (left <= Clock::duration::zero())
        return 0;
    // Round up: "wait 1 s" until the very end, never "wait 0 s".
    return static_cast<int>(std::chrono::ceil<std::chrono::seconds>(left).count());
}

// The count is not reset after the wait: every further wrong try waits
// again, until the right password is given.
void UnlockGuard::wrongPassword(Clock::time_point now) {
    ++wrongTries_;
    lastWrong_ = now;
}

void UnlockGuard::unlocked(Clock::time_point now) {
    wrongTries_ = 0;
    unlocked_ = true;
    lastInput_ = now;
}

void UnlockGuard::touched(Clock::time_point now) {
    if (isUnlocked(now))
        lastInput_ = now;
}

void UnlockGuard::lock() { unlocked_ = false; }

bool UnlockGuard::isUnlocked(Clock::time_point now) const {
    return unlocked_ && now - lastInput_ < kIdleLock;
}

} // namespace f20app
