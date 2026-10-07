#pragma once
#include <functional>

namespace f20app {

// The one state machine of spec §7 - the single source of truth for
// "may we measure?". UI, network and bridge client all ask this class;
// none of them keeps its own idea of the state.
//
// Baselining and Analyzing are extra states the spec diagram leaves
// implicit: while the wizard is open or a saved spectrum is re-analyzed,
// the instrument is in use and must not start a measurement (spec §6.6:
// a remote measure is refused while the wizard is open).
enum class AppState { Starting, NoBaseline, Baselining, Ready, Measuring, Analyzing, Fault };

const char* toString(AppState state);

class AppStateMachine {
public:
    using ChangeHandler = std::function<void(AppState from, AppState to)>;

    AppState state() const { return state_; }

    // Ask first, then send the matching input - the Tango "is_allowed"
    // pattern. The full measure check (baseline age, storage) is
    // checkMeasure() in measuregate.h.
    bool canMeasure() const { return state_ == AppState::Ready; }
    bool canOpenBaselineWizard() const { return isIdle(); }
    bool canAnalyze() const { return isIdle(); }

    // Called whenever the state changes (UI update, server status publish).
    void setChangeHandler(ChangeHandler handler);

    // Inputs (events). Illegal inputs for the current state are ignored -
    // e.g. onMeasureStarted() while NoBaseline does nothing.
    void onBridgeUp();          // Starting/Fault -> NoBaseline
    void onBridgeDown();        // any -> Fault
    void onBaselineValid();     // NoBaseline -> Ready
    void onBaselineInvalid();   // Ready -> NoBaseline (stale, fiber moved...);
                                //   during Measuring/Analyzing it takes effect
                                //   when that ends
    void onBaselineWizardOpened();               // NoBaseline/Ready -> Baselining
    void onBaselineWizardClosed(bool committed); // Baselining -> Ready if committed,
                                                 //   else NoBaseline
    void onMeasureStarted();    // Ready -> Measuring
    void onMeasureFinished();   // Measuring -> Ready (NoBaseline if the
                                //   baseline was invalidated meanwhile)
    void onAnalyzeStarted();    // NoBaseline/Ready -> Analyzing
    void onAnalyzeFinished();   // Analyzing -> the state it came from

private:
    bool isIdle() const {
        return state_ == AppState::NoBaseline || state_ == AppState::Ready;
    }
    void setState(AppState next);

    AppState state_ = AppState::Starting;
    AppState afterAnalyze_ = AppState::NoBaseline;
    bool baselineLostWhileMeasuring_ = false;
    ChangeHandler onChange_;
};

} // namespace f20app
