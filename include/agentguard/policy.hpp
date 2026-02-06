#pragma once

#include "agentguard/types.hpp"
#include <string>
#include <vector>

namespace agentguard {

// Abstract scheduling policy interface
class SchedulingPolicy {
public:
    virtual ~SchedulingPolicy() = default;

    // Given pending requests, return them ordered by priority to attempt granting.
    virtual std::vector<ResourceRequest> prioritize(
        const std::vector<ResourceRequest>& pending_requests,
        const SystemSnapshot& current_state) const = 0;

    virtual std::string name() const = 0;
};

// First-come, first-served (default)
class FifoPolicy : public SchedulingPolicy {
public:
    std::vector<ResourceRequest> prioritize(
        const std::vector<ResourceRequest>& pending_requests,
        const SystemSnapshot& current_state) const override;
    std::string name() const override { return "FIFO"; }
};

// Higher priority agents get served first
class PriorityPolicy : public SchedulingPolicy {
public:
    std::vector<ResourceRequest> prioritize(
        const std::vector<ResourceRequest>& pending_requests,
        const SystemSnapshot& current_state) const override;
    std::string name() const override { return "Priority"; }
};

// Prefer agents closest to finishing (maximizes throughput)
class ShortestNeedPolicy : public SchedulingPolicy {
public:
    std::vector<ResourceRequest> prioritize(
        const std::vector<ResourceRequest>& pending_requests,
        const SystemSnapshot& current_state) const override;
    std::string name() const override { return "ShortestNeedFirst"; }
};

// Prefer requests closest to their timeout deadline
class DeadlinePolicy : public SchedulingPolicy {
public:
    std::vector<ResourceRequest> prioritize(
        const std::vector<ResourceRequest>& pending_requests,
        const SystemSnapshot& current_state) const override;
    std::string name() const override { return "DeadlineAware"; }
};

// Prevents starvation by preferring agents that have waited the longest
class FairnessPolicy : public SchedulingPolicy {
public:
    std::vector<ResourceRequest> prioritize(
        const std::vector<ResourceRequest>& pending_requests,
        const SystemSnapshot& current_state) const override;
    std::string name() const override { return "Fairness"; }
};

} // namespace agentguard
