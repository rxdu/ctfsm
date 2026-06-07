# Examples — runnable references that also self-check, so they double as tests.
foreach(ex turnstile robot_control)
  add_executable(ctfsm_example_${ex} ${ex}.cpp)
  target_link_libraries(ctfsm_example_${ex} PRIVATE ctfsm::ctfsm)
  target_compile_options(ctfsm_example_${ex} PRIVATE -Wall -Wextra -Wpedantic)
  if(CTFSM_BUILD_TESTS)
    add_test(NAME example_${ex} COMMAND ctfsm_example_${ex})
  endif()
endforeach()
