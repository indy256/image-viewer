cmake_minimum_required(VERSION 3.21)

project(ImageViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(image-viewer
    WIN32
    MACOSX_BUNDLE
    main.cpp
)
target_link_libraries(image-viewer PRIVATE Qt6::Widgets)

install(TARGETS image-viewer
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
qt_generate_deploy_app_script(
    TARGET image-viewer
    OUTPUT_SCRIPT deploy_script
    NO_TRANSLATIONS
)
install(SCRIPT ${deploy_script})
install(FILES README.md DESTINATION .)

set(CPACK_PACKAGE_NAME "image-viewer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Single-window JPEG viewer")
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)
