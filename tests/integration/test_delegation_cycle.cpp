#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

#include <algorithm>
#include <mutex>
#include <vector>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Test Monitor that records all events for verification
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
// Fixture: delegation enabled with NotifyOnly (default for most tests)
// ===========================================================================

class DelegationCycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        Config config;
        config.delegation.enabled = true;
        config.delegation.cycle_action = DelegationCycleAction::NotifyOnly;

        manager = std::make_unique<ResourceManager>(config);
        monitor = std::make_shared<TestMonitor>();
        manager->set_monitor(monitor);
    }

    std::unique_ptr<ResourceManager> manager;
    std::shared_ptr<TestMonitor> monitor;
};

// ===========================================================================
// Test 1: Delegation through ResourceManager (report, complete, verify empty)
// ===========================================================================

TEST_F(DelegationCycleTest, ReportAndCompleteDelegation) {
    AgentId idA = manager->register_agent(Agent(0, "AgentA"));
    AgentId idB = manager->register_agent(Agent(0, "AgentB"));

    // Report a delegation from A to B
    auto result = manager->report_delegation(idA, idB, "summarize document");
    EXPECT_TRUE(result.accepted);
    EXPECT_FALSE(result.cycle_detected);
    EXPECT_TRUE(result.cycle_path.empty());

    // Verify it appears in active delegations
    auto delegations = manager->get_all_delegations();
    ASSERT_EQ(delegations.size(), 1u);
    EXPECT_EQ(delegations[0].from, idA);
    EXPECT_EQ(delegations[0].to, idB);
    EXPECT_EQ(delegations[0].task_description, "summarize document");

    // Complete the delegation
    manager->complete_delegation(idA, idB);

    // Delegations should now be empty
    delegations = manager->get_all_delegations();
    EXPECT_TRUE(delegations.empty());

    // No cycle should be found
    auto cycle = manager->find_delegation_cycle();
    EXPECT_FALSE(cycle.has_value());
}

// ===========================================================================
// Test 2: Cycle detection through ResourceManager (A->B->C->A)
// ===========================================================================

TEST_F(DelegationCycleTest, CycleDetection_ThreeAgents) {
    AgentId idA = manager->register_agent(Agent(0, "AgentA"));
    AgentId idB = manager->register_agent(Agent(0, "AgentB"));
    AgentId idC = manager->register_agent(Agent(0, "AgentC"));

    // Build chain: A -> B -> C
    auto r1 = manager->report_delegation(idA, idB, "task1");
    EXPECT_TRUE(r1.accepted);
    EXPECT_FALSE(r1.cycle_detected);

    auto r2 = manager->report_delegation(idB, idC, "task2");
    EXPECT_TRUE(r2.accepted);
    EXPECT_FALSE(r2.cycle_detected);

    // Close the cycle: C -> A
    auto r3 = manager->report_delegation(idC, idA, "task3");
    EXPECT_TRUE(r3.cycle_detected);
    // With NotifyOnly, the delegation is still accepted
    EXPECT_TRUE(r3.accepted);

    // cycle_path should form the cycle: contains A, B, C, and closes back to start
    ASSERT_GE(r3.cycle_path.size(), 3u);
    // The cycle path should start and end with the same agent
    EXPECT_EQ(r3.cycle_path.front(), r3.cycle_path.back());

    // Verify all three agents appear in the cycle path
    auto& path = r3.cycle_path;
    EXPECT_NE(std::find(path.begin(), path.end(), idA), path.end());
    EXPECT_NE(std::find(path.begin(), path.end(), idB), path.end());
    EXPECT_NE(std::find(path.begin(), path.end(), idC), path.end());

    // find_delegation_cycle should also detect the cycle
    auto cycle = manager->find_delegation_cycle();
    ASSERT_TRUE(cycle.has_value());
    EXPECT_GE(cycle->size(), 3u);
}

// ===========================================================================
// Test 3: DelegationCycleDetected event emitted to monitor
// ===========================================================================

TEST_F(DelegationCycleTest, CycleDetectedEventEmitted) {
    AgentId idA = manager->register_agent(Agent(0, "AgentA"));
    AgentId idB = manager->register_agent(Agent(0, "AgentB"));

    // A -> B (no cycle)
    manager->report_delegation(idA, idB, "task1");

    // B -> A (creates cycle)
    manager->report_delegation(idB, idA, "task2");

    // Check that DelegationCycleDetected event was emitted
    auto cycle_events = monitor->get_events_of_type(EventType::DelegationCycleDetected);
    ASSERT_EQ(cycle_events.size(), 1u);

    // The event should contain the cycle path
    ASSERT_TRUE(cycle_events[0].cycle_path.has_value());
    auto& path = cycle_events[0].cycle_path.value();
    EXPECT_GE(path.size(), 2u);
    EXPECT_EQ(path.front(), path.back());

    // DelegationReported events should also have been emitted for both delegations
    auto reported_events = monitor->get_events_of_type(EventType::DelegationReported);
    EXPECT_EQ(reported_events.size(), 2u);
}

// ===========================================================================
// Test 4: RejectDelegation config prevents cycle
// ===========================================================================

