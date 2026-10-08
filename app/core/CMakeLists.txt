# f20appcore: f20app's decision logic, NO Qt - so the unit tests can drive
# it and the rules of spec §7 stay independent of any UI framework.
add_library(f20appcore STATIC
    appstatemachine.cpp
    baselinetracker.cpp
    devicecheck.cpp
    filenames.cpp
    measuregate.cpp
)
target_include_directories(f20appcore PUBLIC .)
f20_warnings(f20appcore)
