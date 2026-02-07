#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// TestMonitor: captures events for assertion
// ===========================================================================

class TestMonitor : public Monitor {
public:
    void on_event(const MonitorEvent& event) override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(event);
    }
    void on_snapshot(const SystemSnapshot&) override {}
    std::vector<MonitorEvent> get_events() {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }
    bool has_event_type(EventType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& e : events_) {
            if (e.type == type) return true;
        }
        return false;
    }
private:
    std::mutex mutex_;
    std::vector<MonitorEvent> events_;
};

// ===========================================================================
// Fixture
// ===========================================================================

class ProgressMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        Config config;
        config.progress.enabled = true;
        config.progress.default_stall_threshold = std::chrono::milliseconds(100);
        config.progress.check_interval = std::chrono::milliseconds(20);
        config.progress.auto_release_on_stall = false;
        config.default_request_timeout = std::chrono::seconds(1);

        manager = std::make_unique<ResourceManager>(config);
        monitor = std::make_shared<TestMonitor>();
        manager->set_monitor(monitor);
    }

    void TearDown() override {
        manager->stop();
    }

    std::unique_ptr<ResourceManager> manager;
    std::shared_ptr<TestMonitor> monitor;
};

// ===========================================================================
// Test 1: Progress reporting through ResourceManager
// ===========================================================================

TEST_F(ProgressMonitorTest, ProgressReportingKeepsAgentNotStalled) {
    manager->register_resource(
        Resource(1, "API-Slots", ResourceCategory::ApiRateLimit, 10));

    Agent a(1, "Worker");
    a.declare_max_need(1, 5);
    AgentId aid = manager->register_agent(std::move(a));

    manager->start();

    // Report progress several times
    manager->report_progress(aid, "tokens_processed", 100.0);
    std::this_thread::sleep_for(30ms);
    manager->report_progress(aid, "tokens_processed", 200.0);
    std::this_thread::sleep_for(30ms);
    manager->report_progress(aid, "tokens_processed", 300.0);

    // Agent should NOT be stalled because we keep reporting progress
    EXPECT_FALSE(manager->is_agent_stalled(aid));

    // The stalled agents list should be empty
    auto stalled = manager->get_stalled_agents();
    EXPECT_TRUE(stalled.empty());
}

// ===========================================================================
// Test 2: Stall detection through ResourceManager
// ===========================================================================

TEST_F(ProgressMonitorTest, StallDetectedWhenNoProgress) {
    manager->register_resource(
        Resource(1, "API-Slots", ResourceCategory::ApiRateLimit, 10));

    Agent a(1, "SilentWorker");
    a.declare_max_need(1, 5);
    AgentId aid = manager->register_agent(std::move(a));

    manager->start();

    // Report progress once to establish a baseline, then go silent
    manager->report_progress(aid, "steps", 1.0);

    // Wait longer than the stall threshold (100ms) plus some margin
    // for the checker thread to run (check_interval = 20ms)
    std::this_thread::sleep_for(200ms);

    // The agent should now be detected as stalled
    EXPECT_TRUE(manager->is_agent_stalled(aid));

    auto stalled = manager->get_stalled_agents();
    ASSERT_EQ(stalled.size(), 1u);
    EXPECT_EQ(stalled[0], aid);
}

// ===========================================================================
// Test 3: Monitor events emitted for stall and resolution
// ===========================================================================

TEST_F(ProgressMonitorTest, MonitorEventsForStallAndResolution) {
    manager->register_resource(
        Resource(1, "API-Slots", ResourceCategory::ApiRateLimit, 10));

    Agent a(1, "EventAgent");
    a.declare_max_need(1, 5);
    AgentId aid = manager->register_agent(std::move(a));

    manager->start();

    // Report progress to establish baseline, then go silent
    manager->report_progress(aid, "steps", 1.0);

    // Wait for stall detection
    std::this_thread::sleep_for(200ms);
    ASSERT_TRUE(manager->is_agent_stalled(aid));

    // Verify AgentStalled event was emitted
    EXPECT_TRUE(monitor->has_event_type(EventType::AgentStalled));

    // Now report progress again to resolve the stall
    manager->report_progress(aid, "steps", 2.0);

    // Wait for the checker to notice the resolution
    std::this_thread::sleep_for(100ms);

    // Agent should no longer be stalled
    EXPECT_FALSE(manager->is_agent_stalled(aid));

    // Verify AgentStallResolved event was emitted
    EXPECT_TRUE(monitor->has_event_type(EventType::AgentStallResolved));
}

// ===========================================================================
// Test 4: Auto-release on stall
// ===========================================================================

