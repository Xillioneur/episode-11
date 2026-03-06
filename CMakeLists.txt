cmake_minimum_required(VERSION 3.11)

project(LumenFidei)

set(CMAKE_CXX_STANDARD 20)

# Find Raylib
find_package(raylib REQUIRED)

add_executable(LumenFidei main.cpp)

target_link_libraries(LumenFidei PRIVATE raylib)

if(APPLE)
    # Explicitly link OpenGL and Core macOS frameworks for Shader support
    target_link_libraries(LumenFidei PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL" "-framework CoreVideo")
endif()
