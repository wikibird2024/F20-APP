# f20common: protocol messages, line framing, error model.
# NO Qt in this library — the Windows bridge links it and must stay Qt-free.
add_library(f20common STATIC
    src/framing.cpp
    src/errors.cpp
    src/jsonread.cpp
    src/logtext.cpp
    src/protocol.cpp
    src/results.cpp
)
target_include_directories(f20common PUBLIC include)
# SYSTEM: warnings inside the vendored nlohmann header are not ours to fix.
target_include_directories(f20common SYSTEM PUBLIC vendor)
f20_warnings(f20common)
