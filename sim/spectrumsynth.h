#pragma once
#include "f20/results.h"

namespace f20sim {

// Synthetic reflectance spectrum of a single transparent film on Si:
// two-beam interference, R oscillating with 4*pi*n*d/lambda. Deterministic
// (no clock, no random) so tests always see the same curve.
f20::Spectrum synthesizeSpectrum(double thicknessNm, int points = 512);

// The "calculated" curve for a fitted thickness - same model, no ripple.
f20::Spectrum calculatedSpectrum(double thicknessNm, int points = 512);

} // namespace f20sim
