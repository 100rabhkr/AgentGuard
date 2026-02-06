#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "AgentGuard::agentguard" for configuration "Debug"
set_property(TARGET AgentGuard::agentguard APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(AgentGuard::agentguard PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libagentguard.a"
  )

list(APPEND _cmake_import_check_targets AgentGuard::agentguard )
list(APPEND _cmake_import_check_files_for_AgentGuard::agentguard "${_IMPORT_PREFIX}/lib/libagentguard.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
