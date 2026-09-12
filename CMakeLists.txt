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
FetchContent_Declare(jasper
    URL https://github.com/jasper-software/jasper/archive/refs/tags/version-4.2.8.tar.gz
    URL_HASH SHA256=987e8c8b4afcff87553833b6f0fa255b5556a0ecc617b45ee1882e10c1b5ec14
    SOURCE_SUBDIR unused
)
FetchContent_Declare(qtimageformats
    URL https://github.com/qt/qtimageformats/archive/refs/tags/v6.8.3.tar.gz
    URL_HASH SHA256=ad5039af9382395641e68905e26d5e29c6fd4e02a93214c8c9df3ef88c16a26f
    SOURCE_SUBDIR unused
)
FetchContent_MakeAvailable(jasper qtimageformats)
set(JAS_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_DOC OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_LIBJPEG OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_LIBHEIF OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_OPENGL OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_MULTITHREADING_SUPPORT OFF CACHE BOOL "" FORCE)
foreach(codec IN ITEMS BMP JPG HEIC MIF PGX PNM RAS)
    set(JAS_INCLUDE_${codec}_CODEC OFF CACHE BOOL "" FORCE)
endforeach()
function(add_bundled_jasper)
    # JasPer checks the top-level paths, so it rejects our usual -B build layout.
    # Its own source and binary directories remain separate under build/_deps.
    set(ALLOW_IN_SOURCE_BUILD ON)
    add_subdirectory(${jasper_SOURCE_DIR} ${jasper_BINARY_DIR} EXCLUDE_FROM_ALL)
endfunction()
add_bundled_jasper()
set_target_properties(libjasper PROPERTIES AUTOMOC OFF AUTOUIC OFF)
set(jp2_source_dir ${qtimageformats_SOURCE_DIR}/src/plugins/imageformats/jp2)
qt_add_plugin(iv_jp2 STATIC CLASS_NAME QJp2Plugin
    ${jp2_source_dir}/main.cpp
    ${jp2_source_dir}/qjp2handler.cpp
    ${jp2_source_dir}/qjp2handler_p.h
)
target_include_directories(iv_jp2 PRIVATE
    ${jasper_SOURCE_DIR}/src/libjasper/include
    ${jasper_BINARY_DIR}/src/libjasper/include
)
target_link_libraries(iv_jp2 PRIVATE Qt6::Gui libjasper)

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
        ${qtimageformats_SOURCE_DIR}/tests/shared/images/jp2/logo.jp2)
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
install(FILES ${jasper_SOURCE_DIR}/LICENSE.txt
    DESTINATION licenses RENAME JasPer.txt)
install(FILES ${qtimageformats_SOURCE_DIR}/LICENSES/LGPL-3.0-only.txt
    DESTINATION licenses RENAME QtImageFormats-LGPL-3.0.txt)

set(CPACK_PACKAGE_NAME "iv")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Single-window JPEG and JPEG 2000 viewer")
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)