TEST_F(ProgressMonitorTest, AutoReleaseOnStall) {
    // Reconfigure with auto_release_on_stall = true
    Config config;
    config.progress.enabled = true;
    config.progress.default_stall_threshold = std::chrono::milliseconds(100);
    config.progress.check_interval = std::chrono::milliseconds(20);
    config.progress.auto_release_on_stall = true;
    config.default_request_timeout = std::chrono::seconds(1);

    manager = std::make_unique<ResourceManager>(config);
    monitor = std::make_shared<TestMonitor>();
    manager->set_monitor(monitor);

    manager->register_resource(
        Resource(1, "Tokens", ResourceCategory::TokenBudget, 10));

    Agent a(1, "StallableAgent");
    a.declare_max_need(1, 5);
    AgentId aid = manager->register_agent(std::move(a));

    manager->start();

    // Acquire some resources
    auto status = manager->request_resources(aid, 1, 3);
    ASSERT_EQ(status, RequestStatus::Granted);

    // Verify resources are held
    auto res_before = manager->get_resource(1);
    ASSERT_TRUE(res_before.has_value());
    EXPECT_EQ(res_before->allocated(), 3);

    // Report progress once, then go silent to trigger stall
    manager->report_progress(aid, "steps", 1.0);

    // Wait for stall detection and auto-release
    std::this_thread::sleep_for(300ms);

    // The agent should be stalled (or was stalled)
    // Resources should have been auto-released
    auto res_after = manager->get_resource(1);
    ASSERT_TRUE(res_after.has_value());
    EXPECT_EQ(res_after->allocated(), 0)
        << "Resources should have been auto-released after stall";

    // Verify the auto-release event was emitted
    EXPECT_TRUE(monitor->has_event_type(EventType::AgentResourcesAutoReleased));
}

// ===========================================================================
// Test 5: Progress methods are no-ops when feature is disabled
// ===========================================================================

TEST_F(ProgressMonitorTest, ProgressDisabledIsNoOp) {
    // Reconfigure with progress disabled
    Config config;
    config.progress.enabled = false;
    config.default_request_timeout = std::chrono::seconds(1);

    manager = std::make_unique<ResourceManager>(config);
    monitor = std::make_shared<TestMonitor>();
    manager->set_monitor(monitor);

    manager->register_resource(
        Resource(1, "API-Slots", ResourceCategory::ApiRateLimit, 10));

    Agent a(1, "DisabledProgressAgent");
    a.declare_max_need(1, 5);
    AgentId aid = manager->register_agent(std::move(a));

    manager->start();

    // These should not throw or crash -- they are no-ops
    EXPECT_NO_THROW(manager->report_progress(aid, "metric", 42.0));
    EXPECT_NO_THROW(manager->set_agent_stall_threshold(aid, 500ms));

    // is_agent_stalled should return false (no tracking)
    EXPECT_FALSE(manager->is_agent_stalled(aid));

    // get_stalled_agents should return empty
    auto stalled = manager->get_stalled_agents();
    EXPECT_TRUE(stalled.empty());

    // No progress-related events should have been emitted
    EXPECT_FALSE(monitor->has_event_type(EventType::AgentProgressReported));
    EXPECT_FALSE(monitor->has_event_type(EventType::AgentStalled));
}

// ===========================================================================
// Test 6: Multiple agents with different stall states
// ===========================================================================

TEST_F(ProgressMonitorTest, MultipleAgentsWithDifferentStallStates) {
    manager->register_resource(
        Resource(1, "API-Slots", ResourceCategory::ApiRateLimit, 20));

    Agent a1(1, "ActiveAgent");
    a1.declare_max_need(1, 5);
    AgentId aid1 = manager->register_agent(std::move(a1));

    Agent a2(2, "StalledAgent");
    a2.declare_max_need(1, 5);
    AgentId aid2 = manager->register_agent(std::move(a2));

    Agent a3(3, "CustomThresholdAgent");
    a3.declare_max_need(1, 5);
    AgentId aid3 = manager->register_agent(std::move(a3));

    // Give agent 3 a much longer stall threshold so it does not stall
    manager->set_agent_stall_threshold(aid3, 5s);

    manager->start();

    // All agents report once to establish a baseline
    manager->report_progress(aid1, "steps", 1.0);
    manager->report_progress(aid2, "steps", 1.0);
    manager->report_progress(aid3, "steps", 1.0);

    // Keep agent 1 active with periodic reports; let agent 2 go silent;
    // agent 3 goes silent too but has a 5s threshold so should not stall
    for (int i = 0; i < 8; ++i) {
        std::this_thread::sleep_for(30ms);
        manager->report_progress(aid1, "steps", static_cast<double>(i + 2));
    }

    // By now ~240ms have elapsed since agent 2's last report, well past 100ms threshold
    // Agent 3 has a 5s threshold so should not be stalled yet

    EXPECT_FALSE(manager->is_agent_stalled(aid1))
        << "Active agent should not be stalled";
    EXPECT_TRUE(manager->is_agent_stalled(aid2))
        << "Silent agent should be stalled";
    EXPECT_FALSE(manager->is_agent_stalled(aid3))
        << "Agent with long threshold should not be stalled yet";

    auto stalled = manager->get_stalled_agents();
    ASSERT_EQ(stalled.size(), 1u);
    EXPECT_EQ(stalled[0], aid2);
}
