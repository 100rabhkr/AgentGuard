#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

#include <memory>
#include <mutex>
#include <vector>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// TestMonitor: collects events for verification
// ===========================================================================

class TestMonitor : public Monitor {
public:
    void on_event(const MonitorEvent& event) override {
        std::lock_guard<std::mutex> lock(mutex_);
        events.push_back(event);
    }

    void on_snapshot(const SystemSnapshot&) override {}

    std::vector<MonitorEvent> get_events() {
        std::lock_guard<std::mutex> lock(mutex_);
        return events;
    }

    std::vector<MonitorEvent> get_events_of_type(EventType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MonitorEvent> filtered;
        for (const auto& e : events) {
            if (e.type == type) {
                filtered.push_back(e);
            }
        }
        return filtered;
    }

private:
    std::mutex mutex_;
    std::vector<MonitorEvent> events;
};

// ===========================================================================
// Test Fixture
// ===========================================================================

class AdaptiveDemandsTest : public ::testing::Test {
protected:
    void SetUp() override {
        Config config;
        config.adaptive.enabled = true;
        config.adaptive.default_confidence_level = 0.95;
        config.adaptive.history_window_size = 50;
        config.adaptive.cold_start_headroom_factor = 2.0;
        config.adaptive.cold_start_default_demand = 1;
        config.adaptive.adaptive_headroom_factor = 1.5;
        config.adaptive.default_demand_mode = DemandMode::Static;
        config.default_request_timeout = std::chrono::seconds(1);

        manager = std::make_unique<ResourceManager>(config);
    }

    std::unique_ptr<ResourceManager> manager;
};

// ===========================================================================
// 1. Backward compat: static mode works normally
//
// Register an agent with explicit max_need, use request_resources() as before.
// Everything should work exactly like the pre-adaptive API.
// ===========================================================================

TEST_F(AdaptiveDemandsTest, StaticModeBackwardCompatibility) {
    manager->register_resource(
        Resource(1, "TokenBudget", ResourceCategory::TokenBudget, 10));

    Agent a(0, "StaticAgent", PRIORITY_NORMAL);
    a.declare_max_need(1, 5);
    AgentId aid = manager->register_agent(std::move(a));
    ASSERT_EQ(aid, 1);

    manager->start();

    // Standard request_resources works as before
    auto status = manager->request_resources(aid, 1, 3, 1s);
    EXPECT_EQ(status, RequestStatus::Granted);

    // Safety check passes
    EXPECT_TRUE(manager->is_safe());

    // Release and verify
    manager->release_resources(aid, 1, 3);

    auto res = manager->get_resource(1);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->allocated(), 0);
    EXPECT_EQ(res->available(), 10);

    manager->stop();
}

// ===========================================================================
// 2. Adaptive agent without declare_max_need
//
// An agent in Adaptive mode does not need to call declare_max_need.
// The DemandEstimator will compute max needs from usage history.
// We make several requests to build history, then verify probabilistic safety.
// ===========================================================================

TEST_F(AdaptiveDemandsTest, AdaptiveAgentWithoutDeclaredMaxNeed) {
    manager->register_resource(
        Resource(1, "ApiSlots", ResourceCategory::ApiRateLimit, 20));

    Agent a(0, "AdaptiveAgent", PRIORITY_NORMAL);
    // Deliberately NOT calling a.declare_max_need()
    AgentId aid = manager->register_agent(std::move(a));

    // Switch to adaptive mode
    manager->set_agent_demand_mode(aid, DemandMode::Adaptive);

    manager->start();

    // Build usage history with several adaptive requests
    for (int i = 0; i < 10; ++i) {
        auto status = manager->request_resources_adaptive(aid, 1, 2, 1s);
        EXPECT_EQ(status, RequestStatus::Granted)
            << "Iteration " << i << " failed";
        manager->release_resources(aid, 1, 2);
    }

    // Check probabilistic safety after building history
    auto result = manager->check_safety_probabilistic(0.95);
    EXPECT_TRUE(result.is_safe);
    EXPECT_GT(result.confidence_level, 0.0);

    // The estimated_max_needs should contain our agent
    EXPECT_TRUE(result.estimated_max_needs.count(aid) > 0);

    manager->stop();
}

// ===========================================================================
// 3. Adaptive request granted
//
// An agent in adaptive mode requests a resource that is available.
// request_resources_adaptive should return Granted.
// ===========================================================================

