add_library(testharness STATIC harness.c)
target_link_libraries(testharness PUBLIC msp430core)

foreach(name basic interrupts memory uart run debugger trace)
    add_executable(test_${name} test_${name}.c)
    target_link_libraries(test_${name} PRIVATE testharness)
    add_test(NAME ${name} COMMAND test_${name})
endforeach()

add_test(NAME cli COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/cli_test.sh $<TARGET_FILE:mspsim>)
