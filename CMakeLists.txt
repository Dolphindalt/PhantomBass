cmake_minimum_required(VERSION 3.20)

# Single source of truth for the version: the VERSION file (read by nix too).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PBE_VERSION)
string(STRIP "${PBE_VERSION}" PBE_VERSION)
project(phantom-bass VERSION ${PBE_VERSION} LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
  enable_testing()
  find_package(Unity REQUIRED)
endif()

add_subdirectory(pbe-dsp)

option(BUILD_PLUGIN "Build the VST3/CLAP plugin (needs CPLUG, pugl, NanoVG sources)" OFF)
if(BUILD_PLUGIN)
  add_subdirectory(pbe-plugin)
endif()
