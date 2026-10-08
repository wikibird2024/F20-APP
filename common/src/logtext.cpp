#include "f20/logtext.h"

#include <nlohmann/json.hpp>

namespace f20 {
namespace {

// ordered_json keeps the keys in wire order; plain json would sort them.
using ordered_json = nlohmann::ordered_json;

void collapseLongArrays(ordered_json& value, std::size_t maxArrayItems) {
    if (value.is_array() && value.size() > maxArrayItems) {
        value = "[" + std::to_string(value.size()) + " values]";
        return;
    }
    if (value.is_structured())
        for (auto& child : value)
            collapseLongArrays(child, maxArrayItems);
}

// Cut at maxChars, but never inside a UTF-8 character.
std::string cut(const std::string& text, std::size_t maxChars) {
    if (text.size() <= maxChars)
        return text;
    std::size_t end = maxChars;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
        --end;
    return text.substr(0, end) + "... (" + std::to_string(text.size()) + " chars)";
}

} // namespace

std::string shortenForLog(const std::string& line, std::size_t maxArrayItems,
                          std::size_t maxChars) {
    if (line.size() <= maxChars)
        return line;
    ordered_json parsed = ordered_json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded())
        return cut(line, maxChars);
    collapseLongArrays(parsed, maxArrayItems);
    // replace: a bad UTF-8 byte must not make dump() throw.
    const std::string shorter =
        parsed.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
    return cut(shorter, maxChars);
}

} // namespace f20
