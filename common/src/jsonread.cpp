#include "f20/jsonread.h"

#include <cstdint>
#include <limits>

namespace f20 {
namespace {

const json* find(const json& object, const char* key) {
    if (!object.is_object())
        return nullptr;
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

} // namespace

std::optional<double> numberAt(const json& object, const char* key) {
    const json* value = find(object, key);
    if (!value || !value->is_number())
        return std::nullopt;
    return value->get<double>();
}

std::optional<int> intAt(const json& object, const char* key) {
    const json* value = find(object, key);
    if (!value || !value->is_number_integer())
        return std::nullopt;
    if (value->is_number_unsigned()) {
        const auto number = value->get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return std::nullopt;
        return static_cast<int>(number);
    }
    const auto number = value->get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
        return std::nullopt;
    return static_cast<int>(number);
}

std::optional<bool> boolAt(const json& object, const char* key) {
    const json* value = find(object, key);
    if (!value || !value->is_boolean())
        return std::nullopt;
    return value->get<bool>();
}

std::optional<std::string> stringAt(const json& object, const char* key) {
    const json* value = find(object, key);
    if (!value || !value->is_string())
        return std::nullopt;
    return value->get<std::string>();
}

std::optional<std::vector<double>> numberArrayAt(const json& object, const char* key) {
    const json* value = find(object, key);
    if (!value || !value->is_array())
        return std::nullopt;
    std::vector<double> numbers;
    numbers.reserve(value->size());
    for (const json& element : *value) {
        if (!element.is_number())
            return std::nullopt;
        numbers.push_back(element.get<double>());
    }
    return numbers;
}

} // namespace f20
