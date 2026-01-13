#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Lightweight::Lightweight" for configuration "Release"
set_property(TARGET Lightweight::Lightweight APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Lightweight::Lightweight PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLightweight.a"
  )

list(APPEND _cmake_import_check_targets Lightweight::Lightweight )
list(APPEND _cmake_import_check_files_for_Lightweight::Lightweight "${_IMPORT_PREFIX}/lib/libLightweight.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
