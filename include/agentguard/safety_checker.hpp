#pragma once

#include "agentguard/types.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace agentguard {

// Input to the safety check: a snapshot of current system state
struct SafetyCheckInput {
    // Total resources in the system per type
    std::unordered_map<ResourceTypeId, ResourceQuantity> total;

    // Currently available (unallocated) resources per type
    std::unordered_map<ResourceTypeId, ResourceQuantity> available;

    // Per-agent current allocation per resource type
    std::unordered_map<AgentId,
        std::unordered_map<ResourceTypeId, ResourceQuantity>> allocation;

    // Per-agent maximum declared need per resource type
    std::unordered_map<AgentId,
        std::unordered_map<ResourceTypeId, ResourceQuantity>> max_need;
};

struct SafetyCheckResult {
    bool is_safe{false};
    std::vector<AgentId> safe_sequence;  // Valid completion order (if safe)
    std::string reason;                  // Human-readable explanation (if unsafe)
};

class SafetyChecker {
public:
    SafetyChecker() = default;

    // Core Banker's Algorithm safety check.
    // Pure function: no side effects, no locking.
    SafetyCheckResult check_safety(const SafetyCheckInput& input) const;

    // "If we grant this request, is the resulting state safe?"
    SafetyCheckResult check_hypothetical(
        const SafetyCheckInput& current_state,
        AgentId requesting_agent,
        ResourceTypeId resource_type,
        ResourceQuantity quantity) const;

    // Check if granting multiple requests simultaneously is safe.
    SafetyCheckResult check_hypothetical_batch(
        const SafetyCheckInput& current_state,
        const std::vector<ResourceRequest>& requests) const;

    // From a set of candidates, find which can be safely granted.
    std::vector<RequestId> find_grantable_requests(
        const SafetyCheckInput& current_state,
        const std::vector<ResourceRequest>& candidates) const;

    // Identify agents whose remaining needs are closest to exhausting available resources.
    std::vector<AgentId> identify_bottleneck_agents(
        const SafetyCheckInput& input) const;

    // Probabilistic safety check: runs Banker's on input whose max_need
    // values were populated from statistical estimates at the given confidence level.
    // The caller (ResourceManager) is responsible for building the input with
    // estimated max needs from DemandEstimator.
    ProbabilisticSafetyResult check_safety_probabilistic(
        const SafetyCheckInput& input,
        double confidence_level) const;

    // Hypothetical probabilistic check: "if we grant this request, is the
    // resulting state safe under probabilistic max-need estimates?"
    ProbabilisticSafetyResult check_hypothetical_probabilistic(
        const SafetyCheckInput& current_state,
        AgentId agent,
        ResourceTypeId resource,
        ResourceQuantity quantity,
        double confidence_level) const;
};

} // namespace agentguard
