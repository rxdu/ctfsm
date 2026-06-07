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

# --- examples (runnable references; also registered as tests above) ---
option(CTFSM_BUILD_EXAMPLES "Build ctfsm examples" ${PROJECT_IS_TOP_LEVEL})
if(CTFSM_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# --- install / export / package (gated; off when ctfsm is a subproject) ---
# Three integration paths are supported:
#   1. source       : add_subdirectory(ctfsm) + link ctfsm::ctfsm
#   2. find_package : install, then find_package(ctfsm) + link ctfsm::ctfsm
#   3. deb          : cpack -> .deb, then dpkg -i, then find_package(ctfsm)
option(CTFSM_INSTALL "Generate install + package rules" ${PROJECT_IS_TOP_LEVEL})
if(CTFSM_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  set(CTFSM_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/ctfsm)

  install(TARGETS ctfsm EXPORT ctfsmTargets
      INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY include/ctfsm DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT ctfsmTargets
      FILE ctfsmTargets.cmake
      NAMESPACE ctfsm::
      DESTINATION ${CTFSM_CMAKEDIR})

  configure_package_config_file(
      cmake/ctfsmConfig.cmake.in
      "${CMAKE_CURRENT_BINARY_DIR}/ctfsmConfig.cmake"
      INSTALL_DESTINATION ${CTFSM_CMAKEDIR})
  write_basic_package_version_file(
      "${CMAKE_CURRENT_BINARY_DIR}/ctfsmConfigVersion.cmake"
      VERSION ${PROJECT_VERSION}
      COMPATIBILITY SameMajorVersion          # header-only: any 0.x consumes any 0.y
      ARCH_INDEPENDENT)
  install(FILES
      "${CMAKE_CURRENT_BINARY_DIR}/ctfsmConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/ctfsmConfigVersion.cmake"
      DESTINATION ${CTFSM_CMAKEDIR})

  # --- CPack: produce an architecture-independent .deb (header-only) ---
  set(CPACK_PACKAGE_NAME "ctfsm")
  set(CPACK_PACKAGE_VENDOR "Ruixiang Du")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "A compile-time finite-state-machine engine for real-time, safety-critical control.")
  set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
  set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
  set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
  set(CPACK_GENERATOR "DEB")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ruixiang Du")
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "all")  # header-only -> arch-independent
  set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")
  set(CPACK_SOURCE_IGNORE_FILES /.git /build.* /\\.DS_Store)
  include(CPack)
endif()
