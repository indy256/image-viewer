cmake_minimum_required(VERSION 3.21)

project(ImageViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(IMAGE_VIEWER_STATIC_RUNTIME "Link MinGW Qt dependencies statically" OFF)
if(MINGW AND IMAGE_VIEWER_STATIC_RUNTIME)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    set(PKG_CONFIG_ARGN --static)
    # MSYS2's font CMake packages select shared HarfBuzz and omit some
    # FreeType dependencies. pkg-config supplies the complete static link set.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(STATIC_FONTS REQUIRED IMPORTED_TARGET harfbuzz freetype2)
    foreach(font_library IN ITEMS Freetype Harfbuzz)
        add_library(WrapSystem${font_library}::WrapSystem${font_library} INTERFACE IMPORTED)
        target_link_libraries(WrapSystem${font_library}::WrapSystem${font_library}
            INTERFACE PkgConfig::STATIC_FONTS
        )
    endforeach()
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(image-viewer
    WIN32
    MACOSX_BUNDLE
    main.cpp
)
target_link_libraries(image-viewer PRIVATE Qt6::Widgets)
get_target_property(qt_library_type Qt6::Core TYPE)
if(qt_library_type STREQUAL "STATIC_LIBRARY")
    qt_import_plugins(image-viewer
        INCLUDE_BY_TYPE imageformats Qt6::QJpegPlugin
    )
    if(MINGW)
        target_link_options(image-viewer PRIVATE -static)
    endif()
endif()

install(TARGETS image-viewer
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(NOT qt_library_type STREQUAL "STATIC_LIBRARY")
    qt_generate_deploy_app_script(
        TARGET image-viewer
        OUTPUT_SCRIPT deploy_script
        NO_TRANSLATIONS
    )
    install(SCRIPT ${deploy_script})
endif()
install(FILES README.md DESTINATION .)

set(CPACK_PACKAGE_NAME "image-viewer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Single-window JPEG viewer")
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)
