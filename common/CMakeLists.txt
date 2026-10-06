# f20common: protocol messages, line framing, error model.
# NO Qt in this library — the Windows bridge links it and must stay Qt-free.
add_library(f20common STATIC
    src/framing.cpp
    src/errors.cpp
    src/protocol.cpp
    src/results.cpp
)
target_include_directories(f20common PUBLIC include vendor)
