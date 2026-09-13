cmake_minimum_required(VERSION 3.21)

project(ImageViewer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(IMAGE_VIEWER_STATIC_RUNTIME "Link the Windows compiler runtime statically" OFF)
if(MSVC AND IMAGE_VIEWER_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(MINGW AND IMAGE_VIEWER_STATIC_RUNTIME)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    set(PKG_CONFIG_ARGN --static)
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

# Bundle Qt's WebP handler so support does not depend on installed plugins.
FetchContent_Declare(libwebp
    URL https://github.com/webmproject/libwebp/archive/refs/tags/v1.6.0.tar.gz
    URL_HASH SHA256=93a852c2b3efafee3723efd4636de855b46f9fe1efddd607e1f42f60fc8f2136
    SOURCE_SUBDIR unused
)
FetchContent_Declare(qtimageformats
    URL https://github.com/qt/qtimageformats/archive/refs/tags/v6.8.3.tar.gz
    URL_HASH SHA256=ad5039af9382395641e68905e26d5e29c6fd4e02a93214c8c9df3ef88c16a26f
    SOURCE_SUBDIR unused
)
FetchContent_MakeAvailable(libwebp qtimageformats)
function(add_bundled_webp)
    set(BUILD_SHARED_LIBS OFF)
    set(WEBP_LINK_STATIC ON)
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_AUTOUIC OFF)
    foreach(tool IN ITEMS ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP
            WEBPINFO WEBPMUX EXTRAS WEBP_JS FUZZTEST)
        set(WEBP_BUILD_${tool} OFF)
    endforeach()
    set(WEBP_BUILD_LIBWEBPMUX ON)
    add_subdirectory(${libwebp_SOURCE_DIR} ${libwebp_BINARY_DIR} EXCLUDE_FROM_ALL)
endfunction()
add_bundled_webp()
set(webp_plugin_dir ${qtimageformats_SOURCE_DIR}/src/plugins/imageformats/webp)
qt_add_plugin(iv_webp STATIC CLASS_NAME QWebpPlugin
    ${webp_plugin_dir}/main.cpp
    ${webp_plugin_dir}/qwebphandler.cpp
    ${webp_plugin_dir}/qwebphandler_p.h
)
target_link_libraries(iv_webp PRIVATE Qt6::Gui webp webpdemux libwebpmux)

FetchContent_Declare(libde265
    URL https://github.com/strukturag/libde265/archive/refs/tags/v1.1.2.tar.gz
    URL_HASH SHA256=982f7838cc25aa6bda7fd33b9b3a05621d0f9b8456dc495d5fb4977fed6dcdbc
    SOURCE_SUBDIR unused
)
FetchContent_Declare(libheif
    URL https://github.com/strukturag/libheif/archive/refs/tags/v1.23.4.tar.gz
    URL_HASH SHA256=ce7739356637b7371dcc0ae876027f6f692de9c9ace8cd0e9ed8d79a01ea61fe
    SOURCE_SUBDIR unused
)
FetchContent_Declare(libaom
    URL https://storage.googleapis.com/aom-releases/libaom-3.13.3.tar.gz
    URL_HASH SHA256=446a4ae9741cb8f3eeb98c949d25f91b48cb2b8569cae975c4b737392e9024fc
    SOURCE_SUBDIR unused
)
FetchContent_MakeAvailable(libde265 libheif libaom)
function(add_bundled_heic)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_AUTOUIC OFF)
    set(CONFIG_AV1_ENCODER 0 CACHE STRING "Build AV1 encoder" FORCE)
    set(CONFIG_AV1_DECODER 1 CACHE STRING "Build AV1 decoder" FORCE)
    foreach(feature IN ITEMS DOCS EXAMPLES TESTS TOOLS)
        set(ENABLE_${feature} OFF CACHE BOOL "Build libaom ${feature}" FORCE)
    endforeach()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory(${libaom_SOURCE_DIR} ${libaom_BINARY_DIR} EXCLUDE_FROM_ALL)
    foreach(feature IN ITEMS SDL DECODER ENCODER SHERLOCK265 INTERNAL_DEVELOPMENT_TOOLS)
        set(ENABLE_${feature} OFF)
    endforeach()
    set(WITH_FUZZERS OFF)
    # Both codecs declare a global 'dist' target; namespace libde265's copy.
    file(READ ${libde265_SOURCE_DIR}/CMakeLists.txt de265_cmake)
    if(de265_cmake MATCHES "add_custom_target\\(dist")
        string(REPLACE "add_custom_target(dist" "add_custom_target(libde265_dist"
            de265_cmake "${de265_cmake}")
        file(WRITE ${libde265_SOURCE_DIR}/CMakeLists.txt "${de265_cmake}")
    endif()
    add_subdirectory(${libde265_SOURCE_DIR} ${libde265_BINARY_DIR} EXCLUDE_FROM_ALL)
    target_compile_definitions(de265 INTERFACE LIBDE265_STATIC_BUILD)
    target_include_directories(de265 INTERFACE
        $<BUILD_INTERFACE:${libde265_SOURCE_DIR}>
        $<BUILD_INTERFACE:${libde265_BINARY_DIR}>
    )
    # Let libheif find the bundled target before looking for an installed codec.
    set(LIBDE265_DIR ${libde265_BINARY_DIR}/bundled-config)
    file(MAKE_DIRECTORY ${LIBDE265_DIR})
    file(WRITE ${LIBDE265_DIR}/LIBDE265Config.cmake
        "set(LIBDE265_FOUND TRUE)\nset(LIBDE265_LIBRARIES de265)\nset(LIBDE265_INCLUDE_DIRS \"${libde265_SOURCE_DIR}\" \"${libde265_BINARY_DIR}\")\n")
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
    set(AOM_DIR ${libaom_BINARY_DIR}/bundled-config)
    file(MAKE_DIRECTORY ${AOM_DIR})
    file(WRITE ${AOM_DIR}/AOMConfig.cmake
        "set(AOM_FOUND TRUE)\nset(AOM_DECODER_FOUND TRUE)\nset(AOM_LIBRARIES aom)\nset(AOM_INCLUDE_DIRS \"${libaom_SOURCE_DIR}\")\n")
    set(WITH_LIBDE265 ON)
    set(WITH_LIBDE265_PLUGIN OFF)
    foreach(codec IN ITEMS X265 KVAZAAR UVG266 VVDEC VVENC X264 OpenH264_DECODER
            DAV1D AOM_ENCODER SvtEnc RAV1E JPEG_DECODER JPEG_ENCODER
            OpenJPEG_ENCODER OpenJPEG_DECODER FFMPEG_DECODER OPENJPH_ENCODER
            UNCOMPRESSED_CODEC WEBCODECS LIBSHARPYUV)
        set(WITH_${codec} OFF)
    endforeach()
    set(WITH_AOM_DECODER ON)
    set(WITH_AOM_DECODER_PLUGIN OFF)
    set(ENABLE_PLUGIN_LOADING OFF)
    set(WITH_EXAMPLES OFF)
    set(WITH_GDK_PIXBUF OFF)
    set(WITH_HEADER_COMPRESSION OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_DOCUMENTATION OFF)
    add_subdirectory(${libheif_SOURCE_DIR} ${libheif_BINARY_DIR} EXCLUDE_FROM_ALL)
