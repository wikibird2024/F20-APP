# f20bridge-sim: simulator speaking the bridge protocol (spec §4.2).
# Logic lives in a library so the unit tests can link it without sockets.
add_library(f20simcore STATIC
    simengine.cpp
    spectrumsynth.cpp
)
target_include_directories(f20simcore PUBLIC .)
target_link_libraries(f20simcore PUBLIC f20common)
f20_warnings(f20simcore)

add_executable(f20bridge-sim
    main.cpp
    socketserver.cpp
)
# File name on disk is upper case; the CMake target name stays f20bridge-sim.
set_target_properties(f20bridge-sim PROPERTIES OUTPUT_NAME "F20BRIDGE-SIM")
target_link_libraries(f20bridge-sim PRIVATE f20simcore)
f20_warnings(f20bridge-sim)
