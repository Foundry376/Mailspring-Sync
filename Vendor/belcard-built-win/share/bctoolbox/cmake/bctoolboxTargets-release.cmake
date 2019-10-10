#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "bctoolbox-static" for configuration "Release"
set_property(TARGET bctoolbox-static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(bctoolbox-static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C;CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/bctoolbox.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS bctoolbox-static )
list(APPEND _IMPORT_CHECK_FILES_FOR_bctoolbox-static "${_IMPORT_PREFIX}/lib/bctoolbox.lib" )

# Import target "bctoolbox" for configuration "Release"
set_property(TARGET bctoolbox APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(bctoolbox PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/bctoolbox.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/bctoolbox.dll"
  )

list(APPEND _IMPORT_CHECK_TARGETS bctoolbox )
list(APPEND _IMPORT_CHECK_FILES_FOR_bctoolbox "${_IMPORT_PREFIX}/lib/bctoolbox.lib" "${_IMPORT_PREFIX}/bin/bctoolbox.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
