#pragma once
#include <functional>

namespace f20app {

// The one state machine of spec §8.1 - the single source of truth for
// "may we measure?". UI, network and bridge client all ask this class;
// none of them keeps its own idea of the state.
enum class AppState { Starting, NoBaseline, Ready, Measuring, Fault };

const char* toString(AppState state);

class AppStateMachine {
public:
    using ChangeHandler = std::function<void(AppState from, AppState to)>;

    AppState state() const { return state_; }
    bool canMeasure() const { return state_ == AppState::Ready; }

    // Called whenever the state changes (UI update, server status publish).
    void setChangeHandler(ChangeHandler handler);

    // Inputs (events). Illegal inputs for the current state are ignored -
    // e.g. onMeasureStarted() while NoBaseline does nothing.
    void onBridgeUp();          // Starting/Fault -> NoBaseline
    void onBridgeDown();        // any -> Fault (after the restart budget,
                                //   counted by the caller, spec §8.1)
    void onBaselineValid();     // NoBaseline -> Ready
    void onBaselineInvalid();   // Ready -> NoBaseline (stale, fiber moved...)
    void onMeasureStarted();    // Ready -> Measuring
    void onMeasureFinished();   // Measuring -> Ready

private:
    void setState(AppState next);

    AppState state_ = AppState::Starting;
    ChangeHandler onChange_;
};

} // namespace f20app