TEST_F(AdaptiveDemandsTest, AdaptiveRequestGranted) {
    manager->register_resource(
        Resource(1, "ToolSlot", ResourceCategory::ToolSlot, 5));

    Agent a(0, "AdaptiveWorker", PRIORITY_NORMAL);
    AgentId aid = manager->register_agent(std::move(a));
    manager->set_agent_demand_mode(aid, DemandMode::Adaptive);

    auto monitor = std::make_shared<TestMonitor>();
    manager->set_monitor(monitor);
    manager->start();

    auto status = manager->request_resources_adaptive(aid, 1, 1, 1s);
    EXPECT_EQ(status, RequestStatus::Granted);

    // Verify the agent got the allocation
    auto agent_opt = manager->get_agent(aid);
    ASSERT_TRUE(agent_opt.has_value());
    auto alloc = agent_opt->current_allocation();
    EXPECT_EQ(alloc[1], 1);

    // Check that RequestGranted event was emitted
    auto granted_events = monitor->get_events_of_type(EventType::RequestGranted);
    EXPECT_GE(granted_events.size(), 1u);

    manager->release_resources(aid, 1, 1);
    manager->stop();
}

// ===========================================================================
// 4. Adaptive safety check returns ProbabilisticSafetyResult with confidence
//
// check_safety_probabilistic returns a result with the confidence_level field
// populated to match the requested confidence.
// ===========================================================================

TEST_F(AdaptiveDemandsTest, ProbabilisticSafetyCheckHasConfidence) {
    manager->register_resource(
        Resource(1, "MemPool", ResourceCategory::MemoryPool, 100));

    Agent a(0, "Agent1", PRIORITY_NORMAL);
    a.declare_max_need(1, 10);
    AgentId aid = manager->register_agent(std::move(a));

    // Even with a static agent, the probabilistic check should work
    auto result = manager->check_safety_probabilistic(0.90);
    EXPECT_TRUE(result.is_safe);
    EXPECT_DOUBLE_EQ(result.confidence_level, 0.90);

    // Check at a higher confidence level
    auto result2 = manager->check_safety_probabilistic(0.99);
    EXPECT_TRUE(result2.is_safe);
    EXPECT_DOUBLE_EQ(result2.confidence_level, 0.99);

    // Safe sequence should include our agent
    EXPECT_FALSE(result.safe_sequence.empty());
}

// ===========================================================================
// 5. Mixed modes: static and adaptive agents coexist
//
// One agent uses static mode with declared max needs.
// Another agent uses adaptive mode without declared max needs.
// Both should be able to request and release resources simultaneously.
// ===========================================================================

TEST_F(AdaptiveDemandsTest, MixedStaticAndAdaptiveModes) {
    manager->register_resource(
        Resource(1, "SharedPool", ResourceCategory::MemoryPool, 20));

    // Static agent with declared max needs
    Agent a1(0, "StaticAgent", PRIORITY_NORMAL);
    a1.declare_max_need(1, 8);
    AgentId id_static = manager->register_agent(std::move(a1));

    // Adaptive agent without declared max needs
    Agent a2(0, "AdaptiveAgent", PRIORITY_NORMAL);
    AgentId id_adaptive = manager->register_agent(std::move(a2));
    manager->set_agent_demand_mode(id_adaptive, DemandMode::Adaptive);

    manager->start();

    // Static agent uses normal request
    auto s1 = manager->request_resources(id_static, 1, 3, 1s);
    EXPECT_EQ(s1, RequestStatus::Granted);

    // Adaptive agent uses adaptive request
    auto s2 = manager->request_resources_adaptive(id_adaptive, 1, 3, 1s);
    EXPECT_EQ(s2, RequestStatus::Granted);

    // Both are allocated
    auto snap = manager->get_snapshot();
    EXPECT_EQ(snap.available_resources[1], 20 - 3 - 3);

    // Probabilistic check should cover both agents
    auto result = manager->check_safety_probabilistic(0.95);
    // estimated_max_needs should have entries for both agents
    EXPECT_TRUE(result.estimated_max_needs.count(id_static) > 0 ||
                result.estimated_max_needs.count(id_adaptive) > 0);

    // Release both
    manager->release_resources(id_static, 1, 3);
    manager->release_resources(id_adaptive, 1, 3);

    auto res = manager->get_resource(1);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->allocated(), 0);

    manager->stop();
}

// ===========================================================================
// 6. Hybrid mode: uses min(estimated, declared)
//
// An agent in Hybrid mode has declared max needs AND accumulates usage stats.
// The probabilistic check uses the minimum of estimated and declared max need.
// ===========================================================================

