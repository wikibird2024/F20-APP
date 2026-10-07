#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace f20 {

// Splits a TCP byte stream into complete '\n'-terminated lines.
// TCP gives no message boundaries: one read may hold half a line or three
// lines, so every byte goes through here and only whole lines come out.
// The incomplete tail stays buffered until its '\n' arrives.
class LineSplitter {
public:
    // A spectrum reply is tens of kB; 1 MiB leaves a wide margin.
    static constexpr std::size_t kDefaultMaxLineBytes = std::size_t{1} << 20;

    explicit LineSplitter(std::size_t maxLineBytes = kDefaultMaxLineBytes);

    // Returns the complete lines. After an overflow it returns the lines
    // that came before the long one, then nothing until reset().
    std::vector<std::string> feed(const char* data, std::size_t len);
    std::vector<std::string> feed(const std::string& data);

    // True once a line grew past maxLineBytes. The stream cannot be
    // resynced reliably after that, so the caller drops the connection -
    // Redis closes a client with a "too big inline request" the same way
    // (networking.c). The buffer is freed at once, so memory stays bounded.
    bool overflowed() const { return overflowed_; }

    // Forget everything: use for a new connection.
    void reset();

private:
    void markOverflow();

    std::string buffer_;
    std::size_t maxLineBytes_;
    bool overflowed_ = false;
};

} // namespace f20
