#pragma once

#include "agentguard/types.hpp"
#include "agentguard/config.hpp"

#include <cmath>
#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace agentguard {

struct UsageStats {
    std::size_t count{0};
    double sum{0.0};
    double sum_sq{0.0};
    ResourceQuantity max_single_request{0};
    ResourceQuantity max_cumulative{0};

    // Circular buffer for rolling window
    std::vector<ResourceQuantity> window;
    std::size_t window_head{0};
    std::size_t window_count{0};

    double mean() const;
    double variance() const;
    double stddev() const;
};

class DemandEstimator {
public:
    explicit DemandEstimator(AdaptiveConfig config = AdaptiveConfig{});

    // Recording observations
    void record_request(AgentId agent, ResourceTypeId resource, ResourceQuantity quantity);
    void record_allocation_level(AgentId agent, ResourceTypeId resource,
                                  ResourceQuantity current_total_allocation);
    void clear_agent(AgentId agent);

    // Demand estimation
    ResourceQuantity estimate_max_need(AgentId agent, ResourceTypeId resource,
                                        double confidence_level) const;
    std::unordered_map<AgentId,
        std::unordered_map<ResourceTypeId, ResourceQuantity>>
    estimate_all_max_needs(double confidence_level) const;

    // Configuration
    void set_agent_demand_mode(AgentId agent, DemandMode mode);
    DemandMode get_agent_demand_mode(AgentId agent) const;
    std::optional<UsageStats> get_stats(AgentId agent, ResourceTypeId resource) const;

    const AdaptiveConfig& config() const noexcept;

private:
    AdaptiveConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<AgentId,
        std::unordered_map<ResourceTypeId, UsageStats>> stats_;
    std::unordered_map<AgentId, DemandMode> agent_modes_;

    static double confidence_to_k(double confidence);
    ResourceQuantity estimate_impl(const UsageStats& stats, double confidence) const;
};

} // namespace agentguard
