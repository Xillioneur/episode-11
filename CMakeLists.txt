cmake_minimum_required(VERSION 3.11)

project(LumenFidei)

set(CMAKE_CXX_STANDARD 17)

# Find Raylib
find_package(raylib REQUIRED)

add_executable(LumenFidei main.cpp)

target_link_libraries(LumenFidei PRIVATE raylib)

if(APPLE)
    target_link_libraries(LumenFidei PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
