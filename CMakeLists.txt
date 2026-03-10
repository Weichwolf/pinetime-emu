cmake_minimum_required(VERSION 3.20)
project(pinetime-emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 REQUIRED)

add_executable(pinetime-emu
    src/main.cpp
    src/cpu.cpp
)

target_include_directories(pinetime-emu PRIVATE src ${SDL2_INCLUDE_DIRS})
target_link_libraries(pinetime-emu PRIVATE ${SDL2_LIBRARIES})
target_compile_options(pinetime-emu PRIVATE -Wall -Wextra -Wpedantic -O2)
