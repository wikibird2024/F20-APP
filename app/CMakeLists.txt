add_subdirectory(core)

# f20app: the Qt 6 operator application (spec §7).
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Charts Sql)

qt_standard_project_setup()

qt_add_executable(f20app
    main.cpp
    mainwindow.cpp
    bridgeclient.cpp
    storage.cpp
    serverlink.cpp
    measurescreen.cpp
    baselinewizard.cpp
    historyscreen.cpp
    diagnosticsscreen.cpp
)
target_link_libraries(f20app PRIVATE
    Qt6::Widgets Qt6::Network Qt6::Charts Qt6::Sql
    f20appcore f20common
)
