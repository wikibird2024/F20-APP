#include "doctest.h"
#include "appstatemachine.h"
#include "baselinetracker.h"
#include "devicecheck.h"
#include "filenames.h"
#include "measuregate.h"
#include "recipelimits.h"
#include "warmup.h"

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

    // Spec §6.2 says "> 30 min": exactly 30:00 is still aging.
    CHECK_ENUM_EQ(tracker.status(t0 + 20min), BaselineStatus::Fresh);
    CHECK_ENUM_EQ(tracker.status(t0 + 30min), BaselineStatus::Aging);
    CHECK_ENUM_EQ(tracker.status(t0 + 30min + 1s), BaselineStatus::Stale);

    tracker.invalidate();
    CHECK_ENUM_EQ(tracker.status(t0 + 21min), BaselineStatus::None);
}

TEST_CASE("a restored commit time keeps its real age; a clock set back is stale") {
    BaselineTracker tracker;
    tracker.setThresholds(20, 30);
    const auto now = BaselineTracker::Clock::now();

    tracker.committed(now - 29min); // restored from the database
    CHECK_ENUM_EQ(tracker.status(now), BaselineStatus::Aging);
    CHECK_ENUM_EQ(tracker.status(now + 2min), BaselineStatus::Stale);

    tracker.committed(now + 5min); // commit time in the future
    CHECK_ENUM_EQ(tracker.status(now), BaselineStatus::Stale);
    CHECK(*tracker.ageMinutes(now) == 0);
}

TEST_CASE("wizard: Baselining blocks measuring; commit -> Ready, cancel -> NoBaseline") {
    AppStateMachine machine;
    machine.onBridgeUp();
    CHECK(machine.canOpenBaselineWizard());
    machine.onBaselineWizardOpened();
    CHECK_ENUM_EQ(machine.state(), AppState::Baselining);
    CHECK_FALSE(machine.canMeasure());
    CHECK_FALSE(machine.canOpenBaselineWizard());
    machine.onBaselineWizardClosed(true);
    CHECK_ENUM_EQ(machine.state(), AppState::Ready);

    machine.onBaselineWizardOpened(); // redo from Ready, then cancel
    machine.onBaselineWizardClosed(false);
    CHECK_ENUM_EQ(machine.state(), AppState::NoBaseline);

    machine.onBaselineWizardOpened();
    machine.onBridgeDown(); // bridge lost while the wizard is open
    machine.onBaselineWizardClosed(true);
    CHECK_ENUM_EQ(machine.state(), AppState::Fault);
}

// MainWindow closes the wizard BEFORE it reports the bridge drop, because a
// close after the drop is ignored (case above). In this order a commit made
// before the drop survives: after reconnect the stored commit restores Ready.
TEST_CASE("bridge drop with the wizard open: closing it first keeps the commit") {
    AppStateMachine machine;
    machine.onBridgeUp();
    machine.onBaselineWizardOpened();

    machine.onBaselineWizardClosed(true); // committed, then the bridge drops
    machine.onBridgeDown();
    CHECK_ENUM_EQ(machine.state(), AppState::Fault);

    machine.onBridgeUp();
    machine.onBaselineValid(); // restoreBaselineAge() found the stored commit
    CHECK_ENUM_EQ(machine.state(), AppState::Ready);
}

TEST_CASE("re-analysis returns to the state it came from") {
    AppStateMachine machine;
    machine.onBridgeUp();
    machine.onAnalyzeStarted();
    CHECK_ENUM_EQ(machine.state(), AppState::Analyzing);
    machine.onAnalyzeFinished();
    CHECK_ENUM_EQ(machine.state(), AppState::NoBaseline);

    machine.onBaselineValid();
    machine.onAnalyzeStarted();
    machine.onBaselineInvalid(); // e.g. remote invalidate during the analysis
    machine.onAnalyzeFinished();
    CHECK_ENUM_EQ(machine.state(), AppState::NoBaseline);

    machine.onBaselineValid();
    machine.onMeasureStarted();
    CHECK_FALSE(machine.canAnalyze());
}