TEST(DelegationCycleActionTest, RejectDelegation_PreventsCycle) {
    Config config;
    config.delegation.enabled = true;
    config.delegation.cycle_action = DelegationCycleAction::RejectDelegation;

    ResourceManager mgr(config);
    auto monitor = std::make_shared<TestMonitor>();
    mgr.set_monitor(monitor);

    AgentId idA = mgr.register_agent(Agent(0, "AgentA"));
    AgentId idB = mgr.register_agent(Agent(0, "AgentB"));
    AgentId idC = mgr.register_agent(Agent(0, "AgentC"));

    // A -> B -> C: no cycles
    auto r1 = mgr.report_delegation(idA, idB, "step1");
    EXPECT_TRUE(r1.accepted);
    EXPECT_FALSE(r1.cycle_detected);

    auto r2 = mgr.report_delegation(idB, idC, "step2");
    EXPECT_TRUE(r2.accepted);
    EXPECT_FALSE(r2.cycle_detected);

    // C -> A would create a cycle: should be REJECTED
    auto r3 = mgr.report_delegation(idC, idA, "step3");
    EXPECT_FALSE(r3.accepted);
    EXPECT_TRUE(r3.cycle_detected);
    EXPECT_GE(r3.cycle_path.size(), 3u);

    // The edge C -> A should NOT exist in the graph
    auto delegations = mgr.get_all_delegations();
    EXPECT_EQ(delegations.size(), 2u);  // Only A->B and B->C remain
    for (const auto& d : delegations) {
        // Ensure no edge from C to A
        EXPECT_FALSE(d.from == idC && d.to == idA);
    }

    // No cycle should be found in the graph now
    auto cycle = mgr.find_delegation_cycle();
    EXPECT_FALSE(cycle.has_value());

    // DelegationCycleDetected event should still be emitted
    auto cycle_events = monitor->get_events_of_type(EventType::DelegationCycleDetected);
    EXPECT_EQ(cycle_events.size(), 1u);

    // DelegationReported should only appear for the two accepted delegations
    auto reported_events = monitor->get_events_of_type(EventType::DelegationReported);
    EXPECT_EQ(reported_events.size(), 2u);
}

// ===========================================================================
// Test 5: CancelLatest config cancels the cycle-creating delegation
// ===========================================================================

TEST(DelegationCycleActionTest, CancelLatest_RemovesCycleEdge) {
    Config config;
    config.delegation.enabled = true;
    config.delegation.cycle_action = DelegationCycleAction::CancelLatest;

    ResourceManager mgr(config);
    auto monitor = std::make_shared<TestMonitor>();
    mgr.set_monitor(monitor);

    AgentId idA = mgr.register_agent(Agent(0, "AgentA"));
    AgentId idB = mgr.register_agent(Agent(0, "AgentB"));
    AgentId idC = mgr.register_agent(Agent(0, "AgentC"));

    // Build chain: A -> B -> C
    auto r1 = mgr.report_delegation(idA, idB, "step1");
    EXPECT_TRUE(r1.accepted);

    auto r2 = mgr.report_delegation(idB, idC, "step2");
    EXPECT_TRUE(r2.accepted);

    // C -> A would create a cycle: should be cancelled
    auto r3 = mgr.report_delegation(idC, idA, "step3");
    EXPECT_FALSE(r3.accepted);
    EXPECT_TRUE(r3.cycle_detected);
    EXPECT_GE(r3.cycle_path.size(), 3u);

    // Only the original two delegations remain
    auto delegations = mgr.get_all_delegations();
    EXPECT_EQ(delegations.size(), 2u);

    // No cycle in the graph
    auto cycle = mgr.find_delegation_cycle();
    EXPECT_FALSE(cycle.has_value());

    // Both DelegationCycleDetected and DelegationCancelled events should be emitted
    auto cycle_events = monitor->get_events_of_type(EventType::DelegationCycleDetected);
    EXPECT_EQ(cycle_events.size(), 1u);

    auto cancelled_events = monitor->get_events_of_type(EventType::DelegationCancelled);
    EXPECT_EQ(cancelled_events.size(), 1u);
}

// ===========================================================================
// Test 6: Delegation when feature disabled (no-ops, returns safe defaults)
// ===========================================================================

TEST(DelegationCycleActionTest, DisabledDelegation_NoOps) {
    Config config;
    config.delegation.enabled = false;

    ResourceManager mgr(config);
    auto monitor = std::make_shared<TestMonitor>();
    mgr.set_monitor(monitor);

    AgentId idA = mgr.register_agent(Agent(0, "AgentA"));
    AgentId idB = mgr.register_agent(Agent(0, "AgentB"));

    // report_delegation should return accepted=true, cycle_detected=false
    auto result = mgr.report_delegation(idA, idB, "task");
    EXPECT_TRUE(result.accepted);
    EXPECT_FALSE(result.cycle_detected);
    EXPECT_TRUE(result.cycle_path.empty());

    // get_all_delegations returns empty
    auto delegations = mgr.get_all_delegations();
    EXPECT_TRUE(delegations.empty());

    // find_delegation_cycle returns nullopt
    auto cycle = mgr.find_delegation_cycle();
    EXPECT_FALSE(cycle.has_value());

    // complete and cancel should not throw
    EXPECT_NO_THROW(mgr.complete_delegation(idA, idB));
    EXPECT_NO_THROW(mgr.cancel_delegation(idA, idB));

    // No delegation-related events should have been emitted
    auto delegation_events = monitor->get_events_of_type(EventType::DelegationReported);
    EXPECT_TRUE(delegation_events.empty());

    auto cycle_events = monitor->get_events_of_type(EventType::DelegationCycleDetected);
    EXPECT_TRUE(cycle_events.empty());
}
