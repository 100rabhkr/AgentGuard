#include "agentguard/safety_checker.hpp"

#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace agentguard {

namespace {

// Get all resource type IDs present in the system
std::vector<ResourceTypeId> collect_resource_types(const SafetyCheckInput& input) {
    std::unordered_set<ResourceTypeId> types;
    for (auto& [rt, _] : input.total) {
        types.insert(rt);
    }
    for (auto& [rt, _] : input.available) {
        types.insert(rt);
    }
    return {types.begin(), types.end()};
}

// Get remaining need for an agent on a specific resource
ResourceQuantity get_remaining_need(
    const SafetyCheckInput& input,
    AgentId agent,
    ResourceTypeId rt)
{
    ResourceQuantity max_val = 0;
    auto max_it = input.max_need.find(agent);
    if (max_it != input.max_need.end()) {
        auto rt_it = max_it->second.find(rt);
        if (rt_it != max_it->second.end()) {
            max_val = rt_it->second;
        }
    }

    ResourceQuantity alloc_val = 0;
    auto alloc_it = input.allocation.find(agent);
    if (alloc_it != input.allocation.end()) {
        auto rt_it = alloc_it->second.find(rt);
        if (rt_it != alloc_it->second.end()) {
            alloc_val = rt_it->second;
        }
    }

    return max_val - alloc_val;
}

// Get current allocation for an agent on a specific resource
ResourceQuantity get_allocation(
    const SafetyCheckInput& input,
    AgentId agent,
    ResourceTypeId rt)
{
    auto alloc_it = input.allocation.find(agent);
    if (alloc_it != input.allocation.end()) {
        auto rt_it = alloc_it->second.find(rt);
        if (rt_it != alloc_it->second.end()) {
            return rt_it->second;
        }
    }
    return 0;
}

// Check if an agent's remaining needs can be satisfied with available resources
bool can_finish(
    const SafetyCheckInput& input,
    AgentId agent,
    const std::unordered_map<ResourceTypeId, ResourceQuantity>& work,
    const std::vector<ResourceTypeId>& resource_types)
{
    for (auto rt : resource_types) {
        ResourceQuantity need = get_remaining_need(input, agent, rt);
        auto avail_it = work.find(rt);
        ResourceQuantity avail = (avail_it != work.end()) ? avail_it->second : 0;
        if (need > avail) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

SafetyCheckResult SafetyChecker::check_safety(const SafetyCheckInput& input) const {
    SafetyCheckResult result;
    auto resource_types = collect_resource_types(input);

    // Collect all agent IDs
    std::vector<AgentId> agents;
    for (auto& [aid, _] : input.max_need) {
        agents.push_back(aid);
    }
    // Also include agents with allocations but no max_need entry
    for (auto& [aid, _] : input.allocation) {
        if (input.max_need.find(aid) == input.max_need.end()) {
            agents.push_back(aid);
        }
    }

    if (agents.empty()) {
        result.is_safe = true;
        result.reason = "No agents in the system";
        return result;
    }

    // Banker's Algorithm: try to find a safe sequence
    std::unordered_map<ResourceTypeId, ResourceQuantity> work = input.available;
    std::unordered_set<AgentId> finished;
    std::vector<AgentId> safe_sequence;

    std::size_t n = agents.size();
    for (std::size_t round = 0; round < n; ++round) {
        bool found_one = false;

        for (auto aid : agents) {
            if (finished.count(aid)) continue;

            if (can_finish(input, aid, work, resource_types)) {
                // This agent can finish. Simulate it releasing its resources.
                for (auto rt : resource_types) {
                    work[rt] += get_allocation(input, aid, rt);
                }
                finished.insert(aid);
                safe_sequence.push_back(aid);
                found_one = true;
            }
        }

        if (!found_one) {
            // No agent could finish in this round
            if (finished.size() == agents.size()) {
                // All agents already finished - we're done
                break;
            }
            // Truly unsafe: remaining agents cannot complete
            result.is_safe = false;
            result.safe_sequence.clear();

            std::string blocked_agents;
            for (auto aid : agents) {
                if (!finished.count(aid)) {
                    if (!blocked_agents.empty()) blocked_agents += ", ";
                    blocked_agents += std::to_string(aid);
                }
            }
            result.reason = "Unsafe state: agents [" + blocked_agents +
                           "] cannot complete with available resources";
            return result;
        }
    }

    result.is_safe = true;
    result.safe_sequence = std::move(safe_sequence);
    result.reason = "Safe state found";
    return result;
}

SafetyCheckResult SafetyChecker::check_hypothetical(
    const SafetyCheckInput& current_state,
    AgentId requesting_agent,
    ResourceTypeId resource_type,
    ResourceQuantity quantity) const
{
    // Create a modified state as if the request were granted
    SafetyCheckInput hypothetical = current_state;

    // Decrease available
    hypothetical.available[resource_type] -= quantity;

    // Increase agent's allocation
    hypothetical.allocation[requesting_agent][resource_type] += quantity;

    return check_safety(hypothetical);
}

SafetyCheckResult SafetyChecker::check_hypothetical_batch(
    const SafetyCheckInput& current_state,
    const std::vector<ResourceRequest>& requests) const
{
    SafetyCheckInput hypothetical = current_state;

    for (auto& req : requests) {
        hypothetical.available[req.resource_type] -= req.quantity;
        hypothetical.allocation[req.agent_id][req.resource_type] += req.quantity;
    }

    return check_safety(hypothetical);
}

std::vector<RequestId> SafetyChecker::find_grantable_requests(
    const SafetyCheckInput& current_state,
    const std::vector<ResourceRequest>& candidates) const
{
    std::vector<RequestId> grantable;

    for (auto& req : candidates) {
        // Quick check: is there enough available?
        auto avail_it = current_state.available.find(req.resource_type);
        if (avail_it == current_state.available.end() || avail_it->second < req.quantity) {
            continue;
        }

        auto result = check_hypothetical(
            current_state, req.agent_id, req.resource_type, req.quantity);

        if (result.is_safe) {
            grantable.push_back(req.id);
        }
    }

    return grantable;
}

std::vector<AgentId> SafetyChecker::identify_bottleneck_agents(
    const SafetyCheckInput& input) const
{
    auto resource_types = collect_resource_types(input);

    // Score each agent by how much of the available resources their remaining
    // needs would consume (higher score = bigger bottleneck)
    struct AgentScore {
        AgentId id;
        double score;
    };

    std::vector<AgentScore> scores;

    for (auto& [aid, _] : input.max_need) {
        double total_need_ratio = 0.0;
        int resource_count = 0;

        for (auto rt : resource_types) {
            ResourceQuantity need = get_remaining_need(input, aid, rt);
            auto avail_it = input.available.find(rt);
            ResourceQuantity avail = (avail_it != input.available.end()) ? avail_it->second : 0;

            if (avail > 0) {
                total_need_ratio += static_cast<double>(need) / static_cast<double>(avail);
                resource_count++;
            } else if (need > 0) {
                // Need resources but none available -> very high score
                total_need_ratio += 1000.0;
                resource_count++;
            }
        }

        double avg_ratio = (resource_count > 0) ? total_need_ratio / resource_count : 0.0;
        scores.push_back({aid, avg_ratio});
    }

    // Sort by score descending (biggest bottleneck first)
    std::sort(scores.begin(), scores.end(),
              [](const AgentScore& a, const AgentScore& b) {
                  return a.score > b.score;
              });

    std::vector<AgentId> result;
    result.reserve(scores.size());
    for (auto& s : scores) {
        result.push_back(s.id);
    }
    return result;
}

ProbabilisticSafetyResult SafetyChecker::check_safety_probabilistic(
    const SafetyCheckInput& input,
    double confidence_level) const
{
    auto binary_result = check_safety(input);

    ProbabilisticSafetyResult result;
    result.is_safe = binary_result.is_safe;
    result.confidence_level = confidence_level;
    result.safe_sequence = std::move(binary_result.safe_sequence);
    result.reason = std::move(binary_result.reason);

    // max_safe_confidence: if safe at this level, report it; if not, 0.0
    // (the caller can binary-search confidence levels externally if needed)
    result.max_safe_confidence = binary_result.is_safe ? confidence_level : 0.0;

    // Copy the max_need map so the caller can inspect what estimates were used
    result.estimated_max_needs = input.max_need;

    return result;
}

ProbabilisticSafetyResult SafetyChecker::check_hypothetical_probabilistic(
    const SafetyCheckInput& current_state,
    AgentId agent,
    ResourceTypeId resource,
    ResourceQuantity quantity,
    double confidence_level) const
{
    // Build hypothetical state with the grant applied
    SafetyCheckInput hypothetical = current_state;
    hypothetical.available[resource] -= quantity;
    hypothetical.allocation[agent][resource] += quantity;

    return check_safety_probabilistic(hypothetical, confidence_level);
}

} // namespace agentguard
