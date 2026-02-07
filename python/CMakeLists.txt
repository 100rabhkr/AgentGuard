find_package(pybind11 REQUIRED)

pybind11_add_module(_agentguard
    src/bindings.cpp
    src/bind_core.cpp
    src/bind_monitors.cpp
    src/bind_policies.cpp
    src/bind_subsystems.cpp
    src/bind_ai.cpp
)

target_link_libraries(_agentguard PRIVATE AgentGuard::agentguard)
target_compile_features(_agentguard PRIVATE cxx_std_17)

install(TARGETS _agentguard DESTINATION agentguard)
