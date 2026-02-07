#pragma once

#include "agentguard/types.hpp"
#include <cstddef>

namespace agentguard {

// Progress monitoring configuration
struct ProgressConfig {
    bool enabled = false;
    Duration default_stall_threshold = std::chrono::seconds(120);
    Duration check_interval = std::chrono::seconds(5);
    bool auto_release_on_stall = false;
};

// Delegation cycle detection action
enum class DelegationCycleAction {
    NotifyOnly,        // Emit event but accept the delegation
    RejectDelegation,  // Refuse to add the edge, return accepted=false
    CancelLatest       // Add the edge but immediately remove it
};

// Delegation cycle detection configuration
struct DelegationConfig {
    bool enabled = false;
    DelegationCycleAction cycle_action = DelegationCycleAction::NotifyOnly;
};

// Adaptive demand estimation configuration
struct AdaptiveConfig {
    bool enabled = false;
    double default_confidence_level = 0.95;
    std::size_t history_window_size = 50;
    double cold_start_headroom_factor = 2.0;
    ResourceQuantity cold_start_default_demand = 1;
    double adaptive_headroom_factor = 1.5;
    DemandMode default_demand_mode = DemandMode::Static;
};

struct Config {
    // Maximum number of agents that can be registered simultaneously
    std::size_t max_agents = 1024;

    // Maximum number of resource types
    std::size_t max_resource_types = 256;

    // Request queue capacity
    std::size_t max_queue_size = 10000;

    // Default timeout for blocking requests (0 = no timeout)
    Duration default_request_timeout = std::chrono::seconds(30);

    // How often the background processor checks the queue
    Duration processor_poll_interval = std::chrono::milliseconds(10);

    // How often to emit system snapshots to the monitor
    Duration snapshot_interval = std::chrono::seconds(5);

    // Enable automatic timeout expiration in the background
    bool enable_timeout_expiration = true;

    // Warn if a request has been pending longer than this
    Duration starvation_threshold = std::chrono::seconds(60);

    // If false, all locking is disabled (for single-threaded use)
    bool thread_safe = true;

    // Progress monitoring
    ProgressConfig progress;

    // Delegation cycle detection
    DelegationConfig delegation;

    // Adaptive demand estimation
    AdaptiveConfig adaptive;
};

} // namespace agentguard
