#include <gtest/gtest.h>
#include <agentguard/agentguard.hpp>
#include <agentguard/progress_tracker.hpp>

#include <algorithm>
#include <mutex>
#include <thread>
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
// Fixture
// ===========================================================================

class ProgressTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enabled = true;
        config_.default_stall_threshold = 50ms;
        config_.check_interval = 20ms;
        config_.auto_release_on_stall = false;

        monitor_ = std::make_shared<TestMonitor>();
    }

    void TearDown() override {
        // Ensure the tracker is stopped if it was started
        if (tracker_) {
            tracker_->stop();
        }
    }

    void create_tracker() {
        tracker_ = std::make_unique<ProgressTracker>(config_);
    }

    ProgressConfig config_;
    std::shared_ptr<TestMonitor> monitor_;
    std::unique_ptr<ProgressTracker> tracker_;
};

// ===========================================================================
// 1. Register and deregister agent
// ===========================================================================

TEST_F(ProgressTrackerTest, RegisterAndDeregisterAgent) {
    create_tracker();

    tracker_->register_agent(1);
    auto progress = tracker_->get_progress(1);
    ASSERT_TRUE(progress.has_value());
    EXPECT_TRUE(progress->metrics.empty());
    EXPECT_FALSE(progress->is_stalled);

    tracker_->deregister_agent(1);
    auto after = tracker_->get_progress(1);
    EXPECT_FALSE(after.has_value());
}

// ===========================================================================
// 2. Report progress updates metric values
// ===========================================================================

TEST_F(ProgressTrackerTest, ReportProgressUpdatesMetricValues) {
    create_tracker();
    tracker_->start(monitor_);

    tracker_->register_agent(1);
    tracker_->report_progress(1, "tokens_processed", 42.0);
    tracker_->report_progress(1, "steps_completed", 7.0);

    auto progress = tracker_->get_progress(1);
    ASSERT_TRUE(progress.has_value());
    ASSERT_EQ(progress->metrics.size(), 2);
    EXPECT_DOUBLE_EQ(progress->metrics.at("tokens_processed"), 42.0);
    EXPECT_DOUBLE_EQ(progress->metrics.at("steps_completed"), 7.0);

    // Overwrite an existing metric
    tracker_->report_progress(1, "tokens_processed", 100.0);
    progress = tracker_->get_progress(1);
    EXPECT_DOUBLE_EQ(progress->metrics.at("tokens_processed"), 100.0);
}

// ===========================================================================
// 3. Get progress returns recorded metrics
// ===========================================================================

TEST_F(ProgressTrackerTest, GetProgressReturnsRecordedMetrics) {
    create_tracker();

    // Unknown agent returns nullopt
    auto none = tracker_->get_progress(999);
    EXPECT_FALSE(none.has_value());

    // Registered agent with no progress reports still returns a record
    tracker_->register_agent(10);
    auto record = tracker_->get_progress(10);
    ASSERT_TRUE(record.has_value());
    EXPECT_TRUE(record->metrics.empty());
    EXPECT_FALSE(record->is_stalled);
    EXPECT_FALSE(record->stall_threshold.has_value());

    // After a progress report, the metric is present
    tracker_->start(monitor_);
    tracker_->report_progress(10, "accuracy", 0.95);
    record = tracker_->get_progress(10);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->metrics.size(), 1);
    EXPECT_DOUBLE_EQ(record->metrics.at("accuracy"), 0.95);
}

// ===========================================================================
// 4. Stall detection after threshold expires
// ===========================================================================

TEST_F(ProgressTrackerTest, StallDetectionAfterThresholdExpires) {
    config_.default_stall_threshold = 50ms;
    config_.check_interval = 20ms;
    create_tracker();

    tracker_->register_agent(1);
    tracker_->start(monitor_);

    // Sleep past the stall threshold plus a check interval to ensure detection
    std::this_thread::sleep_for(100ms);

    EXPECT_TRUE(tracker_->is_stalled(1));

    auto stalled = tracker_->get_stalled_agents();
    ASSERT_EQ(stalled.size(), 1);
    EXPECT_EQ(stalled[0], 1);
}

// ===========================================================================
// 5. Stall resolution on new progress report
// ===========================================================================

TEST_F(ProgressTrackerTest, StallResolutionOnNewProgressReport) {
    config_.default_stall_threshold = 50ms;
    config_.check_interval = 20ms;
    create_tracker();

    tracker_->register_agent(1);
    tracker_->start(monitor_);

    // Wait for stall detection
    std::this_thread::sleep_for(100ms);
    ASSERT_TRUE(tracker_->is_stalled(1));

    // Report progress to resolve the stall
    tracker_->report_progress(1, "step", 1.0);

    EXPECT_FALSE(tracker_->is_stalled(1));
    EXPECT_TRUE(tracker_->get_stalled_agents().empty());

    // Verify stall-resolved event was emitted
    auto resolved_events = monitor_->get_events_of_type(EventType::AgentStallResolved);
    EXPECT_GE(resolved_events.size(), 1);
}

