#include "doctest.h"
#include "appstatemachine.h"
#include "baselinetracker.h"

using namespace f20app;
using namespace std::chrono_literals;

// Compare enums as ints: doctest's stringifier collides with our own
// toString(AppState) found by argument-dependent lookup.
#define CHECK_ENUM_EQ(a, b) CHECK(static_cast<int>(a) == static_cast<int>(b))

TEST_CASE("state machine follows spec 8.1") {
    AppStateMachine machine;
    CHECK_ENUM_EQ(machine.state(), AppState::Starting);
    CHECK_FALSE(machine.canMeasure());

    machine.onBridgeUp();
    CHECK_ENUM_EQ(machine.state(), AppState::NoBaseline);

    machine.onBaselineValid();
    CHECK_ENUM_EQ(machine.state(), AppState::Ready);
    CHECK(machine.canMeasure());

    machine.onMeasureStarted();
    CHECK_ENUM_EQ(machine.state(), AppState::Measuring);
    CHECK_FALSE(machine.canMeasure());

    machine.onMeasureFinished();
    CHECK_ENUM_EQ(machine.state(), AppState::Ready);

    machine.onBaselineInvalid(); // stale -> back to NoBaseline
    CHECK_ENUM_EQ(machine.state(), AppState::NoBaseline);
}

TEST_CASE("bridge death is Fault from anywhere; bridge up recovers to NoBaseline") {
    AppStateMachine machine;
    machine.onBridgeUp();
    machine.onBaselineValid();
    machine.onBridgeDown();
    CHECK_ENUM_EQ(machine.state(), AppState::Fault);

    machine.onBridgeUp(); // restart succeeded
    CHECK_ENUM_EQ(machine.state(), AppState::NoBaseline);
}

TEST_CASE("illegal inputs are ignored, state cannot be corrupted") {
    AppStateMachine machine;
    machine.onMeasureStarted(); // not Ready: ignored
    CHECK_ENUM_EQ(machine.state(), AppState::Starting);
    machine.onBaselineValid(); // not NoBaseline: ignored
    CHECK_ENUM_EQ(machine.state(), AppState::Starting);
}

TEST_CASE("change handler fires with from/to") {
    AppStateMachine machine;
    AppState seenFrom{}, seenTo{};
    machine.setChangeHandler([&](AppState from, AppState to) {
        seenFrom = from;
        seenTo = to;
    });
    machine.onBridgeUp();
    CHECK_ENUM_EQ(seenFrom, AppState::Starting);
    CHECK_ENUM_EQ(seenTo, AppState::NoBaseline);
}

TEST_CASE("baseline tracker: fresh / aging / stale per spec 7.2") {
    BaselineTracker tracker;
    tracker.setThresholds(20, 30);
    const auto t0 = BaselineTracker::Clock::now();

    CHECK_ENUM_EQ(tracker.status(t0), BaselineStatus::None);

    tracker.committed(t0);
    CHECK_ENUM_EQ(tracker.status(t0 + 5min), BaselineStatus::Fresh);
    CHECK_ENUM_EQ(tracker.status(t0 + 21min), BaselineStatus::Aging);
    CHECK_ENUM_EQ(tracker.status(t0 + 31min), BaselineStatus::Stale);
    CHECK(*tracker.ageMinutes(t0 + 21min) == 21);

    tracker.invalidate();
    CHECK_ENUM_EQ(tracker.status(t0 + 21min), BaselineStatus::None);
}
