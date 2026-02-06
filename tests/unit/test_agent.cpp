#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Construction and basic getters
// ===========================================================================

TEST(AgentTest, ConstructionWithDefaults) {
    Agent a(1, "ResearchBot");
    EXPECT_EQ(a.id(), 1);
    EXPECT_EQ(a.name(), "ResearchBot");
    EXPECT_EQ(a.priority(), PRIORITY_NORMAL);
    EXPECT_EQ(a.state(), AgentState::Registered);
}

TEST(AgentTest, ConstructionWithCustomPriority) {
    Agent a(42, "CriticalBot", PRIORITY_CRITICAL);
    EXPECT_EQ(a.id(), 42);
    EXPECT_EQ(a.name(), "CriticalBot");
    EXPECT_EQ(a.priority(), PRIORITY_CRITICAL);
}

TEST(AgentTest, ConstructionWithLowPriority) {
    Agent a(99, "BackgroundWorker", PRIORITY_LOW);
    EXPECT_EQ(a.priority(), PRIORITY_LOW);
}

// ===========================================================================
// Priority modification
// ===========================================================================

TEST(AgentTest, SetPriority) {
    Agent a(1, "Agent-1");
    EXPECT_EQ(a.priority(), PRIORITY_NORMAL);

    a.set_priority(PRIORITY_HIGH);
    EXPECT_EQ(a.priority(), PRIORITY_HIGH);

    a.set_priority(PRIORITY_LOW);
    EXPECT_EQ(a.priority(), PRIORITY_LOW);
}

// ===========================================================================
// Max need declaration
// ===========================================================================

TEST(AgentTest, DeclareMaxNeedSingle) {
    Agent a(1, "Agent-1");
    a.declare_max_need(100, 5);

    const auto& needs = a.max_needs();
    ASSERT_EQ(needs.size(), 1);
    EXPECT_EQ(needs.at(100), 5);
}

TEST(AgentTest, DeclareMaxNeedMultiple) {
    Agent a(1, "Agent-1");
    a.declare_max_need(1, 10);
    a.declare_max_need(2, 20);
    a.declare_max_need(3, 5);

    const auto& needs = a.max_needs();
    EXPECT_EQ(needs.size(), 3);
    EXPECT_EQ(needs.at(1), 10);
    EXPECT_EQ(needs.at(2), 20);
    EXPECT_EQ(needs.at(3), 5);
}

TEST(AgentTest, DeclareMaxNeedOverwrite) {
    Agent a(1, "Agent-1");
    a.declare_max_need(1, 10);
    a.declare_max_need(1, 25);  // Overwrite

    EXPECT_EQ(a.max_needs().at(1), 25);
}

// ===========================================================================
// remaining_need()
// ===========================================================================

TEST(AgentTest, RemainingNeedNoAllocation) {
    Agent a(1, "Agent-1");
    a.declare_max_need(1, 10);

    // No allocation yet, remaining_need should equal max
    EXPECT_EQ(a.remaining_need(1), 10);
}

TEST(AgentTest, RemainingNeedAfterAllocationThroughManager) {
    Config cfg;
    cfg.thread_safe = false;
    ResourceManager mgr(cfg);

    Resource r(1, "API Slots", ResourceCategory::ApiRateLimit, 20);
    mgr.register_resource(std::move(r));

    Agent a(1, "Agent-1");
    a.declare_max_need(1, 10);
    AgentId aid = mgr.register_agent(std::move(a));

    mgr.request_resources(aid, 1, 4);

    auto agent = mgr.get_agent(aid);
    ASSERT_TRUE(agent.has_value());
    EXPECT_EQ(agent->remaining_need(1), 6);  // 10 - 4 = 6
}

TEST(AgentTest, RemainingNeedUndeclaredResourceReturnsZero) {
    Agent a(1, "Agent-1");
    a.declare_max_need(1, 10);

    // Resource type 999 was never declared
    EXPECT_EQ(a.remaining_need(999), 0);
}

// ===========================================================================
// Current allocation
// ===========================================================================

TEST(AgentTest, CurrentAllocationInitiallyEmpty) {
    Agent a(1, "Agent-1");
    EXPECT_TRUE(a.current_allocation().empty());
}

// ===========================================================================
// AI-specific metadata
// ===========================================================================

TEST(AgentTest, ModelIdentifierDefault) {
    Agent a(1, "Agent-1");
    EXPECT_TRUE(a.model_identifier().empty());
}

TEST(AgentTest, SetAndGetModelIdentifier) {
    Agent a(1, "Agent-1");
    a.set_model_identifier("gpt-4-turbo");
    EXPECT_EQ(a.model_identifier(), "gpt-4-turbo");
}

TEST(AgentTest, TaskDescriptionDefault) {
    Agent a(1, "Agent-1");
    EXPECT_TRUE(a.task_description().empty());
}

TEST(AgentTest, SetAndGetTaskDescription) {
    Agent a(1, "Agent-1");
    a.set_task_description("Summarize research papers");
    EXPECT_EQ(a.task_description(), "Summarize research papers");
}

TEST(AgentTest, OverwriteMetadata) {
    Agent a(1, "Agent-1");
    a.set_model_identifier("gpt-3.5");
    a.set_task_description("Draft emails");

    a.set_model_identifier("gpt-4");
    a.set_task_description("Code review");

    EXPECT_EQ(a.model_identifier(), "gpt-4");
    EXPECT_EQ(a.task_description(), "Code review");
}

// ===========================================================================
// State (initial)
// ===========================================================================

TEST(AgentTest, InitialStateIsRegistered) {
    Agent a(1, "Agent-1");
    EXPECT_EQ(a.state(), AgentState::Registered);
}
