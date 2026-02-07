#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>

using namespace agentguard;
using namespace std::chrono_literals;

// ===========================================================================
// Test Monitor helper
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
        for (auto& e : events) {
            if (e.type == type) filtered.push_back(e);
        }
        return filtered;
    }
private:
    std::mutex mutex_;
    std::vector<MonitorEvent> events;
};

// ===========================================================================
// Fixture: NotifyOnly (default)
// ===========================================================================

class DelegationTrackerNotifyTest : public ::testing::Test {
protected:
    void SetUp() override {
        DelegationConfig cfg;
        cfg.enabled = true;
        cfg.cycle_action = DelegationCycleAction::NotifyOnly;
        tracker = std::make_unique<DelegationTracker>(cfg);
    }

    std::unique_ptr<DelegationTracker> tracker;
};

// ===========================================================================
// Fixture: RejectDelegation
// ===========================================================================

class DelegationTrackerRejectTest : public ::testing::Test {
protected:
    void SetUp() override {
        DelegationConfig cfg;
        cfg.enabled = true;
        cfg.cycle_action = DelegationCycleAction::RejectDelegation;
        tracker = std::make_unique<DelegationTracker>(cfg);
    }

    std::unique_ptr<DelegationTracker> tracker;
};

// ===========================================================================
// Fixture: CancelLatest
// ===========================================================================

class DelegationTrackerCancelTest : public ::testing::Test {
protected:
    void SetUp() override {
        DelegationConfig cfg;
        cfg.enabled = true;
        cfg.cycle_action = DelegationCycleAction::CancelLatest;
        tracker = std::make_unique<DelegationTracker>(cfg);
    }

    std::unique_ptr<DelegationTracker> tracker;
};

// ===========================================================================
// 1. Register / deregister agents
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, RegisterAndDeregisterAgents) {
    tracker->register_agent(1);
    tracker->register_agent(2);

    // After registration, delegation between them should work
    auto result = tracker->report_delegation(1, 2, "task");
    EXPECT_TRUE(result.accepted);
    EXPECT_FALSE(result.cycle_detected);

    // Deregister agent 2; delegation from 1->2 should be gone
    tracker->deregister_agent(2);
    auto delegations = tracker->get_all_delegations();
    EXPECT_TRUE(delegations.empty());
}

// ===========================================================================
// 2. Report simple delegation (no cycle)
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, ReportSimpleDelegationNoCycle) {
    tracker->register_agent(1);
    tracker->register_agent(2);

    auto result = tracker->report_delegation(1, 2, "summarize docs");
    EXPECT_TRUE(result.accepted);
    EXPECT_FALSE(result.cycle_detected);
    EXPECT_TRUE(result.cycle_path.empty());

    auto all = tracker->get_all_delegations();
    ASSERT_EQ(all.size(), 1);
    EXPECT_EQ(all[0].from, 1);
    EXPECT_EQ(all[0].to, 2);
    EXPECT_EQ(all[0].task_description, "summarize docs");
}

// ===========================================================================
// 3. Complete delegation removes edge
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, CompleteDelegationRemovesEdge) {
    tracker->register_agent(1);
    tracker->register_agent(2);

    tracker->report_delegation(1, 2, "task");
    EXPECT_EQ(tracker->get_all_delegations().size(), 1);

    tracker->complete_delegation(1, 2);
    EXPECT_TRUE(tracker->get_all_delegations().empty());
}

// ===========================================================================
// 4. Cancel delegation removes edge
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, CancelDelegationRemovesEdge) {
    tracker->register_agent(1);
    tracker->register_agent(2);

    tracker->report_delegation(1, 2, "task");
    EXPECT_EQ(tracker->get_all_delegations().size(), 1);

    tracker->cancel_delegation(1, 2);
    EXPECT_TRUE(tracker->get_all_delegations().empty());
}

// ===========================================================================
// 5. Get all delegations
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, GetAllDelegations) {
    tracker->register_agent(1);
    tracker->register_agent(2);
    tracker->register_agent(3);

    tracker->report_delegation(1, 2, "task A");
    tracker->report_delegation(2, 3, "task B");

    auto all = tracker->get_all_delegations();
    EXPECT_EQ(all.size(), 2);
}

// ===========================================================================
// 6. Get delegations from specific agent
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, GetDelegationsFromAgent) {
    tracker->register_agent(1);
    tracker->register_agent(2);
    tracker->register_agent(3);

    tracker->report_delegation(1, 2, "task A");
    tracker->report_delegation(1, 3, "task B");
    tracker->report_delegation(2, 3, "task C");

    auto from_1 = tracker->get_delegations_from(1);
    EXPECT_EQ(from_1.size(), 2);

    auto from_2 = tracker->get_delegations_from(2);
    EXPECT_EQ(from_2.size(), 1);
    EXPECT_EQ(from_2[0].to, 3);

    auto from_3 = tracker->get_delegations_from(3);
    EXPECT_TRUE(from_3.empty());
}

