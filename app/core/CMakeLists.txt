# f20appcore: f20app's decision logic, NO Qt - so the unit tests can drive
# it and the rules of spec §8.1 stay independent of any UI framework.
add_library(f20appcore STATIC
    appstatemachine.cpp
    baselinetracker.cpp
)
target_include_directories(f20appcore PUBLIC .)
