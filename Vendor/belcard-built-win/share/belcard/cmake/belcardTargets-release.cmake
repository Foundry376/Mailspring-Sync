#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "belcard" for configuration "Release"
set_property(TARGET belcard APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(belcard PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/belcard.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/belcard.dll"
  )

list(APPEND _IMPORT_CHECK_TARGETS belcard )
list(APPEND _IMPORT_CHECK_FILES_FOR_belcard "${_IMPORT_PREFIX}/lib/belcard.lib" "${_IMPORT_PREFIX}/bin/belcard.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
