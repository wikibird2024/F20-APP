#pragma once
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace f20 {

using json = nlohmann::json;

// Typed reads from untrusted JSON that never throw.
// nlohmann's value() and get<T>() throw type_error when a key holds another
// type, and get<int>() silently narrows a big number
// (json.nlohmann.me/api/basic_json/value, .../get). Socket input is read
// only through these: each returns nullopt when `object` is not an object,
// the key is missing, or the value has another type.
std::optional<double> numberAt(const json& object, const char* key);
std::optional<int> intAt(const json& object, const char* key); // integer that fits in int
std::optional<bool> boolAt(const json& object, const char* key);
std::optional<std::string> stringAt(const json& object, const char* key);
// An array whose every element is a number.
std::optional<std::vector<double>> numberArrayAt(const json& object, const char* key);

} // namespace f20
