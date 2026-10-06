#include "f20/errors.h"

#include <array>
#include <utility>

namespace f20 {
namespace {

constexpr std::array<std::pair<ErrorCode, const char*>, 10> kNames{{
    {ErrorCode::measureNotReady, "measureNotReady"},
    {ErrorCode::baselineOrderWrong, "baselineOrderWrong"},
    {ErrorCode::baselineRecoverFailed, "baselineRecoverFailed"},
    {ErrorCode::referenceSignalBad, "referenceSignalBad"},
    {ErrorCode::recipeNotFound, "recipeNotFound"},
    {ErrorCode::fileOpenFailed, "fileOpenFailed"},
    {ErrorCode::layerOutOfBounds, "layerOutOfBounds"},
    {ErrorCode::hardwareMissing, "hardwareMissing"},
    {ErrorCode::busy, "busy"},
    {ErrorCode::filmeasureError, "filmeasureError"},
}};

} // namespace

const char* toString(ErrorCode code) {
    for (const auto& [c, name] : kNames)
        if (c == code)
            return name;
    return "filmeasureError";
}

std::optional<ErrorCode> errorCodeFromString(const std::string& text) {
    for (const auto& [c, name] : kNames)
        if (text == name)
            return c;
    return std::nullopt;
}

std::string operatorMessage(ErrorCode code, const std::string& detail) {
    switch (code) {
    case ErrorCode::measureNotReady:
        return "Run the baseline first";
    case ErrorCode::baselineOrderWrong:
        return "Baseline steps out of order - restart the baseline";
    case ErrorCode::baselineRecoverFailed:
        return "No saved baseline - run a full baseline";
    case ErrorCode::referenceSignalBad:
        return "Reference reading too high/low - check wafer and focus";
    case ErrorCode::recipeNotFound:
        return "Recipe '" + detail + "' not found in FILMeasure";
    case ErrorCode::fileOpenFailed:
        return "Cannot open/save file";
    case ErrorCode::layerOutOfBounds:
        return "Layer number or value out of range";
    case ErrorCode::hardwareMissing:
        return "F20 not connected - check USB";
    case ErrorCode::busy:
        return "Instrument busy";
    case ErrorCode::filmeasureError:
        return detail.empty() ? "FILMeasure error" : detail;
    }
    return "FILMeasure error";
}

} // namespace f20
