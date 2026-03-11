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

# C++ unit tests (no SDL2 dependency)
enable_testing()

set(DEVICE_TESTS
    memory nvic clock_power gpio timer spim twim nvmc
    radio ppi saadc temp rng ecb wdt st7789 i2c_devices
    spi_flash egu
)

foreach(name ${DEVICE_TESTS})
    add_executable(test_device_${name} tests/device/test_${name}.cpp)
    target_include_directories(test_device_${name} PRIVATE src)
    target_compile_options(test_device_${name} PRIVATE -Wall -Wextra -O2)
    add_test(NAME test_device_${name} COMMAND test_device_${name})
endforeach()

# Bus test
add_executable(test_bus tests/test_bus.cpp)
target_include_directories(test_bus PRIVATE src)
target_compile_options(test_bus PRIVATE -Wall -Wextra -O2)
add_test(NAME test_bus COMMAND test_bus)

# CPU test (needs cpu.cpp)
add_executable(test_cpu tests/test_cpu.cpp src/cpu.cpp)
target_include_directories(test_cpu PRIVATE src)
target_compile_options(test_cpu PRIVATE -Wall -Wextra -O2)
add_test(NAME test_cpu COMMAND test_cpu)
