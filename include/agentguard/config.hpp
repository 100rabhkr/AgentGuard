#pragma once

#include "agentguard/types.hpp"
#include <cstddef>

namespace agentguard {

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
};

} // namespace agentguard
