cmake_minimum_required(VERSION 3.21)

project(MpdBee VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPDCLIENT REQUIRED IMPORTED_TARGET libmpdclient)

qt_add_executable(mpdbee
    main.cpp
    mpdcontroller.cpp
    mpdcontroller.h
    artworkprovider.cpp
    artworkprovider.h
)

qt_add_qml_module(mpdbee
    URI MpdBee
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(mpdbee PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    PkgConfig::MPDCLIENT
)

install(TARGETS mpdbee
    RUNTIME DESTINATION bin
)
