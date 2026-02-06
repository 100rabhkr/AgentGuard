#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Fixture: creates a ResourceManager in single-threaded mode
// ===========================================================================

class ResourceManagerTest : public ::testing::Test {
protected:
    Config cfg;
    std::unique_ptr<ResourceManager> mgr;

    void SetUp() override {
        cfg.thread_safe = false;
        mgr = std::make_unique<ResourceManager>(cfg);
    }
};

// ===========================================================================
// Resource registration and querying
// ===========================================================================

TEST_F(ResourceManagerTest, RegisterAndQueryResource) {
    Resource r(1, "API Slots", ResourceCategory::ApiRateLimit, 10);
    mgr->register_resource(std::move(r));

    auto queried = mgr->get_resource(1);
    ASSERT_TRUE(queried.has_value());
    EXPECT_EQ(queried->id(), 1);
    EXPECT_EQ(queried->name(), "API Slots");
    EXPECT_EQ(queried->total_capacity(), 10);
}

TEST_F(ResourceManagerTest, QueryNonexistentResourceReturnsNullopt) {
    auto queried = mgr->get_resource(999);
    EXPECT_FALSE(queried.has_value());
}

TEST_F(ResourceManagerTest, RegisterMultipleResources) {
    mgr->register_resource(Resource(1, "API", ResourceCategory::ApiRateLimit, 10));
    mgr->register_resource(Resource(2, "Tokens", ResourceCategory::TokenBudget, 5000));
    mgr->register_resource(Resource(3, "GPU", ResourceCategory::GpuCompute, 4));

    auto all = mgr->get_all_resources();
    EXPECT_EQ(all.size(), 3);
}

TEST_F(ResourceManagerTest, UnregisterResource) {
    mgr->register_resource(Resource(1, "API", ResourceCategory::ApiRateLimit, 10));
    EXPECT_TRUE(mgr->unregister_resource(1));
    EXPECT_FALSE(mgr->get_resource(1).has_value());
}

TEST_F(ResourceManagerTest, UnregisterNonexistentResourceReturnsFalse) {
    EXPECT_FALSE(mgr->unregister_resource(999));
}

TEST_F(ResourceManagerTest, AdjustResourceCapacity) {
    mgr->register_resource(Resource(1, "API", ResourceCategory::ApiRateLimit, 10));
    EXPECT_TRUE(mgr->adjust_resource_capacity(1, 20));

    auto r = mgr->get_resource(1);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->total_capacity(), 20);
}

// ===========================================================================
// Agent registration and deregistration
// ===========================================================================

TEST_F(ResourceManagerTest, RegisterAndQueryAgent) {
    Agent a(1, "ResearchBot", PRIORITY_HIGH);
    a.declare_max_need(1, 5);
    AgentId aid = mgr->register_agent(std::move(a));

    auto queried = mgr->get_agent(aid);
    ASSERT_TRUE(queried.has_value());
    EXPECT_EQ(queried->name(), "ResearchBot");
    EXPECT_EQ(queried->priority(), PRIORITY_HIGH);
}

TEST_F(ResourceManagerTest, AgentCountTracking) {
    EXPECT_EQ(mgr->agent_count(), 0);

    Agent a1(1, "A1");
    Agent a2(2, "A2");
    mgr->register_agent(std::move(a1));
    mgr->register_agent(std::move(a2));
    EXPECT_EQ(mgr->agent_count(), 2);
}

TEST_F(ResourceManagerTest, DeregisterAgent) {
    Agent a(1, "A1");
    AgentId aid = mgr->register_agent(std::move(a));
    EXPECT_EQ(mgr->agent_count(), 1);

    EXPECT_TRUE(mgr->deregister_agent(aid));
    EXPECT_EQ(mgr->agent_count(), 0);
}

TEST_F(ResourceManagerTest, DeregisterNonexistentAgentReturnsFalse) {
    EXPECT_FALSE(mgr->deregister_agent(999));
}

