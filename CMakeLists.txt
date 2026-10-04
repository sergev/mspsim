cmake_minimum_required(VERSION 3.16)
project(mspsim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

add_library(msp430core STATIC
    emulator/emulator.c
    emulator/utilities.c
    emulator/cpu/registers.c
    emulator/cpu/decoder.c
    emulator/cpu/flag_handler.c
    emulator/cpu/formatI.c
    emulator/cpu/formatII.c
    emulator/cpu/formatIII.c
    emulator/cpu/interrupts.c
    emulator/memory/memory.c
    emulator/uart/uart.c
    emulator/debugger/debugger.c
    emulator/debugger/disassembler.c
    emulator/debugger/register_display.c
)
target_include_directories(msp430core PUBLIC emulator)

add_executable(mspsim
    emulator/cli/main.c
    emulator/cli/stub_io.c
)
target_link_libraries(mspsim PRIVATE msp430core)
install(TARGETS mspsim RUNTIME DESTINATION bin)

# Every header must compile on its own (and survive double inclusion).
file(GLOB_RECURSE all_headers CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/emulator/*.h ${CMAKE_SOURCE_DIR}/test/*.h)
set(header_checks)
foreach(header ${all_headers})
    file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR} ${header})
    string(MAKE_C_IDENTIFIER ${rel} id)
    set(src ${CMAKE_BINARY_DIR}/header_check/${id}.c)
    file(CONFIGURE OUTPUT ${src}
        CONTENT "#include \"${header}\"\n#include \"${header}\"\n")
    list(APPEND header_checks ${src})
endforeach()
add_library(header_check OBJECT ${header_checks})
target_include_directories(header_check PRIVATE emulator)

enable_testing()
add_subdirectory(test)
