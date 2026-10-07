#pragma once
#include <cstddef>
#include <string>

namespace f20app {

// Makes operator text safe as part of a file name: keeps A-Z a-z 0-9 _ -,
// turns every other byte into '_', cuts to maxLength; empty gives "sample".
// Without it a sample id like "../x" writes outside spectra/, and "A:B"
// fails on Windows.
std::string safeFileNamePart(const std::string& text, std::size_t maxLength = 40);

} // namespace f20app
