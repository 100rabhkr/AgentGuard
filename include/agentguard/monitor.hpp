#pragma once

#include "agentguard/types.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace agentguard {

enum class EventType {
    AgentRegistered,
    AgentDeregistered,
    ResourceRegistered,
    ResourceCapacityChanged,
    RequestSubmitted,
    RequestGranted,
    RequestDenied,
    RequestTimedOut,
    RequestCancelled,
    ResourcesReleased,
    SafetyCheckPerformed,
    UnsafeStateDetected,
    QueueSizeChanged,
    // Progress monitoring events
    AgentProgressReported,
    AgentStalled,
    AgentStallResolved,
    AgentResourcesAutoReleased,
    // Delegation tracking events
    DelegationReported,
    DelegationCompleted,
    DelegationCancelled,
    DelegationCycleDetected,
    // Adaptive demand events
    DemandEstimateUpdated,
    ProbabilisticSafetyCheck,
    AdaptiveDemandModeChanged
};

struct MonitorEvent {
    EventType type;
    Timestamp timestamp;
    std::string message;

    std::optional<AgentId> agent_id;
    std::optional<ResourceTypeId> resource_type;
    std::optional<RequestId> request_id;
    std::optional<ResourceQuantity> quantity;
    std::optional<bool> safety_result;

    // Delegation tracking: the target agent (e.g., delegation "to" agent)
    std::optional<AgentId> target_agent_id;
    // Delegation cycle detection: the cycle path
    std::optional<std::vector<AgentId>> cycle_path;

    // Operation duration in microseconds (e.g., safety check duration)
    std::optional<double> duration_us;
};

// Abstract monitor interface
class Monitor {
public:
    virtual ~Monitor() = default;
    virtual void on_event(const MonitorEvent& event) = 0;
    virtual void on_snapshot(const SystemSnapshot& snapshot) = 0;
};

// Console logger
class ConsoleMonitor : public Monitor {
public:
    enum class Verbosity { Quiet, Normal, Verbose, Debug };

    explicit ConsoleMonitor(Verbosity v = Verbosity::Normal);

    void on_event(const MonitorEvent& event) override;
    void on_snapshot(const SystemSnapshot& snapshot) override;

private:
    Verbosity verbosity_;
    mutable std::mutex output_mutex_;
};

// Metrics collector
class MetricsMonitor : public Monitor {
public:
    struct Metrics {
        std::uint64_t total_requests{0};
        std::uint64_t granted_requests{0};
        std::uint64_t denied_requests{0};
        std::uint64_t timed_out_requests{0};
        double average_wait_time_ms{0.0};
        double safety_check_avg_duration_us{0.0};
        std::uint64_t unsafe_state_detections{0};
        double resource_utilization_percent{0.0};
    };

    MetricsMonitor();

    void on_event(const MonitorEvent& event) override;
    void on_snapshot(const SystemSnapshot& snapshot) override;

    Metrics get_metrics() const;
    void reset_metrics();

    using AlertCallback = std::function<void(const std::string&)>;
    void set_utilization_alert_threshold(double threshold, AlertCallback cb);
    void set_queue_size_alert_threshold(std::size_t threshold, AlertCallback cb);

private:
    mutable std::mutex metrics_mutex_;
    Metrics metrics_;

    double utilization_threshold_{1.1};  // > 1.0 means disabled
    AlertCallback utilization_cb_;
    std::size_t queue_size_threshold_{0};
    AlertCallback queue_size_cb_;

    // Timing tracking for wait time calculation
    std::unordered_map<RequestId, Timestamp> pending_submit_times_;
    std::uint64_t wait_time_sample_count_{0};
    double wait_time_sum_ms_{0.0};

    // Safety check duration tracking
    std::uint64_t safety_check_count_{0};
    double safety_check_duration_sum_us_{0.0};
};

// Fan-out to multiple monitors
class CompositeMonitor : public Monitor {
public:
    void add_monitor(std::shared_ptr<Monitor> monitor);

    void on_event(const MonitorEvent& event) override;
    void on_snapshot(const SystemSnapshot& snapshot) override;

private:
    std::vector<std::shared_ptr<Monitor>> monitors_;
};

} // namespace agentguard
