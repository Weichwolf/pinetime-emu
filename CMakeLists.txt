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
    spi_flash egu uarte
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

# Integration test (end-to-end peripheral pipelines)
add_executable(test_integration tests/test_integration.cpp)
target_include_directories(test_integration PRIVATE src)
target_compile_options(test_integration PRIVATE -Wall -Wextra -O2)
add_test(NAME test_integration COMMAND test_integration)

# ARM assembly tests (cross-compiled, run in emulator)
find_program(ARM_GCC arm-none-eabi-gcc)
if(ARM_GCC)
    set(ARM_TESTS test_basic test_alu test_thumb32 test_mem test_vfp test_vfp_fused test_vfp_negmul test_it test_exceptions test_peripherals test_fpu_exceptions test_faults)
    set(ARM_TEST_DIR ${CMAKE_SOURCE_DIR}/tests/arm)
    set(ARM_BUILD_DIR ${CMAKE_BINARY_DIR}/arm_tests)
    file(MAKE_DIRECTORY ${ARM_BUILD_DIR})

    find_program(ARM_OBJCOPY arm-none-eabi-objcopy REQUIRED)
    set(ARM_CFLAGS -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -nostdlib -ffreestanding)
    set(ARM_LDFLAGS -T ${ARM_TEST_DIR}/test.ld -nostdlib)

    foreach(name ${ARM_TESTS})
        add_custom_command(
            OUTPUT ${ARM_BUILD_DIR}/${name}.bin
            COMMAND ${ARM_GCC} ${ARM_CFLAGS} -c ${ARM_TEST_DIR}/${name}.s -o ${ARM_BUILD_DIR}/${name}.o
            COMMAND ${ARM_GCC} ${ARM_CFLAGS} ${ARM_LDFLAGS} ${ARM_BUILD_DIR}/${name}.o -o ${ARM_BUILD_DIR}/${name}.elf
            COMMAND ${ARM_OBJCOPY} -O binary ${ARM_BUILD_DIR}/${name}.elf ${ARM_BUILD_DIR}/${name}.bin
            DEPENDS ${ARM_TEST_DIR}/${name}.s ${ARM_TEST_DIR}/test.ld
            COMMENT "Building ARM test: ${name}"
        )
        add_custom_target(arm_${name} ALL DEPENDS ${ARM_BUILD_DIR}/${name}.bin)
        add_test(NAME arm_${name} COMMAND pinetime-emu --headless --timeout 5000 ${ARM_BUILD_DIR}/${name}.bin)
        set_tests_properties(arm_${name} PROPERTIES DEPENDS pinetime-emu)
    endforeach()
else()
    message(WARNING "arm-none-eabi-gcc not found — ARM assembly tests will be skipped")
endif()
