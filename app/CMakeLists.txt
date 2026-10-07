add_subdirectory(core)

# f20app: the Qt 6 operator application (spec §6).
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Charts Sql)

qt_standard_project_setup()

# The bridge client and the storage in a library of their own, so the Qt
# unit tests (tests/qt) can link them without the screens.
add_library(f20appqt STATIC
    bridgeclient.cpp
    storage.cpp
    mqtttransport.h # interface only; listed so moc sees it
)
target_include_directories(f20appqt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(f20appqt PUBLIC Qt6::Network Qt6::Sql f20common)
f20_warnings(f20appqt)

# MQTT link to the server (spec §6.6) on Eclipse Paho MQTT C++. Optional:
# without Paho, f20app still builds and only logs what it would send.
option(F20_WITH_MQTT "Build the MQTT link to the server (needs Eclipse Paho MQTT C++)" ON)
if(F20_WITH_MQTT)
    find_package(PahoMqttCpp CONFIG QUIET)
    if(PahoMqttCpp_FOUND)
        target_sources(f20appqt PRIVATE pahomqtttransport.cpp)
        # vcpkg may provide only the static library.
        target_link_libraries(f20appqt PUBLIC
            $<IF:$<TARGET_EXISTS:PahoMqttCpp::paho-mqttpp3>,PahoMqttCpp::paho-mqttpp3,PahoMqttCpp::paho-mqttpp3-static>)
        target_compile_definitions(f20appqt PUBLIC F20_HAS_MQTT)
        set(F20_HAS_MQTT ON PARENT_SCOPE) # tests/ adds the Paho tests
        message(STATUS "MQTT: Eclipse Paho MQTT C++ ${PahoMqttCpp_VERSION}")
    else()
        message(WARNING "Eclipse Paho MQTT C++ not found - f20app builds without MQTT. "
                        "Linux: sudo apt install libpaho-mqttpp-dev libpaho-mqtt-dev; "
                        "Windows: vcpkg install paho-mqttpp3")
    endif()
endif()

qt_add_executable(f20app
    main.cpp
    mainwindow.cpp
    serverlink.cpp
    measurescreen.cpp
    baselinewizard.cpp
    historyscreen.cpp
    diagnosticsscreen.cpp
)
# File name on disk is upper case; the CMake target name stays f20app.
set_target_properties(f20app PROPERTIES OUTPUT_NAME "F20APP")
target_link_libraries(f20app PRIVATE
    Qt6::Widgets Qt6::Charts
    f20appqt f20appcore
)
target_compile_definitions(f20app PRIVATE F20_APP_VERSION="${PROJECT_VERSION}")
f20_warnings(f20app)

# Development: f20.ini and the dummy recipes next to the executable, where
# f20app looks for its config. Copied again when they change.
configure_file("${PROJECT_SOURCE_DIR}/f20.ini" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/f20.ini" COPYONLY)
file(GLOB recipeFiles CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/recipes/*.fmrcp")
foreach(recipe IN LISTS recipeFiles)
    get_filename_component(recipeName "${recipe}" NAME)
    configure_file("${recipe}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/recipes/${recipeName}" COPYONLY)
endforeach()