// ===========================================================================
// 6. Per-agent stall threshold override
// ===========================================================================

TEST_F(ProgressTrackerTest, PerAgentStallThresholdOverride) {
    config_.default_stall_threshold = 200ms;  // High default so agent 1 won't stall
    config_.check_interval = 20ms;
    create_tracker();

    tracker_->register_agent(1);
    tracker_->register_agent(2);

    // Give agent 2 a much shorter threshold
    tracker_->set_agent_stall_threshold(2, 50ms);

    tracker_->start(monitor_);

    // Sleep long enough for agent 2's threshold but not agent 1's default
    std::this_thread::sleep_for(100ms);

    EXPECT_FALSE(tracker_->is_stalled(1));
    EXPECT_TRUE(tracker_->is_stalled(2));

    // Verify the override is stored in the record
    auto record = tracker_->get_progress(2);
    ASSERT_TRUE(record.has_value());
    ASSERT_TRUE(record->stall_threshold.has_value());
}

// ===========================================================================
// 7. get_stalled_agents returns correct list
// ===========================================================================

TEST_F(ProgressTrackerTest, GetStalledAgentsReturnsCorrectList) {
    config_.default_stall_threshold = 50ms;
    config_.check_interval = 20ms;
    create_tracker();

    tracker_->register_agent(1);
    tracker_->register_agent(2);
    tracker_->register_agent(3);

    tracker_->start(monitor_);

    // Wait for all agents to stall
    std::this_thread::sleep_for(100ms);

    auto stalled = tracker_->get_stalled_agents();
    ASSERT_EQ(stalled.size(), 3);

    // Sort for deterministic comparison
    std::sort(stalled.begin(), stalled.end());
    EXPECT_EQ(stalled[0], 1);
    EXPECT_EQ(stalled[1], 2);
    EXPECT_EQ(stalled[2], 3);

    // Report progress for agent 2 to resolve its stall
    tracker_->report_progress(2, "alive", 1.0);

    stalled = tracker_->get_stalled_agents();
    ASSERT_EQ(stalled.size(), 2);
    std::sort(stalled.begin(), stalled.end());
    EXPECT_EQ(stalled[0], 1);
    EXPECT_EQ(stalled[1], 3);
}

// ===========================================================================
// 8. is_stalled for unknown agent returns false
// ===========================================================================

TEST_F(ProgressTrackerTest, IsStalledForUnknownAgentReturnsFalse) {
    create_tracker();

    EXPECT_FALSE(tracker_->is_stalled(999));
    EXPECT_FALSE(tracker_->is_stalled(0));
    EXPECT_FALSE(tracker_->is_stalled(42));
}

// ===========================================================================
// 9. Start/stop lifecycle
// ===========================================================================

TEST_F(ProgressTrackerTest, StartStopLifecycle) {
    create_tracker();

    tracker_->register_agent(1);

    // Start the tracker
    tracker_->start(monitor_);

    // Report progress while running
    tracker_->report_progress(1, "metric", 1.0);

    // Stop should be safe and join the thread
    tracker_->stop();

    // After stop, can still query but stall detection won't run
    auto progress = tracker_->get_progress(1);
    ASSERT_TRUE(progress.has_value());
    EXPECT_DOUBLE_EQ(progress->metrics.at("metric"), 1.0);

    // Double-stop should be safe (joinable check in stop())
    tracker_->stop();
}

// ===========================================================================
// 10. Monitor events emitted
// ===========================================================================

TEST_F(ProgressTrackerTest, MonitorEventsEmitted) {
    config_.default_stall_threshold = 50ms;
    config_.check_interval = 20ms;
    create_tracker();

    tracker_->register_agent(1);
    tracker_->start(monitor_);

    // Report progress -- should emit AgentProgressReported
    tracker_->report_progress(1, "step", 1.0);

    auto progress_events = monitor_->get_events_of_type(EventType::AgentProgressReported);
    ASSERT_GE(progress_events.size(), 1);
    EXPECT_TRUE(progress_events[0].agent_id.has_value());
    EXPECT_EQ(progress_events[0].agent_id.value(), 1);

    // Wait for stall detection
    std::this_thread::sleep_for(100ms);

    auto stall_events = monitor_->get_events_of_type(EventType::AgentStalled);
    ASSERT_GE(stall_events.size(), 1);
    EXPECT_TRUE(stall_events[0].agent_id.has_value());
    EXPECT_EQ(stall_events[0].agent_id.value(), 1);

    // Resolve the stall
    tracker_->report_progress(1, "step", 2.0);

    auto resolved_events = monitor_->get_events_of_type(EventType::AgentStallResolved);
    ASSERT_GE(resolved_events.size(), 1);
    EXPECT_TRUE(resolved_events[0].agent_id.has_value());
    EXPECT_EQ(resolved_events[0].agent_id.value(), 1);

    // Verify overall event ordering: progress -> stall -> progress -> resolved
    auto all_events = monitor_->get_events();
    EXPECT_GE(all_events.size(), 4);  // At least: progress, stall, progress, resolved
}
