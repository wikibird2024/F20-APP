# Unit tests (doctest). One binary, registered with ctest.
add_executable(f20tests
    main.cpp
    test_framing.cpp
    test_protocol.cpp
    test_errors.cpp
    test_results.cpp
    test_simengine.cpp
    test_appcore.cpp
)
target_include_directories(f20tests SYSTEM PRIVATE vendor)
target_link_libraries(f20tests PRIVATE f20common f20simcore f20appcore)
add_test(NAME f20tests COMMAND f20tests)

f20_warnings(f20tests)

add_subdirectory(qt)  # Qt-side tests (QtTest)
