cmake_minimum_required(VERSION 3.21)

project(ImageViewer LANGUAGES C CXX)

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

# Build the JP2 decoder into the executable, including with shared Qt installs.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(openjpeg
    URL https://github.com/uclouvain/openjpeg/archive/refs/tags/v2.5.4.tar.gz
    URL_HASH SHA256=a695fbe19c0165f295a8531b1e4e855cd94d0875d2f88ec4b61080677e27188a
    SOURCE_SUBDIR unused
)
FetchContent_MakeAvailable(openjpeg)
function(add_bundled_openjpeg)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_CODEC OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_DOC OFF)
    set(BUILD_JPIP OFF)
    set(BUILD_JAVA OFF)
    set(BUILD_VIEWER OFF)
    # Remove legacy cache defaults introduced by OpenJPEG after configuring it.
    foreach(variable IN ITEMS EXECUTABLE_OUTPUT_PATH LIBRARY_OUTPUT_PATH)
        if(NOT DEFINED CACHE{${variable}})
            list(APPEND temporary_cache_variables ${variable})
        endif()
    endforeach()
    set(EXECUTABLE_OUTPUT_PATH "${openjpeg_BINARY_DIR}/bin")
    set(LIBRARY_OUTPUT_PATH "${openjpeg_BINARY_DIR}/bin")
    add_subdirectory(${openjpeg_SOURCE_DIR} ${openjpeg_BINARY_DIR} EXCLUDE_FROM_ALL)
    foreach(variable IN LISTS temporary_cache_variables)
        unset(${variable} CACHE)
    endforeach()
endfunction()
add_bundled_openjpeg()
set_target_properties(openjp2 PROPERTIES AUTOMOC OFF AUTOUIC OFF POSITION_INDEPENDENT_CODE ON)
# Use OpenJPEG's color converters; ICC handling is provided by Qt.
file(WRITE ${openjpeg_BINARY_DIR}/opj_apps_config.h "/* No optional application dependencies. */\n")
qt_add_plugin(iv_jp2 STATIC CLASS_NAME OpenJpegPlugin
    jp2plugin.cpp
    ${openjpeg_SOURCE_DIR}/src/bin/common/color.c
)
target_include_directories(iv_jp2 PRIVATE
    ${openjpeg_SOURCE_DIR}/src/lib/openjp2
    ${openjpeg_BINARY_DIR}/src/lib/openjp2
    ${openjpeg_SOURCE_DIR}/src/bin/common
    ${openjpeg_BINARY_DIR}
)
target_link_libraries(iv_jp2 PRIVATE Qt6::Gui openjp2)

qt_add_executable(iv
    WIN32
    MACOSX_BUNDLE
    main.cpp
)
target_link_libraries(iv PRIVATE Qt6::Widgets iv_jp2)
if(WIN32)
    target_link_libraries(iv PRIVATE dwmapi)
endif()

option(IMAGE_VIEWER_LTO "Enable link-time optimization for release builds" ${WIN32})
if(IMAGE_VIEWER_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(LANGUAGES CXX)
    set_target_properties(iv PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
        INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE
    )
endif()
if(MINGW)
    target_compile_options(iv PRIVATE "$<$<CONFIG:Release,MinSizeRel>:-Os>")
    target_link_options(iv PRIVATE "$<$<CONFIG:Release,MinSizeRel>:-Os;-s>")
elseif(MSVC)
    target_compile_options(iv PRIVATE "$<$<CONFIG:Release,MinSizeRel>:/O1>")
    target_link_options(iv PRIVATE "$<$<CONFIG:Release,MinSizeRel>:/OPT:REF;/OPT:ICF>")
endif()

get_target_property(qt_library_type Qt6::Core TYPE)
if(qt_library_type STREQUAL "STATIC_LIBRARY")
    qt_import_plugins(iv
        INCLUDE_BY_TYPE imageformats Qt6::QJpegPlugin
    )
    if(MINGW)
        target_link_options(iv PRIVATE -static)
    endif()
endif()

option(IMAGE_VIEWER_BUILD_TESTS "Build image-format integration tests" OFF)
if(IMAGE_VIEWER_BUILD_TESTS)
    enable_testing()
    qt_add_executable(iv_image_tests tests/image-formats.cpp)
    target_link_libraries(iv_image_tests PRIVATE Qt6::Widgets iv_jp2)
    if(WIN32)
        target_link_libraries(iv_image_tests PRIVATE dwmapi)
    endif()
    if(qt_library_type STREQUAL "STATIC_LIBRARY")
        qt_import_plugins(iv_image_tests INCLUDE_BY_TYPE imageformats Qt6::QJpegPlugin)
        if(MINGW)
            target_link_options(iv_image_tests PRIVATE -static)
        endif()
    endif()
    add_test(NAME image-formats COMMAND iv_image_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
    if(NOT WIN32)
        set_tests_properties(image-formats PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

install(TARGETS iv
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(NOT qt_library_type STREQUAL "STATIC_LIBRARY")
    qt_generate_deploy_app_script(
        TARGET iv
        OUTPUT_SCRIPT deploy_script
        NO_TRANSLATIONS
    )
    install(SCRIPT ${deploy_script})
endif()
install(FILES README.md DESTINATION .)
install(FILES ${openjpeg_SOURCE_DIR}/LICENSE
    DESTINATION licenses RENAME OpenJPEG.txt)

set(CPACK_PACKAGE_NAME "iv")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Single-window JPEG and JPEG 2000 viewer")
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)
