#include "f20/framing.h"

namespace f20 {

std::vector<std::string> LineSplitter::feed(const char* data, std::size_t len) {
    buffer_.append(data, len);
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = buffer_.find('\n', start);
        if (nl == std::string::npos)
            break;
        std::string line = buffer_.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        start = nl + 1;
    }
    buffer_.erase(0, start);
    return lines;
}

std::vector<std::string> LineSplitter::feed(const std::string& data) {
    return feed(data.data(), data.size());
}

} // namespace f20
