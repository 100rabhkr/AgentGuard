cmake_minimum_required(VERSION 3.16)

project(AgentGuard
    VERSION 1.0.0
    DESCRIPTION "Deadlock prevention library for multi-AI-agent systems"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(AGENTGUARD_BUILD_TESTS "Build unit and integration tests" ON)
option(AGENTGUARD_BUILD_EXAMPLES "Build example programs" ON)
option(AGENTGUARD_BUILD_BENCHMARKS "Build benchmark programs" OFF)
option(AGENTGUARD_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(AGENTGUARD_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(AGENTGUARD_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(AGENTGUARD_INSTALL "Generate install targets" ON)

# Custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

# The library
add_subdirectory(src)

# Tests
if(AGENTGUARD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(AGENTGUARD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install
if(AGENTGUARD_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS agentguard
        EXPORT AgentGuardTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY include/agentguard
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(EXPORT AgentGuardTargets
        FILE AgentGuardTargets.cmake
        NAMESPACE AgentGuard::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AgentGuard
    )
endif()
