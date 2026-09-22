cmake_minimum_required(VERSION 3.21)

project(
  integra_settings_record
  VERSION 0.1.0
  LANGUAGES CXX)

# When this repository is built on its own (its own CI), the sibling component
# is not there yet. Fetch it at the version this release was verified against; a
# consumer never reaches this branch, because it supplies the component itself.
if(PROJECT_IS_TOP_LEVEL AND NOT TARGET Integra::crc)
  include(FetchContent)
  set(INTEGRA_REMOTE
      "git@gitlab.integrasources.com:6111/internal-projects/integra-lib"
      CACHE
        STRING
        "Base URL the sibling component is fetched from in standalone builds")
  FetchContent_Declare(
    integra_crc
    GIT_REPOSITORY ${INTEGRA_REMOTE}/crc.git
    GIT_TAG v0.1.0)
  FetchContent_MakeAvailable(integra_crc)
endif()

# The dependency is an equal component, not a submodule of this one: the
# consumer adds it next to this repository. Checking it here turns a mismatched
# pair into a configure error naming both versions, instead of a build that
# silently verifies settings with a different checksum than the one that wrote
# them.
function(integra_require name minimum below)
  if(NOT TARGET Integra::${name})
    message(
      FATAL_ERROR
        "settings_record requires Integra::${name}. Add the ${name} component next to this one "
        "and add_subdirectory() it before this directory.")
  endif()
  get_target_property(found Integra::${name} INTEGRA_VERSION)
  if(NOT found)
    message(
      FATAL_ERROR
        "Integra::${name} carries no INTEGRA_VERSION property — it is too old or not an integra-lib component."
    )
  endif()
  if(found VERSION_LESS minimum OR NOT found VERSION_LESS below)
    message(
      FATAL_ERROR
        "settings_record needs Integra::${name} >= ${minimum} and < ${below}, found ${found}."
    )
  endif()
endfunction()

integra_require(crc 0.1.0 0.2.0)

add_library(integra_settings_record INTERFACE)
add_library(Integra::settings_record ALIAS integra_settings_record)

target_include_directories(integra_settings_record
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(integra_settings_record INTERFACE cxx_std_20)
target_link_libraries(integra_settings_record INTERFACE Integra::crc)

# Dependants read the version from this property rather than from a variable: a
# variable set here is invisible in a sibling directory, and a stale one left in
# the parent scope would be read instead — a version check that silently passes.
set_target_properties(integra_settings_record PROPERTIES INTEGRA_VERSION
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
  target_link_libraries(settings-record-test PRIVATE Integra::settings_record
                                                     GTest::gtest_main)
  gtest_discover_tests(settings-record-test)
endif()
