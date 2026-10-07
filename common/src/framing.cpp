#include "f20/framing.h"

namespace f20 {

LineSplitter::LineSplitter(std::size_t maxLineBytes) : maxLineBytes_(maxLineBytes) {}

std::vector<std::string> LineSplitter::feed(const char* data, std::size_t len) {
    std::vector<std::string> lines;
    if (overflowed_)
        return lines;
    buffer_.append(data, len);
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = buffer_.find('\n', start);
        if (nl == std::string::npos)
            break;
        if (nl - start > maxLineBytes_) {
            markOverflow();
            return lines;
        }
        std::string line = buffer_.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        start = nl + 1;
    }
    buffer_.erase(0, start);
    if (buffer_.size() > maxLineBytes_)
        markOverflow();
    return lines;
}

std::vector<std::string> LineSplitter::feed(const std::string& data) {
    return feed(data.data(), data.size());
}

void LineSplitter::reset() {
    buffer_.clear();
    overflowed_ = false;
}

void LineSplitter::markOverflow() {
    overflowed_ = true;
    std::string().swap(buffer_); // free the memory, not just the content
}

} // namespace f20
