#include "devicecheck.h"

#include <algorithm>

namespace f20app {
namespace {

std::string_view trimmed(std::string_view text) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

} // namespace

SerialCheck checkChannelSerial(std::string_view expected, std::string_view reported) {
    expected = trimmed(expected);
    reported = trimmed(reported);
    if (expected.empty())
        return SerialCheck::notConfigured;
    if (reported.empty())
        return SerialCheck::missing;
    const bool same = expected.size() == reported.size() &&
                      std::equal(expected.begin(), expected.end(), reported.begin(),
                                 [](char a, char b) { return lower(a) == lower(b); });
    return same ? SerialCheck::ok : SerialCheck::mismatch;
}

} // namespace f20app
