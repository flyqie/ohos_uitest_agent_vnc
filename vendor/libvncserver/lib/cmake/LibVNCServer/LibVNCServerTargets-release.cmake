#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "LibVNCServer::vncclient" for configuration "Release"
set_property(TARGET LibVNCServer::vncclient APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LibVNCServer::vncclient PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libvncclient.a"
  )

list(APPEND _cmake_import_check_targets LibVNCServer::vncclient )
list(APPEND _cmake_import_check_files_for_LibVNCServer::vncclient "${_IMPORT_PREFIX}/lib/libvncclient.a" )

# Import target "LibVNCServer::vncserver" for configuration "Release"
set_property(TARGET LibVNCServer::vncserver APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LibVNCServer::vncserver PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libvncserver.a"
  )

list(APPEND _cmake_import_check_targets LibVNCServer::vncserver )
list(APPEND _cmake_import_check_files_for_LibVNCServer::vncserver "${_IMPORT_PREFIX}/lib/libvncserver.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