TEST_F(ResourceManagerTest, GetAllAgents) {
    mgr->register_agent(Agent(1, "A1"));
    mgr->register_agent(Agent(2, "A2"));
    mgr->register_agent(Agent(3, "A3"));

    auto all = mgr->get_all_agents();
    EXPECT_EQ(all.size(), 3);
}

// ===========================================================================
// Simple request and release flow
// ===========================================================================

TEST_F(ResourceManagerTest, SimpleRequestAndRelease) {
    mgr->register_resource(Resource(1, "Tools", ResourceCategory::ToolSlot, 5));

    Agent a(1, "Agent-1");
    a.declare_max_need(1, 3);
    AgentId aid = mgr->register_agent(std::move(a));

    // Request 2 units
    auto status = mgr->request_resources(aid, 1, 2);
    EXPECT_EQ(status, RequestStatus::Granted);

    // Verify allocation
    auto agent = mgr->get_agent(aid);
    ASSERT_TRUE(agent.has_value());
    EXPECT_EQ(agent->current_allocation().at(1), 2);

    // Release 1 unit
    mgr->release_resources(aid, 1, 1);
    agent = mgr->get_agent(aid);
    ASSERT_TRUE(agent.has_value());
    EXPECT_EQ(agent->current_allocation().at(1), 1);

    // Release all remaining
    mgr->release_all_resources(aid, 1);
    agent = mgr->get_agent(aid);
    ASSERT_TRUE(agent.has_value());
    // After releasing all, allocation for resource 1 should be 0
    auto it = agent->current_allocation().find(1);
    if (it != agent->current_allocation().end()) {
        EXPECT_EQ(it->second, 0);
    }
}

TEST_F(ResourceManagerTest, ReleaseAllResourcesForAgent) {
    mgr->register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 10));
    mgr->register_resource(Resource(2, "R2", ResourceCategory::ApiRateLimit, 10));

    Agent a(1, "A1");
    a.declare_max_need(1, 5);
    a.declare_max_need(2, 5);
    AgentId aid = mgr->register_agent(std::move(a));

    mgr->request_resources(aid, 1, 3);
    mgr->request_resources(aid, 2, 2);

    // Release all resources for the agent
    mgr->release_all_resources(aid);

    auto r1 = mgr->get_resource(1);
    auto r2 = mgr->get_resource(2);
    EXPECT_EQ(r1->available(), 10);
    EXPECT_EQ(r2->available(), 10);
}

// ===========================================================================
// Request denied when exceeds max claim
// ===========================================================================

TEST_F(ResourceManagerTest, RequestDeniedWhenExceedsMaxClaim) {
    mgr->register_resource(Resource(1, "Tools", ResourceCategory::ToolSlot, 10));

    Agent a(1, "Agent-1");
    a.declare_max_need(1, 3);
    AgentId aid = mgr->register_agent(std::move(a));

    // Request more than declared max need should throw or be denied
    EXPECT_THROW(mgr->request_resources(aid, 1, 5), MaxClaimExceededException);
}

TEST_F(ResourceManagerTest, RequestForUnknownResourceThrows) {
    Agent a(1, "Agent-1");
    AgentId aid = mgr->register_agent(std::move(a));

    EXPECT_THROW(mgr->request_resources(aid, 999, 1), ResourceNotFoundException);
}

TEST_F(ResourceManagerTest, RequestForUnknownAgentThrows) {
    mgr->register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 10));
    EXPECT_THROW(mgr->request_resources(999, 1, 1), AgentNotFoundException);
}

// ===========================================================================
// Batch request (all-or-nothing)
// ===========================================================================

