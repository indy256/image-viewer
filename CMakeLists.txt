cmake_minimum_required(VERSION 3.21)

project(ImageViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(image-viewer WIN32 main.cpp)
target_link_libraries(image-viewer PRIVATE Qt6::Widgets)
