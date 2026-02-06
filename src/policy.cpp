#include "agentguard/policy.hpp"

#include <algorithm>
#include <unordered_map>

namespace agentguard {

// ========== FifoPolicy ==========

std::vector<ResourceRequest> FifoPolicy::prioritize(
    const std::vector<ResourceRequest>& pending_requests,
    const SystemSnapshot& /*current_state*/) const
{
    auto result = pending_requests;
    std::stable_sort(result.begin(), result.end(),
        [](const ResourceRequest& a, const ResourceRequest& b) {
            return a.submitted_at < b.submitted_at;
        });
    return result;
}

// ========== PriorityPolicy ==========

std::vector<ResourceRequest> PriorityPolicy::prioritize(
    const std::vector<ResourceRequest>& pending_requests,
    const SystemSnapshot& /*current_state*/) const
{
    auto result = pending_requests;
    std::stable_sort(result.begin(), result.end(),
        [](const ResourceRequest& a, const ResourceRequest& b) {
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.submitted_at < b.submitted_at;
        });
    return result;
}

// ========== ShortestNeedPolicy ==========

std::vector<ResourceRequest> ShortestNeedPolicy::prioritize(
    const std::vector<ResourceRequest>& pending_requests,
    const SystemSnapshot& current_state) const
{
    // Build a map of agent -> total remaining need across all resources
    std::unordered_map<AgentId, ResourceQuantity> total_remaining;
    for (auto& snap : current_state.agents) {
        ResourceQuantity total = 0;
        for (auto& [rt, max_val] : snap.max_claim) {
            auto alloc_it = snap.allocation.find(rt);
            ResourceQuantity alloc = (alloc_it != snap.allocation.end()) ? alloc_it->second : 0;
            total += (max_val - alloc);
        }
        total_remaining[snap.agent_id] = total;
    }

    auto result = pending_requests;
    std::stable_sort(result.begin(), result.end(),
        [&total_remaining](const ResourceRequest& a, const ResourceRequest& b) {
            auto a_need = total_remaining.count(a.agent_id) ? total_remaining[a.agent_id] : 0;
            auto b_need = total_remaining.count(b.agent_id) ? total_remaining[b.agent_id] : 0;
            if (a_need != b_need) return a_need < b_need;  // Shortest need first
            return a.submitted_at < b.submitted_at;
        });
    return result;
}

// ========== DeadlinePolicy ==========

std::vector<ResourceRequest> DeadlinePolicy::prioritize(
    const std::vector<ResourceRequest>& pending_requests,
    const SystemSnapshot& /*current_state*/) const
{
    auto result = pending_requests;
    std::stable_sort(result.begin(), result.end(),
        [](const ResourceRequest& a, const ResourceRequest& b) {
            // Requests with timeouts come first (most urgent deadline first)
            bool a_has_deadline = a.timeout.has_value();
            bool b_has_deadline = b.timeout.has_value();

            if (a_has_deadline && b_has_deadline) {
                auto a_deadline = a.submitted_at + a.timeout.value();
                auto b_deadline = b.submitted_at + b.timeout.value();
                return a_deadline < b_deadline;
            }
            if (a_has_deadline != b_has_deadline) {
                return a_has_deadline;  // Requests with deadlines come first
            }
            return a.submitted_at < b.submitted_at;
        });
    return result;
}

// ========== FairnessPolicy ==========

std::vector<ResourceRequest> FairnessPolicy::prioritize(
    const std::vector<ResourceRequest>& pending_requests,
    const SystemSnapshot& /*current_state*/) const
{
    // Longest-waiting request first (prevents starvation)
    auto result = pending_requests;
    std::stable_sort(result.begin(), result.end(),
        [](const ResourceRequest& a, const ResourceRequest& b) {
            return a.submitted_at < b.submitted_at;  // Oldest first
        });
    return result;
}

} // namespace agentguard
