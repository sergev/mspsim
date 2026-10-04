add_library(testharness STATIC harness.c asm.c)
target_link_libraries(testharness PUBLIC msp430core)
target_compile_definitions(testharness PUBLIC TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

foreach(name basic interrupts memory uart run debugger trace loader openmsp430 asm cpu)
    add_executable(test_${name} test_${name}.c)
    target_link_libraries(test_${name} PRIVATE testharness)
    add_test(NAME ${name} COMMAND test_${name})
endforeach()

add_test(NAME cli COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/cli_test.sh $<TARGET_FILE:mspsim>)

# Firmware tests: C programs built with msp430-elf-gcc, when it is installed.
find_program(MSP430_GCC msp430-elf-gcc)
set(MSP430_FLAGS "-mmcu=msp430g2553 -Os" CACHE STRING
    "msp430-elf-gcc flags for the firmware tests (add -I/-L for the TI support files)")
if(MSP430_GCC)
    separate_arguments(msp430_flags UNIX_COMMAND "${MSP430_FLAGS}")
    set(fw_dir ${CMAKE_CURRENT_SOURCE_DIR}/firmware)
    foreach(fw hello arith)
        add_custom_command(OUTPUT ${fw}.elf
            COMMAND ${MSP430_GCC} ${msp430_flags} -o ${fw}.elf ${fw_dir}/${fw}.c
            DEPENDS ${fw_dir}/${fw}.c ${fw_dir}/sim.h)
        add_custom_target(firmware_${fw} ALL DEPENDS ${fw}.elf)
        add_test(NAME firmware_${fw}
            COMMAND sh ${fw_dir}/run.sh $<TARGET_FILE:mspsim>
                    ${CMAKE_CURRENT_BINARY_DIR}/${fw}.elf ${fw_dir}/${fw}.expected)
    endforeach()
else()
    message(STATUS "msp430-elf-gcc not found: firmware tests skipped")
endif()
