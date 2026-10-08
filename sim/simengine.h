#pragma once
#include "f20/protocol.h"
#include "f20/results.h"

#include <map>
#include <string>
#include <vector>

namespace f20sim {

// Simulates f20bridge + FILMeasure: same commands (spec §5.4), same error
// codes (§5.2), FILMeasure's baseline order rules enforced. No sockets in
// here - handle() maps one request to one reply, so tests drive it directly.
//
// Magic recipe names for testing error paths (spec plan, phase 2):
//   "FAIL_GOF"  - measure succeeds but the verdict is FAIL (low GOF)
//   "REF_LOW"   - baseline step 2 fails with referenceSignalBad
// Not nested in SimEngine: a nested struct's member initializers are not
// parsed yet where the constructor's default argument needs them (C++ rule).
struct SimConfig {
    std::string channelSerial = "F20:SIM001";
    std::vector<std::string> recipes = {"SiO2 on Si", "SiN thick",
                                        "FAIL_GOF", "REF_LOW"};
    // Like FILMeasure restarted after a commit: no active baseline, but the
    // stored one can be recovered (f20bridge-sim --stored-baseline).
    bool storedBaselineOnDisk = false;
};

class SimEngine {
public:
    explicit SimEngine(SimConfig config = SimConfig());

    f20::Reply handle(const f20::Request& request);
    bool quitRequested() const { return quitRequested_; }

private:
    f20::Reply measureReply(int id);
    double targetThicknessNm() const;

    SimConfig config_;
    std::string currentRecipe_;
    std::map<int, double> forcedThicknessNm_; // layer -> value via setThicknessNm

    bool refMatSet_ = false;
    bool step1Done_ = false;
    bool step2Done_ = false;
    bool step3Done_ = false;
    bool baselineCommitted_ = false;
    bool hasStoredBaseline_ = false; // enables baselineRecover
    bool haveSpectrum_ = false;      // acquireSpectrum / openSpectrum ran
    f20::Spectrum lastSpectrum_;
    bool quitRequested_ = false;
};

} // namespace f20sim
