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

// What the baseline's age means for the state right now. The limits change
// with the recipe, so a stale baseline can be valid again: switching from a
// thin-film to a thick-film recipe must bring Ready back.
//   nowStale: Ready, but the age is past the block limit
//   nowValid: NoBaseline, but the tracker holds a commit within the limits.
//             Safe because every other way into NoBaseline (bridge drop,
//             wizard cancel, "Baseline invalid") also clears the tracker.
enum class BaselineChange { none, nowStale, nowValid };
BaselineChange baselineChange(AppState state, BaselineStatus baseline);

const char* toString(Refusal refusal);     // machine name, for server replies
const char* operatorText(Refusal refusal); // the sentence shown on screen
// The error code of a refused server measure (spec 7.2 / 8.2.3): the 7.2
// code where one fits, else a code of its own (storageDown, bridgeFault).
const char* serverErrorCode(Refusal refusal);

} // namespace f20app
