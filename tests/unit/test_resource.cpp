#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Resource construction and getters
// ===========================================================================

TEST(ResourceTest, ConstructionAndGetters) {
    Resource r(1, "GPT-4 API Slots", ResourceCategory::ApiRateLimit, 100);

    EXPECT_EQ(r.id(), 1);
    EXPECT_EQ(r.name(), "GPT-4 API Slots");
    EXPECT_EQ(r.category(), ResourceCategory::ApiRateLimit);
    EXPECT_EQ(r.total_capacity(), 100);
    EXPECT_EQ(r.allocated(), 0);
    EXPECT_EQ(r.available(), 100);
}

TEST(ResourceTest, ConstructionWithDifferentCategories) {
    Resource r1(10, "Token Budget", ResourceCategory::TokenBudget, 50000);
    EXPECT_EQ(r1.category(), ResourceCategory::TokenBudget);
    EXPECT_EQ(r1.total_capacity(), 50000);

    Resource r2(20, "Tool Slot", ResourceCategory::ToolSlot, 5);
    EXPECT_EQ(r2.category(), ResourceCategory::ToolSlot);
    EXPECT_EQ(r2.total_capacity(), 5);

    Resource r3(30, "GPU Compute", ResourceCategory::GpuCompute, 8);
    EXPECT_EQ(r3.category(), ResourceCategory::GpuCompute);

    Resource r4(40, "My Custom Resource", ResourceCategory::Custom, 1);
    EXPECT_EQ(r4.category(), ResourceCategory::Custom);
}

TEST(ResourceTest, InitialAvailableEqualsCapacity) {
    Resource r(1, "Memory Pool", ResourceCategory::MemoryPool, 256);
    EXPECT_EQ(r.available(), r.total_capacity());
    EXPECT_EQ(r.allocated(), 0);
}

// ===========================================================================
// Dynamic capacity adjustment
// ===========================================================================

TEST(ResourceTest, SetTotalCapacityIncrease) {
    Resource r(1, "API Slots", ResourceCategory::ApiRateLimit, 10);
    EXPECT_TRUE(r.set_total_capacity(20));
    EXPECT_EQ(r.total_capacity(), 20);
    EXPECT_EQ(r.available(), 20);
}

TEST(ResourceTest, SetTotalCapacityDecreaseAboveAllocated) {
    // With 0 allocated, we can decrease freely
    Resource r(1, "API Slots", ResourceCategory::ApiRateLimit, 20);
    EXPECT_TRUE(r.set_total_capacity(5));
    EXPECT_EQ(r.total_capacity(), 5);
    EXPECT_EQ(r.available(), 5);
}

TEST(ResourceTest, SetTotalCapacityToZeroWhenNothingAllocated) {
    Resource r(1, "API Slots", ResourceCategory::ApiRateLimit, 10);
    EXPECT_TRUE(r.set_total_capacity(0));
    EXPECT_EQ(r.total_capacity(), 0);
    EXPECT_EQ(r.available(), 0);
}

// ===========================================================================
// AI-specific metadata
// ===========================================================================

TEST(ResourceTest, ReplenishIntervalDefaultIsNullopt) {
    Resource r(1, "Rate Limit", ResourceCategory::ApiRateLimit, 100);
    EXPECT_FALSE(r.replenish_interval().has_value());
}

TEST(ResourceTest, SetAndGetReplenishInterval) {
    Resource r(1, "Rate Limit", ResourceCategory::ApiRateLimit, 100);
    r.set_replenish_interval(1s);
    ASSERT_TRUE(r.replenish_interval().has_value());
    EXPECT_EQ(r.replenish_interval().value(), 1s);
}

TEST(ResourceTest, CostPerUnitDefaultIsNullopt) {
    Resource r(1, "Token Budget", ResourceCategory::TokenBudget, 50000);
    EXPECT_FALSE(r.cost_per_unit().has_value());
}

TEST(ResourceTest, SetAndGetCostPerUnit) {
    Resource r(1, "Token Budget", ResourceCategory::TokenBudget, 50000);
    r.set_cost_per_unit(0.002);
    ASSERT_TRUE(r.cost_per_unit().has_value());
    EXPECT_DOUBLE_EQ(r.cost_per_unit().value(), 0.002);
}

// ===========================================================================
// Resource allocation tested through ResourceManager (allocate/deallocate
// are private, ResourceManager is a friend)
// ===========================================================================

TEST(ResourceTest, AllocationThroughResourceManager) {
    Config cfg;
    cfg.thread_safe = false;
    ResourceManager mgr(cfg);

    Resource r(1, "Tool Slots", ResourceCategory::ToolSlot, 5);
    mgr.register_resource(std::move(r));

    Agent a(1, "Agent-1");
    a.declare_max_need(1, 3);
    mgr.register_agent(std::move(a));

    // Request 2 units
    auto status = mgr.request_resources(1, 1, 2);
    EXPECT_EQ(status, RequestStatus::Granted);

    // Verify via resource query
    auto res = mgr.get_resource(1);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->allocated(), 2);
    EXPECT_EQ(res->available(), 3);
}

TEST(ResourceTest, DeallocationThroughResourceManager) {
    Config cfg;
    cfg.thread_safe = false;
    ResourceManager mgr(cfg);

    Resource r(1, "Tool Slots", ResourceCategory::ToolSlot, 5);
    mgr.register_resource(std::move(r));

    Agent a(1, "Agent-1");
    a.declare_max_need(1, 5);
    mgr.register_agent(std::move(a));

    mgr.request_resources(1, 1, 3);
    mgr.release_resources(1, 1, 2);

    auto res = mgr.get_resource(1);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->allocated(), 1);
    EXPECT_EQ(res->available(), 4);
}
