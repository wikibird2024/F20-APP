#pragma once
#include "f20/protocol.h"

#include <optional>
#include <string>
#include <vector>

namespace f20 {

// One film layer of a measurement result (spec §5.4 `measure`).
// n, k and roughness are present only when the recipe solves them.
struct LayerResult {
    int layer = 1;
    double thicknessNm = 0.0;
    std::optional<double> n;
    std::optional<double> k;
    std::optional<double> roughnessNm;
};

struct MeasureResult {
    std::vector<LayerResult> layers;
    double gof = 0.0;
    std::string summary;
    bool passed = false;
};

// Reflectance vs wavelength (spec §5.4 `acquireSpectrum`).
struct Spectrum {
    std::vector<double> wavelengthNm;
    std::vector<double> reflectance;
};

json toJson(const MeasureResult& result);
std::optional<MeasureResult> measureResultFromJson(const json& j);

json toJson(const Spectrum& spectrum);
std::optional<Spectrum> spectrumFromJson(const json& j);

} // namespace f20
