#include "appstatemachine.h"

namespace f20app {

const char* toString(AppState state) {
    switch (state) {
    case AppState::Starting:   return "starting";
    case AppState::NoBaseline: return "noBaseline";
    case AppState::Ready:      return "ready";
    case AppState::Measuring:  return "measuring";
    case AppState::Fault:      return "fault";
    }
    return "?";
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
    if (state_ == AppState::Ready)
        setState(AppState::NoBaseline);
}

void AppStateMachine::onMeasureStarted() {
    if (state_ == AppState::Ready)
        setState(AppState::Measuring);
}

void AppStateMachine::onMeasureFinished() {
    if (state_ == AppState::Measuring)
        setState(AppState::Ready);
}

} // namespace f20app
