#pragma once

#include "agentguard/resource.hpp"

#include <string>
#include <unordered_map>

namespace agentguard::ai {

// Represents API rate limits (requests per time window)
class RateLimiter {
public:
    enum class WindowType { PerSecond, PerMinute, PerHour, PerDay };

    RateLimiter(ResourceTypeId id,
                std::string api_name,
                ResourceQuantity requests_per_window,
                WindowType window);

    Resource as_resource() const;

    WindowType window_type() const noexcept;
    ResourceQuantity requests_per_window() const noexcept;

    // Burst allowance (allow short bursts above steady-state rate)
    void set_burst_allowance(ResourceQuantity burst_extra);
    ResourceQuantity burst_allowance() const noexcept;

    // Endpoint-specific sub-limits
    void add_endpoint_sublimit(std::string endpoint, ResourceQuantity limit);
    const std::unordered_map<std::string, ResourceQuantity>& endpoint_sublimits() const noexcept;

    ResourceTypeId id() const noexcept;
    const std::string& api_name() const noexcept;

private:
    ResourceTypeId id_;
    std::string api_name_;
    ResourceQuantity requests_per_window_;
    WindowType window_type_;
    ResourceQuantity burst_allowance_{0};
    std::unordered_map<std::string, ResourceQuantity> endpoint_sublimits_;

    Duration window_to_duration() const;
};

} // namespace agentguard::ai