TEST_F(ResourceManagerTest, BatchRequestAllGranted) {
    mgr->register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 10));
    mgr->register_resource(Resource(2, "R2", ResourceCategory::ApiRateLimit, 10));

    Agent a(1, "Agent-1");
    a.declare_max_need(1, 5);
    a.declare_max_need(2, 5);
    AgentId aid = mgr->register_agent(std::move(a));

    std::unordered_map<ResourceTypeId, ResourceQuantity> batch;
    batch[1] = 3;
    batch[2] = 2;

    auto status = mgr->request_resources_batch(aid, batch);
    EXPECT_EQ(status, RequestStatus::Granted);

    auto r1 = mgr->get_resource(1);
    auto r2 = mgr->get_resource(2);
    EXPECT_EQ(r1->allocated(), 3);
    EXPECT_EQ(r2->allocated(), 2);
}

TEST_F(ResourceManagerTest, BatchRequestDeniedIfUnsafe) {
    // Setup a scenario where batch grant would be unsafe
    mgr->register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 4));

    Agent a1(1, "Agent-1");
    a1.declare_max_need(1, 4);
    AgentId aid1 = mgr->register_agent(std::move(a1));

    Agent a2(2, "Agent-2");
    a2.declare_max_need(1, 4);
    AgentId aid2 = mgr->register_agent(std::move(a2));

    // Grant 2 to agent 1
    mgr->request_resources(aid1, 1, 2);

    // Now agent 2 requesting 2 would leave 0 available,
    // with A1 needing 2 more and A2 needing 2 more -> unsafe
    std::unordered_map<ResourceTypeId, ResourceQuantity> batch;
    batch[1] = 2;

    auto status = mgr->request_resources_batch(aid2, batch);
    EXPECT_EQ(status, RequestStatus::Denied);
}

// ===========================================================================
// is_safe() query
// ===========================================================================

TEST_F(ResourceManagerTest, IsSafeInitially) {
    mgr->register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 10));
    EXPECT_TRUE(mgr->is_safe());
}

TEST_F(ResourceManagerTest, IsSafeAfterValidAllocations) {
    mgr->register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 10));

    Agent a(1, "Agent-1");
    a.declare_max_need(1, 5);
    AgentId aid = mgr->register_agent(std::move(a));

    mgr->request_resources(aid, 1, 3);
    EXPECT_TRUE(mgr->is_safe());
}

TEST_F(ResourceManagerTest, SystemSnapshot) {
    mgr->register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 10));

    Agent a(1, "Agent-1");
    a.declare_max_need(1, 5);
    AgentId aid = mgr->register_agent(std::move(a));
    mgr->request_resources(aid, 1, 2);

    auto snapshot = mgr->get_snapshot();
    EXPECT_TRUE(snapshot.is_safe);
    EXPECT_EQ(snapshot.total_resources.at(1), 10);
    EXPECT_EQ(snapshot.available_resources.at(1), 8);
    EXPECT_EQ(snapshot.agents.size(), 1);
}

// ===========================================================================
// Update max claim after registration
// ===========================================================================

TEST_F(ResourceManagerTest, UpdateAgentMaxClaim) {
    mgr->register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 10));

    Agent a(1, "Agent-1");
    a.declare_max_need(1, 3);
    AgentId aid = mgr->register_agent(std::move(a));

    EXPECT_TRUE(mgr->update_agent_max_claim(aid, 1, 7));

    auto agent = mgr->get_agent(aid);
    ASSERT_TRUE(agent.has_value());
    EXPECT_EQ(agent->max_needs().at(1), 7);
}

// ===========================================================================
// Deregistering an agent releases its resources
// ===========================================================================

TEST_F(ResourceManagerTest, DeregisterReleasesResources) {
    mgr->register_resource(Resource(1, "R1", ResourceCategory::ToolSlot, 10));

    Agent a(1, "Agent-1");
    a.declare_max_need(1, 5);
    AgentId aid = mgr->register_agent(std::move(a));

    mgr->request_resources(aid, 1, 4);

    auto r = mgr->get_resource(1);
    EXPECT_EQ(r->available(), 6);

    mgr->deregister_agent(aid);

    r = mgr->get_resource(1);
    EXPECT_EQ(r->available(), 10);
}
