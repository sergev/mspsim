add_library(testharness STATIC harness.c)
target_link_libraries(testharness PUBLIC msp430core)

foreach(name basic interrupts)
    add_executable(test_${name} test_${name}.c)
    target_link_libraries(test_${name} PRIVATE testharness)
    add_test(NAME ${name} COMMAND test_${name})
endforeach()
