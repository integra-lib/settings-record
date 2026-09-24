cmake_minimum_required(VERSION 3.21)

project(
  hwlib_settings_record
  VERSION 0.2.0
  LANGUAGES CXX)

# When this repository is built on its own (its own CI), the sibling component
# is not there yet. Fetch it at the version this release was verified against; a
# consumer never reaches this branch, because it supplies the component itself.
if(PROJECT_IS_TOP_LEVEL AND NOT TARGET Hwlib::crc)
  include(FetchContent)
  set(HWLIB_REMOTE
      "https://gitlab.integrasources.com/internal-projects/a000-hwlib"
      CACHE
        STRING
        "Base URL the sibling component is fetched from in standalone builds")
  FetchContent_Declare(
    hwlib_crc
    GIT_REPOSITORY ${HWLIB_REMOTE}/algorithms/crc.git
    GIT_TAG v0.2.0)
  FetchContent_MakeAvailable(hwlib_crc)
endif()

# The dependency is an equal component, not a submodule of this one: the
# consumer adds it next to this repository. Checking it here turns a mismatched
# pair into a configure error naming both versions, instead of a build that
# silently verifies settings with a different checksum than the one that wrote
# them.
function(hwlib_require name minimum below)
  if(NOT TARGET Hwlib::${name})
    message(
      FATAL_ERROR
        "settings_record requires Hwlib::${name}. Add the ${name} component next to this one "
        "and add_subdirectory() it before this directory.")
  endif()
  get_target_property(found Hwlib::${name} HWLIB_VERSION)
  if(NOT found)
    message(
      FATAL_ERROR
        "Hwlib::${name} carries no HWLIB_VERSION property — it is too old or not an hwlib component."
    )
  endif()
  if(found VERSION_LESS minimum OR NOT found VERSION_LESS below)
    message(
      FATAL_ERROR
        "settings_record needs Hwlib::${name} >= ${minimum} and < ${below}, found ${found}."
    )
  endif()
endfunction()

hwlib_require(crc 0.2.0 0.3.0)

add_library(hwlib_settings_record INTERFACE)
add_library(Hwlib::settings_record ALIAS hwlib_settings_record)

target_include_directories(hwlib_settings_record
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(hwlib_settings_record INTERFACE cxx_std_20)
target_link_libraries(hwlib_settings_record INTERFACE Hwlib::crc)

# Dependants read the version from this property rather than from a variable: a
# variable set here is invisible in a sibling directory, and a stale one left in
# the parent scope would be read instead — a version check that silently passes.
set_target_properties(hwlib_settings_record PROPERTIES HWLIB_VERSION
                                                       ${PROJECT_VERSION})

if(PROJECT_IS_TOP_LEVEL)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2)
  FetchContent_MakeAvailable(googletest)

  include(GoogleTest)
  enable_testing()

  add_executable(settings-record-test tests/settings_record_test.cpp)
  target_link_libraries(settings-record-test PRIVATE Hwlib::settings_record
                                                     GTest::gtest_main)
  gtest_discover_tests(settings-record-test)
endif()
