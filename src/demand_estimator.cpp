#include "agentguard/demand_estimator.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace agentguard {

// ---------------------------------------------------------------------------
// UsageStats
// ---------------------------------------------------------------------------

double UsageStats::mean() const {
    return (count > 0) ? sum / static_cast<double>(count) : 0.0;
}

double UsageStats::variance() const {
    if (count < 2) {
        return 0.0;
    }
    double n = static_cast<double>(count);
    double var = (sum_sq - (sum * sum) / n) / (n - 1.0);
    // Clamp to 0 if negative due to floating-point imprecision
    return (var < 0.0) ? 0.0 : var;
}

double UsageStats::stddev() const {
    return std::sqrt(variance());
}

// ---------------------------------------------------------------------------
// DemandEstimator -- construction / config
// ---------------------------------------------------------------------------

DemandEstimator::DemandEstimator(AdaptiveConfig config)
    : config_(std::move(config)) {}

const AdaptiveConfig& DemandEstimator::config() const noexcept {
    return config_;
}

// ---------------------------------------------------------------------------
// Recording observations
// ---------------------------------------------------------------------------

void DemandEstimator::record_request(AgentId agent, ResourceTypeId resource,
                                     ResourceQuantity quantity) {
    std::lock_guard<std::mutex> lock(mutex_);

    UsageStats& s = stats_[agent][resource];

    // Lazy-initialise the circular buffer on first use
    if (s.window.empty()) {
        s.window.resize(config_.history_window_size, 0);
    }

    s.count++;
    s.sum += static_cast<double>(quantity);
    s.sum_sq += static_cast<double>(quantity) * static_cast<double>(quantity);
    s.max_single_request = std::max(s.max_single_request, quantity);

    // Circular buffer insert
    s.window[s.window_head] = quantity;
    s.window_head = (s.window_head + 1) % s.window.size();
    s.window_count = std::min(s.window_count + 1, s.window.size());
}

void DemandEstimator::record_allocation_level(AgentId agent,
                                              ResourceTypeId resource,
                                              ResourceQuantity current_total_allocation) {
    std::lock_guard<std::mutex> lock(mutex_);

    UsageStats& s = stats_[agent][resource];
    s.max_cumulative = std::max(s.max_cumulative, current_total_allocation);
}

void DemandEstimator::clear_agent(AgentId agent) {
    std::lock_guard<std::mutex> lock(mutex_);

    stats_.erase(agent);
    agent_modes_.erase(agent);
}

// ---------------------------------------------------------------------------
// Demand estimation
// ---------------------------------------------------------------------------

ResourceQuantity DemandEstimator::estimate_max_need(AgentId agent,
                                                    ResourceTypeId resource,
                                                    double confidence_level) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto agent_it = stats_.find(agent);
    if (agent_it == stats_.end()) {
        return config_.cold_start_default_demand;
    }
    auto res_it = agent_it->second.find(resource);
    if (res_it == agent_it->second.end()) {
        return config_.cold_start_default_demand;
    }

    return estimate_impl(res_it->second, confidence_level);
}

std::unordered_map<AgentId,
    std::unordered_map<ResourceTypeId, ResourceQuantity>>
DemandEstimator::estimate_all_max_needs(double confidence_level) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_map<AgentId,
        std::unordered_map<ResourceTypeId, ResourceQuantity>> result;

    for (const auto& [agent, res_map] : stats_) {
        for (const auto& [resource, stats] : res_map) {
            result[agent][resource] = estimate_impl(stats, confidence_level);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Per-agent mode
// ---------------------------------------------------------------------------

void DemandEstimator::set_agent_demand_mode(AgentId agent, DemandMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    agent_modes_[agent] = mode;
}

DemandMode DemandEstimator::get_agent_demand_mode(AgentId agent) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = agent_modes_.find(agent);
    if (it == agent_modes_.end()) {
        return config_.default_demand_mode;
    }
    return it->second;
}

std::optional<UsageStats> DemandEstimator::get_stats(AgentId agent,
                                                     ResourceTypeId resource) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto agent_it = stats_.find(agent);
    if (agent_it == stats_.end()) {
        return std::nullopt;
    }
    auto res_it = agent_it->second.find(resource);
    if (res_it == agent_it->second.end()) {
        return std::nullopt;
    }
    return res_it->second;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

double DemandEstimator::confidence_to_k(double confidence) {
    if (confidence <= 0.5) {
        return 0.0;
    }
    if (confidence >= 0.9999) {
        return 3.719;
    }

    // Beasley-Springer-Moro rational approximation for the inverse normal CDF
    double t = std::sqrt(-2.0 * std::log(1.0 - confidence));
    double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
    double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
    return t - (c0 + c1 * t + c2 * t * t) /
               (1.0 + d1 * t + d2 * t * t + d3 * t * t * t);
}

ResourceQuantity DemandEstimator::estimate_impl(const UsageStats& stats,
                                                double confidence) const {
    if (stats.count == 0) {
        return config_.cold_start_default_demand;
    }

    if (stats.count == 1) {
        auto raw = static_cast<double>(stats.max_single_request)
                   * config_.cold_start_headroom_factor;
        auto result = static_cast<ResourceQuantity>(std::ceil(raw));
        return std::max(static_cast<ResourceQuantity>(1), result);
    }

    // General case: mean + k * stddev
    double k = confidence_to_k(confidence);
    double estimated = stats.mean() + k * stats.stddev();

    // Floor: never estimate below the observed single-request maximum
    estimated = std::max(estimated, static_cast<double>(stats.max_single_request));

    // Cap: if we have cumulative data, don't exceed it (with headroom)
    if (stats.max_cumulative > 0) {
        double cap = static_cast<double>(stats.max_cumulative)
                     * config_.adaptive_headroom_factor;
        estimated = std::min(estimated, cap);
    }

    auto result = static_cast<ResourceQuantity>(std::ceil(estimated));
    return std::max(static_cast<ResourceQuantity>(1), result);
}

} // namespace agentguard