// ===========================================================================
// 7. Get delegations to specific agent
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, GetDelegationsToAgent) {
    tracker->register_agent(1);
    tracker->register_agent(2);
    tracker->register_agent(3);

    tracker->report_delegation(1, 3, "task A");
    tracker->report_delegation(2, 3, "task B");

    auto to_3 = tracker->get_delegations_to(3);
    EXPECT_EQ(to_3.size(), 2);

    auto to_1 = tracker->get_delegations_to(1);
    EXPECT_TRUE(to_1.empty());
}

// ===========================================================================
// 8. Self-delegation cycle detection (A->A)
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, SelfDelegationCycleDetected) {
    tracker->register_agent(1);

    auto result = tracker->report_delegation(1, 1, "self-delegate");
    EXPECT_TRUE(result.cycle_detected);
    EXPECT_FALSE(result.cycle_path.empty());
    // Cycle path for self-loop: [1, 1]
    ASSERT_GE(result.cycle_path.size(), 2u);
    EXPECT_EQ(result.cycle_path.front(), 1);
    EXPECT_EQ(result.cycle_path.back(), 1);
}

// ===========================================================================
// 9. Two-node cycle (A->B->A)
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, TwoNodeCycleDetected) {
    tracker->register_agent(1);
    tracker->register_agent(2);

    auto r1 = tracker->report_delegation(1, 2, "task A");
    EXPECT_TRUE(r1.accepted);
    EXPECT_FALSE(r1.cycle_detected);

    auto r2 = tracker->report_delegation(2, 1, "task B");
    EXPECT_TRUE(r2.cycle_detected);
    // With NotifyOnly, delegation is still accepted
    EXPECT_TRUE(r2.accepted);
    // Cycle path should include both agents and close the loop
    ASSERT_GE(r2.cycle_path.size(), 3u);
    EXPECT_EQ(r2.cycle_path.front(), r2.cycle_path.back());
}

// ===========================================================================
// 10. Three-node cycle (A->B->C->A)
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, ThreeNodeCycleDetected) {
    tracker->register_agent(1);
    tracker->register_agent(2);
    tracker->register_agent(3);

    tracker->report_delegation(1, 2, "step 1");
    tracker->report_delegation(2, 3, "step 2");

    auto r = tracker->report_delegation(3, 1, "step 3");
    EXPECT_TRUE(r.cycle_detected);
    EXPECT_TRUE(r.accepted);  // NotifyOnly keeps the edge
    // Cycle path should form a closed loop
    ASSERT_GE(r.cycle_path.size(), 4u);
    EXPECT_EQ(r.cycle_path.front(), r.cycle_path.back());
}

// ===========================================================================
// 11. No cycle in linear chain (A->B->C)
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, NoCycleInLinearChain) {
    tracker->register_agent(1);
    tracker->register_agent(2);
    tracker->register_agent(3);

    auto r1 = tracker->report_delegation(1, 2, "step 1");
    EXPECT_TRUE(r1.accepted);
    EXPECT_FALSE(r1.cycle_detected);

    auto r2 = tracker->report_delegation(2, 3, "step 2");
    EXPECT_TRUE(r2.accepted);
    EXPECT_FALSE(r2.cycle_detected);
    EXPECT_TRUE(r2.cycle_path.empty());
}

// ===========================================================================
// 12. NotifyOnly action: accepts delegation but detects cycle
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, NotifyOnlyAcceptsDelegationOnCycle) {
    tracker->register_agent(1);
    tracker->register_agent(2);

    tracker->report_delegation(1, 2);
    auto r = tracker->report_delegation(2, 1);

    EXPECT_TRUE(r.accepted);
    EXPECT_TRUE(r.cycle_detected);
    // Both edges should still exist
    EXPECT_EQ(tracker->get_all_delegations().size(), 2);
}

// ===========================================================================
// 13. RejectDelegation action: rejects the delegation
// ===========================================================================

TEST_F(DelegationTrackerRejectTest, RejectDelegationOnCycle) {
    tracker->register_agent(1);
    tracker->register_agent(2);

    tracker->report_delegation(1, 2, "task A");
    auto r = tracker->report_delegation(2, 1, "task B");

    EXPECT_FALSE(r.accepted);
    EXPECT_TRUE(r.cycle_detected);
    // Only the first edge should remain
    auto all = tracker->get_all_delegations();
    ASSERT_EQ(all.size(), 1);
    EXPECT_EQ(all[0].from, 1);
    EXPECT_EQ(all[0].to, 2);
}

