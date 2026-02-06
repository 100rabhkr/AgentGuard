function(agentguard_add_example EXAMPLE_NAME EXAMPLE_SOURCE)
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SOURCE})
    target_link_libraries(${EXAMPLE_NAME} PRIVATE AgentGuard::agentguard)
endfunction()

agentguard_add_example(example_basic               01_basic_usage.cpp)
agentguard_add_example(example_llm_rate_limits      02_llm_api_rate_limits.cpp)
agentguard_add_example(example_tool_sharing         03_tool_sharing.cpp)
agentguard_add_example(example_priority_agents      04_priority_agents.cpp)
