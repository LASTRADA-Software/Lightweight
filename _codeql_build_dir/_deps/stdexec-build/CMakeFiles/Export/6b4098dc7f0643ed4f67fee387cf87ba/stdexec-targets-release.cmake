#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "STDEXEC::system_context" for configuration "Release"
set_property(TARGET STDEXEC::system_context APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(STDEXEC::system_context PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libsystem_context.a"
  )

list(APPEND _cmake_import_check_targets STDEXEC::system_context )
list(APPEND _cmake_import_check_files_for_STDEXEC::system_context "${_IMPORT_PREFIX}/lib/libsystem_context.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
