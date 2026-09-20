# FindUnity.cmake - locate the Unity C test framework via pkg-config.
# Provides the imported target Unity::Unity.
find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_Unity QUIET IMPORTED_TARGET unity)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Unity
  REQUIRED_VARS PC_Unity_FOUND
  VERSION_VAR PC_Unity_VERSION
)

if(Unity_FOUND AND NOT TARGET Unity::Unity)
  add_library(Unity::Unity ALIAS PkgConfig::PC_Unity)
endif()
