cmake_minimum_required(VERSION 3.19)
project(FileScanner LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Gui)

qt_standard_project_setup()

add_library(FileScannerDLL SHARED
    core/FileScannerDLL.cpp
    core/FileScannerDLL.h
)
qt_add_executable(FileScanner
    WIN32 MACOSX_BUNDLE
    main.cpp
    filescanner.cpp
    filescanner.h
    filescanner.ui
)

target_include_directories(FileScanner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)
target_link_libraries(FileScanner
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Gui
        FileScannerDLL
)

install(TARGETS FileScanner
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET FileScanner
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