TEST_CASE("baseline invalidated during a measurement ends in NoBaseline") {
    AppStateMachine machine;
    machine.onBridgeUp();
    machine.onBaselineValid();
    machine.onMeasureStarted();
    machine.onBaselineInvalid();
    CHECK_ENUM_EQ(machine.state(), AppState::Measuring);
    machine.onMeasureFinished();
    CHECK_ENUM_EQ(machine.state(), AppState::NoBaseline);

    machine.onBaselineValid(); // the flag does not leak into the next run
    machine.onMeasureStarted();
    machine.onMeasureFinished();
    CHECK_ENUM_EQ(machine.state(), AppState::Ready);
}

TEST_CASE("measure gate: one answer with a reason for every trigger") {
    auto check = [](AppState state, BaselineStatus baseline, bool storageOk = true) {
        const auto refusal = checkMeasure(state, baseline, storageOk);
        return refusal ? static_cast<int>(*refusal) : -1;
    };
    const int allowed = -1;
    CHECK(check(AppState::Ready, BaselineStatus::Fresh) == allowed);
    CHECK(check(AppState::Ready, BaselineStatus::Aging) == allowed);
    // Stale is refused even while the state still says Ready (no timer gap).
    CHECK(check(AppState::Ready, BaselineStatus::Stale) == static_cast<int>(Refusal::baselineStale));
    CHECK(check(AppState::Ready, BaselineStatus::None) == static_cast<int>(Refusal::noBaseline));
    CHECK(check(AppState::NoBaseline, BaselineStatus::Stale) == static_cast<int>(Refusal::baselineStale));
    CHECK(check(AppState::NoBaseline, BaselineStatus::None) == static_cast<int>(Refusal::noBaseline));
    CHECK(check(AppState::Baselining, BaselineStatus::Fresh) == static_cast<int>(Refusal::baselineWizardOpen));
    CHECK(check(AppState::Measuring, BaselineStatus::Fresh) == static_cast<int>(Refusal::busy));
    CHECK(check(AppState::Analyzing, BaselineStatus::Fresh) == static_cast<int>(Refusal::busy));
    CHECK(check(AppState::Fault, BaselineStatus::Fresh) == static_cast<int>(Refusal::fault));
    CHECK(check(AppState::Starting, BaselineStatus::None) == static_cast<int>(Refusal::starting));
    CHECK(check(AppState::Ready, BaselineStatus::Fresh, false) == static_cast<int>(Refusal::storageDown));
    CHECK(std::string(operatorText(Refusal::baselineStale)) == "Baseline too old - redo the baseline");
}

TEST_CASE("sample ids become safe file name parts") {
    CHECK(safeFileNamePart("W12-A_3") == "W12-A_3");
    CHECK(safeFileNamePart("../etc/x") == "___etc_x");
    CHECK(safeFileNamePart("A:B*?") == "A_B__");
    CHECK(safeFileNamePart("") == "sample");
    CHECK(safeFileNamePart(std::string(100, 'a')).size() == 40);
}

TEST_CASE("channel serial: same F20 despite case and spaces; another F20 is caught") {
    CHECK_ENUM_EQ(checkChannelSerial("F20:09A006", "F20:09A006"), SerialCheck::ok);
    CHECK_ENUM_EQ(checkChannelSerial(" f20:09a006 ", "F20:09A006\n"), SerialCheck::ok);
    CHECK_ENUM_EQ(checkChannelSerial("F20:09A006", "F20:09A007"), SerialCheck::mismatch);
    CHECK_ENUM_EQ(checkChannelSerial("F20:09A006", "F20:09A0061"), SerialCheck::mismatch);
    CHECK_ENUM_EQ(checkChannelSerial("", "F20:09A006"), SerialCheck::notConfigured);
    CHECK_ENUM_EQ(checkChannelSerial("  ", "F20:09A006"), SerialCheck::notConfigured);
    CHECK_ENUM_EQ(checkChannelSerial("F20:09A006", ""), SerialCheck::missing);
}

