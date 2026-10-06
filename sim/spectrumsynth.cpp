#include "spectrumsynth.h"

#include <cmath>

namespace f20sim {
namespace {

constexpr double kLambdaMinNm = 400.0;
constexpr double kLambdaMaxNm = 1000.0;
constexpr double kFilmIndex = 1.46; // SiO2-like

f20::Spectrum model(double thicknessNm, int points, double rippleAmplitude) {
    f20::Spectrum s;
    s.wavelengthNm.reserve(points);
    s.reflectance.reserve(points);
    for (int i = 0; i < points; ++i) {
        const double lambda =
            kLambdaMinNm + (kLambdaMaxNm - kLambdaMinNm) * i / (points - 1);
        const double phase = 4.0 * M_PI * kFilmIndex * thicknessNm / lambda;
        double r = 0.20 + 0.13 * std::cos(phase);
        // Small deterministic "measurement ripple" so measured != calculated.
        r += rippleAmplitude * std::sin(i * 0.731);
        s.wavelengthNm.push_back(lambda);
        s.reflectance.push_back(r);
    }
    return s;
}

} // namespace

f20::Spectrum synthesizeSpectrum(double thicknessNm, int points) {
    return model(thicknessNm, points, 0.004);
}

f20::Spectrum calculatedSpectrum(double thicknessNm, int points) {
    return model(thicknessNm, points, 0.0);
}

} // namespace f20sim
