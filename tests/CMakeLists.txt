include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

function(agentguard_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            AgentGuard::agentguard
            GTest::gtest_main
    )
    target_apply_sanitizers(${TEST_NAME})
    gtest_discover_tests(${TEST_NAME})
endfunction()

# Unit tests
agentguard_add_test(test_resource             unit/test_resource.cpp)
agentguard_add_test(test_agent                unit/test_agent.cpp)
agentguard_add_test(test_safety_checker       unit/test_safety_checker.cpp)
agentguard_add_test(test_resource_manager     unit/test_resource_manager.cpp)
agentguard_add_test(test_request_queue        unit/test_request_queue.cpp)
agentguard_add_test(test_policy               unit/test_policy.cpp)

# Integration tests
agentguard_add_test(test_deadlock_prevention  integration/test_deadlock_prevention.cpp)
agentguard_add_test(test_concurrent_agents    integration/test_concurrent_agents.cpp)