TEST_CASE("recipe limits: profile by recipe name, defaults otherwise") {
    RecipeLimits limits(BaselineLimits{20, 30, 15});
    limits.addProfile({"thick", {"SiN thick", "Oxide\\Thick 2um"}, {240, 480, 5}});
    limits.addProfile({"other", {"sin THICK"}, {1, 2, 0}});

    CHECK(limits.forRecipe("SiN thick").blockMinutes == 480);
    CHECK(limits.forRecipe("sin thick").warmUpMinutes == 5); // case-insensitive
    CHECK(limits.forRecipe("Oxide\\Thick 2um").warnMinutes == 240);
    CHECK(limits.profileFor("SiN thick") == "thick");          // first profile wins
    CHECK(limits.forRecipe("SiO2 on Si").blockMinutes == 30);
    CHECK(limits.profileFor("SiO2 on Si") == "default");
    CHECK(limits.recipesInSeveralProfiles() == std::vector<std::string>{"SiN thick"});
}

TEST_CASE("recipe limits: unusable values are named") {
    CHECK_FALSE(checkLimits({20, 30, 15}).has_value());
    CHECK_FALSE(checkLimits({30, 30, 0}).has_value());
    CHECK(checkLimits({0, 30, 15}).has_value());
    CHECK(checkLimits({40, 30, 15}).has_value());
    CHECK(checkLimits({20, 30, -1}).has_value());
}

TEST_CASE("baseline change: stale leaves Ready, valid again returns to Ready") {
    using S = BaselineStatus;
    CHECK_ENUM_EQ(baselineChange(AppState::Ready, S::Stale), BaselineChange::nowStale);
    CHECK_ENUM_EQ(baselineChange(AppState::Ready, S::Fresh), BaselineChange::none);
    CHECK_ENUM_EQ(baselineChange(AppState::NoBaseline, S::Fresh), BaselineChange::nowValid);
    CHECK_ENUM_EQ(baselineChange(AppState::NoBaseline, S::Aging), BaselineChange::nowValid);
    CHECK_ENUM_EQ(baselineChange(AppState::NoBaseline, S::None), BaselineChange::none);
    CHECK_ENUM_EQ(baselineChange(AppState::NoBaseline, S::Stale), BaselineChange::none);
    // Busy or broken states are left alone; the change applies when they end.
    for (AppState state : {AppState::Starting, AppState::Baselining, AppState::Measuring,
                           AppState::Analyzing, AppState::Fault}) {
        CHECK_ENUM_EQ(baselineChange(state, S::Stale), BaselineChange::none);
        CHECK_ENUM_EQ(baselineChange(state, S::Fresh), BaselineChange::none);
    }
}

TEST_CASE("lamp warm-up: counts down in whole minutes, rounded up") {
    const auto t0 = WarmUpTimer::Clock::now();
    WarmUpTimer warmUp;
    warmUp.setRequiredMinutes(15);
    warmUp.start(t0);
    CHECK(warmUp.minutesLeft(t0) == 15);
    CHECK_FALSE(warmUp.isDone(t0));
    CHECK(warmUp.minutesLeft(t0 + 14min + 1s) == 1);
    CHECK(warmUp.minutesLeft(t0 + 15min) == 0);
    CHECK(warmUp.isDone(t0 + 15min));
}

TEST_CASE("lamp warm-up: recipe change, skip, never started, clock set back") {
    const auto t0 = WarmUpTimer::Clock::now();
    WarmUpTimer warmUp;
    CHECK_FALSE(warmUp.isDone(t0)); // not started: not warm

    warmUp.start(t0);
    warmUp.setRequiredMinutes(5);
    CHECK(warmUp.isDone(t0 + 6min));
    warmUp.setRequiredMinutes(15); // thin-film recipe picked after 6 min
    CHECK(warmUp.minutesLeft(t0 + 6min) == 9);

    CHECK(warmUp.minutesLeft(t0 - 1h) == 15); // clock set back: full time

    warmUp.skip();
    CHECK(warmUp.isSkipped());
    CHECK(warmUp.isDone(t0));
    warmUp.start(t0); // a new power-on forgets the skip
    CHECK_FALSE(warmUp.isDone(t0));

    warmUp.setRequiredMinutes(0);
    CHECK(warmUp.isDone(t0));
}
