#include "measuregate.h"

namespace f20app {

std::optional<Refusal> checkMeasure(AppState state, BaselineStatus baseline, bool storageOk) {
    // A storage problem first: nothing the operator does here fixes it.
    if (!storageOk)
        return Refusal::storageDown;

    switch (state) {
    case AppState::Fault:      return Refusal::fault;
    case AppState::Starting:   return Refusal::starting;
    case AppState::Baselining: return Refusal::baselineWizardOpen;
    case AppState::Measuring:
    case AppState::Analyzing:  return Refusal::busy;
    case AppState::NoBaseline:
        return baseline == BaselineStatus::Stale ? Refusal::baselineStale
                                                 : Refusal::noBaseline;
    case AppState::Ready:
        break;
    }

    // Ready, but the age decides: the age timer may not have run yet.
    switch (baseline) {
    case BaselineStatus::None:  return Refusal::noBaseline;
    case BaselineStatus::Stale: return Refusal::baselineStale;
    case BaselineStatus::Fresh:
    case BaselineStatus::Aging: break;
    }
    return std::nullopt;
}

BaselineChange baselineChange(AppState state, BaselineStatus baseline) {
    if (state == AppState::Ready && baseline == BaselineStatus::Stale)
        return BaselineChange::nowStale;
    if (state == AppState::NoBaseline &&
        (baseline == BaselineStatus::Fresh || baseline == BaselineStatus::Aging))
        return BaselineChange::nowValid;
    return BaselineChange::none;
}

const char* toString(Refusal refusal) {
    switch (refusal) {
    case Refusal::storageDown:        return "storageDown";
    case Refusal::fault:              return "fault";
    case Refusal::starting:           return "starting";
    case Refusal::baselineWizardOpen: return "baselineWizardOpen";
    case Refusal::busy:               return "busy";
    case Refusal::noBaseline:         return "noBaseline";
    case Refusal::baselineStale:      return "baselineStale";
    }
    return "?";
}

const char* serverErrorCode(Refusal refusal) {
    switch (refusal) {
    case Refusal::storageDown:        return "storageDown";
    case Refusal::fault:
    case Refusal::starting:           return "bridgeFault";
    case Refusal::baselineWizardOpen:
    case Refusal::busy:               return "busy";
    case Refusal::noBaseline:         return "measureNotReady";
    case Refusal::baselineStale:      return "baselineStale";
    }
    return "filmeasureError";
}

const char* operatorText(Refusal refusal) {
    switch (refusal) {
    case Refusal::storageDown:        return "Database not available - results cannot be saved";
    case Refusal::fault:              return "Bridge fault - check Diagnostics";
    case Refusal::starting:           return "Connecting to the bridge...";
    case Refusal::baselineWizardOpen: return "Baseline wizard is open";
    case Refusal::busy:               return "Instrument busy - measurement running";
    case Refusal::noBaseline:         return "Run the baseline first";
    case Refusal::baselineStale:      return "Baseline too old - redo the baseline";
    }
    return "Cannot measure now";
}

} // namespace f20app
