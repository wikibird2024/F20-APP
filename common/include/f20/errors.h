#pragma once
#include <optional>
#include <string>

namespace f20 {

// Error codes of spec §5.2 / §8.2. The bridge maps .NET exceptions to these;
// the app maps them to operator sentences.
enum class ErrorCode {
    measureNotReady,
    baselineOrderWrong,
    baselineRecoverFailed,
    referenceSignalBad,
    recipeNotFound,
    fileOpenFailed,
    layerOutOfBounds,
    hardwareMissing,
    busy,
    filmeasureError,
};

const char* toString(ErrorCode code);
std::optional<ErrorCode> errorCodeFromString(const std::string& text);

// The plain sentence shown to the operator (spec §8.2).
// `detail` fills the placeholder where a message has one (recipe name).
std::string operatorMessage(ErrorCode code, const std::string& detail = {});

} // namespace f20
