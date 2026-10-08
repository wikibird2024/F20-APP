#pragma once
#include <cstddef>
#include <string>

namespace f20 {

// A protocol line made short enough for a log file. Spec 2.4 #6: every
// bridge command and reply is logged, but not full spectra.
// Lines up to maxChars come back unchanged - normal traffic is logged exactly
// as it went over the wire. In a longer JSON line every array with more than
// maxArrayItems elements becomes the text "[N values]" (key order kept); what
// is still too long, and any long non-JSON line, is cut at maxChars with
// "... (N chars)" added. Never throws.
std::string shortenForLog(const std::string& line, std::size_t maxArrayItems = 16,
                          std::size_t maxChars = 1000);

} // namespace f20
