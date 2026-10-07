# f20apptests (QtTest): BridgeClient against an in-process fake bridge,
# Storage on a temporary file. Registered with ctest beside f20tests.
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(f20apptests
    main.cpp
    test_bridgeclient.cpp
    test_storage.cpp
)
set_target_properties(f20apptests PROPERTIES AUTOMOC ON)
target_link_libraries(f20apptests PRIVATE Qt6::Test f20appqt)
# Paho wrapper against the local broker (skipped when none is running).
if(F20_HAS_MQTT)
    target_sources(f20apptests PRIVATE test_pahomqtttransport.cpp)
endif()
f20_warnings(f20apptests)
add_test(NAME f20apptests COMMAND f20apptests)
