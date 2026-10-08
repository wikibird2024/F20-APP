#include "simengine.h"

#include "f20/errors.h"
#include "f20/jsonread.h"
#include "spectrumsynth.h"

#include <charconv>
#include <fstream>

namespace f20sim {

using f20::ErrorCode;
using f20::errorReply;
using f20::json;
using f20::okReply;
using f20::Reply;
using f20::Request;

namespace {

Reply err(int id, ErrorCode code, const std::string& message) {
    return errorReply(id, f20::toString(code), message);
}

// One number of a spectrum CSV line. std::from_chars, not std::stod: a bad
// line gives an error reply instead of an exception out of the server loop,
// and the result does not depend on the C locale's decimal separator.
std::optional<double> parseNumber(const std::string& text) {
    std::size_t begin = text.find_first_not_of(" \t");
    std::size_t end = text.find_last_not_of(" \t\r");
    if (begin == std::string::npos)
        return std::nullopt;
    double value = 0.0;
    const char* first = text.data() + begin;
    const char* last = text.data() + end + 1;
    const auto [stop, error] = std::from_chars(first, last, value);
    if (error != std::errc() || stop != last)
        return std::nullopt;
    return value;
}

} // namespace

SimEngine::SimEngine(SimConfig config)
    : config_(std::move(config)), hasStoredBaseline_(config_.storedBaselineOnDisk) {}

double SimEngine::targetThicknessNm() const {
    if (auto it = forcedThicknessNm_.find(1); it != forcedThicknessNm_.end())
        return it->second;
    if (currentRecipe_ == "SiN thick")
        return 2450.0;
    return 512.3;
}

Reply SimEngine::measureReply(int id) {
    const bool failGof = (currentRecipe_ == "FAIL_GOF");
    f20::MeasureResult result;
    f20::LayerResult layer;
    layer.layer = 1;
    layer.thicknessNm = targetThicknessNm() + 0.4; // small fixed "fit noise"
    result.layers.push_back(layer);
    result.gof = failGof ? 0.62 : 0.987;
    result.passed = !failGof;
    result.summary = "Layer 1: " + std::to_string(layer.thicknessNm) +
                     " nm, GOF " + std::to_string(result.gof);
    return okReply(id, f20::toJson(result));
}

Reply SimEngine::handle(const Request& request) {
    const int id = request.id;
    const std::string& cmd = request.cmd;
    const json& p = request.params;

    if (cmd == "getVersion")
        return okReply(id, {{"bridge", "f20bridge-sim 0.1"},
                            {"filmeasure", "simulated 6.1.0"}});

    if (cmd == "getStatus")
        return okReply(id, {{"baselineValid", baselineCommitted_},
                            {"busy", false},
                            {"channelSerial", config_.channelSerial}});

    if (cmd == "listChannels")
        return okReply(id, {{"channels",
                             json::array({{{"name", "F20"},
                                           {"serial", config_.channelSerial},
                                           {"guid", "sim-guid-0001"}}})}});

    if (cmd == "setRecipe") {
        const std::string name = f20::stringAt(p, "name").value_or("");
        bool known = false;
        for (const auto& r : config_.recipes)
            known = known || (r == name);
        if (!known)
            return err(id, ErrorCode::recipeNotFound,
                       "Recipe '" + name + "' not found");
        currentRecipe_ = name;
        return okReply(id);
    }

    if (cmd == "setThicknessNm" || cmd == "setRoughnessNm") {
        const int layer = f20::intAt(p, "layer").value_or(0);
        const double nm = f20::numberAt(p, "nm").value_or(-1.0);
        if (layer < 1 || layer > 3 || nm < 0.0 || nm > 1e6)
            return err(id, ErrorCode::layerOutOfBounds,
                       "layer or value out of range");
        if (cmd == "setThicknessNm")
            forcedThicknessNm_[layer] = nm;
        return okReply(id);
    }

    if (cmd == "baselineSetRefMat") {
        refMatSet_ = !f20::stringAt(p, "name").value_or("").empty();
        if (!refMatSet_)
            return err(id, ErrorCode::filmeasureError,
                       "unknown reference material");
        return okReply(id);
    }

    if (cmd == "baselineStep1") {
        step1Done_ = true;
        step2Done_ = step3Done_ = false; // restarting invalidates later steps
        return okReply(id);
    }

    if (cmd == "baselineStep2") {
        if (!step1Done_ || !refMatSet_)
            return err(id, ErrorCode::baselineOrderWrong,
                       "take the sample spectrum first (and set the "
                       "reference material)");
        if (currentRecipe_ == "REF_LOW")
            return err(id, ErrorCode::referenceSignalBad,
                       "reference intensity below threshold");
        step2Done_ = true;
        return okReply(id);
    }

    if (cmd == "baselineStep2FromOldSample") {
        if (!hasStoredBaseline_)
            return err(id, ErrorCode::baselineRecoverFailed,
                       "no stored sample reflectance");
        step1Done_ = step2Done_ = true;
        return okReply(id);
    }

    if (cmd == "baselineStep3") {
        if (!step2Done_)
            return err(id, ErrorCode::baselineOrderWrong,
                       "take the reference spectrum first");
        step3Done_ = true;
        return okReply(id);
    }

    if (cmd == "baselineCommit") {
        if (!(step1Done_ && step2Done_ && step3Done_))
            return err(id, ErrorCode::baselineOrderWrong,
                       "baseline steps incomplete");
        baselineCommitted_ = true;
        hasStoredBaseline_ = true;
        step1Done_ = step2Done_ = step3Done_ = false;
        return okReply(id);
    }

    if (cmd == "baselineRecover") {
        if (!hasStoredBaseline_)
            return err(id, ErrorCode::baselineRecoverFailed,
                       "no baseline stored on disk");
        baselineCommitted_ = true;
        return okReply(id);
    }

    if (cmd == "measure") {
        if (!baselineCommitted_)
            return err(id, ErrorCode::measureNotReady,
                       "Measure would be disabled: no baseline");
        if (currentRecipe_.empty())
            return err(id, ErrorCode::measureNotReady, "no recipe selected");
        return measureReply(id);
    }

    if (cmd == "acquireSpectrum") {
        if (!baselineCommitted_)
            return err(id, ErrorCode::measureNotReady,
                       "no baseline for spectrum acquisition");
        lastSpectrum_ = synthesizeSpectrum(targetThicknessNm());
        haveSpectrum_ = true;
        return okReply(id, f20::toJson(lastSpectrum_));
    }

    if (cmd == "analyzeSpectrum") {
        if (!haveSpectrum_)
            return err(id, ErrorCode::filmeasureError,
                       "no spectrum acquired or opened");
        if (currentRecipe_.empty())
            return err(id, ErrorCode::measureNotReady, "no recipe selected");
        return measureReply(id);
    }

    if (cmd == "saveSpectrum") {
        if (!haveSpectrum_)
            return err(id, ErrorCode::filmeasureError, "no spectrum to save");
        const std::string path = f20::stringAt(p, "path").value_or("");
        if (path.empty())
            return err(id, ErrorCode::fileOpenFailed, "no path given");
        std::ofstream out(path);
        if (!out)
            return err(id, ErrorCode::fileOpenFailed, "cannot write " + path);
        out << "wavelengthNm,reflectance\n";
        for (std::size_t i = 0; i < lastSpectrum_.wavelengthNm.size(); ++i)
            out << lastSpectrum_.wavelengthNm[i] << ','
                << lastSpectrum_.reflectance[i] << '\n';
        return okReply(id);
    }

    if (cmd == "openSpectrum") {
        const std::string path = f20::stringAt(p, "path").value_or("");
        if (path.empty())
            return err(id, ErrorCode::fileOpenFailed, "no path given");
        std::ifstream in(path);
        if (!in)
            return err(id, ErrorCode::fileOpenFailed, "cannot open " + path);
        f20::Spectrum s;
        std::string line;
        std::getline(in, line); // header
        for (int lineNumber = 2; std::getline(in, line); ++lineNumber) {
            const auto comma = line.find(',');
            if (comma == std::string::npos)
                continue;
            const auto wavelengthNm = parseNumber(line.substr(0, comma));
            const auto reflectance = parseNumber(line.substr(comma + 1));
            if (!wavelengthNm || !reflectance)
                return err(id, ErrorCode::fileOpenFailed,
                           "bad number in line " + std::to_string(lineNumber) +
                               " of " + path);
            s.wavelengthNm.push_back(*wavelengthNm);
            s.reflectance.push_back(*reflectance);
        }
        if (s.wavelengthNm.empty())
            return err(id, ErrorCode::fileOpenFailed, "no data in " + path);
        lastSpectrum_ = std::move(s);
        haveSpectrum_ = true;
        return okReply(id);
    }

    if (cmd == "getDiagnostics")
        return okReply(id, {{"referenceCounts", 3012}, {"backgroundCounts", 148}});

    if (cmd == "quit") {
        quitRequested_ = true;
        return okReply(id);
    }

    return err(id, ErrorCode::filmeasureError, "unknown command: " + cmd);
}

} // namespace f20sim
