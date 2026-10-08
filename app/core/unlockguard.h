#pragma once
#include <chrono>

namespace f20app {

// The Settings screen's lock rules (spec §6.7, IEC 62443 SR 1.11 and an
// idle log-out like WinCC's user timeout): after kMaxWrongTries wrong
// passwords in a row the next try waits kWaitAfterWrong; an unlocked
// screen locks again after kIdleLock without input. The password check
// itself is EngineerPassword's; this class only keeps the counts and times.
class UnlockGuard {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr int kMaxWrongTries = 5;
    static constexpr std::chrono::seconds kWaitAfterWrong{30};
    static constexpr std::chrono::minutes kIdleLock{10};

    // Seconds until the next try is allowed; 0 = try now.
    int waitSeconds(Clock::time_point now) const;

    void wrongPassword(Clock::time_point now);
    void unlocked(Clock::time_point now); // right password
    void touched(Clock::time_point now);  // any input while unlocked
    void lock();

    // False when locked by hand or after kIdleLock without input.
    bool isUnlocked(Clock::time_point now) const;

private:
    int wrongTries_ = 0;
    Clock::time_point lastWrong_{};
    bool unlocked_ = false;
    Clock::time_point lastInput_{};
};

} // namespace f20app
