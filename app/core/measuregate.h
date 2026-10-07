#pragma once
#include "appstatemachine.h"
#include "baselinetracker.h"

#include <optional>

namespace f20app {

// Why a measurement may not start now. Every trigger - MEASURE button,
// auto cycle, remote command - asks checkMeasure() once, right before it
// starts, so there is no gap between a check and the start. A refusal
// always has a reason, never silence: the Tango "is_allowed" pattern, and
// SiLA's CommandExecutionNotAccepted error.
enum class Refusal {
    storageDown,        // results could not be saved
    fault,              // bridge gone
    starting,           // bridge not reached yet
    baselineWizardOpen,
    busy,               // measuring or re-analyzing
    noBaseline,
    baselineStale,
};

std::optional<Refusal> checkMeasure(AppState state, BaselineStatus baseline, bool storageOk);

const char* toString(Refusal refusal);     // machine name, for server replies
const char* operatorText(Refusal refusal); // the sentence shown on screen

} // namespace f20app
