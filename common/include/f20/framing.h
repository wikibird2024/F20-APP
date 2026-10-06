#pragma once
#include <string>
#include <vector>

namespace f20 {

// Splits a TCP byte stream into complete '\n'-terminated lines.
// TCP gives no message boundaries: one read may hold half a line or three
// lines, so every byte goes through here and only whole lines come out.
// The incomplete tail stays buffered until its '\n' arrives.
class LineSplitter {
public:
    std::vector<std::string> feed(const char* data, std::size_t len);
    std::vector<std::string> feed(const std::string& data);

private:
    std::string buffer_;
};

} // namespace f20