// ===========================================================================
// 14. CancelLatest action: accepts then removes
// ===========================================================================

TEST_F(DelegationTrackerCancelTest, CancelLatestOnCycle) {
    tracker->register_agent(1);
    tracker->register_agent(2);

    tracker->report_delegation(1, 2, "task A");
    auto r = tracker->report_delegation(2, 1, "task B");

    EXPECT_FALSE(r.accepted);
    EXPECT_TRUE(r.cycle_detected);
    // The cycle-causing edge should be removed
    auto all = tracker->get_all_delegations();
    ASSERT_EQ(all.size(), 1);
    EXPECT_EQ(all[0].from, 1);
    EXPECT_EQ(all[0].to, 2);
}

// ===========================================================================
// 15. find_cycle detects existing cycles
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, FindCycleDetectsExistingCycle) {
    tracker->register_agent(1);
    tracker->register_agent(2);

    // NotifyOnly keeps cycle edges
    tracker->report_delegation(1, 2);
    tracker->report_delegation(2, 1);

    auto cycle = tracker->find_cycle();
    ASSERT_TRUE(cycle.has_value());
    ASSERT_GE(cycle->size(), 3u);
    EXPECT_EQ(cycle->front(), cycle->back());
}

// ===========================================================================
// 16. find_cycle returns nullopt when no cycle
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, FindCycleReturnsNulloptWhenNoCycle) {
    tracker->register_agent(1);
    tracker->register_agent(2);
    tracker->register_agent(3);

    tracker->report_delegation(1, 2);
    tracker->report_delegation(2, 3);

    auto cycle = tracker->find_cycle();
    EXPECT_FALSE(cycle.has_value());
}

// ===========================================================================
// 17. Deregister removes all edges involving agent
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, DeregisterRemovesAllEdges) {
    tracker->register_agent(1);
    tracker->register_agent(2);
    tracker->register_agent(3);

    tracker->report_delegation(1, 2, "outgoing from 2");
    tracker->report_delegation(3, 2, "incoming to 2");
    tracker->report_delegation(1, 3, "unrelated");

    EXPECT_EQ(tracker->get_all_delegations().size(), 3);

    // Deregister agent 2 -- should remove edges 1->2 and 3->2
    tracker->deregister_agent(2);

    auto remaining = tracker->get_all_delegations();
    ASSERT_EQ(remaining.size(), 1);
    EXPECT_EQ(remaining[0].from, 1);
    EXPECT_EQ(remaining[0].to, 3);
}

// ===========================================================================
// 18. Monitor events emitted correctly
// ===========================================================================

TEST_F(DelegationTrackerNotifyTest, MonitorEventsEmitted) {
    auto monitor = std::make_shared<TestMonitor>();
    tracker->set_monitor(monitor);

    tracker->register_agent(1);
    tracker->register_agent(2);

    // Report delegation -- should emit DelegationReported
    tracker->report_delegation(1, 2, "do work");
    {
        auto reported = monitor->get_events_of_type(EventType::DelegationReported);
        ASSERT_EQ(reported.size(), 1);
        EXPECT_TRUE(reported[0].agent_id.has_value());
        EXPECT_EQ(reported[0].agent_id.value(), 1);
        EXPECT_TRUE(reported[0].target_agent_id.has_value());
        EXPECT_EQ(reported[0].target_agent_id.value(), 2);
    }

    // Complete delegation -- should emit DelegationCompleted
    tracker->complete_delegation(1, 2);
    {
        auto completed = monitor->get_events_of_type(EventType::DelegationCompleted);
        ASSERT_EQ(completed.size(), 1);
        EXPECT_EQ(completed[0].agent_id.value(), 1);
        EXPECT_EQ(completed[0].target_agent_id.value(), 2);
    }

    // Report and cancel -- should emit DelegationReported then DelegationCancelled
    tracker->report_delegation(1, 2, "another task");
    tracker->cancel_delegation(1, 2);
    {
        auto cancelled = monitor->get_events_of_type(EventType::DelegationCancelled);
        ASSERT_EQ(cancelled.size(), 1);
    }

    // Create a cycle -- should emit DelegationCycleDetected
    tracker->report_delegation(1, 2);
    tracker->report_delegation(2, 1);
    {
        auto cycle_events = monitor->get_events_of_type(EventType::DelegationCycleDetected);
        ASSERT_EQ(cycle_events.size(), 1);
        EXPECT_TRUE(cycle_events[0].cycle_path.has_value());
        EXPECT_GE(cycle_events[0].cycle_path->size(), 3u);
    }
}
