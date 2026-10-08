#pragma once
#include <optional>
#include <string>
#include <vector>

namespace f20app {

// Baseline rules that depend on the film (spec 2.2 #3, #4 and 6.2): thin
// films (< 1000 Å) need a fresh baseline every 20-30 min and a 15 min lamp
// warm-up; thick films are fine for a shift with 5 min warm-up.
struct BaselineLimits {
    int warnMinutes = 20;   // yellow after this age
    int blockMinutes = 30;  // red after this age: measuring blocked
    int warmUpMinutes = 15; // lamp warm-up after power-on
};

// Why a set of limits cannot be used, or nullopt when it can.
std::optional<std::string> checkLimits(const BaselineLimits& limits);

// A group of recipes that share the same limits ([baselineProfile_<name>]
// in f20.ini).
struct LimitProfile {
    std::string name;
    std::vector<std::string> recipes; // as named in FILMeasure
    BaselineLimits limits;
};

class RecipeLimits {
public:
    explicit RecipeLimits(BaselineLimits defaults = {}) : defaults_(defaults) {}

    // The caller checks the limits with checkLimits() first.
    void addProfile(LimitProfile profile);

    // The first profile that lists the recipe (case-insensitive), else the
    // defaults.
    BaselineLimits forRecipe(const std::string& recipe) const;
    // The profile name used for a recipe, or "default".
    std::string profileFor(const std::string& recipe) const;

    // Recipes listed in more than one profile - a config mistake worth a
    // warning (the first profile wins).
    std::vector<std::string> recipesInSeveralProfiles() const;

private:
    const LimitProfile* find(const std::string& recipe) const;

    BaselineLimits defaults_;
    std::vector<LimitProfile> profiles_;
};

} // namespace f20app
