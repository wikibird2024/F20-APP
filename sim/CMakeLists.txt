# f20bridge-sim: simulator speaking the bridge protocol (spec §4.2).
# Logic lives in a library so the unit tests can link it without sockets.
add_library(f20simcore STATIC
    simengine.cpp
    spectrumsynth.cpp
)
target_include_directories(f20simcore PUBLIC .)
target_link_libraries(f20simcore PUBLIC f20common)

add_executable(f20bridge-sim
    main.cpp
    socketserver.cpp
)
target_link_libraries(f20bridge-sim PRIVATE f20simcore)
