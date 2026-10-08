#include "appstatemachine.h"

namespace f20app {

const char* toString(AppState state) {
    switch (state) {
    case AppState::Starting:   return "starting";
    case AppState::NoBaseline: return "noBaseline";
    case AppState::Baselining: return "baselining";
    case AppState::Ready:      return "ready";
    case AppState::Measuring:  return "measuring";
    case AppState::Analyzing:  return "analyzing";
    case AppState::Fault:      return "fault";
    }
    return "?";
}

const char* machineStatus(AppState state) {
    switch (state) {
    case AppState::Starting:   return "Starting";
    case AppState::NoBaseline: return "NoBaseline";
    case AppState::Baselining: return "Baselining";
    case AppState::Ready:      return "Ready";
    case AppState::Measuring:  return "Measuring";
    case AppState::Analyzing:  return "Analyzing";
    case AppState::Fault:      return "Fault";
    }
    return "Unknown";
}

void AppStateMachine::setChangeHandler(ChangeHandler handler) {
    onChange_ = std::move(handler);
}

void AppStateMachine::setState(AppState next) {
    if (next == state_)
        return;
    const AppState from = state_;
    state_ = next;
    if (onChange_)
        onChange_(from, next);
}

void AppStateMachine::onBridgeUp() {
    if (state_ == AppState::Starting || state_ == AppState::Fault)
        setState(AppState::NoBaseline);
}

void AppStateMachine::onBridgeDown() {
    setState(AppState::Fault);
}

void AppStateMachine::onBaselineValid() {
    if (state_ == AppState::NoBaseline)
        setState(AppState::Ready);
}

void AppStateMachine::onBaselineInvalid() {
    switch (state_) {
    case AppState::Ready:
        setState(AppState::NoBaseline);
        break;
    case AppState::Measuring:
        baselineLostWhileMeasuring_ = true;
        break;
    case AppState::Analyzing:
        afterAnalyze_ = AppState::NoBaseline;
        break;
    default:
        break;
    }
}

void AppStateMachine::onBaselineWizardOpened() {
    if (isIdle())
        setState(AppState::Baselining);
}

void AppStateMachine::onBaselineWizardClosed(bool committed) {
    if (state_ == AppState::Baselining)
        setState(committed ? AppState::Ready : AppState::NoBaseline);
}

void AppStateMachine::onMeasureStarted() {
    if (state_ == AppState::Ready) {
        baselineLostWhileMeasuring_ = false;
        setState(AppState::Measuring);
    }
}

void AppStateMachine::onMeasureFinished() {
    if (state_ == AppState::Measuring)
        setState(baselineLostWhileMeasuring_ ? AppState::NoBaseline : AppState::Ready);
}

void AppStateMachine::onAnalyzeStarted() {
    if (isIdle()) {
        afterAnalyze_ = state_;
        setState(AppState::Analyzing);
    }
}

void AppStateMachine::onAnalyzeFinished() {
    if (state_ == AppState::Analyzing)
        setState(afterAnalyze_);
}

} // namespace f20app
