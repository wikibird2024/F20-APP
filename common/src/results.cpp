#include "f20/results.h"

#include "f20/jsonread.h"

namespace f20 {
namespace {

// An optional number: absent is fine (stays nullopt), present with another
// type makes the whole result malformed.
bool readOptionalNumber(const json& object, const char* key, std::optional<double>& out) {
    if (!object.contains(key))
        return true;
    out = numberAt(object, key);
    return out.has_value();
}

std::optional<LayerResult> layerFromJson(const json& l) {
    LayerResult layer;
    const auto thicknessNm = numberAt(l, "thicknessNm");
    if (!thicknessNm)
        return std::nullopt;
    layer.thicknessNm = *thicknessNm;
    if (l.contains("layer")) {
        const auto number = intAt(l, "layer");
        if (!number)
            return std::nullopt;
        layer.layer = *number;
    }
    if (!readOptionalNumber(l, "n", layer.n) || !readOptionalNumber(l, "k", layer.k) ||
        !readOptionalNumber(l, "roughnessNm", layer.roughnessNm))
        return std::nullopt;
    return layer;
}

} // namespace

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
    if (!j.is_object())
        return std::nullopt;
    const auto layers = j.find("layers");
    const auto gof = numberAt(j, "gof");
    const auto passed = boolAt(j, "passed");
    if (layers == j.end() || !layers->is_array() || !gof || !passed)
        return std::nullopt;

    MeasureResult result;
    for (const json& l : *layers) {
        auto layer = layerFromJson(l);
        if (!layer)
            return std::nullopt;
        result.layers.push_back(*layer);
    }
    result.gof = *gof;
    result.summary = stringAt(j, "summary").value_or("");
    result.passed = *passed;
    return result;
}

json toJson(const Spectrum& spectrum) {
    return json{{"wavelengthNm", spectrum.wavelengthNm},
                {"reflectance", spectrum.reflectance}};
}

std::optional<Spectrum> spectrumFromJson(const json& j) {
    auto wavelengthNm = numberArrayAt(j, "wavelengthNm");
    auto reflectance = numberArrayAt(j, "reflectance");
    if (!wavelengthNm || !reflectance || wavelengthNm->size() != reflectance->size())
        return std::nullopt;

    Spectrum spectrum;
    spectrum.wavelengthNm = std::move(*wavelengthNm);
    spectrum.reflectance = std::move(*reflectance);
    return spectrum;
}

} // namespace f20
