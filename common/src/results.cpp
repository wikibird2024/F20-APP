#include "f20/results.h"

namespace f20 {

json toJson(const MeasureResult& result) {
    json layers = json::array();
    for (const auto& layer : result.layers) {
        json l{{"layer", layer.layer}, {"thicknessNm", layer.thicknessNm}};
        if (layer.n)
            l["n"] = *layer.n;
        if (layer.k)
            l["k"] = *layer.k;
        if (layer.roughnessNm)
            l["roughnessNm"] = *layer.roughnessNm;
        layers.push_back(std::move(l));
    }
    return json{{"layers", std::move(layers)},
                {"gof", result.gof},
                {"summary", result.summary},
                {"passed", result.passed}};
}

std::optional<MeasureResult> measureResultFromJson(const json& j) {
    if (!j.is_object() || !j.contains("layers") || !j["layers"].is_array() ||
        !j.contains("gof") || !j.contains("passed"))
        return std::nullopt;

    MeasureResult result;
    for (const json& l : j["layers"]) {
        if (!l.is_object() || !l.contains("thicknessNm"))
            return std::nullopt;
        LayerResult layer;
        layer.layer = l.value("layer", 1);
        layer.thicknessNm = l["thicknessNm"].get<double>();
        if (l.contains("n"))
            layer.n = l["n"].get<double>();
        if (l.contains("k"))
            layer.k = l["k"].get<double>();
        if (l.contains("roughnessNm"))
            layer.roughnessNm = l["roughnessNm"].get<double>();
        result.layers.push_back(layer);
    }
    result.gof = j["gof"].get<double>();
    result.summary = j.value("summary", "");
    result.passed = j["passed"].get<bool>();
    return result;
}

json toJson(const Spectrum& spectrum) {
    return json{{"wavelengthNm", spectrum.wavelengthNm},
                {"reflectance", spectrum.reflectance}};
}

std::optional<Spectrum> spectrumFromJson(const json& j) {
    if (!j.is_object() || !j.contains("wavelengthNm") || !j.contains("reflectance") ||
        !j["wavelengthNm"].is_array() || !j["reflectance"].is_array())
        return std::nullopt;

    Spectrum spectrum;
    spectrum.wavelengthNm = j["wavelengthNm"].get<std::vector<double>>();
    spectrum.reflectance = j["reflectance"].get<std::vector<double>>();
    if (spectrum.wavelengthNm.size() != spectrum.reflectance.size())
        return std::nullopt;
    return spectrum;
}

} // namespace f20
