cmake_minimum_required(VERSION 3.21)
project(ctfsm VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "A compile-time finite-state-machine engine for real-time, safety-critical control")

# --- the header-only library ---
add_library(ctfsm INTERFACE)
add_library(ctfsm::ctfsm ALIAS ctfsm)
target_include_directories(ctfsm INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")
target_compile_features(ctfsm INTERFACE cxx_std_17)

# --- tests (on by default only when ctfsm is the top-level project) ---
option(CTFSM_BUILD_TESTS "Build ctfsm tests" ${PROJECT_IS_TOP_LEVEL})
if(CTFSM_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(ctfsm_tests test/test_state_machine.cpp)
  target_link_libraries(ctfsm_tests PRIVATE ctfsm::ctfsm GTest::gtest_main)
  target_compile_options(ctfsm_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME ctfsm_tests COMMAND ctfsm_tests)
endif()

# --- install / export (consumers may also just add_subdirectory or vendor) ---
include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS ctfsm EXPORT ctfsmTargets)
install(EXPORT ctfsmTargets
    FILE ctfsmTargets.cmake
    NAMESPACE ctfsm::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ctfsm)
