#include "recipelimits.h"

#include <algorithm>

namespace f20app {
namespace {

bool sameName(const std::string& a, const std::string& b) {
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [&](char x, char y) { return lower(x) == lower(y); });
}

} // namespace

std::optional<std::string> checkLimits(const BaselineLimits& limits) {
    if (limits.warnMinutes <= 0)
        return "warnMinutes must be more than 0";
    if (limits.blockMinutes < limits.warnMinutes)
        return "blockMinutes must not be less than warnMinutes";
    if (limits.warmUpMinutes < 0)
        return "warmUpMinutes must not be negative";
    return std::nullopt;
}

void RecipeLimits::addProfile(LimitProfile profile) {
    profiles_.push_back(std::move(profile));
}

const LimitProfile* RecipeLimits::find(const std::string& recipe) const {
    for (const LimitProfile& profile : profiles_)
        for (const std::string& name : profile.recipes)
            if (sameName(name, recipe))
                return &profile;
    return nullptr;
}

BaselineLimits RecipeLimits::forRecipe(const std::string& recipe) const {
    const LimitProfile* profile = find(recipe);
    return profile ? profile->limits : defaults_;
}

std::string RecipeLimits::profileFor(const std::string& recipe) const {
    const LimitProfile* profile = find(recipe);
    return profile ? profile->name : "default";
}

std::vector<std::string> RecipeLimits::recipesInSeveralProfiles() const {
    std::vector<std::string> repeated;
    for (std::size_t i = 0; i < profiles_.size(); ++i)
        for (const std::string& name : profiles_[i].recipes) {
            const bool laterToo =
                std::any_of(profiles_.begin() + static_cast<long>(i) + 1, profiles_.end(),
                            [&](const LimitProfile& later) {
                                return std::any_of(later.recipes.begin(), later.recipes.end(),
                                                   [&](const std::string& other) {
                                                       return sameName(name, other);
                                                   });
                            });
            const bool alreadyListed =
                std::any_of(repeated.begin(), repeated.end(),
                            [&](const std::string& r) { return sameName(r, name); });
            if (laterToo && !alreadyListed)
                repeated.push_back(name);
        }
    return repeated;
}

} // namespace f20app