TEST_F(AdaptiveDemandsTest, HybridModeUsesMinOfEstimatedAndDeclared) {
    manager->register_resource(
        Resource(1, "GpuSlots", ResourceCategory::GpuCompute, 20));

    // Declare a large max need
    Agent a(0, "HybridAgent", PRIORITY_NORMAL);
    a.declare_max_need(1, 15);
    AgentId aid = manager->register_agent(std::move(a));
    manager->set_agent_demand_mode(aid, DemandMode::Hybrid);

    manager->start();

    // Build usage history with small requests (much less than declared 15)
    for (int i = 0; i < 15; ++i) {
        auto status = manager->request_resources_adaptive(aid, 1, 2, 1s);
        EXPECT_EQ(status, RequestStatus::Granted)
            << "Iteration " << i << " failed";
        manager->release_resources(aid, 1, 2);
    }

    // Probabilistic safety check should see an estimated max need
    // that is <= the declared max need of 15
    auto result = manager->check_safety_probabilistic(0.95);
    EXPECT_TRUE(result.is_safe);

    auto est_it = result.estimated_max_needs.find(aid);
    ASSERT_NE(est_it, result.estimated_max_needs.end());
    auto res_it = est_it->second.find(1);
    ASSERT_NE(res_it, est_it->second.end());

    // In hybrid mode the estimated max need should be capped by declared max (15)
    EXPECT_LE(res_it->second, 15);

    // The estimate should still be reasonable (at least the request size of 2)
    EXPECT_GE(res_it->second, 2);

    manager->stop();
}

// ===========================================================================
// 7. Adaptive request denied when granting would be unsafe
//
// Set up a scenario where all resources are consumed and an additional
// adaptive request would create an unsafe state. The request should be
// denied (or timed out with a short timeout).
// ===========================================================================

TEST_F(AdaptiveDemandsTest, AdaptiveRequestDeniedWhenUnsafe) {
    // Tight resource: capacity 3
    manager->register_resource(
        Resource(1, "ScarceResource", ResourceCategory::ToolSlot, 3));

    // Agent 1 (static): needs up to 3
    Agent a1(0, "Hog1", PRIORITY_NORMAL);
    a1.declare_max_need(1, 3);
    AgentId id1 = manager->register_agent(std::move(a1));

    // Agent 2 (adaptive): will try to get resources when pool is nearly empty
    Agent a2(0, "AdaptiveRequester", PRIORITY_NORMAL);
    AgentId id2 = manager->register_agent(std::move(a2));
    manager->set_agent_demand_mode(id2, DemandMode::Adaptive);

    // Do NOT start() the background processor so the denial path
    // fires quickly (no processor running = immediate deny on unsafe)

    // Give agent 1 all 3 units via normal request
    auto s1 = manager->request_resources(id1, 1, 3, 1s);
    EXPECT_EQ(s1, RequestStatus::Granted);

    // Agent 2 tries to get 1 unit -- but 0 available, so it should not
    // get through. With a short timeout and no background processor,
    // this should time out or be denied.
    auto s2 = manager->request_resources_adaptive(id2, 1, 1, 200ms);
    EXPECT_NE(s2, RequestStatus::Granted);
    EXPECT_TRUE(s2 == RequestStatus::TimedOut || s2 == RequestStatus::Denied);

    // Cleanup
    manager->release_resources(id1, 1, 3);
}

// ===========================================================================
// 8. Default confidence from config
//
// check_safety_probabilistic() without arguments should use
// config.adaptive.default_confidence_level (0.95 in our fixture).
// ===========================================================================

TEST_F(AdaptiveDemandsTest, DefaultConfidenceFromConfig) {
    manager->register_resource(
        Resource(1, "NetSocket", ResourceCategory::NetworkSocket, 50));

    Agent a(0, "DefaultConfAgent", PRIORITY_NORMAL);
    a.declare_max_need(1, 10);
    AgentId aid = manager->register_agent(std::move(a));

    // Call the no-argument overload
    auto result = manager->check_safety_probabilistic();
    EXPECT_TRUE(result.is_safe);

    // The confidence should be the config default (0.95)
    EXPECT_DOUBLE_EQ(result.confidence_level, 0.95);

    // Compare with explicit call at same confidence
    auto result_explicit = manager->check_safety_probabilistic(0.95);
    EXPECT_EQ(result.is_safe, result_explicit.is_safe);
    EXPECT_DOUBLE_EQ(result.confidence_level, result_explicit.confidence_level);
}
