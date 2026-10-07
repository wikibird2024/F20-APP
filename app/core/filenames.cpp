#include "filenames.h"

namespace f20app {

std::string safeFileNamePart(const std::string& text, std::size_t maxLength) {
    std::string out;
    for (const char c : text) {
        if (out.size() >= maxLength)
            break;
        const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '_' || c == '-';
        out += keep ? c : '_';
    }
    return out.empty() ? "sample" : out;
}

} // namespace f20app