endfunction()
add_bundled_heic()
qt_add_plugin(iv_heic STATIC CLASS_NAME HeicPlugin heicplugin.cpp)
target_include_directories(iv_heic PRIVATE ${libheif_SOURCE_DIR}/libheif/api ${libheif_BINARY_DIR})
target_link_libraries(iv_heic PRIVATE Qt6::Gui heif)

qt_add_executable(iv
    WIN32
    MACOSX_BUNDLE
    main.cpp
)
qt_add_resources(iv "iv_icons"
    PREFIX "/iv"
    BASE "packaging"
    FILES packaging/image-viewer.png
)
if(WIN32)
    target_sources(iv PRIVATE packaging/iv.rc)
    target_include_directories(iv PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/packaging")
elseif(APPLE)
    target_sources(iv PRIVATE packaging/iv.icns)
    set_source_files_properties(packaging/iv.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    set_target_properties(iv PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "iv.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
    )
endif()
get_target_property(qt_library_type Qt6::Core TYPE)
function(configure_viewer_target target)
    target_link_libraries(${target} PRIVATE Qt6::Widgets iv_jp2 iv_webp iv_heic)
    if(WIN32)
        target_link_libraries(${target} PRIVATE dwmapi)
    endif()
    if(qt_library_type STREQUAL "STATIC_LIBRARY")
        qt_import_plugins(${target} INCLUDE_BY_TYPE imageformats Qt6::QJpegPlugin)
        if(MINGW)
            target_link_options(${target} PRIVATE -static)
        endif()
    endif()
endfunction()
configure_viewer_target(iv)

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
    # GCC PR106103: constructor/destructor decloning breaks MinGW -Os + LTO.
    target_compile_options(iv PRIVATE "$<$<CONFIG:Release,MinSizeRel>:-Os;-fno-declone-ctor-dtor>")
    target_link_options(iv PRIVATE "$<$<CONFIG:Release,MinSizeRel>:-Os;-fno-declone-ctor-dtor;-s>")
elseif(MSVC)
    target_compile_options(iv PRIVATE "$<$<CONFIG:Release,MinSizeRel>:/O1>")
    target_link_options(iv PRIVATE "$<$<CONFIG:Release,MinSizeRel>:/OPT:REF;/OPT:ICF>")
endif()

install(TARGETS iv
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES packaging/image-viewer.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/image-viewer.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
endif()
if(NOT qt_library_type STREQUAL "STATIC_LIBRARY")
    set(deploy_options)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Qt normally excludes system libraries. Bundle the X11 client runtime
        # and vendor-neutral GL loader; GPU drivers remain supplied by the host.
        list(APPEND deploy_options POST_INCLUDE_REGEXES
            "/libOpenGL[.]so[.]"
            "/libGLdispatch[.]so[.]"
            "/libxcb.*[.]so[.]"
            "/libxkbcommon.*[.]so[.]"
            "/libX.*[.]so[.]"
            "/libICE[.]so[.]"
            "/libSM[.]so[.]")
        # A bundled glibc cannot safely use the host's ELF loader. Exclude it
        # by SONAME before traversal, regardless of system-directory aliases.
        # Qt serializes these arguments unquoted into its deployment script;
        # use separate patterns without parentheses to preserve CMake parsing.
        list(APPEND deploy_options PRE_EXCLUDE_REGEXES "^ld-linux.*[.]so[.]")
        foreach(library IN ITEMS c m mvec pthread dl rt resolv util anl BrokenLocale thread_db)
            list(APPEND deploy_options "^lib${library}[.]so[.]")
        endforeach()
        list(APPEND deploy_options "^libnss_.*[.]so[.]")
    endif()
    qt_generate_deploy_app_script(
        TARGET iv
        OUTPUT_SCRIPT deploy_script
        NO_TRANSLATIONS
        ${deploy_options}
    )
    install(SCRIPT ${deploy_script})
endif()
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
install(FILES ${openjpeg_SOURCE_DIR}/LICENSE
    DESTINATION licenses RENAME OpenJPEG.txt)

install(FILES ${libwebp_SOURCE_DIR}/COPYING ${libwebp_SOURCE_DIR}/PATENTS
    DESTINATION licenses/libwebp)
install(DIRECTORY ${qtimageformats_SOURCE_DIR}/LICENSES/
    DESTINATION licenses/QtImageFormats)
install(FILES ${libheif_SOURCE_DIR}/COPYING DESTINATION licenses RENAME libheif.txt)
install(FILES ${libde265_SOURCE_DIR}/COPYING DESTINATION licenses RENAME libde265.txt)
install(FILES ${libaom_SOURCE_DIR}/LICENSE ${libaom_SOURCE_DIR}/PATENTS DESTINATION licenses/libaom)

set(CPACK_PACKAGE_NAME "iv")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Single-window JPEG, JPEG 2000, WebP, HEIC and AVIF viewer")
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)
