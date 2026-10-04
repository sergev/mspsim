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
    src/emulator.c
    src/io.c
    src/utilities.c
    src/cpu/registers.c
    src/cpu/decoder.c
    src/cpu/flag_handler.c
    src/cpu/formatI.c
    src/cpu/formatII.c
    src/cpu/formatIII.c
    src/cpu/interrupts.c
    src/loader/loader.c
    src/loader/symbols.c
    src/mem/memory.c
    src/uart/uart.c
    src/debug/debugger.c
    src/debug/disassembler.c
    src/debug/register_display.c
    src/debug/trace.c
)
target_include_directories(msp430core PUBLIC src)

# Vendored line editor, BSD-2-Clause.
add_library(linenoise STATIC src/linenoise/linenoise.c)
target_include_directories(linenoise PUBLIC src/linenoise)

add_executable(mspsim src/main.c)
target_link_libraries(mspsim PRIVATE msp430core linenoise)
install(TARGETS mspsim RUNTIME DESTINATION bin)

# Every header must compile on its own (and survive double inclusion).
file(GLOB_RECURSE all_headers CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.h ${CMAKE_SOURCE_DIR}/test/*.h)
list(FILTER all_headers EXCLUDE REGEX "/src/linenoise/")
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
target_include_directories(header_check PRIVATE src)

enable_testing()
add_subdirectory(test)
