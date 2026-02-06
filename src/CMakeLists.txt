add_library(agentguard
    resource.cpp
    agent.cpp
    resource_manager.cpp
    safety_checker.cpp
    request_queue.cpp
    monitor.cpp
    policy.cpp
    config.cpp
    ai/token_budget.cpp
    ai/rate_limiter.cpp
    ai/tool_slot.cpp
    ai/memory_pool.cpp
)

add_library(AgentGuard::agentguard ALIAS agentguard)

target_include_directories(agentguard
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(agentguard PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(agentguard PUBLIC Threads::Threads)

target_apply_warnings(agentguard)
target_apply_sanitizers(agentguard)
